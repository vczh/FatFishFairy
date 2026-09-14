#include "Runtime.h"
#include <Windows.h>

using namespace vl;
using namespace vl::collections;
using namespace vl::filesystem;
using namespace vl::glr;

namespace fatfish
{
	extern void RunMemoryTests();
	extern void RunPlatformTests();

	class RuntimeTestFolder
	{
	public:
		FilePath root;

		RuntimeTestFolder()
		{
			wchar_t temporary[MAX_PATH] = {};
			wchar_t unique[MAX_PATH] = {};
			CHECK_ERROR(GetTempPathW(MAX_PATH, temporary) != 0, L"GetTempPath failed.");
			CHECK_ERROR(GetTempFileNameW(temporary, L"fff", 0, unique) != 0, L"GetTempFileName failed.");
			root = FilePath(unique);
			CHECK_ERROR(File(root).Delete() && Folder(root).Create(false), L"Test folder creation failed.");
		}

		~RuntimeTestFolder() noexcept(false)
		{
			wchar_t temporary[MAX_PATH] = {};
			GetTempPathW(MAX_PATH, temporary);
			auto prefix = FilePath(temporary).GetFullPath() + L"\\";
			auto path = root.GetFullPath();
			CHECK_ERROR(path.Length() > prefix.Length() && path.Left(prefix.Length()) == prefix && root.GetName().Left(3) == L"fff", L"Unsafe test cleanup path.");
			CHECK_ERROR(Folder(root).Delete(true), L"Test cleanup failed.");
		}
	};

