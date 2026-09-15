#include "../../Agents/Platform.h"
#include "../../Agents/Json.h"
#include <VlppOS.Windows.h>

using namespace fatfish;
using namespace vl;
using namespace vl::collections;
using namespace vl::filesystem;
using namespace vl::glr;

void ExpectPlatformFailure(const Func<void()>& action)
{
	TEST_EXCEPTION(action(), Exception, [](const Exception& error)
	{
		TEST_ASSERT(wcsstr(error.Message().Buffer(), L"test-secret") == nullptr);
	});
}

void ExpectCompletionRejection(const Func<void()>& action, bool contextLimit)
{
	bool rejected = false;
	try
	{
		action();
	}
	catch (const ContextLimitExceeded& error)
	{
		rejected = true;
		TEST_ASSERT(contextLimit && error.Message() == L"The model request exceeds the model's context limit.");
	}
	catch (const ChatCompletionError& error)
	{
		rejected = true;
		TEST_ASSERT(!contextLimit && error.Message() == L"The model server reported an error.");
	}
	TEST_ASSERT(rejected);
}

MonitorSnapshot MakePlatformSnapshot(vint index)
{
	MonitorSnapshot snapshot;
	snapshot.name = L"synthetic-monitor-" + itow(index);
	snapshot.dataUrl = L"data:image/png;base64,synthetic-" + itow(index);
	snapshot.left = -1920 + index * 1920;
	snapshot.top = -120;
	snapshot.width = 1920;
	snapshot.height = 1080;
	return snapshot;
}

