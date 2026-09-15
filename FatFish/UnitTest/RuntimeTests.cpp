#include "../../Agents/Runtime.h"
#include "../../Agents/Desktop.h"
#include <Windows.h>

using namespace vl;
using namespace vl::collections;
using namespace vl::filesystem;
using namespace vl::glr;
using namespace fatfish;

class RuntimeTestFolder
{
public:
	FilePath root;

	RuntimeTestFolder()
	{
		wchar_t temporary[MAX_PATH] = {};
		wchar_t unique[MAX_PATH] = {};
		TEST_ASSERT(GetTempPathW(MAX_PATH, temporary) != 0); // GetTempPath failed.
		TEST_ASSERT(GetTempFileNameW(temporary, L"fff", 0, unique) != 0); // GetTempFileName failed.
		root = FilePath(unique);
		TEST_ASSERT(File(root).Delete() && Folder(root).Create(false)); // Test folder creation failed.
	}

	~RuntimeTestFolder() noexcept(false)
	{
		wchar_t temporary[MAX_PATH] = {};
		GetTempPathW(MAX_PATH, temporary);
		auto prefix = FilePath(temporary).GetFullPath() + L"\\";
		auto path = root.GetFullPath();
		TEST_ASSERT(path.Length() > prefix.Length() && path.Left(prefix.Length()) == prefix && root.GetName().Left(3) == L"fff"); // Unsafe test cleanup path.
		TEST_ASSERT(Folder(root).Delete(true)); // Test cleanup failed.
	}
};

class RuntimeTestClock : public feature_injection::FeatureImpl<IDateTimeImpl>
{
public:
	vuint64_t	currentTime = 0;
	vint		localTimeCalls = 0;

	RuntimeTestClock()
	{
		InjectDateTimeImpl(this);
	}

	~RuntimeTestClock()
	{
		EjectDateTimeImpl(this);
	}

	DateTime FromDateTime(vint year, vint month, vint day, vint hour, vint minute, vint second, vint milliseconds) override
	{
		return Previous()->FromDateTime(year, month, day, hour, minute, second, milliseconds);
	}

	DateTime FromOSInternal(vuint64_t osInternal) override
	{
		return Previous()->FromOSInternal(osInternal);
	}

	vuint64_t LocalTime() override
	{
		localTimeCalls++;
		return currentTime;
	}

	vuint64_t UtcTime() override
	{
		return Previous()->UtcTime();
	}

	vuint64_t LocalToUtcTime(vuint64_t osInternal) override
	{
		return Previous()->LocalToUtcTime(osInternal);
	}

	vuint64_t UtcToLocalTime(vuint64_t osInternal) override
	{
		return Previous()->UtcToLocalTime(osInternal);
	}

	vuint64_t Forward(vuint64_t osInternal, vuint64_t milliseconds) override
	{
		return Previous()->Forward(osInternal, milliseconds);
	}

	vuint64_t Backward(vuint64_t osInternal, vuint64_t milliseconds) override
	{
		return Previous()->Backward(osInternal, milliseconds);
	}
};

U8String ReadRuntimeFixtureBytes(const FilePath& path)
{
	stream::FileStream file(path.GetFullPath(), stream::FileStream::ReadOnly);
	TEST_ASSERT(file.IsAvailable() && file.Size() > 0 && file.Size() < 4096);
	char8_t bytes[4096];
	auto length = static_cast<vint>(file.Size());
	TEST_ASSERT(file.Read(bytes, length) == length);
	return U8String::CopyFrom(bytes, length);
}

class RuntimeWorkerFixture
{
public:
	RuntimeTestFolder folder;
	ApiConfig config;
	AgentPrompts prompts{ L"工具说明", L"记忆指引", L"视觉请求", L"精灵请求", L"固定性格" };
	Ptr<CancellationToken> cancellation = Ptr(new CancellationToken);
	List<WString> requests;
	vint captures = 0;
	WString terminal = LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":""}}]})";
	WString observation = LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"observe","type":"function","function":{"name":"speak","arguments":"{\"text\":\"合成屏幕观察\"}"}}]}}]})";

	RuntimeWorkerFixture()
	{
		config.visionModel = L"test-vision";
		config.fairyModel = L"test-fairy";
	}

	void Capture(List<MonitorSnapshot>& snapshots)
	{
		captures++;
		MonitorSnapshot snapshot;
		snapshot.name = L"synthetic-display";
		snapshot.width = snapshot.height = 1;
		snapshot.dataUrl = L"data:image/png;base64,fixture";
		snapshots.Add(snapshot);
	}

	WString Complete(const WString& body)
	{
		requests.Add(body);
		json::Parser parser;
		auto request = ParseJson(body, parser);
		auto messages = GetField(request, L"messages").Cast<json::JsonArray>();
		if (GetString(request, L"model") == config.visionModel)
			return messages->items.Count() == 2 ? observation : terminal;
		if (GetString(messages->items[messages->items.Count() - 1], L"role") != L"user") return terminal;
		if (messages->items.Count() == 2)
			return LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"answer","type":"function","function":{"name":"speak","arguments":"{\"text\":\"第一轮回应\"}"}}]}}]})";
		return LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"silent","type":"function","function":{"name":"speak","arguments":"{\"text\":\"\"}"}}]}}]})";
	}

	Ptr<FairyApplication> Create(Func<WString(const WString&)> complete, Func<WString()> loadCharacter = {})
	{
		return Ptr(new FairyApplication(folder.root / L"state", config, prompts, complete,
			[this](List<MonitorSnapshot>& snapshots) { Capture(snapshots); },
			[](const WString&) -> WebResponse { throw Exception(L"Unexpected network access in worker fixture."); }, cancellation, loadCharacter));
	}

	Ptr<FairyApplication> Create()
	{
		return Create([this](const WString& body) { return Complete(body); });
	}
};

class RuntimeContextFixture : public RuntimeWorkerFixture
{
public:
	List<Ptr<json::JsonArray>> fairyRequests;
	List<Ptr<json::JsonArray>> completedRounds;
	Func<WString(Ptr<json::JsonNode>, Ptr<json::JsonArray>)> respond;
	Func<WString(Ptr<json::JsonNode>, Ptr<json::JsonArray>)> visionRespond;
	Ptr<FairyApplication> application;

	static WString Speech(const WString& text, bool listFiles = false)
	{
		auto calls = Ptr(new json::JsonArray);
		auto add = [&](const WString& id, const WString& name, const WString& arguments)
		{
			auto function = Ptr(new json::JsonObject);
			SetString(function, L"name", name);
			SetString(function, L"arguments", arguments);
			auto call = Ptr(new json::JsonObject);
			SetString(call, L"id", id);
			SetString(call, L"type", L"function");
			SetField(call, L"function", function);
			calls->items.Add(call);
		};
		if (listFiles) add(L"list", L"file_list", L"{}");
		auto arguments = Ptr(new json::JsonObject);
		SetString(arguments, L"text", text);
		add(L"say", L"speak", json::JsonToString(arguments));
		auto message = TextMessage(L"assistant", L"");
		SetField(message, L"tool_calls", calls);
		auto choice = Ptr(new json::JsonObject);
		SetString(choice, L"finish_reason", L"tool_calls");
		SetField(choice, L"message", message);
		auto choices = Ptr(new json::JsonArray);
		choices->items.Add(choice);
		auto response = Ptr(new json::JsonObject);
		SetField(response, L"choices", choices);
		return json::JsonToString(response);
	}

	static Ptr<json::JsonArray> Tail(Ptr<json::JsonArray> messages, vint first)
	{
		auto result = Ptr(new json::JsonArray);
		for (vint i = first; i < messages->items.Count(); i++) result->items.Add(messages->items[i]);
		return result;
	}

	RuntimeContextFixture()
	{
		application = Create([this](const WString& body)
		{
			requests.Add(body);
			json::Parser parser;
			auto request = ParseJson(body, parser);
			auto messages = GetField(request, L"messages").Cast<json::JsonArray>();
			if (GetString(request, L"model") == config.visionModel)
			{
				if (visionRespond) return visionRespond(request, messages);
				return messages->items.Count() == 2 ? Speech(L"合成观察 " + itow(captures)) : terminal;
			}
			fairyRequests.Add(messages);
			if (respond) return respond(request, messages);
			return GetString(messages->items[messages->items.Count() - 1], L"role") == L"user"
				? Speech(L"历史回应 " + itow(captures), captures % 2 == 0) : terminal;
		});
	}

	void Seed(vint count)
	{
		for (vint i = 0; i < count; i++)
		{
			auto firstRequest = fairyRequests.Count();
			auto speech = application->RunRound();
			TEST_ASSERT(speech == L"历史回应 " + itow(captures));
			auto segment = Tail(fairyRequests[fairyRequests.Count() - 1], fairyRequests[firstRequest]->items.Count() - 1);
			segment->items.Add(TextMessage(L"assistant", L""));
			completedRounds.Add(segment);
		}
	}

	void AssertRetained(Ptr<json::JsonArray> messages, vint removed, Ptr<json::JsonArray> active)
	{
		TEST_ASSERT(GetString(messages->items[0], L"role") == L"system");
		TEST_ASSERT(GetString(messages->items[0], L"content") == L"工具说明\n\n记忆指引\n\n精灵请求\n\n固定性格");
		vint index = 1;
		for (vint round = removed; round < completedRounds.Count(); round++)
		{
			for (auto message : completedRounds[round]->items)
			{
				TEST_ASSERT(index < messages->items.Count());
				TEST_ASSERT(json::JsonToString(messages->items[index++]) == json::JsonToString(message));
			}
		}
		for (auto message : active->items)
		{
			TEST_ASSERT(index < messages->items.Count());
			TEST_ASSERT(json::JsonToString(messages->items[index++]) == json::JsonToString(message));
		}
		TEST_ASSERT(index == messages->items.Count());
	}
};