	void RunRuntimeTests()
	{
		RuntimeTestFolder folder;
		json::Parser parser;
		ApiConfig config;
		config.visionModel = L"test-vision";
		config.fairyModel = L"test-fairy";
		AgentPrompts prompts{ L"工具说明", L"记忆指引", L"视觉请求", L"精灵请求", L"固定性格" };
		vint captures = 0;
		vint requestIndex = 0;
		auto capture = [&](List<MonitorSnapshot>& snapshots)
		{
			captures++;
			for (vint i = 0; i < 2; i++)
			{
				MonitorSnapshot snapshot;
				snapshot.name = L"display-" + itow(i);
				snapshot.left = i == 0 ? -1920 : 0;
				snapshot.top = 0;
				snapshot.width = 1920;
				snapshot.height = 1080;
				snapshot.dataUrl = L"data:image/png;base64,fixture";
				snapshots.Add(snapshot);
			}
		};
		auto terminal = WString(LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":""}}]})");
		auto complete = [&](const WString& body)
		{
			auto request = ParseJson(body, parser);
			auto model = GetString(request, L"model");
			auto messages = GetField(request, L"messages").Cast<json::JsonArray>();
			auto system = GetString(messages->items[0], L"content");
			CHECK_ERROR(wcsstr(system.Buffer(), L"工具说明") && wcsstr(system.Buffer(), L"记忆指引"), L"Prompts must accompany every request.");
			CHECK_ERROR(GetField(request, L"tools").Cast<json::JsonArray>()->items.Count() == 7, L"Both agents need all tools.");
			switch (requestIndex++)
			{
			case 0:
			case 4:
			{
				CHECK_ERROR(model == config.visionModel && messages->items.Count() == 2, L"Vision history must start fresh.");
				CHECK_ERROR(!wcsstr(system.Buffer(), L"固定性格"), L"Only fairy receives the character prompt.");
				auto parts = GetField(messages->items[1], L"content").Cast<json::JsonArray>();
				CHECK_ERROR(parts->items.Count() == 4, L"Each monitor needs metadata and an image.");
				CHECK_ERROR(GetString(parts->items[1], L"type") == L"image_url", L"Image content type.");
				CHECK_ERROR(GetString(GetField(parts->items[3], L"image_url"), L"url") == L"data:image/png;base64,fixture", L"Second monitor missing.");
				return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":null,"tool_calls":[{"id":"vision-speak","type":"function","function":{"name":"speak","arguments":"{\"text\":\"用户正在阅读 C++ 代码。\"}"}}]}}]})");
			}
			case 1:
			case 5:
				CHECK_ERROR(model == config.visionModel && messages->items.Count() == 4, L"Vision tool result history.");
				CHECK_ERROR(GetString(messages->items[3], L"tool_call_id") == L"vision-speak", L"Tool result must match call id.");
				return terminal;
			case 2:
				CHECK_ERROR(model == config.fairyModel && messages->items.Count() == 2, L"Initial fairy history.");
				CHECK_ERROR(wcsstr(system.Buffer(), L"固定性格"), L"Fairy needs character.");
				CHECK_ERROR(wcsstr(GetString(messages->items[1], L"content").Buffer(), L"C++"), L"Vision description must reach fairy.");
				return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":null,"tool_calls":[{"id":"write","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"interests/cpp.md\",\"content\":\"用户阅读 C++\"}"}},{"id":"bad-path","type":"function","function":{"name":"file_read","arguments":"{\"path\":\"../env/apikey.json\"}"}},{"id":"say","type":"function","function":{"name":"speak","arguments":"{\"text\":\"今天也在研究 C++ 呀。\"}"}}]}}]})");
			case 3:
				CHECK_ERROR(messages->items.Count() == 6, L"All tool calls need responses.");
				CHECK_ERROR(GetString(messages->items[4], L"tool_call_id") == L"bad-path", L"Invalid tool response id.");
				CHECK_ERROR(GetField(ParseJson(GetString(messages->items[4], L"content"), parser), L"ok").Cast<json::JsonLiteral>()->value == json::JsonLiteralValue::False, L"Invalid tool arguments must be returned as errors.");
				return terminal;
			case 6:
				CHECK_ERROR(model == config.fairyModel && messages->items.Count() == 8, L"Fairy conversation must persist across rounds.");
				return WString(LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"继续加油。"}}]})");
			default:
				throw Exception(L"Unexpected fixture request.");
			}
		};
		auto web = [](const WString&) -> WebResponse { throw Exception(L"Unexpected network request in offline test."); };
		FairyApplication application(folder.root, config, prompts, complete, capture, web);
		CHECK_ERROR(application.RunRound() == L"今天也在研究 C++ 呀。", L"Fairy speak output.");
		CHECK_ERROR(application.RunRound() == L"继续加油。", L"Direct content compatibility.");
		CHECK_ERROR(captures == 2 && requestIndex == 7, L"Each round must capture once and run vision before fairy.");
		CHECK_ERROR(File(folder.root / L"memory" / L"interests" / L"cpp.md").ReadAllTextByBom() == L"用户阅读 C++", L"Tool memory must persist to disk.");

		// A failed fairy response must not poison the next round's conversation.
		vint attempt = 0;
		auto flaky = [&](const WString& body)
		{
			auto request = ParseJson(body, parser);
			if (GetString(request, L"model") == config.visionModel)
				return WString(LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"观察"}}]})");
			CHECK_ERROR(GetField(request, L"messages").Cast<json::JsonArray>()->items.Count() == 2, L"Failed fairy round must roll back history.");
			if (attempt++ == 0) return WString(LR"({"choices":[{"finish_reason":"length","message":{"role":"assistant","content":"截断"}}]})");
			return terminal;
		};
		FairyApplication retry(folder.root, config, prompts, flaky, capture, web);
		bool rejected = false;
		try { retry.RunRound(); }
		catch (const Exception&) { rejected = true; }
		CHECK_ERROR(rejected, L"Incomplete completions must fail.");
		CHECK_ERROR(retry.RunRound().Length() == 0, L"Fairy may stay silent.");

		for (auto response : {
			LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":true}}]})",
			LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","tool_calls":true}}]})",
			LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","tool_calls":[{"id":"write","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"should-not-exist.md\",\"content\":\"bad\"}"}}]}}]})",
			LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"duplicate","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"should-not-exist.md\",\"content\":\"bad\"}"}},{"id":"duplicate","type":"function","function":{"name":"speak","arguments":"{}"}}]}}]})"
		})
		{
			auto malformed = [=](const WString&) { return WString(response); };
			FairyApplication invalid(folder.root, config, prompts, malformed, capture, web);
			rejected = false;
			try { invalid.RunRound(); }
			catch (const Exception&) { rejected = true; }
			CHECK_ERROR(rejected && !File(folder.root / L"memory" / L"should-not-exist.md").Exists(), L"Malformed envelope must not execute tools.");
		}

		vint loopRequests = 0;
		auto looping = [&](const WString&)
		{
			loopRequests++;
			return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"repeat","type":"function","function":{"name":"file_list","arguments":"{}"}}]}}]})");
		};
		FairyApplication loop(folder.root, config, prompts, looping, capture, web);
		rejected = false;
		try { loop.RunRound(); }
		catch (const Exception&) { rejected = true; }
		CHECK_ERROR(rejected && loopRequests == 24, L"Tool loop must have a finite request budget.");
	}

	void RunSelfTests()
	{
		RunMemoryTests();
		RunPlatformTests();
		RunRuntimeTests();
	}
}