TEST_FILE
{
	TEST_CASE(L"Capture aggregation keeps successful monitors in order after denied or unrelated failures")
	{
		List<MonitorSnapshot> snapshots;
		snapshots.Add(MakePlatformSnapshot(99));
		vint enumerations = 0;
		vint attempts = 0;
		CaptureMonitors(snapshots, [&]()
		{
			enumerations++;
			TEST_ASSERT(snapshots.Count() == 0);
			return 5;
		}, [&](vint index)
		{
			TEST_ASSERT(index == attempts++);
			if (index == 0 || index == 4) throw MonitorCaptureError(L"BitBlt", ERROR_ACCESS_DENIED);
			if (index == 2) throw Exception(L"PNG encoding failed.");
			return MakePlatformSnapshot(index);
		});
		TEST_ASSERT(enumerations == 1 && attempts == 5 && snapshots.Count() == 2);
		for (vint index = 0; index < snapshots.Count(); index++)
		{
			auto expected = MakePlatformSnapshot(index * 2 + 1);
			auto& actual = snapshots[index];
			TEST_ASSERT(actual.name == expected.name && actual.dataUrl == expected.dataUrl);
			TEST_ASSERT(actual.left == expected.left && actual.top == expected.top && actual.width == expected.width && actual.height == expected.height);
		}
	});

	TEST_CASE(L"Capture aggregation treats no monitors and capture enumeration access denial as unavailable")
	{
		List<MonitorSnapshot> snapshots;
		vint attempts = 0;
		auto capture = [&](vint index)
		{
			attempts++;
			return MakePlatformSnapshot(index);
		};
		snapshots.Add(MakePlatformSnapshot(99));
		TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint { return 0; }, capture), ScreenCaptureUnavailable, [](const ScreenCaptureUnavailable&) {});
		TEST_ASSERT(snapshots.Count() == 0 && attempts == 0);
		snapshots.Add(MakePlatformSnapshot(99));
		TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint
		{
			throw MonitorCaptureError(L"EnumDisplayMonitors", ERROR_ACCESS_DENIED);
		}, capture), ScreenCaptureUnavailable, [](const ScreenCaptureUnavailable&) {});
		TEST_ASSERT(snapshots.Count() == 0 && attempts == 0);
		snapshots.Add(MakePlatformSnapshot(99));
		TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint
		{
			throw MonitorCaptureError(L"EnumDisplayMonitors", ERROR_INVALID_PARAMETER);
		}, capture), MonitorCaptureError, [](const MonitorCaptureError& error)
		{
			TEST_ASSERT(error.ErrorCode() == ERROR_INVALID_PARAMETER && error.Message() == L"EnumDisplayMonitors failed (Windows error 87).");
		});
		TEST_ASSERT(snapshots.Count() == 0 && attempts == 0);
	});

	TEST_CASE(L"All denied monitors become unavailable and a later capture replaces the empty result")
	{
		List<MonitorSnapshot> snapshots;
		snapshots.Add(MakePlatformSnapshot(99));
		vint attempts = 0;
		TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint { return 3; }, [&](vint) -> MonitorSnapshot
		{
			attempts++;
			throw MonitorCaptureError(L"BitBlt", ERROR_ACCESS_DENIED);
		}), ScreenCaptureUnavailable, [](const ScreenCaptureUnavailable& error)
		{
			TEST_ASSERT(error.Message() == L"No accessible desktop monitors are available to capture.");
		});
		TEST_ASSERT(attempts == 3 && snapshots.Count() == 0);
		CaptureMonitors(snapshots, []() -> vint { return 1; }, [](vint index) { return MakePlatformSnapshot(index); });
		TEST_ASSERT(snapshots.Count() == 1 && snapshots[0].name == L"synthetic-monitor-0" && snapshots[0].left == -1920);
	});

	TEST_CASE(L"All failed captures preserve the first unrelated native or encoding error instead of unavailable")
	{
		for (vint ordinaryIndex : { 0, 1, 2 })
		{
			List<MonitorSnapshot> snapshots;
			vint attempts = 0;
			TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint { return 4; }, [&](vint index) -> MonitorSnapshot
			{
				attempts++;
				if (index == ordinaryIndex) throw MonitorCaptureError(L"BitBlt", ERROR_INVALID_PARAMETER);
				if (index == 3) throw Exception(L"Later PNG encoding failure.");
				throw MonitorCaptureError(L"BitBlt", ERROR_ACCESS_DENIED);
			}), MonitorCaptureError, [](const MonitorCaptureError& error)
			{
				TEST_ASSERT(error.ErrorCode() == ERROR_INVALID_PARAMETER && error.Message() == L"BitBlt failed (Windows error 87).");
			});
			TEST_ASSERT(attempts == 4 && snapshots.Count() == 0);
		}
		List<MonitorSnapshot> snapshots;
		vint attempts = 0;
		TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint { return 3; }, [&](vint index) -> MonitorSnapshot
		{
			attempts++;
			if (index == 0) throw Exception(L"PNG encoding failed.");
			throw MonitorCaptureError(L"BitBlt", index == 1 ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER);
		}), Exception, [](const Exception& error)
		{
			TEST_ASSERT(error.Message() == L"PNG encoding failed.");
		});
		TEST_ASSERT(attempts == 3 && snapshots.Count() == 0);
	});

	TEST_CASE(L"Capture unavailability depends on a captured native error code instead of error text")
	{
		List<MonitorSnapshot> snapshots;
		TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint { return 1; }, [](vint) -> MonitorSnapshot
		{
			throw Exception(L"BitBlt failed (Windows error 5).");
		}), Exception, [](const Exception& error)
		{
			TEST_ASSERT(error.Message() == L"BitBlt failed (Windows error 5).");
		});
		TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint { return 1; }, [](vint) -> MonitorSnapshot
		{
			throw MonitorCaptureError(L"BitBlt failed (Windows error 5)", ERROR_INVALID_PARAMETER);
		}), MonitorCaptureError, [](const MonitorCaptureError& error)
		{
			TEST_ASSERT(error.ErrorCode() == ERROR_INVALID_PARAMETER);
		});
	});

	TEST_CASE(L"Cancellation during enumeration or after a successful monitor propagates without further capture")
	{
		List<MonitorSnapshot> snapshots;
		snapshots.Add(MakePlatformSnapshot(99));
		vint attempts = 0;
		TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint { throw OperationCancelled(); }, [&](vint index)
		{
			attempts++;
			return MakePlatformSnapshot(index);
		}), OperationCancelled, [](const OperationCancelled&) {});
		TEST_ASSERT(snapshots.Count() == 0 && attempts == 0);
		TEST_EXCEPTION(CaptureMonitors(snapshots, []() -> vint { return 3; }, [&](vint index)
		{
			attempts++;
			if (index == 1) throw OperationCancelled();
			return MakePlatformSnapshot(index);
		}), OperationCancelled, [](const OperationCancelled&) {});
		TEST_ASSERT(attempts == 2 && snapshots.Count() == 1 && snapshots[0].name == L"synthetic-monitor-0");
	});

	TEST_CASE(L"JSON and streaming server errors classify recognized context overflows without exposing response text")
	{
		json::Parser parser;
		for (auto envelope : {
			LR"({"error":{"code":"context_length_exceeded","message":"test-secret"}})",
			LR"({"error":{"type":"Context_Window_Exceeded","code":null,"message":"test-secret"}})",
			LR"({"error":{"code":"prompt_too_long","message":"test-secret"}})",
			LR"({"error":{"message":"This model's maximum context length is 4096 tokens. However, you requested 5000 tokens. test-secret"}})",
			LR"({"object":"error","type":"BadRequestError","code":400,"message":"The input length exceeds the context length: test-secret"})",
			LR"({"error":"The request exceeds the available context size: test-secret"})",
			LR"({"error":{"type":"invalid_request_error","message":"Prompt is too long: test-secret"}})"
		})
		{
			ExpectCompletionRejection([&] { ParseChatCompletion(envelope, parser); }, true);
			ExpectCompletionRejection([&] { ParseChatCompletion(L"event: error\ndata: " + WString(envelope) + L"\n\n", parser); }, true);
			// No partially accumulated assistant/tool response may escape an errored stream.
			auto partial = WString(LR"({"choices":[{"index":0,"delta":{"role":"assistant","tool_calls":[{"index":0,"id":"pending","type":"function","function":{"name":"speak","arguments":"{\"text\":\"partial\"}"}}]},"finish_reason":null}]})");
			ExpectCompletionRejection([&] { ParseChatCompletion(L"data: " + partial + L"\n\ndata: " + WString(envelope) + L"\n\n", parser); }, true);
		}
	});

	TEST_CASE(L"Other model rejections stay distinct from context and assistant formatting errors")
	{
		json::Parser parser;
		for (auto envelope : {
			LR"({"error":{"code":"rate_limit_exceeded","message":"test-secret requested more than the maximum context length"}})",
			LR"({"error":{"type":"server_error","message":"test-secret: context length exceeded"}})",
			LR"({"error":{"type":"authentication_error","message":"test-secret: prompt too long"}})",
			LR"({"error":{"code":"max_tokens_exceeded","message":"test-secret: maximum context length exceeded"}})",
			LR"({"error":{"type":"invalid_request_error","message":"max_tokens must be smaller than the maximum context length: test-secret"}})",
			LR"({"error":{"message":"test-secret exceeded the tokens per minute quota; prompt too long"}})",
			LR"({"error":{"message":"test-secret exceeded the maximum output tokens"}})",
			LR"({"error":{"message":"test-secret: token limit exceeded"}})",
			LR"({"error":{"message":"test-secret: invalid model"}})",
			LR"({"error":{"message":["test-secret"],"code":["context_length_exceeded"]}})",
			LR"({"error":{"message":"test-secret"},"error":{"code":"context_length_exceeded"}})"
		})
		{
			ExpectCompletionRejection([&] { ParseChatCompletion(envelope, parser); }, false);
			ExpectCompletionRejection([&] { ParseChatCompletion(L"data: " + WString(envelope) + L"\n\n", parser); }, false);
		}
		auto completion = ParseChatCompletion(LR"({"error":null,"choices":[{"finish_reason":"length","message":{"role":"assistant","content":"test-secret"}}]})", parser);
		TEST_ASSERT(GetString(GetField(completion, L"choices").Cast<json::JsonArray>()->items[0], L"finish_reason") == L"length"); // Output truncation is not a context rejection.
	});

	TEST_CASE(L"HTTP authentication rate and server failures override apparent context errors in their bodies")
	{
		json::Parser parser;
		auto envelope = ParseJson(LR"({"error":{"code":"context_length_exceeded","message":"test-secret: maximum context length exceeded"}})", parser);
		for (vint status : { 200, 400, 413, 422 })
		{
			ExpectCompletionRejection([&] { ThrowIfChatCompletionError(envelope, status); }, true);
		}
		for (vint status : { 301, 401, 403, 404, 408, 429, 500, 503 })
		{
			ExpectCompletionRejection([&] { ThrowIfChatCompletionError(envelope, status); }, false);
		}
	});

	TEST_CASE(L"Cancellation stays signaled and prevents HTTP I/O")
	{
		auto cancellation = Ptr(new CancellationToken);
		TEST_ASSERT(!cancellation->IsCancelled());
		cancellation->ThrowIfCancelled();
		cancellation->Cancel();
		cancellation->Cancel();
		TEST_ASSERT(cancellation->IsCancelled() && cancellation->Event().WaitForTime(0));
		TEST_ASSERT(cancellation->IsCancelled());
		TEST_EXCEPTION(cancellation->ThrowIfCancelled(), OperationCancelled, [](const OperationCancelled&) {});
		// Deliberately invalid inputs prove cancellation wins before validation or I/O.
		TEST_EXCEPTION(PostChatCompletion({}, L"", cancellation), OperationCancelled, [](const OperationCancelled&) {});
		TEST_EXCEPTION(HttpGet(L"", 0, cancellation), OperationCancelled, [](const OperationCancelled&) {});
	});

	TEST_CASE(L"Completion URLs normalize and unsafe requests fail before I/O")
	{
		TEST_ASSERT(GetChatCompletionUrl(L"https://example.test/v1/") == L"https://example.test/v1/chat/completions"); // Append Chat Completions to a base URL.
		TEST_ASSERT(GetChatCompletionUrl(L"https://example.test/v1/chat/completions") == L"https://example.test/v1/chat/completions"); // Accept a complete endpoint URL.
		TEST_ASSERT(GetChatCompletionUrl(L"http://localhost:8123/api/?version=1") == L"http://localhost:8123/api/chat/completions?version=1"); // Preserve ports, API prefixes and query strings.
		for (auto url : { L"file:///C:/private.txt", L"https://user:password@example.test", L"https://example.test/#fragment", L"https://example.test/\r\nInjected:yes", L"https://example.test/\\private" })
		{
			ExpectPlatformFailure([&] { GetChatCompletionUrl(url); });
			ExpectPlatformFailure([&] { HttpGet(url); });
		}
		ExpectPlatformFailure([] { HttpGet(L"https://example.test", 0); });
	});

	TEST_CASE(L"API configuration validates fields, aliases and authentication without exposing secrets")
	{
		wchar_t tempFolder[MAX_PATH + 1];
		wchar_t tempName[MAX_PATH + 1];
		auto tempLength = GetTempPathW(MAX_PATH, tempFolder);
		TEST_ASSERT(tempLength > 0 && tempLength < MAX_PATH); // Find temporary directory.
		TEST_ASSERT(GetTempFileNameW(tempFolder, L"ffp", 0, tempName) != 0); // Reserve a temporary fixture path.
		auto root = FilePath(tempName);
		auto prefix = FilePath(tempFolder).GetFullPath() + WString::FromChar(FilePath::GetPathDelimiter());
		TEST_ASSERT(root.GetFullPath().Length() > prefix.Length()
			&& Locale::Invariant().CompareOrdinalIgnoreCase(root.GetFullPath().Left(prefix.Length()), prefix) == 0); // Keep the fixture under the system temporary directory.
		TEST_ASSERT(File(root).Delete() && Folder(root / L"custom-prompts").Create(true)); // Create the fixture directory.
		File configFile(root / L"custom-prompts" / L"apikey.json");
		json::Parser parser;
		ExpectPlatformFailure([&] { LoadApiConfig(root / L"custom-prompts", parser); });
		auto fixture = Ptr(new json::JsonObject);
		SetString(fixture, L"apikey", L"test-secret");
		SetString(fixture, L"url", L"https://example.test/v1");
		SetString(fixture, L"auth_header", L"Authorization: Bearer $APIKEY");
		SetString(fixture, L"vision_model", L"vision-test");
		SetString(fixture, L"chat_model", L"fairy-test");
		auto save = [&]
		{
			TEST_ASSERT(configFile.WriteAllText(json::JsonToString(fixture), false, stream::BomEncoder::Utf8)); // Save synthetic UTF-8 configuration.
		};
		save();
		auto config = LoadApiConfig(root / L"custom-prompts", parser);
		TEST_ASSERT(config.apiKey == L"test-secret" && config.url == L"https://example.test/v1/chat/completions"
			&& config.visionModel == L"vision-test" && config.fairyModel == L"fairy-test"); // Load all template configuration fields.
		SetString(fixture, L"fairy_model", L"fairy-test");
		save();
		TEST_ASSERT(LoadApiConfig(root / L"custom-prompts", parser).fairyModel == L"fairy-test"); // Matching canonical and legacy model fields are accepted.
		SetString(fixture, L"fairy_model", L"conflicting-fairy");
		save();
		ExpectPlatformFailure([&] { LoadApiConfig(root / L"custom-prompts", parser); });
		fixture->fields.RemoveAt(fixture->fields.Count() - 1);
		auto legacyModelField = fixture->fields[fixture->fields.Count() - 1];
		legacyModelField->name.value = L"fairy_model";
		save();
		TEST_ASSERT(LoadApiConfig(root / L"custom-prompts", parser).fairyModel == L"fairy-test"); // The canonical fairy_model field works without the legacy alias.
		legacyModelField->name.value = L"chat_model";

		for (auto malformed : { L"{\"apikey\":\"test-secret\"", L"@{}", L"[]", L"{}", L"{\"apikey\":\"test-secret\",\"apikey\":\"duplicate\"}" })
		{
			TEST_ASSERT(configFile.WriteAllText(malformed, false, stream::BomEncoder::Utf8)); // Save malformed configuration.
			ExpectPlatformFailure([&] { LoadApiConfig(root / L"custom-prompts", parser); });
		}
		SetString(fixture, L"chat_model", L"vision-test");
		save();
		TEST_ASSERT(LoadApiConfig(root / L"custom-prompts", parser).fairyModel == L"vision-test"); // Independent model slots can target the same provider model.
		SetString(fixture, L"chat_model", L"fairy-test");
		for (auto header : { L"Authorization", L"Bad Header: $APIKEY", L"Authorization: $APIKEY\r\nX-Injected: yes" })
		{
			SetString(fixture, L"auth_header", header);
			save();
			ExpectPlatformFailure([&] { LoadApiConfig(root / L"custom-prompts", parser); });
		}
		SetString(fixture, L"auth_header", L"X-Api-Key: $APIKEY");
		SetString(fixture, L"apikey", L"test-secret\r\nInjected: yes");
		save();
		ExpectPlatformFailure([&] { LoadApiConfig(root / L"custom-prompts", parser); });
		SetString(fixture, L"apikey", L"test-secret");
		save();
		TEST_ASSERT(LoadApiConfig(root / L"custom-prompts", parser).authHeader == L"X-Api-Key: $APIKEY"); // A valid config loads after parse errors and supports custom authentication headers.
		TEST_ASSERT(configFile.Delete() && Folder(root / L"custom-prompts").Delete(false) && Folder(root).Delete(false)); // Remove only the temporary fixture files.
	});
}
