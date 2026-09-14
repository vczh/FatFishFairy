#include "Platform.h"
#include "Json.h"
#include <VlppOS.Windows.h>
#include <winhttp.h>
#include <wincodec.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace fatfish
{
	using namespace vl;
	using namespace vl::collections;
	using namespace vl::filesystem;
	using namespace vl::glr;
	using namespace vl::inter_process::windows_http;

	struct ParsedHttpUrl
	{
		WString		host;
		WString		path;
		vint		port = 0;
		bool		secure = false;
	};

	ParsedHttpUrl ParseHttpUrl(const WString& url)
	{
		for (vint i = 0; i < url.Length(); i++)
		{
			auto c = url[i];
			if (c <= L' ' || c == 127 || c == L'\\' || c == L'#')
			{
				throw Exception(L"HTTP URLs cannot contain whitespace, fragments, or backslashes.");
			}
		}
		URL_COMPONENTS parts = {};
		parts.dwStructSize = sizeof(parts);
		parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
		parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
		if (!WinHttpCrackUrl(url.Buffer(), (DWORD)url.Length(), 0, &parts)
			|| (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)
			|| parts.dwHostNameLength == 0 || parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0)
		{
			throw Exception(L"Expected an HTTP or HTTPS URL without embedded credentials.");
		}
		ParsedHttpUrl result;
		result.host = WString::CopyFrom(parts.lpszHostName, parts.dwHostNameLength);
		result.path = parts.dwUrlPathLength ? WString::CopyFrom(parts.lpszUrlPath, parts.dwUrlPathLength) : WString(L"/");
		if (parts.dwExtraInfoLength) result.path += WString::CopyFrom(parts.lpszExtraInfo, parts.dwExtraInfoLength);
		result.port = parts.nPort;
		result.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
		return result;
	}

	WString GetChatCompletionUrl(const WString& baseUrl)
	{
		ParseHttpUrl(baseUrl);
		auto queryIndex = baseUrl.IndexOf(L'?');
		auto path = queryIndex == -1 ? baseUrl : baseUrl.Left(queryIndex);
		auto query = queryIndex == -1 ? WString::Empty : baseUrl.Sub(queryIndex, baseUrl.Length() - queryIndex);
		while (path.Length() > 0 && path[path.Length() - 1] == L'/') path = path.Left(path.Length() - 1);
		auto suffix = WString(L"/chat/completions");
		if (path.Length() < suffix.Length() || path.Right(suffix.Length()) != suffix) path += suffix;
		return path + query;
	}

	WString BuildAuthenticationHeader(const ApiConfig& config)
	{
		auto colon = config.authHeader.IndexOf(L':');
		if (colon <= 0) throw Exception(L"auth_header must contain one header name followed by a colon.");
		for (vint i = 0; i < colon; i++)
		{
			auto c = config.authHeader[i];
			if (!((L'A' <= c && c <= L'Z') || (L'a' <= c && c <= L'z') || (L'0' <= c && c <= L'9')
				|| wcschr(L"!#$%&'*+-.^_`|~", c)))
			{
				throw Exception(L"auth_header contains an invalid header name.");
			}
		}
		WString result;
		vint offset = 0;
		while (auto next = wcsstr(config.authHeader.Buffer() + offset, L"$APIKEY"))
		{
			auto index = (vint)(next - config.authHeader.Buffer());
			result += config.authHeader.Sub(offset, index - offset) + config.apiKey;
			offset = index + 7;
		}
		result += config.authHeader.Sub(offset, config.authHeader.Length() - offset);
		for (vint i = 0; i < result.Length(); i++)
		{
			auto c = result[i];
			if (c < L' ' || c > 126) throw Exception(L"The authentication header must contain printable ASCII characters only.");
		}
		return result;
	}

	ApiConfig LoadApiConfig(const FilePath& repositoryRoot, json::Parser& parser)
	{
		WString source;
		stream::BomEncoder::Encoding encoding;
		bool containsBom = false;
		if (!File(repositoryRoot / L"env" / L"apikey.json").ReadAllTextWithEncodingTesting(source, encoding, containsBom))
		{
			throw Exception(L"Cannot read env/apikey.json. Copy env/apikey-template.json and configure the endpoint and dedicated models.");
		}
		Ptr<json::JsonObject> object;
		try
		{
			object = ParseJson(source, parser).Cast<json::JsonObject>();
		}
		catch (const Exception&)
		{
			// Parser diagnostics may contain the original secret-bearing input.
			throw Exception(L"env/apikey.json is not valid JSON.");
		}
		if (!object) throw Exception(L"env/apikey.json must contain a JSON object.");
		Dictionary<WString, WString> fields;
		for (auto&& field : object->fields)
		{
			auto value = field->value.Cast<json::JsonString>();
			if (!value || value->content.value.Length() == 0 || fields.Keys().Contains(field->name.value))
			{
				throw Exception(L"Configuration fields must be unique, nonempty strings.");
			}
			fields.Add(field->name.value, value->content.value);
		}
		for (auto name : { L"apikey", L"url", L"auth_header", L"vision_model" })
		{
			if (!fields.Keys().Contains(name)) throw Exception(L"env/apikey.json is missing a required field from the template.");
		}
		auto hasFairyModel = fields.Keys().Contains(L"fairy_model");
		auto hasChatModel = fields.Keys().Contains(L"chat_model");
		if (!hasFairyModel && !hasChatModel) throw Exception(L"env/apikey.json requires fairy_model (or the legacy chat_model field).");
		if (hasFairyModel && hasChatModel && fields[L"fairy_model"] != fields[L"chat_model"])
		{
			throw Exception(L"fairy_model and legacy chat_model must agree when both are present.");
		}
		ApiConfig config;
		config.apiKey = fields[L"apikey"];
		config.url = GetChatCompletionUrl(fields[L"url"]);
		config.authHeader = fields[L"auth_header"];
		config.visionModel = fields[L"vision_model"];
		config.fairyModel = fields[hasFairyModel ? L"fairy_model" : L"chat_model"];
		// Each role has its own configured model and session; a provider model ID may be shared.
		BuildAuthenticationHeader(config);
		return config;
	}

	void CheckPlatformResult(bool succeeded, const wchar_t* operation)
	{
		if (!succeeded) throw Exception(WString(operation) + L" failed (Windows error " + itow(GetLastError()) + L").");
	}

	struct InternetHandle
	{
		HINTERNET		value = nullptr;
		~InternetHandle() { if (value) WinHttpCloseHandle(value); }
	};

	WString PostChatCompletion(const ApiConfig& config, const WString& body)
	{
		auto url = ParseHttpUrl(GetChatCompletionUrl(config.url));
		auto header = BuildAuthenticationHeader(config) + L"\r\nContent-Type: application/json; charset=utf-8\r\nAccept: text/event-stream, application/json\r\n";
		HttpRequest requestBody;
		requestBody.SetBodyUtf8(body);
		if (requestBody.body.Count() > 256 * 1024 * 1024) throw Exception(L"The Chat Completions request exceeds 256 MiB.");

		// HttpClientApi supports HTTPS, but cannot disable redirect forwarding of custom
		// authentication headers. Use WinHTTP directly for authenticated requests only.
		InternetHandle session{ WinHttpOpen(L"FatFishCli", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
		CheckPlatformResult(session.value != nullptr, L"WinHttpOpen");
		CheckPlatformResult(WinHttpSetTimeouts(session.value, 30000, 30000, 120000, 120000), L"WinHttpSetTimeouts");
		InternetHandle connection{ WinHttpConnect(session.value, url.host.Buffer(), (INTERNET_PORT)url.port, 0) };
		CheckPlatformResult(connection.value != nullptr, L"WinHttpConnect");
		InternetHandle request{ WinHttpOpenRequest(connection.value, L"POST", url.path.Buffer(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0) };
		CheckPlatformResult(request.value != nullptr, L"WinHttpOpenRequest");
		DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
		CheckPlatformResult(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)), L"WinHttpSetOption");
		DWORD autoLogon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
		CheckPlatformResult(WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &autoLogon, sizeof(autoLogon)), L"WinHttpSetOption");
		CheckPlatformResult(WinHttpSendRequest(request.value, header.Buffer(), (DWORD)header.Length(), requestBody.body.Count() ? &requestBody.body[0] : nullptr,
			(DWORD)requestBody.body.Count(), (DWORD)requestBody.body.Count(), 0), L"WinHttpSendRequest");
		CheckPlatformResult(WinHttpReceiveResponse(request.value, nullptr), L"WinHttpReceiveResponse");
		DWORD status = 0;
		DWORD statusSize = sizeof(status);
		CheckPlatformResult(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX), L"WinHttpQueryHeaders");
		if (status < 200 || status >= 300)
		{
			// Never echo a response body: gateways can reflect submitted credentials.
			throw Exception(L"Chat Completions returned HTTP " + itow(status) + L". Check the endpoint, credentials, and model configuration.");
		}
		stream::MemoryStream responseStream;
		char buffer[16384];
		while (true)
		{
			DWORD read = 0;
			CheckPlatformResult(WinHttpReadData(request.value, buffer, sizeof(buffer), &read), L"WinHttpReadData");
			if (!read) break;
			if (responseStream.Size() + read > 16 * 1024 * 1024) throw Exception(L"The Chat Completions response exceeds 16 MiB.");
			responseStream.Write(buffer, read);
		}
		HttpResponse response;
		response.body.Resize((vint)responseStream.Size());
		responseStream.SeekFromBegin(0);
		if (response.body.Count()) responseStream.Read(&response.body[0], response.body.Count());
		WString result;
		if (!response.TryGetBodyUtf8(result)) throw Exception(L"The Chat Completions response is not UTF-8 text.");
		return result;
	}

	WebResponse HttpGet(const WString& url, vint maxCharacters)
	{
		if (maxCharacters < 1 || maxCharacters > 100000) throw Exception(L"HTTP result length must be between 1 and 100000 characters.");
		auto parsed = ParseHttpUrl(url);
		HttpRequest request;
		request.method = L"GET";
		request.query = parsed.path;
		request.secure = parsed.secure;
		request.resolveTimeout = request.connectTimeout = request.sendTimeout = request.receiveTimeout = 30000;
		request.acceptTypes.Add(L"text/*");
		request.acceptTypes.Add(L"application/json");
		request.extraHeaders.Add(L"Accept-Encoding", L"identity");
		EventObject completed;
		CHECK_ERROR(completed.CreateManualUnsignal(false), L"fatfish::HttpGet#Cannot create completion event.");
		Variant<HttpResponse, HttpError> result;
		HttpClientApi client(parsed.host, parsed.port);
		client.HttpQuery(request, [&](Variant<HttpResponse, HttpError> response)
		{
			result = std::move(response);
			completed.Signal();
		});
		auto finished = completed.WaitForTime(120000);
		client.Stop();
		if (!finished) throw Exception(L"HTTP GET exceeded its two-minute deadline.");
		if (result.Index() == 1) throw Exception(L"HTTP GET failed (Windows error " + itow(result.Get<HttpError>().errorCode) + L").");
		auto&& response = result.Get<HttpResponse>();
		if (response.body.Count() > 4 * 1024 * 1024) throw Exception(L"HTTP GET response exceeds 4 MiB.");
		WebResponse output;
		output.statusCode = response.statusCode;
		output.contentType = response.contentType;
		if (!response.TryGetBodyUtf8(output.body)) throw Exception(L"HTTP GET response is not UTF-8 text.");
		output.truncated = output.body.Length() > maxCharacters;
		if (output.truncated) output.body = output.body.Left(maxCharacters);
		return output;
	}

	void CheckImagingResult(HRESULT result, const wchar_t* operation)
	{
		if (FAILED(result)) throw Exception(WString(operation) + L" failed (HRESULT " + itow(result) + L").");
	}

	struct CaptureScope
	{
		HRESULT					comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		DPI_AWARENESS_CONTEXT	dpiContext = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		~CaptureScope()
		{
			if (dpiContext) SetThreadDpiAwarenessContext(dpiContext);
			if (SUCCEEDED(comResult)) CoUninitialize();
		}
	};

	struct MonitorBitmap
	{
		HDC				screen = GetDC(nullptr);
		HDC				memory = nullptr;
		HBITMAP			bitmap = nullptr;
		HGDIOBJ			previous = nullptr;
		~MonitorBitmap()
		{
			if (previous) SelectObject(memory, previous);
			if (bitmap) DeleteObject(bitmap);
			if (memory) DeleteDC(memory);
			if (screen) ReleaseDC(nullptr, screen);
		}
	};

	void CaptureMonitors(List<MonitorSnapshot>& snapshots)
	{
		CaptureScope scope;
		if (scope.comResult != RPC_E_CHANGED_MODE) CheckImagingResult(scope.comResult, L"CoInitializeEx");
		CheckPlatformResult(scope.dpiContext != nullptr, L"SetThreadDpiAwarenessContext");
		IWICImagingFactory* factoryRaw = nullptr;
		CheckImagingResult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factoryRaw)), L"CoCreateInstance(WIC)");
		ComPtr<IWICImagingFactory> factory(factoryRaw);
		List<HMONITOR> monitors;
		CheckPlatformResult(EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL
		{
			reinterpret_cast<List<HMONITOR>*>(context)->Add(monitor);
			return TRUE;
		}, reinterpret_cast<LPARAM>(&monitors)), L"EnumDisplayMonitors");
		if (monitors.Count() == 0) throw Exception(L"No active monitors are available to capture.");
		snapshots.Clear();
		for (auto monitor : monitors)
		{
			MONITORINFOEXW info = {};
			info.cbSize = sizeof(info);
			CheckPlatformResult(GetMonitorInfoW(monitor, &info), L"GetMonitorInfo");
			MonitorSnapshot snapshot;
			snapshot.name = info.szDevice;
			snapshot.left = info.rcMonitor.left;
			snapshot.top = info.rcMonitor.top;
			snapshot.width = info.rcMonitor.right - info.rcMonitor.left;
			snapshot.height = info.rcMonitor.bottom - info.rcMonitor.top;
			MonitorBitmap bitmap;
			CheckPlatformResult(bitmap.screen != nullptr, L"GetDC");
			bitmap.memory = CreateCompatibleDC(bitmap.screen);
			CheckPlatformResult(bitmap.memory != nullptr, L"CreateCompatibleDC");
			bitmap.bitmap = CreateCompatibleBitmap(bitmap.screen, (int)snapshot.width, (int)snapshot.height);
			CheckPlatformResult(bitmap.bitmap != nullptr, L"CreateCompatibleBitmap");
			bitmap.previous = SelectObject(bitmap.memory, bitmap.bitmap);
			CheckPlatformResult(bitmap.previous != nullptr && bitmap.previous != HGDI_ERROR, L"SelectObject");
			CheckPlatformResult(BitBlt(bitmap.memory, 0, 0, (int)snapshot.width, (int)snapshot.height, bitmap.screen, (int)snapshot.left, (int)snapshot.top, SRCCOPY | CAPTUREBLT), L"BitBlt");
			CheckPlatformResult(SelectObject(bitmap.memory, bitmap.previous) == bitmap.bitmap, L"SelectObject(restore)");
			bitmap.previous = nullptr;

			IWICBitmap* sourceRaw = nullptr;
			CheckImagingResult(factory->CreateBitmapFromHBITMAP(bitmap.bitmap, nullptr, WICBitmapIgnoreAlpha, &sourceRaw), L"CreateBitmapFromHBITMAP");
			ComPtr<IWICBitmap> source(sourceRaw);
			IWICFormatConverter* converterRaw = nullptr;
			CheckImagingResult(factory->CreateFormatConverter(&converterRaw), L"CreateFormatConverter");
			ComPtr<IWICFormatConverter> converter(converterRaw);
			CheckImagingResult(converter->Initialize(source.Obj(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), L"Initialize(PNG converter)");
			IStream* pngRaw = nullptr;
			CheckImagingResult(CreateStreamOnHGlobal(nullptr, TRUE, &pngRaw), L"CreateStreamOnHGlobal");
			ComPtr<IStream> png(pngRaw);
			IWICBitmapEncoder* encoderRaw = nullptr;
			CheckImagingResult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoderRaw), L"CreateEncoder(PNG)");
			ComPtr<IWICBitmapEncoder> encoder(encoderRaw);
			CheckImagingResult(encoder->Initialize(png.Obj(), WICBitmapEncoderNoCache), L"Initialize(PNG encoder)");
			IWICBitmapFrameEncode* frameRaw = nullptr;
			CheckImagingResult(encoder->CreateNewFrame(&frameRaw, nullptr), L"CreateNewFrame(PNG)");
			ComPtr<IWICBitmapFrameEncode> frame(frameRaw);
			CheckImagingResult(frame->Initialize(nullptr), L"Initialize(PNG frame)");
			CheckImagingResult(frame->SetSize((UINT)snapshot.width, (UINT)snapshot.height), L"SetSize(PNG frame)");
			auto pixelFormat = GUID_WICPixelFormat24bppBGR;
			CheckImagingResult(frame->SetPixelFormat(&pixelFormat), L"SetPixelFormat(PNG frame)");
			CheckImagingResult(frame->WriteSource(converter.Obj(), nullptr), L"WriteSource(PNG frame)");
			CheckImagingResult(frame->Commit(), L"Commit(PNG frame)");
			CheckImagingResult(encoder->Commit(), L"Commit(PNG encoder)");
			STATSTG stat = {};
			CheckImagingResult(png->Stat(&stat, STATFLAG_NONAME), L"Stat(PNG stream)");
			CheckImagingResult(png->Seek({}, STREAM_SEEK_SET, nullptr), L"Seek(PNG stream)");
			stream::MemoryStream base64;
			{
				stream::UtfGeneralEncoder<wchar_t, char8_t> widenEncoder;
				stream::EncoderStream widenStream(base64, widenEncoder);
				stream::Utf8Base64Encoder base64Encoder;
				stream::EncoderStream base64Stream(widenStream, base64Encoder);
				char bytes[16384];
				for (ULONGLONG remaining = stat.cbSize.QuadPart; remaining > 0;)
				{
					ULONG read = 0;
					CheckImagingResult(png->Read(bytes, (ULONG)(remaining < sizeof(bytes) ? remaining : sizeof(bytes)), &read), L"Read(PNG stream)");
					if (!read) throw Exception(L"PNG stream ended unexpectedly.");
					base64Stream.Write(bytes, read);
					remaining -= read;
				}
			}
			base64.SeekFromBegin(0);
			stream::StreamReader reader(base64);
			snapshot.dataUrl = L"data:image/png;base64," + reader.ReadToEnd();
			snapshots.Add(std::move(snapshot));
		}
	}
}
