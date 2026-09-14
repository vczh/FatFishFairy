#include "../Agents/Output.h"

using namespace vl;
using namespace vl::glr;
using namespace fatfish;

TEST_FILE
{
	TEST_CASE(L"Speech output decodes JSON escapes into the requested block")
	{
		json::Parser parser;
		auto speech = WString(LR"({"role":"assistant","content":null,"tool_calls":[{"id":"say","type":"function","function":{"name":"speak","arguments":"{\"text\":\"第一行\\n\\\"第二行\\\"\"}"}}]})");
		TEST_ASSERT(FormatAgentResponse(true, speech, parser) == L"Vision (speak)>\n****************\n第一行\n\"第二行\"\n****************"); // Speak output must decode JSON escapes and use the requested block format.
	});

	TEST_CASE(L"Mixed output preserves call order without duplicating speech or ordinary content")
	{
		json::Parser parser;
		auto mixed = WString(LR"({"role":"assistant","content":"说明","tool_calls":[{"id":"a","type":"function","function":{"name":"speak","arguments":"{\"text\":\"先说\"}"}},{"id":"b","type":"function","function":{"name":"file_list","arguments":"{}"}},{"id":"c","type":"function","function":{"name":"speak","arguments":"{\"text\":\"\"}"}},{"id":"d","type":"function","function":{"name":"speak","arguments":"{bad"}}]})");
		auto formatted = FormatAgentResponse(false, mixed, parser);
		auto first = wcsstr(formatted.Buffer(), L"Fairy (speak)>\n****************\n先说\n****************");
		auto tool = wcsstr(formatted.Buffer(), L"\"name\":\"file_list\"");
		auto second = wcsstr(formatted.Buffer(), L"Fairy (speak)>\n****************\n\n****************");
		auto invalid = wcsstr(formatted.Buffer(), L"{bad");
		TEST_ASSERT(first && tool && second && invalid && first < tool && tool < second && second < invalid); // Keep mixed calls in order, show empty speech, and preserve malformed calls as JSON.
		auto content = wcsstr(formatted.Buffer(), L"说明");
		TEST_ASSERT(content && !wcsstr(content + 2, L"说明")); // Ordinary content must be logged exactly once in mixed responses.
		TEST_ASSERT(!wcsstr(formatted.Buffer(), L"先说\\\"")); // Do not duplicate rendered speech inside JSON.
	});

	TEST_CASE(L"Non-speech and malformed speech retain their original JSON")
	{
		json::Parser parser;
		for (auto unchanged : {
			LR"({"role":"assistant","content":"普通回复"})",
			LR"({"role":"assistant","content":""})",
			LR"({"role":"assistant","tool_calls":[{"id":"list","type":"function","function":{"name":"file_list","arguments":"{}"}}]})",
			LR"({"role":"assistant","tool_calls":[{"id":"bad","type":"function","function":{"name":"speak","arguments":"{\"text\":true}"}}]})"
		})
		{
			TEST_ASSERT(FormatAgentResponse(false, unchanged, parser) == L"Fairy> " + WString(unchanged)); // Keep non-speech and malformed speech responses as their original JSON.
		}
	});
}
