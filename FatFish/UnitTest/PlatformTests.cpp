#include "../../Agents/Platform.h"
#include "../../Agents/Json.h"
#include <VlppOS.Windows.h>

using namespace fatfish;
using namespace vl;
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

TEST_FILE
{
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
