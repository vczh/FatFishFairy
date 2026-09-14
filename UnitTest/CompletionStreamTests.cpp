#include "../Agents/Json.h"

using namespace vl;
using namespace vl::glr;
using namespace fatfish;

TEST_FILE
{
	TEST_CASE(L"Streaming completions assemble interleaved calls and reject incomplete or conflicting events")
	{
		json::Parser parser;
		auto stream = WString(LR"sse(:
event: completion
id: 1
data: {"choices":[{"index":0,"delta":{"role":"assistant","content":"隐","tool_calls":[{"index":1,"id":"second","type":"function","function":{"name":"file_list","arguments":"{"}},{"index":0,"id":"first","type":"function","function":{"name":"speak","arguments":"{\"text\":"}}]},"finish_reason":null}]}

data: {"choices":[{"index":0,"delta":{"content":"藏","tool_calls":[{"index":0,"id":null,"type":null,"function":{"name":null,"arguments":"\"你好\"}"}},{"index":1,"function":{"arguments":"}"}}]},"finish_reason":null}]}

data: {"choices":[{"index":0,"delta":{},
data: "finish_reason":"tool_calls"}]}

data: {"choices":[],"usage":{"total_tokens":42}}

data: [DONE]

)sse");
		auto check = [&](const WString& input)
		{
			auto response = ParseChatCompletion(input, parser);
			auto choice = GetField(response, L"choices").Cast<json::JsonArray>()->items[0];
			auto message = GetField(choice, L"message");
			TEST_ASSERT(GetString(message, L"content") == L"隐藏"); // Stream content must concatenate.
			TEST_ASSERT(GetString(choice, L"finish_reason") == L"tool_calls"); // Stream finish reason.
			auto calls = GetField(message, L"tool_calls").Cast<json::JsonArray>();
			TEST_ASSERT(calls->items.Count() == 2 && GetString(calls->items[0], L"id") == L"first" && GetString(calls->items[1], L"id") == L"second"); // Interleaved stream calls must be ordered by index.
			auto function = GetField(calls->items[0], L"function");
			TEST_ASSERT(GetString(function, L"name") == L"speak" && GetString(function, L"arguments") == L"{\"text\":\"你好\"}"); // Keep initial name and append argument fragments.
			TEST_ASSERT(GetString(GetField(calls->items[1], L"function"), L"arguments") == L"{}"); // Second call arguments.
		};
		check(stream);
		WString crlf;
		for (vint i = 0; i < stream.Length(); i++) crlf += stream[i] == L'\n' ? WString(L"\r\n") : WString::FromChar(stream[i]);
		check(crlf);
		for (auto invalid : {
			WString(L"data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"),
			WString(L"data: [DONE]\n\n"),
			WString(L"x\n\n"),
			stream + L"data: {}\n\n",
			WString(LR"sse(data: {"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"a"},{"index":0,"id":"b"}]},"finish_reason":"tool_calls"}]}

data: [DONE]

)sse")
		})
		{
			TEST_EXCEPTION(ParseChatCompletion(invalid, parser), Exception, [](const Exception&) {});
		}
	});
}
