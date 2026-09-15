#ifndef FATFISH_JSON_H
#define FATFISH_JSON_H

#include <VlppGlrParser.h>

namespace fatfish
{
	// Server rejections are distinct from malformed assistant replies. Messages
	// must remain sanitized because a gateway can echo submitted credentials.
	class ChatCompletionError : public vl::Exception
	{
	public:
		ChatCompletionError();
		explicit ChatCompletionError(const vl::WString& sanitizedMessage);
	};

	class ContextLimitExceeded : public ChatCompletionError
	{
	public:
		ContextLimitExceeded();
	};

	extern vl::Ptr<vl::glr::json::JsonNode> ParseJson(const vl::WString& text, vl::glr::json::Parser& parser);
	extern vl::Ptr<vl::glr::json::JsonNode> ParseChatCompletion(const vl::WString& text, vl::glr::json::Parser& parser);
	// Recognize JSON/SSE error envelopes. HTTP status takes precedence over body
	// claims: authentication, rate limiting and server failures are never context errors.
	extern void ThrowIfChatCompletionError(vl::Ptr<vl::glr::json::JsonNode> response, vl::vint httpStatus = 200);
	extern vl::Ptr<vl::glr::json::JsonNode> GetField(vl::Ptr<vl::glr::json::JsonNode> object, const vl::WString& name);
	extern vl::WString GetString(vl::Ptr<vl::glr::json::JsonNode> object, const vl::WString& name);
	extern void SetField(vl::Ptr<vl::glr::json::JsonObject> object, const vl::WString& name, vl::Ptr<vl::glr::json::JsonNode> value);
	extern void SetString(vl::Ptr<vl::glr::json::JsonObject> object, const vl::WString& name, const vl::WString& value);
	extern void SetBoolean(vl::Ptr<vl::glr::json::JsonObject> object, const vl::WString& name, bool value);
	extern void SetInteger(vl::Ptr<vl::glr::json::JsonObject> object, const vl::WString& name, vl::vint value);
	extern vl::Ptr<vl::glr::json::JsonObject> TextMessage(const vl::WString& role, const vl::WString& content);
}

#endif
