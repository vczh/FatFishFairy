#include "Output.h"

using namespace vl;
using namespace vl::glr;

namespace fatfish
{
	bool ReadSpeakText(Ptr<json::JsonNode> call, json::Parser& parser, WString& text)
	{
		try
		{
			if (GetString(call, L"type") != L"function") return false;
			auto function = GetField(call, L"function");
			if (GetString(function, L"name") != L"speak") return false;
			auto arguments = ParseJson(GetString(function, L"arguments"), parser);
			text = GetString(arguments, L"text");
			return true;
		}
		catch (const Exception&)
		{
			// Keep invalid calls visible as JSON; the runtime returns their errors.
			return false;
		}
	}

	WString FormatAgentResponse(bool vision, const WString& message, json::Parser& parser)
	{
		auto agent = WString(vision ? L"Vision" : L"Fairy");
		auto original = ParseJson(message, parser).Cast<json::JsonObject>();
		if (!original) return agent + L"> " + message;
		auto calls = GetField(original, L"tool_calls").Cast<json::JsonArray>();
		if (!calls || calls->items.Count() == 0) return agent + L"> " + message;
		WString output;
		auto pending = Ptr(new json::JsonArray);
		bool firstJson = true;
		bool hasSpeak = false;
		auto content = GetField(original, L"content").Cast<json::JsonString>();
		auto append = [&](const WString& part)
		{
			if (output.Length() > 0) output += L"\n";
			output += part;
		};
		auto flushJson = [&](bool includeContent)
		{
			if (pending->items.Count() == 0 && !includeContent) return;
			auto part = Ptr(new json::JsonObject);
			for (auto field : original->fields)
			{
				if (field->name.value != L"tool_calls" && (firstJson || field->name.value != L"content"))
					SetField(part, field->name.value, field->value);
			}
			if (pending->items.Count() > 0) SetField(part, L"tool_calls", pending);
			append(agent + L"> " + json::JsonToString(part));
			pending = Ptr(new json::JsonArray);
			firstJson = false;
		};
		for (auto call : calls->items)
		{
			WString text;
			if (ReadSpeakText(call, parser, text))
			{
				hasSpeak = true;
				flushJson(firstJson && content && content->content.value.Length() > 0);
				append(agent + L" (speak)>\n****************\n" + text + L"\n****************");
			}
			else
			{
				pending->items.Add(call);
			}
		}
		if (!hasSpeak) return agent + L"> " + message;
		flushJson(false);
		return output;
	}
}
