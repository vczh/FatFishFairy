#include "../../Agents/Platform.h"
#include "../../Agents/Json.h"
#include <VlppOS.Windows.h>

using namespace fatfish;
using namespace vl;
using namespace vl::filesystem;
using namespace vl::glr;

namespace
{
	void ExpectPlatformFailure(const Func<void()>& action)
	{
		TEST_EXCEPTION(action(), Exception, [](const Exception& error)
		{
			TEST_ASSERT(wcsstr(error.Message().Buffer(), L"test-secret") == nullptr);
		});
	}
}

TEST_FILE
{
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
