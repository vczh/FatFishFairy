#include "Json.h"

using namespace vl;
using namespace vl::glr;

namespace fatfish
{
	Ptr<json::JsonNode> ParseJson(const WString& text, json::Parser& parser)
	{
		// Parser diagnostics can contain input, including private server responses.
		// Token errors must throw, rather than being skipped by the default parser.
		auto handler = parser.OnError.Add(Func<void(ErrorArgs&)>([](ErrorArgs&) { throw Exception(L"Invalid JSON."); }));
		try
		{
			auto result = json::JsonParse(text, parser);
			parser.OnError.Remove(handler);
			if (result) return result;
		}
		catch (...)
		{
			parser.OnError.Remove(handler);
			throw Exception(L"Invalid JSON.");
		}
		throw Exception(L"Invalid JSON.");
	}

	Ptr<json::JsonNode> GetField(Ptr<json::JsonNode> object, const WString& name)
	{
		auto fields = object.Cast<json::JsonObject>();
		if (!fields) throw Exception(L"Expected a JSON object.");
		Ptr<json::JsonNode> result;
		for (auto field : fields->fields)
		{
			if (field->name.value == name)
			{
				if (result) throw Exception(L"Duplicate JSON field: " + name);
				result = field->value;
			}
		}
		return result;
	}

	WString GetString(Ptr<json::JsonNode> object, const WString& name)
	{
		auto value = GetField(object, name).Cast<json::JsonString>();
		if (!value) throw Exception(L"Expected a string field: " + name);
		return value->content.value;
	}

	void SetField(Ptr<json::JsonObject> object, const WString& name, Ptr<json::JsonNode> value)
	{
		for (auto field : object->fields)
		{
			if (field->name.value == name)
			{
				field->value = value;
				return;
			}
		}
		auto field = Ptr(new json::JsonObjectField);
		field->name.value = name;
		field->value = value;
		object->fields.Add(field);
	}

	void SetString(Ptr<json::JsonObject> object, const WString& name, const WString& value)
	{
		auto text = Ptr(new json::JsonString);
		text->content.value = value;
		SetField(object, name, text);
	}

	Ptr<json::JsonObject> TextMessage(const WString& role, const WString& content)
	{
		auto message = Ptr(new json::JsonObject);
		SetString(message, L"role", role);
		SetString(message, L"content", content);
		return message;
	}

	void SetBoolean(Ptr<json::JsonObject> object, const WString& name, bool value)
	{
		auto node = Ptr(new json::JsonLiteral);
		node->value = value ? json::JsonLiteralValue::True : json::JsonLiteralValue::False;
		SetField(object, name, node);
	}

	void SetInteger(Ptr<json::JsonObject> object, const WString& name, vint value)
	{
		auto node = Ptr(new json::JsonNumber);
		node->content.value = itow(value);
		SetField(object, name, node);
	}
}
