#ifndef FATFISH_JSON_H
#define FATFISH_JSON_H

#include <VlppGlrParser.h>

namespace fatfish
{
	extern vl::Ptr<vl::glr::json::JsonNode> ParseJson(const vl::WString& text, vl::glr::json::Parser& parser);
	extern vl::Ptr<vl::glr::json::JsonNode> GetField(vl::Ptr<vl::glr::json::JsonNode> object, const vl::WString& name);
	extern vl::WString GetString(vl::Ptr<vl::glr::json::JsonNode> object, const vl::WString& name);
	extern void SetField(vl::Ptr<vl::glr::json::JsonObject> object, const vl::WString& name, vl::Ptr<vl::glr::json::JsonNode> value);
	extern void SetString(vl::Ptr<vl::glr::json::JsonObject> object, const vl::WString& name, const vl::WString& value);
	extern void SetBoolean(vl::Ptr<vl::glr::json::JsonObject> object, const vl::WString& name, bool value);
	extern void SetInteger(vl::Ptr<vl::glr::json::JsonObject> object, const vl::WString& name, vl::vint value);
	extern vl::Ptr<vl::glr::json::JsonObject> TextMessage(const vl::WString& role, const vl::WString& content);
}

#endif
