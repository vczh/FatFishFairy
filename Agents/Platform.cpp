#include "Platform.h"
#include "Json.h"
#include <VlppOS.Windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <wtsapi32.h>
#include <exception>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "wtsapi32.lib")

namespace fatfish
{
	using namespace vl;
	using namespace vl::collections;
	using namespace vl::filesystem;
	using namespace vl::glr;
	using namespace vl::inter_process::windows_http;

	OperationCancelled::OperationCancelled()
		: Exception(L"Agent execution was cancelled.")
	{
	}

	ScreenCaptureUnavailable::ScreenCaptureUnavailable()
		: Exception(L"The desktop is unavailable for observation.")
	{
	}

	MonitorCaptureError::MonitorCaptureError(const WString& operation, vuint32_t code)
		: Exception(operation + L" failed (Windows error " + itow(code) + L").")
		, errorCode(code)
	{
	}

	vuint32_t MonitorCaptureError::ErrorCode() const
	{
		return errorCode;
	}

	CancellationToken::CancellationToken()
	{
		CHECK_ERROR(eventCancelled.CreateManualUnsignal(false), L"fatfish::CancellationToken#Cannot create cancellation event.");
	}

	void CancellationToken::Cancel()
	{
		eventCancelled.Signal();
	}

	bool CancellationToken::IsCancelled()
	{
		return eventCancelled.WaitForTime(0);
	}

	void CancellationToken::ThrowIfCancelled()
	{
		if (IsCancelled()) throw OperationCancelled();
	}

	EventObject& CancellationToken::Event()
	{
		return eventCancelled;
	}

	bool WaitForNetwork(EventObject& completed, Ptr<CancellationToken> cancellation, vint milliseconds)
	{
		if (!cancellation) return completed.WaitForTime(milliseconds);
		cancellation->ThrowIfCancelled();
		WaitableObject* events[] = { &cancellation->Event(), &completed };
		bool abandoned = false;
		auto signaled = WaitableObject::WaitAnyForTime(events, 2, milliseconds, &abandoned);
		cancellation->ThrowIfCancelled();
		return signaled == 1;
	}

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

