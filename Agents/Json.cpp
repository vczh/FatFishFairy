#include "Json.h"

using namespace vl;
using namespace vl::glr;
using namespace vl::collections;

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

	void MergeCompletionField(Ptr<json::JsonObject> target, Ptr<json::JsonNode> delta, const WString& name, bool append)
	{
		auto raw = GetField(delta, name);
		if (!raw) return;
		auto null = raw.Cast<json::JsonLiteral>();
		if (null && null->value == json::JsonLiteralValue::Null) return;
		auto value = raw.Cast<json::JsonString>();
		if (!value) throw Exception(L"Invalid text field in completion stream: " + name);
		auto previous = GetField(target, name).Cast<json::JsonString>();
		if (!append && previous && previous->content.value != value->content.value)
			throw Exception(L"Conflicting field in completion stream: " + name);
		SetString(target, name, append && previous ? previous->content.value + value->content.value : value->content.value);
	}

	vint GetCompletionIndex(Ptr<json::JsonNode> object)
	{
		auto number = GetField(object, L"index").Cast<json::JsonNumber>();
		bool success = false;
		auto index = number ? wtoi_test(number->content.value, success) : -1;
		if (!success || index < 0 || index >= 32) throw Exception(L"Invalid index in completion stream.");
		return index;
	}

	Ptr<json::JsonNode> ParseChatCompletion(const WString& text, json::Parser& parser)
	{
		vint first = 0;
		while (first < text.Length() && (text[first] == L' ' || text[first] == L'\t' || text[first] == L'\r' || text[first] == L'\n')) first++;
		// Also accept providers that return JSON despite a streaming request.
		if (first < text.Length() && text[first] == L'{') return ParseJson(text, parser);
		auto message = Ptr(new json::JsonObject);
		auto choice = Ptr(new json::JsonObject);
		Dictionary<vint, Ptr<json::JsonObject>> calls;
		bool done = false;
		bool finished = false;
		WString eventData;
		auto consumeEvent = [&]
		{
			if (eventData.Length() == 0) return;
			auto data = eventData;
			eventData = L"";
			if (done) throw Exception(L"Completion data received after stream end.");
			if (data == L"[DONE]")
			{
				done = true;
				return;
			}
			auto chunk = ParseJson(data, parser);
			if (GetField(chunk, L"error")) throw Exception(L"The model server reported a streaming error.");
			auto choices = GetField(chunk, L"choices").Cast<json::JsonArray>();
			if (!choices) throw Exception(L"Missing choices in completion stream.");
			// Empty choices can carry usage after the final completion chunk.
			for (auto current : choices->items)
			{
				if (GetCompletionIndex(current) != 0) throw Exception(L"Unexpected additional completion choice.");
				if (finished) throw Exception(L"Completion data received after finish reason.");
				auto delta = GetField(current, L"delta");
				if (!delta.Cast<json::JsonObject>()) throw Exception(L"Missing delta in completion stream.");
				MergeCompletionField(message, delta, L"role", false);
				MergeCompletionField(message, delta, L"content", true);
				MergeCompletionField(message, delta, L"refusal", true);
				auto rawCalls = GetField(delta, L"tool_calls");
				auto deltaCalls = rawCalls.Cast<json::JsonArray>();
				auto nullCalls = rawCalls.Cast<json::JsonLiteral>();
				if (rawCalls && !deltaCalls && (!nullCalls || nullCalls->value != json::JsonLiteralValue::Null))
					throw Exception(L"Invalid tool calls in completion stream.");
				if (deltaCalls)
				{
					for (auto callDelta : deltaCalls->items)
					{
						auto index = GetCompletionIndex(callDelta);
						if (!calls.Keys().Contains(index))
						{
							auto call = Ptr(new json::JsonObject);
							auto function = Ptr(new json::JsonObject);
							SetString(function, L"arguments", L"");
							SetField(call, L"function", function);
							calls.Add(index, call);
						}
						auto call = calls[index];
						MergeCompletionField(call, callDelta, L"id", false);
						MergeCompletionField(call, callDelta, L"type", false);
						auto functionDelta = GetField(callDelta, L"function");
						if (functionDelta)
						{
							auto function = GetField(call, L"function").Cast<json::JsonObject>();
							MergeCompletionField(function, functionDelta, L"name", false);
							MergeCompletionField(function, functionDelta, L"arguments", true);
						}
					}
				}
				auto finish = GetField(current, L"finish_reason").Cast<json::JsonString>();
				if (finish)
				{
					SetString(choice, L"finish_reason", finish->content.value);
					finished = true;
				}
			}
		};
		vint start = first;
		for (vint i = first; i <= text.Length(); i++)
		{
			if (i != text.Length() && text[i] != L'\r' && text[i] != L'\n') continue;
			auto line = text.Sub(start, i - start);
			if (i < text.Length() && text[i] == L'\r' && i + 1 < text.Length() && text[i + 1] == L'\n') i++;
			start = i + 1;
			if (line.Length() == 0) consumeEvent();
			else if (line.Length() >= 5 && line.Left(5) == L"data:")
			{
				auto value = line.Sub(5, line.Length() - 5);
				if (value.Length() > 0 && value[0] == L' ') value = value.Sub(1, value.Length() - 1);
				if (eventData.Length() > 0) eventData += L"\n";
				eventData += value;
			}
			else if (line[0] != L':'
				&& !(line.Length() >= 6 && line.Left(6) == L"event:")
				&& !(line.Length() >= 3 && line.Left(3) == L"id:")
				&& !(line.Length() >= 6 && line.Left(6) == L"retry:"))
				throw Exception(L"Invalid completion event stream.");
		}
		consumeEvent();
		if (!done || !finished) throw Exception(L"Incomplete completion stream.");
		if (calls.Count() > 0)
		{
			auto items = Ptr(new json::JsonArray);
			for (vint i = 0; i < calls.Count(); i++)
			{
				if (calls.Keys()[i] != i) throw Exception(L"Missing tool call index in completion stream.");
				items->items.Add(calls.Values()[i]);
			}
			SetField(message, L"tool_calls", items);
		}
		SetField(choice, L"message", message);
		auto choices = Ptr(new json::JsonArray);
		choices->items.Add(choice);
		auto response = Ptr(new json::JsonObject);
		SetField(response, L"choices", choices);
		return response;
	}
}