TEST_FILE
{
	TEST_CASE(L"Fairy context recovery removes whole oldest rounds with cumulative upward rounding")
	{
		vint firstRemoved[] = { 0, 1, 1, 1, 2, 2, 2, 3 };
		vint secondRemoved[] = { 0, 1, 2, 2, 3, 4, 4, 5 };
		for (vint historyCount = 0; historyCount < 8; historyCount++)
		{
			for (vint overflows : { 1, 2 })
			{
				RuntimeContextFixture fixture;
				fixture.Seed(historyCount); // Alternating rounds contain different numbers of tool results.
				vint submissions = 0;
				Ptr<json::JsonArray> observation;
				fixture.respond = [&](Ptr<json::JsonNode> request, Ptr<json::JsonArray> messages)
				{
					auto attempt = submissions++;
					if (attempt <= overflows)
					{
						if (attempt == 0) observation = RuntimeContextFixture::Tail(messages, messages->items.Count() - 1);
						auto removed = attempt == 0 ? 0 : attempt == 1 ? firstRemoved[historyCount] : secondRemoved[historyCount];
						fixture.AssertRetained(messages, removed, observation);
						TEST_ASSERT(GetString(request, L"tool_choice") == L"required");
						if (attempt < overflows) throw ContextLimitExceeded();
						return RuntimeContextFixture::Speech(L"恢复成功");
					}
					TEST_ASSERT(GetString(request, L"tool_choice") == L"auto");
					return fixture.terminal;
				};
				TEST_ASSERT(fixture.application->RunRound() == L"恢复成功");
				TEST_ASSERT(submissions == overflows + 2 && fixture.captures == historyCount + 1);
			}
		}
	});

	TEST_CASE(L"Fairy trimming retains active tool feedback and accumulated speech without replaying tools")
	{
		RuntimeContextFixture fixture;
		fixture.Seed(5);
		vint submissions = 0;
		Ptr<json::JsonArray> active;
		auto memoryFile = fixture.folder.root / L"state" / L"recovery.md";
		fixture.respond = [&](Ptr<json::JsonNode> request, Ptr<json::JsonArray> messages)
		{
			switch (submissions++)
			{
			case 0:
				return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"remember","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"recovery.md\",\"content\":\"初次工具写入\"}"}},{"id":"say","type":"function","function":{"name":"speak","arguments":"{\"text\":\"保留前段\"}"}}]}}]})");
			case 1:
				active = RuntimeContextFixture::Tail(messages, messages->items.Count() - 4);
				fixture.AssertRetained(messages, 0, active);
				TEST_ASSERT(File(memoryFile).ReadAllTextByBom() == L"初次工具写入");
				TEST_ASSERT(File(memoryFile).WriteAllText(L"工具完成后的外部修改", true, stream::BomEncoder::Utf8));
				throw ContextLimitExceeded();
			case 2:
				fixture.AssertRetained(messages, 2, active);
				TEST_ASSERT(GetString(request, L"tool_choice") == L"auto");
				TEST_ASSERT(File(memoryFile).ReadAllTextByBom() == L"工具完成后的外部修改");
				return RuntimeContextFixture::Speech(L"保留后段", true);
			case 3:
				active = RuntimeContextFixture::Tail(messages, messages->items.Count() - 7);
				fixture.AssertRetained(messages, 2, active);
				throw ContextLimitExceeded();
			case 4:
				fixture.AssertRetained(messages, 4, active); // A successful follow-up must not reset the recovery budget.
				TEST_ASSERT(GetString(request, L"tool_choice") == L"auto");
				TEST_ASSERT(File(memoryFile).ReadAllTextByBom() == L"工具完成后的外部修改");
				return fixture.terminal;
			default:
				throw Exception(L"Unexpected replay during context recovery.");
			}
		};
		TEST_ASSERT(fixture.application->RunRound() == L"保留前段\n保留后段");
		TEST_ASSERT(submissions == 5 && fixture.captures == 6);
		TEST_ASSERT(File(memoryFile).ReadAllTextByBom() == L"工具完成后的外部修改");
	});

	TEST_CASE(L"Third fairy overflow restarts the timestamped observation and speech while keeping saved memory")
	{
		RuntimeContextFixture fixture;
		RuntimeTestClock clock;
		clock.currentTime = DateTime::FromDateTime(2027, 2, 3, 4, 5, 6).osInternal;
		fixture.Seed(5);
		vint submissions = 0;
		Ptr<json::JsonArray> active;
		WString originalObservation;
		auto memoryFile = fixture.folder.root / L"state" / L"recovery.md";
		fixture.respond = [&](Ptr<json::JsonNode> request, Ptr<json::JsonArray> messages)
		{
			TEST_ASSERT(clock.localTimeCalls == 6 && fixture.captures == 6);
			clock.currentTime = clock.Forward(clock.currentTime, 60000);
			switch (submissions++)
			{
			case 0:
				originalObservation = GetString(messages->items[messages->items.Count() - 1], L"content");
				TEST_ASSERT(originalObservation == L"当前日期时间是：2027-02-03 04-05-06\n以下是用户所有屏幕的内容：\n合成观察 6");
				return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"remember","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"recovery.md\",\"content\":\"长期记忆\\n保持不变\"}"}},{"id":"say","type":"function","function":{"name":"speak","arguments":"{\"text\":\"应丢弃的发言\"}"}}]}}]})");
			case 1:
				active = RuntimeContextFixture::Tail(messages, messages->items.Count() - 4);
				fixture.AssertRetained(messages, 0, active);
				throw ContextLimitExceeded();
			case 2:
				fixture.AssertRetained(messages, 2, active);
				throw ContextLimitExceeded();
			case 3:
				fixture.AssertRetained(messages, 4, active);
				throw ContextLimitExceeded();
			case 4:
				TEST_ASSERT(messages->items.Count() == 2 && GetString(messages->items[1], L"content") == originalObservation);
				TEST_ASSERT(GetString(request, L"tool_choice") == L"required");
				TEST_ASSERT(File(memoryFile).ReadAllTextByBom() == L"长期记忆\n保持不变");
				return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"recall","type":"function","function":{"name":"file_read","arguments":"{\"path\":\"recovery.md\"}"}},{"id":"say","type":"function","function":{"name":"speak","arguments":"{\"text\":\"新会话发言\"}"}}]}}]})");
			case 5:
			{
				TEST_ASSERT(messages->items.Count() == 5 && GetString(messages->items[1], L"content") == originalObservation);
				TEST_ASSERT(GetString(request, L"tool_choice") == L"auto");
				json::Parser parser;
				auto feedback = ParseJson(GetString(messages->items[3], L"content"), parser);
				TEST_ASSERT(GetString(messages->items[3], L"tool_call_id") == L"recall");
				TEST_ASSERT(GetString(feedback, L"content") == L"长期记忆\n保持不变");
				return fixture.terminal;
			}
			default:
				throw Exception(L"Unexpected session restart request.");
			}
		};
		TEST_ASSERT(fixture.application->RunRound() == L"新会话发言");
		TEST_ASSERT(submissions == 6 && clock.localTimeCalls == 6 && fixture.captures == 6);
		TEST_ASSERT(File(memoryFile).ReadAllTextByBom() == L"长期记忆\n保持不变");
	});

	TEST_CASE(L"Fourth fairy overflow fails even after successful reset tools and the next round starts fresh")
	{
		RuntimeContextFixture fixture;
		fixture.Seed(3);
		vint submissions = 0;
		fixture.respond = [&](Ptr<json::JsonNode> request, Ptr<json::JsonArray> messages)
		{
			auto attempt = submissions++;
			if (attempt < 3) throw ContextLimitExceeded();
			if (attempt == 3)
			{
				TEST_ASSERT(messages->items.Count() == 2 && GetString(request, L"tool_choice") == L"required");
				return RuntimeContextFixture::Speech(L"最终仍会失败的发言");
			}
			TEST_ASSERT(attempt == 4 && messages->items.Count() == 4);
			throw ContextLimitExceeded();
		};
		TEST_EXCEPTION(fixture.application->RunRound(), ContextLimitExceeded, [](const ContextLimitExceeded&) {});
		TEST_ASSERT(submissions == 5 && fixture.captures == 4);
		submissions = 0;
		fixture.respond = [&](Ptr<json::JsonNode> request, Ptr<json::JsonArray> messages)
		{
			if (submissions++ == 0)
			{
				TEST_ASSERT(messages->items.Count() == 2 && GetString(request, L"tool_choice") == L"required");
				TEST_ASSERT(wcsstr(GetString(messages->items[1], L"content").Buffer(), L"合成观察 5"));
				return RuntimeContextFixture::Speech(L"下一轮成功");
			}
			TEST_ASSERT(messages->items.Count() == 4);
			return fixture.terminal;
		};
		TEST_ASSERT(fixture.application->RunRound() == L"下一轮成功");
		TEST_ASSERT(submissions == 2 && fixture.captures == 5);
	});

	TEST_CASE(L"A failed fairy round rolls back only its active exchange after history trimming")
	{
		RuntimeContextFixture fixture;
		fixture.Seed(5);
		vint submissions = 0;
		fixture.respond = [&](Ptr<json::JsonNode>, Ptr<json::JsonArray>) -> WString
		{
			if (submissions++ == 0) throw ContextLimitExceeded();
			if (submissions == 2) return RuntimeContextFixture::Speech(L"失败轮次中的发言", true);
			throw Exception(L"Unrelated transport failure after trimming.");
		};
		TEST_EXCEPTION(fixture.application->RunRound(), Exception, [](const Exception& error)
		{
			TEST_ASSERT(error.Message() == L"Unrelated transport failure after trimming.");
		});
		TEST_ASSERT(submissions == 3);
		submissions = 0;
		fixture.respond = [&](Ptr<json::JsonNode>, Ptr<json::JsonArray> messages)
		{
			if (submissions++ == 0)
			{
				auto observation = RuntimeContextFixture::Tail(messages, messages->items.Count() - 1);
				fixture.AssertRetained(messages, 2, observation);
				TEST_ASSERT(wcsstr(GetString(observation->items[0], L"content").Buffer(), L"合成观察 7"));
				return RuntimeContextFixture::Speech(L"保留修剪后的历史");
			}
			return fixture.terminal;
		};
		TEST_ASSERT(fixture.application->RunRound() == L"保留修剪后的历史");
		TEST_ASSERT(submissions == 2 && fixture.captures == 7);
	});

	TEST_CASE(L"Vision context errors and untyped fairy failures do not trigger history recovery")
	{
		for (bool visionFailure : { false, true })
		{
			RuntimeContextFixture fixture;
			fixture.Seed(3);
			vint failedSubmissions = 0;
			if (visionFailure)
			{
				fixture.visionRespond = [&](Ptr<json::JsonNode>, Ptr<json::JsonArray>) -> WString
				{
					failedSubmissions++;
					throw ContextLimitExceeded();
				};
				TEST_EXCEPTION(fixture.application->RunRound(), ContextLimitExceeded, [](const ContextLimitExceeded&) {});
				fixture.visionRespond = {};
			}
			else
			{
				fixture.respond = [&](Ptr<json::JsonNode>, Ptr<json::JsonArray>) -> WString
				{
					failedSubmissions++;
					throw Exception(L"context_length_exceeded text in an unrelated exception");
				};
				TEST_EXCEPTION(fixture.application->RunRound(), Exception, [](const Exception&) {});
			}
			TEST_ASSERT(failedSubmissions == 1);
			vint submissions = 0;
			fixture.respond = [&](Ptr<json::JsonNode>, Ptr<json::JsonArray> messages)
			{
				if (submissions++ == 0)
				{
					fixture.AssertRetained(messages, 0, RuntimeContextFixture::Tail(messages, messages->items.Count() - 1));
					return RuntimeContextFixture::Speech(L"原历史仍在");
				}
				return fixture.terminal;
			};
			TEST_ASSERT(fixture.application->RunRound() == L"原历史仍在");
			TEST_ASSERT(submissions == 2 && fixture.captures == 5);
		}
	});

	TEST_CASE(L"ResetFairySession clears completed-round bookkeeping before later overflow recovery")
	{
		RuntimeContextFixture fixture;
		fixture.Seed(5);
		fixture.application->ResetFairySession();
		fixture.completedRounds.Clear();
		fixture.Seed(2);
		vint submissions = 0;
		Ptr<json::JsonArray> observation;
		fixture.respond = [&](Ptr<json::JsonNode>, Ptr<json::JsonArray> messages)
		{
			switch (submissions++)
			{
			case 0:
				observation = RuntimeContextFixture::Tail(messages, messages->items.Count() - 1);
				fixture.AssertRetained(messages, 0, observation);
				throw ContextLimitExceeded();
			case 1:
				fixture.AssertRetained(messages, 1, observation);
				throw ContextLimitExceeded();
			case 2:
				fixture.AssertRetained(messages, 2, observation);
				return RuntimeContextFixture::Speech(L"新主题的新历史");
			default:
				return fixture.terminal;
			}
		};
		TEST_ASSERT(fixture.application->RunRound() == L"新主题的新历史");
		TEST_ASSERT(submissions == 4 && fixture.captures == 8);
	});

	TEST_CASE(L"JSON and SSE server errors bypass malformed-response feedback and recover only context errors")
	{
		for (bool streaming : { false, true })
		{
			for (bool contextError : { false, true })
			{
				RuntimeContextFixture fixture;
				fixture.Seed(2);
				vint submissions = 0;
				Ptr<json::JsonArray> observation;
				fixture.respond = [&](Ptr<json::JsonNode>, Ptr<json::JsonArray> messages)
				{
					if (submissions++ == 0)
					{
						observation = RuntimeContextFixture::Tail(messages, messages->items.Count() - 1);
						auto error = contextError
							? WString(LR"({"error":{"code":"context_length_exceeded","message":"Synthetic context limit."}})")
							: WString(LR"({"error":{"code":"insufficient_quota","message":"Synthetic quota failure."}})");
						return streaming ? L"data: " + error + L"\n\n" : error;
					}
					TEST_ASSERT(contextError);
					if (submissions == 2)
					{
						fixture.AssertRetained(messages, 1, observation); // No synthetic correction message enters the active exchange.
						return RuntimeContextFixture::Speech(L"服务端溢出恢复");
					}
					return fixture.terminal;
				};
				if (contextError)
				{
					TEST_ASSERT(fixture.application->RunRound() == L"服务端溢出恢复");
					TEST_ASSERT(submissions == 3);
				}
				else
				{
					TEST_EXCEPTION(fixture.application->RunRound(), ChatCompletionError, [](const ChatCompletionError&) {});
					TEST_ASSERT(submissions == 1);
				}
				TEST_ASSERT(fixture.captures == 3);
			}
		}
	});

	TEST_CASE(L"Third overflow on the last completion step gives the fresh fairy session room to finish")
	{
		RuntimeContextFixture fixture;
		vint submissions = 0;
		WString observation;
		fixture.respond = [&](Ptr<json::JsonNode> request, Ptr<json::JsonArray> messages)
		{
			auto attempt = submissions++;
			if (attempt == 0) observation = GetString(messages->items[1], L"content");
			if (attempt < 23) return RuntimeContextFixture::Speech(L"旧会话发言");
			if (attempt < 26)
			{
				TEST_ASSERT(messages->items.Count() == 48 && GetString(request, L"tool_choice") == L"auto");
				throw ContextLimitExceeded();
			}
			if (attempt == 26)
			{
				TEST_ASSERT(messages->items.Count() == 2 && GetString(messages->items[1], L"content") == observation);
				TEST_ASSERT(GetString(request, L"tool_choice") == L"required");
				return RuntimeContextFixture::Speech(L"最后一步重启成功");
			}
			TEST_ASSERT(attempt == 27 && messages->items.Count() == 4);
			return fixture.terminal;
		};
		TEST_ASSERT(fixture.application->RunRound() == L"最后一步重启成功");
		TEST_ASSERT(submissions == 28 && fixture.captures == 1);
	});

	TEST_CASE(L"Speech history creates the supplied folder and appends complete UTF-8 entries with local timestamps")
	{
		RuntimeTestFolder folder;
		RuntimeTestClock clock;
		auto environment = folder.root / L"custom-location" / L"settings";
		auto history = environment / L"history.md";
		AppendSpeechHistory(environment, L"");
		TEST_ASSERT(!Folder(environment).Exists() && clock.localTimeCalls == 0);
		clock.currentTime = DateTime::FromDateTime(2027, 2, 3, 4, 5, 6).osInternal;
		AppendSpeechHistory(environment, L"你好，鲸鱼！\n第二行\n重复\n重复");
		auto first = wtou8(L"# Speak 2027-02-03 04-05-06\n\n你好，鲸鱼！\n第二行\n重复\n重复\n\n");
		TEST_ASSERT(ReadRuntimeFixtureBytes(history) == first && clock.localTimeCalls == 1);
		clock.currentTime = DateTime::FromDateTime(2027, 12, 31, 23, 59, 59).osInternal;
		AppendSpeechHistory(environment, L"下一次发言");
		auto expected = first + wtou8(L"# Speak 2027-12-31 23-59-59\n\n下一次发言\n\n");
		TEST_ASSERT(ReadRuntimeFixtureBytes(history) == expected && clock.localTimeCalls == 2);
		AppendSpeechHistory(environment, L"");
		TEST_ASSERT(ReadRuntimeFixtureBytes(history) == expected && clock.localTimeCalls == 2);
		TEST_ASSERT(!Folder(folder.root / L"env").Exists());
	});

	TEST_CASE(L"Speech history preserves pre-existing bytes and reports unavailable output paths")
	{
		RuntimeTestFolder folder;
		RuntimeTestClock clock;
		clock.currentTime = DateTime::FromDateTime(2027, 1, 1, 0, 0, 0).osInternal;
		auto history = folder.root / L"history.md";
		auto previous = U8String(u8"\uFEFF# Existing\r\n\r\n已有内容\r\n\r\n");
		{
			stream::FileStream file(history.GetFullPath(), stream::FileStream::WriteOnly);
			TEST_ASSERT(file.IsAvailable() && file.Write(const_cast<char8_t*>(previous.Buffer()), previous.Length()) == previous.Length());
		}
		AppendSpeechHistory(folder.root, L"保留旧文件");
		auto expected = previous + wtou8(L"# Speak 2027-01-01 00-00-00\n\n保留旧文件\n\n");
		TEST_ASSERT(ReadRuntimeFixtureBytes(history) == expected);

		auto locked = CreateFileW(history.GetFullPath().Buffer(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		TEST_ASSERT(locked != INVALID_HANDLE_VALUE);
		bool rejectedLockedFile = false;
		try { AppendSpeechHistory(folder.root, L"写入失败"); }
		catch (const Exception&) { rejectedLockedFile = true; }
		auto closed = CloseHandle(locked);
		TEST_ASSERT(rejectedLockedFile && closed && ReadRuntimeFixtureBytes(history) == expected);
		TEST_EXCEPTION(AppendSpeechHistory(history / L"invalid-folder", L"写入失败"), Exception, [](const Exception&) {});
		TEST_ASSERT(File(history).Delete() && Folder(history).Create(false));
		TEST_EXCEPTION(AppendSpeechHistory(folder.root, L"写入失败"), Exception, [](const Exception&) {});
	});

	TEST_CASE(L"Desktop worker starts immediately and retains its session while waiting for requested rounds")
	{
		RuntimeWorkerFixture fixture;
		EventObject published;
		TEST_ASSERT(published.CreateAutoUnsignal(false));
		List<WString> results;
		List<WString> persisted;
		List<vint> persistedAtPublication;
		auto callingThread = GetCurrentThreadId();
		DWORD persistenceThread = 0;
		vint factoryCalls = 0;
		DesktopAgentRunner runner([&]()
		{
			factoryCalls++;
			return fixture.Create();
		}, fixture.cancellation, [&](const WString& result)
		{
			results.Add(result);
			persistedAtPublication.Add(persisted.Count());
			published.Signal();
		}, [&](const WString& speech)
		{
			persisted.Add(speech);
			persistenceThread = GetCurrentThreadId();
		});
		TEST_ASSERT(runner.Start());
		auto startedImmediately = published.WaitForTime(5000);
		auto waitedForRequest = !published.WaitForTime(100);
		runner.RequestRound();
		auto secondRoundCompleted = published.WaitForTime(5000);
		runner.StopAndWait();
		// Only inspect worker-owned collections after joining the thread.
		TEST_ASSERT(persisted.Count() == 1 && persisted[0] == results[0]);
		TEST_ASSERT(persistedAtPublication.Count() == 2 && persistedAtPublication[0] == 1 && persistedAtPublication[1] == 1);
		TEST_ASSERT(persistenceThread != 0 && persistenceThread != callingThread);
		TEST_ASSERT(startedImmediately && waitedForRequest && secondRoundCompleted);
		TEST_ASSERT(factoryCalls == 1 && fixture.captures == 2 && fixture.requests.Count() == 8);
		TEST_ASSERT(results.Count() == 2 && results[0] == L"第一轮回应" && results[1] == L"");
		json::Parser parser;
		vint expectedCounts[] = { 2, 4, 2, 4, 2, 4, 6, 8 };
		for (vint i = 0; i < fixture.requests.Count(); i++)
		{
			auto request = ParseJson(fixture.requests[i], parser);
			TEST_ASSERT(GetString(request, L"model") == (i % 4 < 2 ? fixture.config.visionModel : fixture.config.fairyModel));
			TEST_ASSERT(GetString(request, L"tool_choice") == (i % 2 == 0 ? L"required" : L"auto")); // Require speech each round; a successful empty fairy speak also enables completion.
			TEST_ASSERT(GetField(request, L"messages").Cast<json::JsonArray>()->items.Count() == expectedCounts[i]);
		}
		auto firstFairy = GetField(ParseJson(fixture.requests[2], parser), L"messages").Cast<json::JsonArray>();
		auto secondFairy = GetField(ParseJson(fixture.requests[6], parser), L"messages").Cast<json::JsonArray>();
		TEST_ASSERT(GetString(firstFairy->items[1], L"content") == GetString(secondFairy->items[1], L"content"));
		TEST_ASSERT(wcsstr(GetString(secondFairy->items[5], L"content").Buffer(), L"合成屏幕观察"));
	});

	TEST_CASE(L"Resetting the fairy session discards conversation history while retaining memory files and tools")
	{
		RuntimeWorkerFixture fixture;
		List<vint> initialFairyMessageCounts;
		bool readPreservedMemory = false;
		auto application = fixture.Create([&](const WString& body)
		{
			auto response = fixture.Complete(body);
			json::Parser parser;
			auto request = ParseJson(body, parser);
			if (GetString(request, L"model") != fixture.config.fairyModel) return response;
			auto messages = GetField(request, L"messages").Cast<json::JsonArray>();
			if (GetString(messages->items[messages->items.Count() - 1], L"role") == L"user")
			{
				initialFairyMessageCounts.Add(messages->items.Count());
				if (fixture.captures == 1)
					return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"remember","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"remembered.md\",\"content\":\"主人喜欢 C++\\n第二行\"}"}},{"id":"say","type":"function","function":{"name":"speak","arguments":"{\"text\":\"写下记忆\"}"}}]}}]})");
				if (fixture.captures == 3)
					return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"recall","type":"function","function":{"name":"file_read","arguments":"{\"path\":\"remembered.md\"}"}},{"id":"say","type":"function","function":{"name":"speak","arguments":"{\"text\":\"读到记忆\"}"}}]}}]})");
			}
			else if (fixture.captures == 3)
			{
				TEST_ASSERT(messages->items.Count() == 5 && GetString(messages->items[3], L"tool_call_id") == L"recall");
				auto feedback = ParseJson(GetString(messages->items[3], L"content"), parser);
				TEST_ASSERT(GetString(feedback, L"content") == L"主人喜欢 C++\n第二行");
				readPreservedMemory = true;
			}
			return response;
		});
		TEST_ASSERT(application->RunRound() == L"写下记忆");
		TEST_ASSERT(application->RunRound() == L"");
		auto memoryFile = fixture.folder.root / L"state" / L"remembered.md";
		auto memoryBeforeReset = ReadRuntimeFixtureBytes(memoryFile);
		application->ResetFairySession();
		TEST_ASSERT(ReadRuntimeFixtureBytes(memoryFile) == memoryBeforeReset);
		TEST_ASSERT(application->RunRound() == L"读到记忆");
		TEST_ASSERT(readPreservedMemory && ReadRuntimeFixtureBytes(memoryFile) == memoryBeforeReset);
		TEST_ASSERT(fixture.captures == 3 && fixture.requests.Count() == 12);
		TEST_ASSERT(initialFairyMessageCounts.Count() == 3);
		TEST_ASSERT(initialFairyMessageCounts[0] == 2 && initialFairyMessageCounts[1] == 7 && initialFairyMessageCounts[2] == 2);
	});

	TEST_CASE(L"Desktop theme switches reset history between rounds and retain a reset after switching back")
	{
		RuntimeWorkerFixture fixture;
		EventObject published;
		TEST_ASSERT(published.CreateAutoUnsignal(false));
		List<WString> results;
		vint factoryCalls = 0;
		DesktopAgentRunner runner([&]()
		{
			factoryCalls++;
			return fixture.Create();
		}, fixture.cancellation, [&](const WString& result)
		{
			results.Add(result);
			published.Signal();
		});
		auto firstCharacter = fixture.folder.root / L"theme-a" / L"Character.md";
		auto secondCharacter = fixture.folder.root / L"theme-b" / L"Character.md";
		runner.SetCharacterFile(firstCharacter);
		TEST_ASSERT(runner.Start());
		auto firstCompleted = published.WaitForTime(5000);
		runner.SetCharacterFile(firstCharacter); // Selecting the same theme keeps its conversation.
		runner.RequestRound();
		auto sameSelectionCompleted = published.WaitForTime(5000);
		runner.SetCharacterFile(secondCharacter);
		runner.SetCharacterFile(firstCharacter); // A -> B -> A still discards A's previous conversation.
		runner.RequestRound();
		auto switchedBackCompleted = published.WaitForTime(5000);
		runner.SetCharacterFile(secondCharacter);
		runner.RequestRound();
		auto newSelectionCompleted = published.WaitForTime(5000);
		runner.RequestRound();
		auto retainedNewSession = published.WaitForTime(5000);
		runner.StopAndWait();
		TEST_ASSERT(firstCompleted && sameSelectionCompleted && switchedBackCompleted && newSelectionCompleted && retainedNewSession);
		TEST_ASSERT(factoryCalls == 1 && fixture.captures == 5 && fixture.requests.Count() == 20);
		TEST_ASSERT(results.Count() == 5);
		TEST_ASSERT(results[0] == L"第一轮回应" && results[1] == L"" && results[2] == L"第一轮回应" && results[3] == L"第一轮回应" && results[4] == L"");
		json::Parser parser;
		vint expectedInitialCounts[] = { 2, 6, 2, 2, 6 };
		for (vint round = 0; round < 5; round++)
		{
			auto request = ParseJson(fixture.requests[round * 4 + 2], parser);
			TEST_ASSERT(GetString(request, L"model") == fixture.config.fairyModel);
			TEST_ASSERT(GetField(request, L"messages").Cast<json::JsonArray>()->items.Count() == expectedInitialCounts[round]);
		}
	});

	TEST_CASE(L"A theme switch during a pending fairy response preserves that round and resets the next round")
	{
		for (vint pendingRequest : { 7, 8 }) // The initial fairy reply and its tool-feedback follow-up.
		{
			RuntimeWorkerFixture fixture;
			EventObject published;
			EventObject requestStarted;
			EventObject releaseRequest;
			TEST_ASSERT(published.CreateAutoUnsignal(false) && requestStarted.CreateAutoUnsignal(false) && releaseRequest.CreateAutoUnsignal(false));
			List<WString> results;
			bool pendingWasReleased = false;
			bool pendingWasCancelled = false;
			DesktopAgentRunner runner([&]()
			{
				return fixture.Create([&](const WString& body)
				{
					auto response = fixture.Complete(body);
					if (fixture.requests.Count() == pendingRequest)
					{
						requestStarted.Signal();
						pendingWasReleased = releaseRequest.WaitForTime(5000);
						pendingWasCancelled = fixture.cancellation->IsCancelled();
						if (!pendingWasReleased) throw Exception(L"Fixture theme switch timed out.");
					}
					return response;
				});
			}, fixture.cancellation, [&](const WString& result)
			{
				results.Add(result);
				published.Signal();
			});
			runner.SetCharacterFile(fixture.folder.root / L"theme-a" / L"Character.md");
			TEST_ASSERT(runner.Start());
			auto firstCompleted = published.WaitForTime(5000);
			runner.RequestRound();
			auto responsePending = requestStarted.WaitForTime(5000);
			runner.SetCharacterFile(fixture.folder.root / L"theme-b" / L"Character.md");
			releaseRequest.Signal();
			auto pendingRoundCompleted = published.WaitForTime(5000);
			runner.RequestRound();
			auto newSessionCompleted = published.WaitForTime(5000);
			runner.StopAndWait();
			TEST_ASSERT(firstCompleted && responsePending && pendingRoundCompleted && newSessionCompleted);
			TEST_ASSERT(pendingWasReleased && !pendingWasCancelled);
			TEST_ASSERT(fixture.captures == 3 && fixture.requests.Count() == 12);
			TEST_ASSERT(results.Count() == 3 && results[0] == L"第一轮回应" && results[1] == L"" && results[2] == L"第一轮回应");
			json::Parser parser;
			vint expectedCounts[] = { 2, 4, 2, 4, 2, 4, 6, 8, 2, 4, 2, 4 };
			for (vint i = 0; i < fixture.requests.Count(); i++)
			{
				auto request = ParseJson(fixture.requests[i], parser);
				TEST_ASSERT(GetField(request, L"messages").Cast<json::JsonArray>()->items.Count() == expectedCounts[i]);
			}
		}
	});

	TEST_CASE(L"Desktop worker reports initialization failure and recovers after a delayed requested retry")
	{
		RuntimeWorkerFixture fixture;
		EventObject published;
		TEST_ASSERT(published.CreateAutoUnsignal(false));
		List<WString> results;
		List<WString> persisted;
		vint factoryCalls = 0;
		DesktopAgentRunner runner([&]() -> Ptr<FairyApplication>
		{
			if (factoryCalls++ == 0) throw Exception(L"合成配置错误");
			return fixture.Create();
		}, fixture.cancellation, [&](const WString& result)
		{
			results.Add(result);
			published.Signal();
		}, [&](const WString& speech)
		{
			persisted.Add(speech);
		});
		TEST_ASSERT(runner.Start());
		auto reportedFailure = published.WaitForTime(5000);
		runner.RequestRound();
		auto delayedRetry = !published.WaitForTime(100);
		auto recovered = published.WaitForTime(5000);
		runner.StopAndWait();
		TEST_ASSERT(reportedFailure && delayedRetry && recovered);
		TEST_ASSERT(persisted.Count() == 1 && persisted[0] == results[1]);
		TEST_ASSERT(factoryCalls == 2 && fixture.captures == 1 && fixture.requests.Count() == 4);
		TEST_ASSERT(results.Count() == 2 && results[0] == L"调用大模型发生错误：合成配置错误" && results[1] == L"第一轮回应");
	});

	TEST_CASE(L"Desktop worker reports speech persistence failures and recovers on a delayed round")
	{
		RuntimeWorkerFixture fixture;
		EventObject published;
		TEST_ASSERT(published.CreateAutoUnsignal(false));
		List<WString> results;
		List<WString> persisted;
		vint persistenceAttempts = 0;
		DesktopAgentRunner runner([&]()
		{
			return fixture.Create([&](const WString& body)
			{
				auto response = fixture.Complete(body);
				if (fixture.requests.Count() == 7)
					return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"recovered","type":"function","function":{"name":"speak","arguments":"{\"text\":\"恢复后的回应\"}"}}]}}]})");
				return response;
			});
		}, fixture.cancellation, [&](const WString& result)
		{
			results.Add(result);
			published.Signal();
		}, [&](const WString& speech)
		{
			if (persistenceAttempts++ == 0) throw Exception(L"合成历史写入错误");
			persisted.Add(speech);
		});
		TEST_ASSERT(runner.Start());
		auto reportedFailure = published.WaitForTime(5000);
		runner.RequestRound();
		auto delayedRetry = !published.WaitForTime(100);
		auto recovered = published.WaitForTime(5000);
		runner.StopAndWait();
		TEST_ASSERT(reportedFailure && delayedRetry && recovered);
		TEST_ASSERT(results.Count() == 2 && results[0] == L"调用大模型发生错误：合成历史写入错误" && results[1] == L"恢复后的回应");
		TEST_ASSERT(persistenceAttempts == 2 && persisted.Count() == 1 && persisted[0] == results[1]);
		TEST_ASSERT(fixture.captures == 2 && fixture.requests.Count() == 8);
	});

	TEST_CASE(L"Stopping the desktop worker interrupts its failure retry delay")
	{
		RuntimeWorkerFixture fixture;
		EventObject published;
		TEST_ASSERT(published.CreateAutoUnsignal(false));
		vint factoryCalls = 0;
		vint publications = 0;
		DesktopAgentRunner runner([&]() -> Ptr<FairyApplication>
		{
			factoryCalls++;
			throw Exception(L"Persistent fixture failure.");
		}, fixture.cancellation, [&](const WString&)
		{
			publications++;
			published.Signal();
		});
		TEST_ASSERT(runner.Start());
		auto reportedFailure = published.WaitForTime(5000);
		runner.RequestRound();
		fixture.cancellation->Cancel();
		auto stoppedDuringDelay = runner.WaitForTime(500);
		runner.StopAndWait();
		TEST_ASSERT(reportedFailure && stoppedDuringDelay && factoryCalls == 1 && publications == 1);
	});

	TEST_CASE(L"Stopping a pending desktop completion cancels it without publishing or persisting speech or errors")
	{
		for (bool afterSpeech : { false, true })
		{
			RuntimeWorkerFixture fixture;
			EventObject requestStarted;
			TEST_ASSERT(requestStarted.CreateAutoUnsignal(false));
			vint publications = 0;
			vint persisted = 0;
			bool sawCancellation = false;
			DesktopAgentRunner runner([&]()
			{
				return fixture.Create([&](const WString& body)
				{
					if (afterSpeech && fixture.requests.Count() < 3) return fixture.Complete(body);
					fixture.requests.Add(body);
					requestStarted.Signal();
					sawCancellation = fixture.cancellation->Event().WaitForTime(5000);
					if (!sawCancellation) throw Exception(L"Fixture cancellation timed out.");
					return afterSpeech ? fixture.terminal : fixture.observation; // Even a completed reply must be ignored after cancellation.
				});
			}, fixture.cancellation, [&](const WString&) { publications++; }, [&](const WString&) { persisted++; });
			TEST_ASSERT(runner.Start());
			auto requestPending = requestStarted.WaitForTime(5000);
			runner.RequestRound();
			runner.StopAndWait();
			TEST_ASSERT(requestPending && sawCancellation && publications == 0 && persisted == 0);
			TEST_ASSERT(fixture.captures == 1 && fixture.requests.Count() == (afterSpeech ? 4 : 1) && runner.WaitForTime(0));
		}
	});

	TEST_CASE(L"Round cancellation prevents capture and later tool or feedback side effects")
	{
		for (vint stage = 0; stage < 7; stage++)
		{
			RuntimeWorkerFixture fixture;
			vint requests = 0;
			vint fetched = 0;
			vint observed = 0;
			auto complete = [&](const WString&)
			{
				requests++;
				if (stage == 2 || (stage == 5 && requests == 2)) fixture.cancellation->Cancel();
				if (stage == 5 && requests == 1)
					return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"fetch","type":"function","function":{"name":"http_get","arguments":"{\"url\":\"https://example.test\"}"}}]}}]})");
				return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"fetch","type":"function","function":{"name":"http_get","arguments":"{\"url\":\"https://example.test\"}"}},{"id":"write","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"should-not-exist.md\",\"content\":\"cancelled side effect\"}"}},{"id":"say","type":"function","function":{"name":"speak","arguments":"{\"text\":\"cancelled speech\"}"}}]}}]})");
			};
			auto capture = [&](List<MonitorSnapshot>& snapshots)
			{
				fixture.Capture(snapshots);
				if (stage == 1) fixture.cancellation->Cancel();
			};
			auto fetch = [&](const WString&)
			{
				fetched++;
				if (stage == 4 || stage == 6) fixture.cancellation->Cancel();
				if (stage == 6) fixture.cancellation->ThrowIfCancelled();
				WebResponse response;
				response.statusCode = 200;
				response.body = L"synthetic page";
				return response;
			};
			FairyApplication application(fixture.folder.root / L"state", fixture.config, fixture.prompts, complete, capture, fetch, fixture.cancellation);
			application.ResponseReceived.Add(Func<void(bool, const WString&)>([&](bool, const WString&)
			{
				observed++;
				if (stage == 3) fixture.cancellation->Cancel();
			}));
			if (stage == 0) fixture.cancellation->Cancel();
			TEST_EXCEPTION(application.RunRound(), OperationCancelled, [](const OperationCancelled&) {});
			TEST_ASSERT(fixture.captures == (stage == 0 ? 0 : 1));
			TEST_ASSERT(requests == (stage < 2 ? 0 : stage == 5 ? 2 : 1));
			TEST_ASSERT(observed == (stage < 3 ? 0 : 1));
			TEST_ASSERT(fetched == (stage < 4 ? 0 : 1));
			TEST_ASSERT(!File(fixture.folder.root / L"state" / L"should-not-exist.md").Exists());
		}
	});

	TEST_CASE(L"Application loads caller-supplied configuration and memory folders independently")
	{
		RuntimeTestFolder configuration;
		RuntimeTestFolder storage;
		RuntimeTestFolder character;
		auto envFolder = configuration.root / L"custom-prompts";
		auto memoryFolder = storage.root / L"nested/custom-state";
		TEST_ASSERT(Folder(envFolder).Create(false));
		TEST_ASSERT(File(envFolder / L"apikey.json").WriteAllText(
			LR"({"apikey":"test-secret","url":"https://example.test/v1","auth_header":"Authorization: Bearer $APIKEY","vision_model":"test-vision","fairy_model":"test-fairy"})",
			false, stream::BomEncoder::Utf8));
		for (auto name : { L"Tools.md", L"Guidance.md", L"Request_Vision.md", L"Request_Fairy.md" })
		{
			TEST_ASSERT(File(envFolder / name).WriteAllText(L"合成测试提示", false, stream::BomEncoder::Utf8));
		}
		auto characterFile = character.root / L"custom-personality.txt";
		TEST_ASSERT(File(characterFile).WriteAllText(L"独立目录的性格", false, stream::BomEncoder::Utf8));
		vint characterReads = 0;
		auto loadCharacter = [&]()
		{
			characterReads++;
			return LoadCharacterPrompt(characterFile, characterFile);
		};
		{
			FairyApplication application(envFolder, memoryFolder, loadCharacter); // Initialization must not capture the desktop or contact a model.
		}
		TEST_ASSERT(characterReads == 0 && !File(envFolder / L"Character.md").Exists()); // Character selection belongs to fairy submissions, with no legacy env dependency.
		TEST_ASSERT(File(memoryFolder / L"Index.md").Exists());
		TEST_ASSERT(!Folder(configuration.root / L"memory").Exists() && !Folder(storage.root / L"memory").Exists());
		TEST_ASSERT(!Folder(memoryFolder / L"memory").Exists()); // Use the exact supplied folder without appending a conventional name.
		TEST_ASSERT(File(envFolder / L"Request_Vision.md").Delete());
		TEST_EXCEPTION(FairyApplication(envFolder, memoryFolder, loadCharacter), Exception, [&](const Exception& error)
		{
			TEST_ASSERT(error.Message() == L"Missing or empty prompt: " + (envFolder / L"Request_Vision.md").GetFullPath());
		}); // A missing supplied prompt must fail instead of searching for the repository's real env.
	});

	TEST_CASE(L"Character loading uses supplied paths and falls back only when the selected file is absent")
	{
		RuntimeTestFolder selected;
		RuntimeTestFolder fallback;
		auto selectedFile = selected.root / L"selected.txt";
		auto fallbackFile = fallback.root / L"default.txt";
		TEST_ASSERT(File(fallbackFile).WriteAllText(L"默认性格", false, stream::BomEncoder::Utf8));
		TEST_ASSERT(LoadCharacterPrompt(selectedFile, fallbackFile) == L"默认性格");
		TEST_ASSERT(File(selectedFile).WriteAllText(L"所选性格", true, stream::BomEncoder::Utf16));
		TEST_ASSERT(LoadCharacterPrompt(selectedFile, fallbackFile) == L"所选性格");
		TEST_ASSERT(File(fallbackFile).Delete());
		TEST_ASSERT(LoadCharacterPrompt(selectedFile, fallbackFile) == L"所选性格"); // An existing selection never requires the fallback.
		TEST_ASSERT(File(fallbackFile).WriteAllText(L"默认性格", false, stream::BomEncoder::Utf8));
		TEST_ASSERT(File(selectedFile).WriteAllText(L"", false, stream::BomEncoder::Utf8));
		TEST_EXCEPTION(LoadCharacterPrompt(selectedFile, fallbackFile), Exception, [&](const Exception& error)
		{
			TEST_ASSERT(error.Message() == L"Missing or empty prompt: " + selectedFile.GetFullPath());
		}); // An existing empty selected character is an error, even with a valid fallback.
		TEST_ASSERT(File(selectedFile).Delete());
		TEST_ASSERT(File(fallbackFile).WriteAllText(L"", false, stream::BomEncoder::Utf8));
		TEST_EXCEPTION(LoadCharacterPrompt(selectedFile, fallbackFile), Exception, [&](const Exception& error)
		{
			TEST_ASSERT(error.Message() == L"Missing or empty prompt: " + fallbackFile.GetFullPath());
		});
		TEST_ASSERT(File(fallbackFile).Delete());
		TEST_EXCEPTION(LoadCharacterPrompt(selectedFile, fallbackFile), Exception, [&](const Exception& error)
		{
			TEST_ASSERT(error.Message() == L"Missing or empty prompt: " + fallbackFile.GetFullPath());
		});
	});

	TEST_CASE(L"Every fairy submission uses the latest character while retaining tool feedback and conversation history")
	{
		RuntimeWorkerFixture fixture;
		RuntimeTestFolder character;
		auto firstFile = character.root / L"first.txt";
		auto secondFile = character.root / L"second.txt";
		TEST_ASSERT(File(firstFile).WriteAllText(L"第一种性格", false, stream::BomEncoder::Utf8));
		TEST_ASSERT(File(secondFile).WriteAllText(L"第二种性格", false, stream::BomEncoder::Utf8));
		auto selectedFile = firstFile;
		vint characterReads = 0;
		vint requestIndex = 0;
		json::Parser parser;
		auto application = fixture.Create([&](const WString& body)
		{
			auto request = ParseJson(body, parser);
			auto messages = GetField(request, L"messages").Cast<json::JsonArray>();
			auto system = GetString(messages->items[0], L"content");
			if (GetString(request, L"model") == fixture.config.visionModel)
			{
				TEST_ASSERT(characterReads == (requestIndex < 4 ? 0 : 2)); // Vision never reads character prompts.
				TEST_ASSERT(system == L"工具说明\n\n记忆指引\n\n视觉请求");
				if (requestIndex == 0) selectedFile = secondFile; // A theme change while vision is pending applies to fairy immediately.
			}
			else
			{
				auto expected = requestIndex == 3 ? L"第一种性格" : L"第二种性格";
				TEST_ASSERT(system == L"工具说明\n\n记忆指引\n\n精灵请求\n\n" + WString(expected));
				TEST_ASSERT(characterReads == (requestIndex < 4 ? requestIndex - 1 : requestIndex - 3));
				if (requestIndex == 2) selectedFile = firstFile; // A change while a fairy response is pending applies to its tool follow-up.
				if (requestIndex == 3)
					TEST_ASSERT(messages->items.Count() == 4 && GetString(messages->items[3], L"tool_call_id") == L"answer");
				if (requestIndex == 6)
				{
					TEST_ASSERT(messages->items.Count() == 6);
					auto firstMessages = GetField(ParseJson(fixture.requests[2], parser), L"messages").Cast<json::JsonArray>();
					TEST_ASSERT(GetString(messages->items[1], L"content") == GetString(firstMessages->items[1], L"content"));
					TEST_ASSERT(GetString(messages->items[3], L"tool_call_id") == L"answer");
				}
			}
			requestIndex++;
			return fixture.Complete(body);
		}, [&]()
		{
			characterReads++;
			return LoadCharacterPrompt(selectedFile, firstFile);
		});
		TEST_ASSERT(application->RunRound() == L"第一轮回应");
		selectedFile = secondFile; // A later round uses the new character without rebuilding the session.
		TEST_ASSERT(application->RunRound() == L"");
		TEST_ASSERT(requestIndex == 8 && characterReads == 4 && fixture.captures == 2);
	});

	TEST_CASE(L"Desktop worker reports character load failures and recovers without retaining a failed fairy exchange")
	{
		RuntimeWorkerFixture fixture;
		auto characterFile = fixture.folder.root / L"character.txt";
		TEST_ASSERT(File(characterFile).WriteAllText(L"初始性格", false, stream::BomEncoder::Utf8));
		EventObject published;
		TEST_ASSERT(published.CreateAutoUnsignal(false));
		List<WString> results;
		List<WString> persisted;
		vint factoryCalls = 0;
		DesktopAgentRunner runner([&]()
		{
			factoryCalls++;
			return fixture.Create([&](const WString& body)
				{
					auto response = fixture.Complete(body);
					if (fixture.requests.Count() == 3)
						TEST_ASSERT(File(characterFile).WriteAllText(L"", false, stream::BomEncoder::Utf8)); // Fail while preparing feedback for the first fairy speak.
					return response;
				},
				[&]() { return LoadCharacterPrompt(characterFile, characterFile); });
		}, fixture.cancellation, [&](const WString& result)
		{
			results.Add(result);
			published.Signal();
		}, [&](const WString& speech)
		{
			persisted.Add(speech);
		});
		TEST_ASSERT(runner.Start());
		auto reportedFailure = published.WaitForTime(5000);
		auto repairedFile = File(characterFile).WriteAllText(L"修复后的性格", false, stream::BomEncoder::Utf8);
		runner.RequestRound();
		auto recovered = published.WaitForTime(5000);
		runner.StopAndWait();
		TEST_ASSERT(reportedFailure && repairedFile && recovered);
		TEST_ASSERT(persisted.Count() == 1 && persisted[0] == results[1]);
		TEST_ASSERT(factoryCalls == 1 && fixture.captures == 2 && fixture.requests.Count() == 7);
		TEST_ASSERT(results.Count() == 2 && results[0] == L"调用大模型发生错误：Missing or empty prompt: " + characterFile.GetFullPath());
		TEST_ASSERT(results[1] == L"第一轮回应");
		json::Parser parser;
		auto messages = GetField(ParseJson(fixture.requests[5], parser), L"messages").Cast<json::JsonArray>();
		TEST_ASSERT(messages->items.Count() == 2 && GetString(messages->items[0], L"content") == L"工具说明\n\n记忆指引\n\n精灵请求\n\n修复后的性格");
	});

	TEST_CASE(L"Agent rounds preserve history, report responses and recover from invalid replies")
	{
		RuntimeTestFolder folder;
		json::Parser parser;
		ApiConfig config;
		config.visionModel = L"test-vision";
		config.fairyModel = L"test-fairy";
		AgentPrompts prompts{ L"工具说明", L"记忆指引", L"视觉请求", L"精灵请求", L"固定性格" };
		vint captures = 0;
		vint requestIndex = 0;
		List<bool> responseAgents;
		List<Ptr<json::JsonNode>> responses;
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
		auto observation = WString(LR"sse(data: {"choices":[{"index":0,"delta":{"role":"assistant","tool_calls":[{"index":0,"id":"vision-speak","type":"function","function":{"name":"speak","arguments":"{\"text\":\"用户正在阅读 C++ 代码。\"}"}}]},"finish_reason":"tool_calls"}]}

