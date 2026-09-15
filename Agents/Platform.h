#ifndef FATFISH_AGENTS_PLATFORM
#define FATFISH_AGENTS_PLATFORM

#include <VlppGlrParser.h>
#include <VlppOS.h>

namespace fatfish
{
	class OperationCancelled : public vl::Exception
	{
	public:
								OperationCancelled();
	};

	// Shared by the desktop UI and its worker. Cancellation stays signaled permanently.
	class CancellationToken : public vl::Object
	{
	private:
		vl::EventObject			eventCancelled;

	public:
								CancellationToken();
		void					Cancel();
		bool					IsCancelled();
		void					ThrowIfCancelled();
		vl::EventObject&		Event();
	};

	struct ApiConfig
	{
		vl::WString		apiKey;
		vl::WString		url;
		vl::WString		authHeader;
		vl::WString		visionModel;
		vl::WString		fairyModel;
	};

	struct WebResponse
	{
		vl::vint		statusCode = 0;
		vl::WString		contentType;
		vl::WString		body;
		bool			truncated = false;
	};

	struct MonitorSnapshot
	{
		vl::WString		name;
		vl::WString		dataUrl;
		vl::vint		left = 0;
		vl::vint		top = 0;
		vl::vint		width = 0;
		vl::vint		height = 0;
	};

	// A base URL ends in /v1 (or another API prefix); a full /chat/completions URL is also accepted.
	extern vl::WString GetChatCompletionUrl(const vl::WString& baseUrl);
	extern ApiConfig LoadApiConfig(const vl::filesystem::FilePath& envFolder, vl::glr::json::Parser& parser);
	extern vl::WString PostChatCompletion(const ApiConfig& config, const vl::WString& body, vl::Ptr<CancellationToken> cancellation = nullptr);
	extern WebResponse HttpGet(const vl::WString& url, vl::vint maxCharacters = 20000, vl::Ptr<CancellationToken> cancellation = nullptr);
	extern void CaptureMonitors(vl::collections::List<MonitorSnapshot>& snapshots);
}

#endif
