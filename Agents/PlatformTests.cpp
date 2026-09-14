#include "Platform.h"
#include "Json.h"
#include <VlppOS.Windows.h>

namespace fatfish
{
	using namespace vl;
	using namespace vl::filesystem;
	using namespace vl::glr;

	void RequirePlatformTest(bool condition, const WString& message)
	{
		if (!condition) throw Exception(L"Platform test failed: " + message);
	}

	void ExpectPlatformFailure(const Func<void()>& action)
	{
		bool failed = false;
		try
		{
			action();
		}
		catch (const Exception& error)
		{
			RequirePlatformTest(wcsstr(error.Message().Buffer(), L"test-secret") == nullptr, L"Errors must not reveal credentials.");
			failed = true;
		}
		RequirePlatformTest(failed, L"Invalid input should be rejected before performing I/O.");
	}

	void RunPlatformTests()
	{
		RequirePlatformTest(GetChatCompletionUrl(L"https://example.test/v1/") == L"https://example.test/v1/chat/completions", L"Append Chat Completions to a base URL.");
		RequirePlatformTest(GetChatCompletionUrl(L"https://example.test/v1/chat/completions") == L"https://example.test/v1/chat/completions", L"Accept a complete endpoint URL.");
		RequirePlatformTest(GetChatCompletionUrl(L"http://localhost:8123/api/?version=1") == L"http://localhost:8123/api/chat/completions?version=1", L"Preserve ports, API prefixes and query strings.");
		for (auto url : { L"file:///C:/private.txt", L"https://user:password@example.test", L"https://example.test/#fragment", L"https://example.test/\r\nInjected:yes", L"https://example.test/\\private" })
		{
			ExpectPlatformFailure([&] { GetChatCompletionUrl(url); });
			ExpectPlatformFailure([&] { HttpGet(url); });
		}
		ExpectPlatformFailure([] { HttpGet(L"https://example.test", 0); });

		wchar_t tempFolder[MAX_PATH + 1];
		wchar_t tempName[MAX_PATH + 1];
		auto tempLength = GetTempPathW(MAX_PATH, tempFolder);
		RequirePlatformTest(tempLength > 0 && tempLength < MAX_PATH, L"Find temporary directory.");
		RequirePlatformTest(GetTempFileNameW(tempFolder, L"ffp", 0, tempName) != 0, L"Reserve a temporary fixture path.");
		auto root = FilePath(tempName);
		auto prefix = FilePath(tempFolder).GetFullPath() + WString::FromChar(FilePath::GetPathDelimiter());
		RequirePlatformTest(root.GetFullPath().Length() > prefix.Length()
			&& Locale::Invariant().CompareOrdinalIgnoreCase(root.GetFullPath().Left(prefix.Length()), prefix) == 0,
			L"Keep the fixture under the system temporary directory.");
		RequirePlatformTest(File(root).Delete() && Folder(root / L"env").Create(true), L"Create the fixture directory.");
		File configFile(root / L"env" / L"apikey.json");
		json::Parser parser;
		ExpectPlatformFailure([&] { LoadApiConfig(root, parser); });
		auto fixture = Ptr(new json::JsonObject);
		SetString(fixture, L"apikey", L"test-secret");
		SetString(fixture, L"url", L"https://example.test/v1");
		SetString(fixture, L"auth_header", L"Authorization: Bearer $APIKEY");
		SetString(fixture, L"vision_model", L"vision-test");
		SetString(fixture, L"chat_model", L"fairy-test");
		auto save = [&]
		{
			RequirePlatformTest(configFile.WriteAllText(json::JsonToString(fixture), false, stream::BomEncoder::Utf8), L"Save synthetic UTF-8 configuration.");
		};
		save();
		auto config = LoadApiConfig(root, parser);
		RequirePlatformTest(config.apiKey == L"test-secret" && config.url == L"https://example.test/v1/chat/completions"
			&& config.visionModel == L"vision-test" && config.fairyModel == L"fairy-test", L"Load all template configuration fields.");
		SetString(fixture, L"fairy_model", L"fairy-test");
		save();
		RequirePlatformTest(LoadApiConfig(root, parser).fairyModel == L"fairy-test", L"Matching canonical and legacy model fields are accepted.");
		SetString(fixture, L"fairy_model", L"conflicting-fairy");
		save();
		ExpectPlatformFailure([&] { LoadApiConfig(root, parser); });
		fixture->fields.RemoveAt(fixture->fields.Count() - 1);
		auto legacyModelField = fixture->fields[fixture->fields.Count() - 1];
		legacyModelField->name.value = L"fairy_model";
		save();
		RequirePlatformTest(LoadApiConfig(root, parser).fairyModel == L"fairy-test", L"The canonical fairy_model field works without the legacy alias.");
		legacyModelField->name.value = L"chat_model";

		for (auto malformed : { L"{\"apikey\":\"test-secret\"", L"@{}", L"[]", L"{}", L"{\"apikey\":\"test-secret\",\"apikey\":\"duplicate\"}" })
		{
			RequirePlatformTest(configFile.WriteAllText(malformed, false, stream::BomEncoder::Utf8), L"Save malformed configuration.");
			ExpectPlatformFailure([&] { LoadApiConfig(root, parser); });
		}
		SetString(fixture, L"chat_model", L"vision-test");
		save();
		RequirePlatformTest(LoadApiConfig(root, parser).fairyModel == L"vision-test", L"Independent model slots can target the same provider model.");
		SetString(fixture, L"chat_model", L"fairy-test");
		for (auto header : { L"Authorization", L"Bad Header: $APIKEY", L"Authorization: $APIKEY\r\nX-Injected: yes" })
		{
			SetString(fixture, L"auth_header", header);
			save();
			ExpectPlatformFailure([&] { LoadApiConfig(root, parser); });
		}
		SetString(fixture, L"auth_header", L"X-Api-Key: $APIKEY");
		SetString(fixture, L"apikey", L"test-secret\r\nInjected: yes");
		save();
		ExpectPlatformFailure([&] { LoadApiConfig(root, parser); });
		SetString(fixture, L"apikey", L"test-secret");
		save();
		RequirePlatformTest(LoadApiConfig(root, parser).authHeader == L"X-Api-Key: $APIKEY", L"A valid config loads after parse errors and supports custom authentication headers.");
		RequirePlatformTest(configFile.Delete() && Folder(root / L"env").Delete(false) && Folder(root).Delete(false), L"Remove only the temporary fixture files.");
	}
}