data: [DONE]

)sse");
		auto complete = [&](const WString& body)
		{
			TEST_ASSERT(responses.Count() == requestIndex); // Report each complete response before requesting the next one.
			auto request = ParseJson(body, parser);
			auto model = GetString(request, L"model");
			auto messages = GetField(request, L"messages").Cast<json::JsonArray>();
			auto system = GetString(messages->items[0], L"content");
			TEST_ASSERT(wcsstr(system.Buffer(), L"工具说明") && wcsstr(system.Buffer(), L"记忆指引")); // Prompts must accompany every request.
			TEST_ASSERT(GetField(request, L"tools").Cast<json::JsonArray>()->items.Count() == 7); // Both agents need all tools.
			TEST_ASSERT(GetField(request, L"stream").Cast<json::JsonLiteral>()->value == json::JsonLiteralValue::True); // Use streaming completions.
			switch (requestIndex++)
			{
			case 0:
			case 4:
			{
				TEST_ASSERT(model == config.visionModel && messages->items.Count() == 2); // Vision history must start fresh.
				TEST_ASSERT(GetString(request, L"tool_choice") == L"required"); // Vision must call tools until it submits an observation.
				TEST_ASSERT(!wcsstr(system.Buffer(), L"固定性格")); // Only fairy receives the character prompt.
				auto parts = GetField(messages->items[1], L"content").Cast<json::JsonArray>();
				TEST_ASSERT(parts->items.Count() == 4); // Each monitor needs metadata and an image.
				TEST_ASSERT(GetString(parts->items[1], L"type") == L"image_url"); // Image content type.
				TEST_ASSERT(GetString(GetField(parts->items[3], L"image_url"), L"url") == L"data:image/png;base64,fixture"); // Second monitor missing.
				return observation;
			}
			case 1:
			case 5:
				TEST_ASSERT(model == config.visionModel && messages->items.Count() == 4); // Vision tool result history.
				TEST_ASSERT(GetString(request, L"tool_choice") == L"auto"); // Vision must be allowed to end after speaking.
				TEST_ASSERT(GetString(messages->items[3], L"tool_call_id") == L"vision-speak"); // Tool result must match call id.
				return terminal;
			case 2:
				TEST_ASSERT(model == config.fairyModel && messages->items.Count() == 2); // Initial fairy history.
				TEST_ASSERT(wcsstr(system.Buffer(), L"固定性格")); // Fairy needs character.
				TEST_ASSERT(wcsstr(GetString(messages->items[1], L"content").Buffer(), L"C++")); // Vision description must reach fairy.
				return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","content":null,"tool_calls":[{"id":"write","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"interests/cpp.md\",\"content\":\"用户阅读 C++\"}"}},{"id":"bad-path","type":"function","function":{"name":"file_read","arguments":"{\"path\":\"../env/apikey.json\"}"}},{"id":"say","type":"function","function":{"name":"speak","arguments":"{\"text\":\"今天也在研究 C++ 呀。\"}"}}]}}]})");
			case 3:
				TEST_ASSERT(messages->items.Count() == 6); // All tool calls need responses.
				TEST_ASSERT(GetString(messages->items[4], L"tool_call_id") == L"bad-path"); // Invalid tool response id.
				TEST_ASSERT(GetField(ParseJson(GetString(messages->items[4], L"content"), parser), L"ok").Cast<json::JsonLiteral>()->value == json::JsonLiteralValue::False); // Invalid tool arguments must be returned as errors.
				return terminal;
			case 6:
				TEST_ASSERT(model == config.fairyModel && messages->items.Count() == 8); // Fairy conversation must persist across rounds.
				return WString(LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"继续加油。"}}]})");
			default:
				throw Exception(L"Unexpected fixture request.");
			}
		};
		auto web = [](const WString&) -> WebResponse { throw Exception(L"Unexpected network request in offline test."); };
		FairyApplication application(folder.root / L"custom-state", config, prompts, complete, capture, web);
		application.ResponseReceived.Add(Func<void(bool, const WString&)>([&](bool vision, const WString& message)
		{
			TEST_ASSERT(!wcschr(message.Buffer(), L'\n') && !wcschr(message.Buffer(), L'\r')); // A response must fit on one JSON log line.
			auto parsed = ParseJson(message, parser);
			TEST_ASSERT(GetString(parsed, L"role") == L"assistant" && !GetField(parsed, L"choices")); // Report the assistant message, not the transport envelope.
			if (!vision && responses.Count() == 2)
				TEST_ASSERT(!File(folder.root / L"custom-state" / L"interests" / L"cpp.md").Exists()); // Report tool requests before executing them.
			responseAgents.Add(vision);
			responses.Add(parsed);
		}));
		TEST_ASSERT(application.RunRound() == L"今天也在研究 C++ 呀。"); // Fairy speak output.
		TEST_ASSERT(application.RunRound().Length() == 0); // Ordinary assistant content must not be treated as speech.
		TEST_ASSERT(captures == 2 && requestIndex == 7); // Each round must capture once and run vision before fairy.
		TEST_ASSERT(responses.Count() == 7); // Log all replies, including speak calls and empty final messages.
		for (vint i = 0; i < responses.Count(); i++)
			TEST_ASSERT(responseAgents[i] == (i == 0 || i == 1 || i == 4 || i == 5)); // Each response needs the correct agent label.
		auto visionCalls = GetField(responses[0], L"tool_calls").Cast<json::JsonArray>();
		TEST_ASSERT(visionCalls->items.Count() == 1 && GetString(GetField(visionCalls->items[0], L"function"), L"name") == L"speak"); // Keep vision speak in the JSON log.
		TEST_ASSERT(GetField(responses[2], L"content").Cast<json::JsonLiteral>()->value == json::JsonLiteralValue::Null); // Preserve the original JSON message, including null content.
		TEST_ASSERT(GetField(responses[2], L"tool_calls").Cast<json::JsonArray>()->items.Count() == 3); // Log all tool calls from a mixed reply.
		TEST_ASSERT(GetString(responses[6], L"content") == L"继续加油。"); // Log ordinary assistant content even when there is no speak.
		TEST_ASSERT(File(folder.root / L"custom-state" / L"interests" / L"cpp.md").ReadAllTextByBom() == L"用户阅读 C++"); // Tool memory must persist to disk.

		// A failed fairy response must not poison the next round's conversation.
		vint attempt = 0;
		auto flaky = [&](const WString& body)
		{
			auto request = ParseJson(body, parser);
			if (GetString(request, L"model") == config.visionModel)
				return GetField(request, L"messages").Cast<json::JsonArray>()->items.Count() == 2 ? observation : terminal;
			TEST_ASSERT(GetField(request, L"messages").Cast<json::JsonArray>()->items.Count() == 2); // Failed fairy round must roll back history.
			if (attempt++ == 0) throw Exception(L"Simulated transport failure.");
			return terminal;
		};
		FairyApplication retry(folder.root / L"custom-state", config, prompts, flaky, capture, web);
		TEST_EXCEPTION(retry.RunRound(), Exception, [](const Exception&) {});
		TEST_ASSERT(retry.RunRound().Length() == 0); // Fairy may stay silent.

		for (auto response : {
			L"invalid JSON",
			LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"这段普通文字不能代替视觉 speak。"}}]})",
			LR"({"choices":[{"finish_reason":"length","message":{"role":"assistant","content":"截断"}}]})",
			LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":true}}]})",
			LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","tool_calls":true}}]})",
			LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","tool_calls":[{"id":"write","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"should-not-exist.md\",\"content\":\"bad\"}"}}]}}]})",
			LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"duplicate","type":"function","function":{"name":"file_write","arguments":"{\"path\":\"should-not-exist.md\",\"content\":\"bad\"}"}},{"id":"duplicate","type":"function","function":{"name":"speak","arguments":"{}"}}]}}]})"
		})
		{
			vint corrections = 0;
			auto malformed = [&](const WString& body)
			{
				auto request = ParseJson(body, parser);
				if (GetString(request, L"model") == config.fairyModel) return terminal;
				auto messages = GetField(request, L"messages").Cast<json::JsonArray>();
				if (corrections++ == 0) return WString(response);
				if (corrections == 2)
				{
					TEST_ASSERT(messages->items.Count() == 3); // Reject malformed assistant envelopes from history.
					auto feedback = GetString(messages->items[2], L"content");
					TEST_ASSERT(GetString(messages->items[2], L"role") == L"user" && feedback.Length() < 300 && !wcsstr(feedback.Buffer(), L"工具说明")); // Envelope feedback must be compact without predefined prompts.
					TEST_ASSERT(GetField(ParseJson(feedback, parser), L"ok").Cast<json::JsonLiteral>()->value == json::JsonLiteralValue::False); // Return envelope parse errors to the model.
					return observation;
				}
				return terminal;
			};
			FairyApplication invalid(folder.root / L"custom-state", config, prompts, malformed, capture, web);
			TEST_ASSERT(invalid.RunRound().Length() == 0 && corrections == 3 && !File(folder.root / L"custom-state" / L"should-not-exist.md").Exists()); // Malformed envelope must get feedback without executing tools, then accept correction.
		}

		for (bool testFairy : { false, true })
		{
			vint argumentRequests = 0;
			auto badArguments = [&](const WString& body)
			{
				auto request = ParseJson(body, parser);
				auto messages = GetField(request, L"messages").Cast<json::JsonArray>();
				if (GetString(request, L"model") != (testFairy ? config.fairyModel : config.visionModel))
					return testFairy && messages->items.Count() == 2 ? observation : terminal;
				TEST_ASSERT(GetString(request, L"tool_choice") == (argumentRequests < 2 ? L"required" : L"auto")); // Invalid speak arguments and successful non-speech tools do not submit speech.
				if (argumentRequests++ == 0)
					return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"bad-json","type":"function","function":{"name":"speak","arguments":"{bad"}},{"id":"unknown","type":"function","function":{"name":"unknown_tool","arguments":"{}"}},{"id":"list","type":"function","function":{"name":"file_list","arguments":"{}"}}]}}]})");
				if (argumentRequests == 2)
				{
					TEST_ASSERT(messages->items.Count() == 6); // Every call needs feedback.
					for (vint i = 3; i < 5; i++)
					{
						auto feedback = GetString(messages->items[i], L"content");
						TEST_ASSERT(GetString(messages->items[i], L"role") == L"tool" && feedback.Length() < 100); // Tool errors must be compact tool messages.
						TEST_ASSERT(GetField(ParseJson(feedback, parser), L"ok").Cast<json::JsonLiteral>()->value == json::JsonLiteralValue::False); // Argument parse and dispatch errors must be returned.
					}
					TEST_ASSERT(GetField(ParseJson(GetString(messages->items[5], L"content"), parser), L"ok").Cast<json::JsonLiteral>()->value == json::JsonLiteralValue::True);
					return observation;
				}
				return terminal;
			};
			FairyApplication arguments(folder.root / L"custom-state", config, prompts, badArguments, capture, web);
			TEST_ASSERT(arguments.RunRound() == (testFairy ? L"用户正在阅读 C++ 代码。" : L"") && argumentRequests == 3); // Continue after either agent corrects its tool arguments.
		}

		vint loopRequests = 0;
		auto looping = [&](const WString&)
		{
			loopRequests++;
			return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"repeat","type":"function","function":{"name":"file_list","arguments":"{}"}}]}}]})");
		};
		FairyApplication loop(folder.root / L"custom-state", config, prompts, looping, capture, web);
		TEST_EXCEPTION(loop.RunRound(), Exception, [](const Exception&) {});
		TEST_ASSERT(loopRequests == 24); // Tool loop must have a finite request budget.
	});

	TEST_CASE(L"Speech accumulates across replies while each round retains its original observation timestamp")
	{
		RuntimeTestFolder folder;
		RuntimeTestClock clock;
		json::Parser parser;
		ApiConfig config;
		config.visionModel = L"test-vision";
		config.fairyModel = L"test-fairy";
		AgentPrompts prompts{ L"工具说明", L"记忆指引", L"视觉请求", L"精灵请求", L"固定性格" };
		vint round = 0;
		vint step = 0;
		DateTime times[] = {
			DateTime::FromDateTime(2026, 12, 31, 23, 59, 59),
			DateTime::FromDateTime(2027, 1, 1, 0, 0, 0),
			DateTime::FromDateTime(2027, 2, 3, 4, 5, 6),
		};
		WString timestamps[] = { L"2026-12-31 23-59-59", L"2027-01-01 00-00-00", L"2027-02-03 04-05-06" };
		List<WString> expectedInputs;
		auto speechReply = [&](const WString& idPrefix, const WString& first, const WString& last)
		{
			auto calls = Ptr(new json::JsonArray);
			for (auto text : { first, WString::Empty, last })
			{
				auto arguments = Ptr(new json::JsonObject);
				SetString(arguments, L"text", text);
				auto function = Ptr(new json::JsonObject);
				SetString(function, L"name", L"speak");
				SetString(function, L"arguments", json::JsonToString(arguments));
				auto call = Ptr(new json::JsonObject);
				SetString(call, L"id", idPrefix + itow(calls->items.Count()));
				SetString(call, L"type", L"function");
				SetField(call, L"function", function);
				calls->items.Add(call);
			}
			auto message = TextMessage(L"assistant", L"普通回复不应加入发言。");
			SetField(message, L"tool_calls", calls);
			auto choice = Ptr(new json::JsonObject);
			SetString(choice, L"finish_reason", L"tool_calls");
			SetField(choice, L"message", message);
			auto choices = Ptr(new json::JsonArray);
			choices->items.Add(choice);
			auto response = Ptr(new json::JsonObject);
			SetField(response, L"choices", choices);
			return json::JsonToString(response);
		};
		auto terminal = WString(LR"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"结束文字不应加入发言。"}}]})");
		auto complete = [&](const WString& body)
		{
			auto request = ParseJson(body, parser);
			auto messages = GetField(request, L"messages").Cast<json::JsonArray>();
			TEST_ASSERT(GetString(request, L"model") == (step < 3 ? config.visionModel : config.fairyModel)); // Keep each agent's repeated speech on its configured model.
			TEST_ASSERT(GetString(request, L"tool_choice") == (step == 0 || step == 3 ? L"required" : L"auto")); // Reset speech submission each round, and allow completion after nonempty or empty fairy speak.
			auto system = step < 3 ? L"工具说明\n\n记忆指引\n\n视觉请求" : L"工具说明\n\n记忆指引\n\n精灵请求\n\n固定性格";
			TEST_ASSERT(GetString(messages->items[0], L"role") == L"system" && GetString(messages->items[0], L"content") == system); // Preserve prompt order and keep the observation timestamp out of system prompts.
			TEST_ASSERT(clock.localTimeCalls == round + (step < 3 ? 0 : 1)); // Sample local time once after vision finishes, never again for tool feedback.
			if (step >= 3)
			{
				vint inputIndex = 0;
				for (auto message : messages->items)
				{
					if (GetString(message, L"role") != L"user") continue;
					TEST_ASSERT(inputIndex < expectedInputs.Count());
					TEST_ASSERT(GetString(message, L"content") == expectedInputs[inputIndex++]); // Keep full repeated vision speech and each original timestamp throughout tool follow-ups and later rounds.
				}
				TEST_ASSERT(inputIndex == round + 1);
				clock.currentTime = clock.Forward(clock.currentTime, 1000); // A follow-up must retain its timestamp even while the clock advances.
			}
			auto observation = L"观察 " + itow(round) + L"\n\"原文\"";
			auto reaction = L"回应 " + itow(round);
			switch (step++)
			{
			case 0:
				TEST_ASSERT(messages->items.Count() == 2); // Repeated speech must not retain the previous vision session.
				return speechReply(L"vision-first-", observation, observation);
			case 1:
				return speechReply(L"vision-more-", L"补充观察", L"");
			case 2:
				clock.currentTime = times[round].osInternal;
				return terminal;
			case 5:
				return terminal;
			case 3:
			{
				auto input = messages->items[messages->items.Count() - 1];
				TEST_ASSERT(GetString(input, L"role") == L"user" && GetString(input, L"content") == expectedInputs[round]); // Add the current observation after all prior conversation messages.
				if (round == 2)
					return WString(LR"({"choices":[{"finish_reason":"tool_calls","message":{"role":"assistant","tool_calls":[{"id":"silent-fairy","type":"function","function":{"name":"speak","arguments":"{\"text\":\"\"}"}}]}}]})");
				return speechReply(L"fairy-first-" + itow(round), reaction, reaction);
			}
			case 4:
				if (round == 2) return terminal;
				return speechReply(L"fairy-more-" + itow(round), L"补充回应", L"");
			default:
				throw Exception(L"Unexpected speech aggregation request.");
			}
		};
		auto capture = [](List<MonitorSnapshot>& snapshots)
		{
			MonitorSnapshot snapshot;
			snapshot.name = L"test-display";
			snapshot.width = snapshot.height = 1;
			snapshot.dataUrl = L"data:image/png;base64,fixture";
			snapshots.Add(snapshot);
		};
		auto web = [](const WString&) -> WebResponse { throw Exception(L"Unexpected network request in speech aggregation test."); };
		FairyApplication application(folder.root / L"custom-state", config, prompts, complete, capture, web);
		for (; round < 3; round++)
		{
			step = 0;
			clock.currentTime = times[round].Backward(1000).osInternal;
			auto observation = L"观察 " + itow(round) + L"\n\"原文\"";
			expectedInputs.Add(L"当前日期时间是：" + timestamps[round] + L"\n以下是用户所有屏幕的内容：\n" + observation + L"\n" + observation + L"\n补充观察");
			auto result = application.RunRound();
			auto reaction = L"回应 " + itow(round);
			auto expected = round == 2 ? WString::Empty : reaction + L"\n" + reaction + L"\n补充回应";
			TEST_ASSERT(result == expected); // Return every fairy speak in order across replies, without leaking prior rounds; one empty speak must return an empty result.
			TEST_ASSERT(step == (round == 2 ? 5 : 6)); // Continue after speak to process tool feedback and the final response.
			TEST_ASSERT(clock.localTimeCalls == round + 1);
		}
	});
}
