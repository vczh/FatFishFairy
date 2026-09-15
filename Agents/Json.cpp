#include "Json.h"

using namespace vl;
using namespace vl::glr;
using namespace vl::collections;

namespace fatfish
{
	ChatCompletionError::ChatCompletionError()
		: Exception(L"The model server reported an error.")
	{
	}

	ChatCompletionError::ChatCompletionError(const WString& sanitizedMessage)
		: Exception(sanitizedMessage)
	{
	}

	ContextLimitExceeded::ContextLimitExceeded()
		: ChatCompletionError(L"The model request exceeds the model's context limit.")
	{
	}

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

	void ThrowIfChatCompletionError(Ptr<json::JsonNode> response, vint httpStatus)
	{
		if (!response.Cast<json::JsonObject>()) return;
		bool contextLimit = false;
		try
		{
			auto optionalString = [](Ptr<json::JsonNode> object, const WString& name)
			{
				auto value = GetField(object, name).Cast<json::JsonString>();
				return value ? wlower(value->content.value) : WString();
			};
			auto error = GetField(response, L"error");
			auto nullError = error.Cast<json::JsonLiteral>();
			if (!error || (nullError && nullError->value == json::JsonLiteralValue::Null))
			{
				// Some compatible servers return an error object without an envelope.
				if (optionalString(response, L"object") != L"error" && optionalString(response, L"type") != L"error") return;
				error = response;
			}
			WString code, type, message;
			if (error.Cast<json::JsonObject>())
			{
				code = optionalString(error, L"code");
				type = optionalString(error, L"type");
				message = optionalString(error, L"message");
			}
			else
			{
				auto text = error.Cast<json::JsonString>();
				if (text) message = wlower(text->content.value);
				type = optionalString(response, L"error_type");
			}
			auto contains = [](const WString& text, const wchar_t* part) { return wcsstr(text.Buffer(), part) != nullptr; };
			auto permittedStatus = (httpStatus >= 200 && httpStatus < 300) || httpStatus == 400 || httpStatus == 413 || httpStatus == 422;
			bool unrelatedError = false;
			for (auto marker : { L"rate_limit", L"quota", L"authentication", L"permission", L"unauthorized", L"forbidden", L"server_error", L"internal_error", L"overloaded", L"max_tokens", L"max_output_tokens", L"max_completion_tokens", L"output_limit", L"output_length" })
			{
				if (contains(code, marker) || contains(type, marker)) unrelatedError = true;
			}
			for (auto marker : { L"rate limit", L"tokens per minute", L"requests per minute", L"quota", L"output token limit", L"maximum output tokens", L"max_tokens must", L"max_tokens is too", L"max_completion_tokens must", L"max_output_tokens must", L"completion token limit" })
			{
				if (contains(message, marker)) unrelatedError = true;
			}
			if (permittedStatus && !unrelatedError)
			{
				for (auto known : { L"context_length_exceeded", L"context_window_exceeded", L"context_limit_exceeded", L"max_context_length_exceeded", L"prompt_too_long", L"input_too_long", L"input_tokens_exceeded" })
				{
					if (code == known || type == known) contextLimit = true;
				}
				// Keep the fallback narrow: generic token limits can mean rate limits
				// or invalid output settings, neither of which is fixed by trimming history.
				contextLimit = contextLimit
					|| (contains(message, L"maximum context length") && (contains(message, L"exceed") || contains(message, L"requested")))
					|| ((contains(message, L"context window") || contains(message, L"context length") || contains(message, L"context size"))
						&& (contains(message, L"exceed") || contains(message, L"too long")))
					|| contains(message, L"prompt is too long") || contains(message, L"prompt too long")
					|| (contains(message, L"input is too long") && contains(message, L"tokens"));
			}
		}
		catch (const Exception&)
		{
			// Malformed error metadata must never leak response data or be sent
			// back to the model as a request to repair its assistant formatting.
			throw ChatCompletionError();
		}
		if (contextLimit) throw ContextLimitExceeded();
		throw ChatCompletionError();
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
		if (first < text.Length() && text[first] == L'{')
		{
			auto response = ParseJson(text, parser);
			ThrowIfChatCompletionError(response);
			return response;
		}
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
			ThrowIfChatCompletionError(chunk);
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