	ApiConfig LoadApiConfig(const FilePath& envFolder, json::Parser& parser)
	{
		WString source;
		stream::BomEncoder::Encoding encoding;
		bool containsBom = false;
		if (!File(envFolder / L"apikey.json").ReadAllTextWithEncodingTesting(source, encoding, containsBom))
		{
			throw Exception(L"Cannot read apikey.json. Copy apikey-template.json and configure the endpoint and dedicated models.");
		}
		Ptr<json::JsonObject> object;
		try
		{
			object = ParseJson(source, parser).Cast<json::JsonObject>();
		}
		catch (const Exception&)
		{
			// Parser diagnostics may contain the original secret-bearing input.
			throw Exception(L"apikey.json is not valid JSON.");
		}
		if (!object) throw Exception(L"apikey.json must contain a JSON object.");
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
			if (!fields.Keys().Contains(name)) throw Exception(L"apikey.json is missing a required field from the template.");
		}
		auto hasFairyModel = fields.Keys().Contains(L"fairy_model");
		auto hasChatModel = fields.Keys().Contains(L"chat_model");
		if (!hasFairyModel && !hasChatModel) throw Exception(L"apikey.json requires fairy_model (or the legacy chat_model field).");
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
		if (!succeeded)
		{
			auto errorCode = GetLastError();
			throw Exception(WString(operation) + L" failed (Windows error " + itow(errorCode) + L").");
		}
	}

	bool IsDesktopSessionAvailable(vint sessionState, vint sessionFlags)
	{
		// Local lock: SessionFlags == WTS_SESSIONSTATE_LOCK, even with WTSActive.
		// Remote Desktop disconnected: SessionState == WTSDisconnected; ignore
		// the lock flag because an unlocked but disconnected session is unavailable too.
		// Remote Desktop connected but its host-side session locked: the connection
		// may remain WTSActive, so require WTS_SESSIONSTATE_UNLOCK independently.
		// LOCK is zero: compare these values, never test them as bit flags.
		if (sessionState < WTSActive || sessionState > WTSInit)
			throw Exception(L"Windows reported an unknown desktop connection state.");
		if (sessionState != WTSActive || sessionFlags == WTS_SESSIONSTATE_LOCK) return false;
		if (sessionFlags == WTS_SESSIONSTATE_UNLOCK) return true;
		throw Exception(L"Windows reported an unknown desktop lock state.");
	}

	bool IsDesktopSessionAvailable()
	{
		// WTS_CURRENT_SESSION identifies the session running this process, whether
		// local or remote. It does not query the RDP client's workstation or another
		// user's console session. Both connection and lock state must permit work.
		// Disconnecting RDP can leave this session present as WTSDisconnected + LOCK,
		// so return false before capture. Locking only the RDP client can leave the
		// host WTSActive + UNLOCK while capture is denied. Thus true only permits a
		// capture attempt; RunRound must also obtain fresh snapshots before either agent.
		// Capture success and an input desktop named "Default" cannot establish this:
		// a locked host session can still allow all monitor captures to succeed.
		struct SessionBuffer
		{
			LPWSTR value = nullptr;
			~SessionBuffer() { if (value) WTSFreeMemory(value); }
		} buffer;
		DWORD bytes = 0;
		CheckPlatformResult(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,
			WTSSessionInfoEx, &buffer.value, &bytes), L"WTSQuerySessionInformation");
		if (!buffer.value || bytes < sizeof(WTSINFOEXW))
			throw Exception(L"Windows returned incomplete desktop session information.");
		auto info = reinterpret_cast<WTSINFOEXW*>(buffer.value);
		if (info->Level != 1) throw Exception(L"Windows returned an unsupported desktop session information level.");
		auto& session = info->Data.WTSInfoExLevel1;
		return IsDesktopSessionAvailable(session.SessionState, session.SessionFlags);
	}

	void EnsureDesktopSessionAvailable()
	{
		if (!IsDesktopSessionAvailable()) throw ScreenCaptureUnavailable();
	}

	struct InternetHandle
	{
		HINTERNET		value = nullptr;
		~InternetHandle() { if (value) WinHttpCloseHandle(value); }
	};

	struct AsyncHttpRequest
	{
		HINTERNET value = nullptr;
		EventObject eventCompleted;
		EventObject eventClosed;
		bool callbackInstalled = false;
		DWORD completionStatus = 0;
		DWORD errorCode = ERROR_SUCCESS;
		DWORD bytesRead = 0;
		char buffer[16384];

		AsyncHttpRequest()
		{
			CHECK_ERROR(eventCompleted.CreateAutoUnsignal(false), L"fatfish::AsyncHttpRequest#Cannot create completion event.");
			CHECK_ERROR(eventClosed.CreateManualUnsignal(false), L"fatfish::AsyncHttpRequest#Cannot create close event.");
		}

		~AsyncHttpRequest()
		{
			if (value)
			{
				// Only this worker uses the handle. An asynchronous operation may still
				// be pending, but the initiating WinHTTP call has already returned.
				WinHttpCloseHandle(value);
				// WinHTTP can still access both the callback context and read buffer
				// after CloseHandle returns. HANDLE_CLOSING is its final notification.
				if (callbackInstalled) eventClosed.Wait();
			}
		}

		static void CALLBACK OnStatus(HINTERNET, DWORD_PTR context, DWORD status, LPVOID information, DWORD length)
		{
			auto self = reinterpret_cast<AsyncHttpRequest*>(context);
			if (!self) return;
			if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
			{
				self->eventClosed.Signal();
			}
			else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE
				|| status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE
				|| status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE
				|| status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
			{
				self->completionStatus = status;
				self->bytesRead = status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ? length : 0;
				self->errorCode = status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR
					? static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError : ERROR_SUCCESS;
				self->eventCompleted.Signal();
			}
		}

		void InstallCallback()
		{
			DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
			CheckPlatformResult(WinHttpSetOption(value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)), L"WinHttpSetOption");
			CheckPlatformResult(WinHttpSetStatusCallback(value, &OnStatus, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0)
				!= WINHTTP_INVALID_STATUS_CALLBACK, L"WinHttpSetStatusCallback");
			callbackInstalled = true;
		}

		void Wait(DWORD expectedStatus, const wchar_t* operation, Ptr<CancellationToken> cancellation)
		{
			if (!WaitForNetwork(eventCompleted, cancellation, 120000))
				throw Exception(L"Chat Completions exceeded its two-minute I/O deadline.");
			if (errorCode != ERROR_SUCCESS)
				throw Exception(WString(operation) + L" failed (Windows error " + itow(errorCode) + L").");
			if (completionStatus != expectedStatus)
				throw Exception(L"Unexpected Chat Completions network status.");
		}
	};

	WString PostChatCompletion(const ApiConfig& config, const WString& body, Ptr<CancellationToken> cancellation)
	{
		if (cancellation) cancellation->ThrowIfCancelled();
		auto url = ParseHttpUrl(GetChatCompletionUrl(config.url));
		auto header = BuildAuthenticationHeader(config) + L"\r\nContent-Type: application/json; charset=utf-8\r\nAccept: text/event-stream, application/json\r\n";
		HttpRequest requestBody;
		requestBody.SetBodyUtf8(body);
		if (requestBody.body.Count() > 256 * 1024 * 1024) throw Exception(L"The Chat Completions request exceeds 256 MiB.");

		// HttpClientApi supports HTTPS, but cannot disable redirect forwarding of custom
		// authentication headers. Use WinHTTP directly for authenticated requests only.
		InternetHandle session{ WinHttpOpen(L"FatFish", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC) };
		CheckPlatformResult(session.value != nullptr, L"WinHttpOpen");
		CheckPlatformResult(WinHttpSetTimeouts(session.value, 30000, 30000, 120000, 120000), L"WinHttpSetTimeouts");
		InternetHandle connection{ WinHttpConnect(session.value, url.host.Buffer(), (INTERNET_PORT)url.port, 0) };
		CheckPlatformResult(connection.value != nullptr, L"WinHttpConnect");
		AsyncHttpRequest request;
		request.value = WinHttpOpenRequest(connection.value, L"POST", url.path.Buffer(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0);
		CheckPlatformResult(request.value != nullptr, L"WinHttpOpenRequest");
		request.InstallCallback();
		DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
		CheckPlatformResult(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)), L"WinHttpSetOption");
		DWORD autoLogon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
		CheckPlatformResult(WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &autoLogon, sizeof(autoLogon)), L"WinHttpSetOption");
		CheckPlatformResult(WinHttpSendRequest(request.value, header.Buffer(), (DWORD)header.Length(), requestBody.body.Count() ? &requestBody.body[0] : nullptr,
			(DWORD)requestBody.body.Count(), (DWORD)requestBody.body.Count(), reinterpret_cast<DWORD_PTR>(&request)), L"WinHttpSendRequest");
		request.Wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, L"WinHttpSendRequest", cancellation);
		CheckPlatformResult(WinHttpReceiveResponse(request.value, nullptr), L"WinHttpReceiveResponse");
		request.Wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, L"WinHttpReceiveResponse", cancellation);
		DWORD status = 0;
		DWORD statusSize = sizeof(status);
		CheckPlatformResult(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX), L"WinHttpQueryHeaders");
		auto readBody = [&](vint maxBytes)
		{
			stream::MemoryStream responseStream;
			while (true)
			{
				if (cancellation) cancellation->ThrowIfCancelled();
				auto remaining = maxBytes - static_cast<vint>(responseStream.Size());
				auto readSize = remaining < static_cast<vint>(sizeof(request.buffer)) ? static_cast<DWORD>(remaining + 1) : static_cast<DWORD>(sizeof(request.buffer));
				CheckPlatformResult(WinHttpReadData(request.value, request.buffer, readSize, nullptr), L"WinHttpReadData");
				request.Wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, L"WinHttpReadData", cancellation);
				auto read = request.bytesRead;
				if (!read) break;
				if (responseStream.Size() + read > maxBytes) throw Exception(L"The Chat Completions response exceeds its size limit.");
				responseStream.Write(request.buffer, read);
			}
			HttpResponse response;
			response.body.Resize((vint)responseStream.Size());
			responseStream.SeekFromBegin(0);
			if (response.body.Count()) responseStream.Read(&response.body[0], response.body.Count());
			WString result;
			if (!response.TryGetBodyUtf8(result)) throw Exception(L"The Chat Completions response is not UTF-8 text.");
			return result;
		};
		if (status < 200 || status >= 300)
		{
			try
			{
				// Inspect bounded error metadata internally. Never echo the body:
				// gateways can reflect submitted credentials or private messages.
				auto errorBody = readBody(64 * 1024);
				json::Parser parser;
				ThrowIfChatCompletionError(ParseJson(errorBody, parser), status);
			}
			catch (const OperationCancelled&)
			{
				throw;
			}
			catch (const ContextLimitExceeded&)
			{
				throw;
			}
			catch (const Exception&)
			{
				// Unrecognized, malformed, oversized and unreadable error bodies
				// keep the known HTTP status without exposing any response text.
			}
			throw ChatCompletionError(L"Chat Completions returned HTTP " + itow(status) + L". Check the endpoint, credentials, and model configuration.");
		}
		return readBody(16 * 1024 * 1024);
	}

	WebResponse HttpGet(const WString& url, vint maxCharacters, Ptr<CancellationToken> cancellation)
	{
		if (cancellation) cancellation->ThrowIfCancelled();
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
		auto finished = WaitForNetwork(completed, cancellation, 120000);
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

	void CheckCaptureResult(bool succeeded, const wchar_t* operation)
	{
		if (!succeeded)
		{
			// Save the thread error before constructing strings or unwinding GDI resources.
			auto errorCode = GetLastError();
			throw MonitorCaptureError(operation, errorCode);
		}
	}

	void CaptureMonitors(List<MonitorSnapshot>& snapshots, const Func<vint()>& enumerateMonitors,
		const Func<MonitorSnapshot(vint)>& captureMonitor)
	{
		snapshots.Clear();
		vint monitorCount;
		try
		{
			monitorCount = enumerateMonitors();
		}
		catch (const MonitorCaptureError& error)
		{
			if (error.ErrorCode() == ERROR_ACCESS_DENIED) throw ScreenCaptureUnavailable();
			throw;
		}
		if (monitorCount == 0) throw ScreenCaptureUnavailable();
		std::exception_ptr firstFailure;
		for (vint index = 0; index < monitorCount; index++)
		{
			try
			{
				snapshots.Add(captureMonitor(index));
			}
			catch (const OperationCancelled&)
			{
				throw;
			}
			catch (const MonitorCaptureError& error)
			{
				if (error.ErrorCode() != ERROR_ACCESS_DENIED && !firstFailure) firstFailure = std::current_exception();
			}
			catch (const Exception&)
			{
				if (!firstFailure) firstFailure = std::current_exception();
			}
		}
		if (snapshots.Count() > 0) return;
		// Keep the original type and message when a non-access failure prevents every capture.
		if (firstFailure) std::rethrow_exception(firstFailure);
		throw ScreenCaptureUnavailable();
	}

	struct CaptureScope
	{
		HRESULT					comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		DPI_AWARENESS_CONTEXT	dpiContext = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		DWORD					dpiError = dpiContext ? ERROR_SUCCESS : GetLastError();
		~CaptureScope()
		{
			if (dpiContext) SetThreadDpiAwarenessContext(dpiContext);
			if (SUCCEEDED(comResult)) CoUninitialize();
		}
	};

	struct MonitorBitmap
	{
		HDC				screen = nullptr;
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
		snapshots.Clear();
		CaptureScope scope;
		if (scope.comResult != RPC_E_CHANGED_MODE) CheckImagingResult(scope.comResult, L"CoInitializeEx");
		if (!scope.dpiContext) throw MonitorCaptureError(L"SetThreadDpiAwarenessContext", scope.dpiError);
		IWICImagingFactory* factoryRaw = nullptr;
		CheckImagingResult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factoryRaw)), L"CoCreateInstance(WIC)");
		ComPtr<IWICImagingFactory> factory(factoryRaw);
		List<HMONITOR> monitors;
		// Clear last error before capture APIs so a failure without its own error cannot reuse stale access denial.
		CaptureMonitors(snapshots, [&]()
		{
			SetLastError(ERROR_SUCCESS);
			CheckCaptureResult(EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL
			{
				reinterpret_cast<List<HMONITOR>*>(context)->Add(monitor);
				return TRUE;
			}, reinterpret_cast<LPARAM>(&monitors)), L"EnumDisplayMonitors");
			return monitors.Count();
		}, [&](vint index)
		{
			MONITORINFOEXW info = {};
			info.cbSize = sizeof(info);
			SetLastError(ERROR_SUCCESS);
			CheckCaptureResult(GetMonitorInfoW(monitors[index], &info), L"GetMonitorInfo");
			MonitorSnapshot snapshot;
			snapshot.name = info.szDevice;
			snapshot.left = info.rcMonitor.left;
			snapshot.top = info.rcMonitor.top;
			snapshot.width = info.rcMonitor.right - info.rcMonitor.left;
			snapshot.height = info.rcMonitor.bottom - info.rcMonitor.top;
			MonitorBitmap bitmap;
			SetLastError(ERROR_SUCCESS);
			bitmap.screen = GetDC(nullptr);
			CheckCaptureResult(bitmap.screen != nullptr, L"GetDC");
			SetLastError(ERROR_SUCCESS);
			bitmap.memory = CreateCompatibleDC(bitmap.screen);
			CheckCaptureResult(bitmap.memory != nullptr, L"CreateCompatibleDC");
			SetLastError(ERROR_SUCCESS);
			bitmap.bitmap = CreateCompatibleBitmap(bitmap.screen, (int)snapshot.width, (int)snapshot.height);
			CheckCaptureResult(bitmap.bitmap != nullptr, L"CreateCompatibleBitmap");
			SetLastError(ERROR_SUCCESS);
			bitmap.previous = SelectObject(bitmap.memory, bitmap.bitmap);
			CheckCaptureResult(bitmap.previous != nullptr && bitmap.previous != HGDI_ERROR, L"SelectObject");
			SetLastError(ERROR_SUCCESS);
			CheckCaptureResult(BitBlt(bitmap.memory, 0, 0, (int)snapshot.width, (int)snapshot.height, bitmap.screen, (int)snapshot.left, (int)snapshot.top, SRCCOPY | CAPTUREBLT), L"BitBlt");
			SetLastError(ERROR_SUCCESS);
			CheckCaptureResult(SelectObject(bitmap.memory, bitmap.previous) == bitmap.bitmap, L"SelectObject(restore)");
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
			return snapshot;
		});
	}
}
