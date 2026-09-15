#include "../../Agents/Desktop.h"
#include "../../Agents/Json.h"
#include <VlppOS.Windows.h>

using namespace fatfish;
using namespace vl;
using namespace vl::collections;
using namespace vl::filesystem;
using namespace vl::glr;

FilePath CreateDesktopTestFolder()
{
	wchar_t tempFolder[MAX_PATH + 1];
	wchar_t tempName[MAX_PATH + 1];
	auto length = GetTempPathW(MAX_PATH, tempFolder);
	TEST_ASSERT(length > 0 && length < MAX_PATH);
	TEST_ASSERT(GetTempFileNameW(tempFolder, L"ffd", 0, tempName) != 0);
	auto root = FilePath(tempName);
	auto prefix = FilePath(tempFolder).GetFullPath() + WString::FromChar(FilePath::GetPathDelimiter());
	TEST_ASSERT(root.GetFullPath().Length() > prefix.Length()
		&& Locale::Invariant().CompareOrdinalIgnoreCase(root.GetFullPath().Left(prefix.Length()), prefix) == 0);
	TEST_ASSERT(File(root).Delete() && Folder(root).Create(false));
	return root;
}

void WriteDesktopFixture(const FilePath& path, const WString& text)
{
	if (!Folder(path.GetFolder()).Exists()) TEST_ASSERT(Folder(path.GetFolder()).Create(true));
	TEST_ASSERT(File(path).WriteAllText(text, true, stream::BomEncoder::Utf8));
}

void ExpectDesktopFailure(const Func<void()>& action)
{
	TEST_EXCEPTION(action(), Exception, [](const Exception&) {});
}

TEST_FILE
{
	TEST_CASE(L"Desktop position uses the supplied folder and preserves unrelated configuration")
	{
		auto root = CreateDesktopTestFolder();
		auto settings = root / L"unrelated-location" / L"settings";
		WriteDesktopFixture(root / L"env" / L"config.json", L"{\"windowX\":999,\"windowY\":888}");
		auto initial = LoadDesktopPosition(settings);
		TEST_ASSERT(initial.x == 0 && initial.y == 0);
		SaveDesktopPosition(settings, { -1920, 180 });
		auto restored = LoadDesktopPosition(settings);
		TEST_ASSERT(restored.x == -1920 && restored.y == 180);
		TEST_ASSERT(LoadDesktopPosition(root / L"env").x == 999);

		WriteDesktopFixture(settings / L"config.json", L"{\"windowX\":1,\"windowY\":2,\"future\":{\"title\":\"保留配置\"},\"enabled\":true}");
		SaveDesktopPosition(settings, { -2147483647 - 1, 2147483647 });
		auto extremes = LoadDesktopPosition(settings);
		TEST_ASSERT(extremes.x == -2147483647 - 1 && extremes.y == 2147483647);
		json::Parser parser;
		auto saved = ParseJson(File(settings / L"config.json").ReadAllTextByBom(), parser);
		TEST_ASSERT(GetString(GetField(saved, L"future"), L"title") == L"保留配置");
		TEST_ASSERT(GetField(saved, L"enabled").Cast<json::JsonLiteral>()->value == json::JsonLiteralValue::True);
		WriteDesktopFixture(settings / L"config.json", L"{\"windowY\":-400}");
		auto partial = LoadDesktopPosition(settings);
		TEST_ASSERT(partial.x == 0 && partial.y == -400);
		// The unique temporary target was checked when created; these fixtures contain no links.
		TEST_ASSERT(Folder(root).Delete(true));
	});

	TEST_CASE(L"Desktop configuration rejects malformed coordinates without overwriting the file")
	{
		auto root = CreateDesktopTestFolder();
		auto configPath = root / L"config.json";
		for (auto malformed : {
			L"[]", L"@{}", L"{", L"{\"windowX\":\"0\"}", L"{\"windowY\":null}",
			L"{\"windowX\":1.5}", L"{\"windowY\":1e3}", L"{\"windowX\":2147483648}",
			L"{\"windowY\":-2147483649}", L"{\"windowX\":0,\"windowX\":1}" })
		{
			WriteDesktopFixture(configPath, malformed);
			ExpectDesktopFailure([&] { LoadDesktopPosition(root); });
			ExpectDesktopFailure([&] { SaveDesktopPosition(root, { 20, 30 }); });
			TEST_ASSERT(File(configPath).ReadAllTextByBom() == malformed);
		}
		WriteDesktopFixture(configPath, L"{}");
		if constexpr (sizeof(vint) > 4)
		{
			ExpectDesktopFailure([&] { SaveDesktopPosition(root, { static_cast<vint>(2147483648LL), 0 }); });
			ExpectDesktopFailure([&] { SaveDesktopPosition(root, { 0, static_cast<vint>(-2147483649LL) }); });
			TEST_ASSERT(File(configPath).ReadAllTextByBom() == L"{}");
		}
		TEST_ASSERT(File(configPath).Delete() && Folder(configPath).Create(false));
		ExpectDesktopFailure([&] { LoadDesktopPosition(root); });
		ExpectDesktopFailure([&] { SaveDesktopPosition(root, { 0, 0 }); });
		TEST_ASSERT(Folder(root).Delete(true));
	});

	TEST_CASE(L"Theme metadata retains ordering and verifies every contiguous declared frame")
	{
		auto root = CreateDesktopTestFolder();
		auto artwork = root / L"custom-artwork";
		WriteDesktopFixture(artwork / L"theme.json", L"{\"second\":\"默认角色\",\"first\":\"另一个角色\"}");
		WriteDesktopFixture(artwork / L"second" / L"index.json", L"{\"long_action\":5,\"short_action\":3}");
		WriteDesktopFixture(artwork / L"first" / L"index.json", L"{\"single\":1}");
		// Metadata verification only needs file existence; fixtures never decode images or use graphics.
		for (vint frame = 1; frame <= 5; frame++)
			WriteDesktopFixture(artwork / L"second" / (L"long_action_" + itow(frame) + L".png"), L"synthetic frame");
		for (vint frame = 1; frame <= 3; frame++)
			WriteDesktopFixture(artwork / L"second" / (L"short_action_" + itow(frame) + L".png"), L"synthetic frame");
		WriteDesktopFixture(artwork / L"first" / L"single_1.png", L"synthetic frame");
		List<Ptr<DesktopTheme>> themes;
		LoadDesktopThemes(artwork, themes);
		TEST_ASSERT(themes.Count() == 2 && themes[0]->name == L"second" && themes[1]->name == L"first");
		TEST_ASSERT(themes[0]->displayName == L"默认角色" && themes[1]->displayName == L"另一个角色");
		TEST_ASSERT(themes[0]->animations.Count() == 2 && themes[0]->animations[0]->name == L"long_action");
		TEST_ASSERT(themes[0]->animations[0]->frames.Count() == 5 && themes[0]->animations[1]->frames.Count() == 3);
		TEST_ASSERT(themes[0]->animations[0]->frames[4].GetFullPath() == (artwork / L"second" / L"long_action_5.png").GetFullPath());
		TEST_ASSERT(themes[1]->animations[0]->frames.Count() == 1);
		auto previous = themes[0];
		TEST_ASSERT(File(artwork / L"second" / L"long_action_3.png").Delete());
		ExpectDesktopFailure([&] { LoadDesktopThemes(artwork, themes); });
		TEST_ASSERT(themes.Count() == 2 && themes[0] == previous);
		TEST_ASSERT(Folder(root).Delete(true));
	});

	TEST_CASE(L"Theme selection restores exact catalog keys and preserves position and unrelated settings")
	{
		auto root = CreateDesktopTestFolder();
		auto settings = root / L"custom-location" / L"settings";
		List<Ptr<DesktopTheme>> themes;
		for (auto name : { L"second", L"first" })
		{
			auto theme = Ptr(new DesktopTheme);
			theme->name = name;
			theme->displayName = L"角色" + theme->name;
			themes.Add(theme);
		}
		TEST_ASSERT(LoadSelectedDesktopTheme(settings, themes) == 0);
		SaveSelectedDesktopTheme(settings, L"first");
		TEST_ASSERT(LoadSelectedDesktopTheme(settings, themes) == 1);
		auto initialPosition = LoadDesktopPosition(settings);
		TEST_ASSERT(initialPosition.x == 0 && initialPosition.y == 0);
		auto configPath = settings / L"config.json";
		for (auto fallback : { L"{}", L"{\"selectedTheme\":\"unknown\"}", L"{\"selectedTheme\":\"FIRST\"}",
			L"{\"selectedTheme\":\"角色first\"}", L"{\"selectedTheme\":\"\"}" })
		{
			WriteDesktopFixture(configPath, fallback);
			TEST_ASSERT(LoadSelectedDesktopTheme(settings, themes) == 0);
			TEST_ASSERT(File(configPath).ReadAllTextByBom() == fallback);
		}
		WriteDesktopFixture(configPath, L"{\"windowX\":-1920,\"windowY\":180,\"selectedTheme\":\"second\",\"future\":{\"title\":\"保留配置\"},\"enabled\":true}");
		SaveSelectedDesktopTheme(settings, L"first");
		TEST_ASSERT(LoadSelectedDesktopTheme(settings, themes) == 1);
		auto position = LoadDesktopPosition(settings);
		TEST_ASSERT(position.x == -1920 && position.y == 180);
		SaveDesktopPosition(settings, { -800, 90 });
		TEST_ASSERT(LoadSelectedDesktopTheme(settings, themes) == 1);
		json::Parser parser;
		auto saved = ParseJson(File(configPath).ReadAllTextByBom(), parser);
		TEST_ASSERT(GetString(GetField(saved, L"future"), L"title") == L"保留配置");
		TEST_ASSERT(GetField(saved, L"enabled").Cast<json::JsonLiteral>()->value == json::JsonLiteralValue::True);
		TEST_ASSERT(Folder(root).Delete(true));
	});

	TEST_CASE(L"Theme selection rejects malformed configuration without overwriting it")
	{
		auto root = CreateDesktopTestFolder();
		auto configPath = root / L"config.json";
		List<Ptr<DesktopTheme>> themes;
		ExpectDesktopFailure([&] { LoadSelectedDesktopTheme(root, themes); });
		auto theme = Ptr(new DesktopTheme);
		theme->name = L"first";
		themes.Add(theme);
		for (auto malformed : { L"[]", L"{", L"{\"selectedTheme\":1}", L"{\"selectedTheme\":null}",
			L"{\"selectedTheme\":true}", L"{\"selectedTheme\":{}}", L"{\"selectedTheme\":[]}",
			L"{\"selectedTheme\":\"first\",\"selectedTheme\":\"first\"}", L"{\"windowX\":\"0\"}" })
		{
			WriteDesktopFixture(configPath, malformed);
			ExpectDesktopFailure([&] { LoadSelectedDesktopTheme(root, themes); });
			ExpectDesktopFailure([&] { SaveSelectedDesktopTheme(root, L"first"); });
			ExpectDesktopFailure([&] { SaveDesktopPosition(root, { 20, 30 }); });
			TEST_ASSERT(File(configPath).ReadAllTextByBom() == malformed);
		}
		TEST_ASSERT(File(configPath).Delete() && Folder(configPath).Create(false));
		ExpectDesktopFailure([&] { LoadSelectedDesktopTheme(root, themes); });
		ExpectDesktopFailure([&] { SaveSelectedDesktopTheme(root, L"first"); });
		TEST_ASSERT(Folder(root).Delete(true));
	});

	TEST_CASE(L"Theme metadata rejects path escapes, invalid counts and duplicate Windows paths")
	{
		auto root = CreateDesktopTestFolder();
		List<Ptr<DesktopTheme>> themes;
		ExpectDesktopFailure([&] { LoadDesktopThemes(root, themes); });
		WriteDesktopFixture(root / L"safe" / L"index.json", L"{\"idle\":1}");
		WriteDesktopFixture(root / L"safe" / L"idle_1.png", L"synthetic frame");
		for (auto malformed : { L"[]", L"{}", L"{\"safe\":1}", L"{\"safe\":\"\"}",
			L"{\"safe\":\"角色\",\"safe\":\"重复\"}", L"{\"safe\":\"角色\",\"SAFE\":\"重复\"}" })
		{
			WriteDesktopFixture(root / L"theme.json", malformed);
			ExpectDesktopFailure([&] { LoadDesktopThemes(root, themes); });
		}
		for (auto invalidId : { L"", L".", L"..", L"../outside", L"a/b", L"a\\b", L"C:outside", L"a.png", L"a " })
		{
			auto catalog = Ptr(new json::JsonObject);
			SetString(catalog, invalidId, L"角色");
			WriteDesktopFixture(root / L"theme.json", json::JsonToString(catalog));
			ExpectDesktopFailure([&] { LoadDesktopThemes(root, themes); });
			WriteDesktopFixture(root / L"theme.json", L"{\"safe\":\"角色\"}");
			auto index = Ptr(new json::JsonObject);
			SetInteger(index, invalidId, 1);
			WriteDesktopFixture(root / L"safe" / L"index.json", json::JsonToString(index));
			ExpectDesktopFailure([&] { LoadDesktopThemes(root, themes); });
		}
		for (auto malformed : { L"[]", L"{}", L"{\"idle\":0}", L"{\"idle\":-1}", L"{\"idle\":1.5}",
			L"{\"idle\":\"1\"}", L"{\"idle\":2147483648}", L"{\"idle\":2}",
			L"{\"idle\":1,\"idle\":1}", L"{\"idle\":1,\"IDLE\":1}" })
		{
			WriteDesktopFixture(root / L"safe" / L"index.json", malformed);
			ExpectDesktopFailure([&] { LoadDesktopThemes(root, themes); });
		}
		TEST_ASSERT(themes.Count() == 0);
		WriteDesktopFixture(root / L"safe" / L"index.json", L"{\"idle\":1}");
		LoadDesktopThemes(root, themes);
		TEST_ASSERT(themes.Count() == 1);
		TEST_ASSERT(Folder(root).Delete(true));
	});

	TEST_CASE(L"Playback completes three whole playthroughs before selecting another series")
	{
		auto theme = Ptr(new DesktopTheme);
		for (auto count : { 1, 3, 5 })
		{
			auto animation = Ptr(new ThemeAnimation);
			animation->name = L"series" + itow(count);
			for (vint frame = 1; frame <= count; frame++)
				animation->frames.Add(FilePath(L"C:\\synthetic-artwork") / (animation->name + L"_" + itow(frame) + L".png"));
			theme->animations.Add(animation);
		}
		ThemePlayback playback(theme, 12345);
		ThemePlayback sameSeed(theme, 12345);
		SortedList<WString> visited;
		for (vint series = 0; series < 64; series++)
		{
			auto name = playback.CurrentAnimation();
			if (!visited.Contains(name)) visited.Add(name);
			Ptr<ThemeAnimation> animation;
			for (auto candidate : theme->animations)
				if (candidate->name == name) animation = candidate;
			TEST_ASSERT(animation);
			for (vint playthrough = 0; playthrough < 3; playthrough++)
			{
				for (vint frame = 0; frame < animation->frames.Count(); frame++)
				{
					TEST_ASSERT(playback.CurrentAnimation() == name);
					TEST_ASSERT(playback.CurrentFrame().GetFullPath() == animation->frames[frame].GetFullPath());
					TEST_ASSERT(playback.CurrentFrame().GetFullPath() == sameSeed.CurrentFrame().GetFullPath());
					playback.Advance();
					sameSeed.Advance();
				}
			}
			TEST_ASSERT(playback.CurrentFrame().GetName() == playback.CurrentAnimation() + L"_1.png");
		}
		TEST_ASSERT(visited.Count() == 3);
		ExpectDesktopFailure([] { ThemePlayback invalid(nullptr, 1); });
		auto empty = Ptr(new DesktopTheme);
		ExpectDesktopFailure([&] { ThemePlayback invalid(empty, 1); });
		empty->animations.Add(Ptr(new ThemeAnimation));
		ExpectDesktopFailure([&] { ThemePlayback invalid(empty, 1); });
	});

	TEST_CASE(L"Switching themes resets the frame and three playthroughs while retaining random state")
	{
		List<Ptr<DesktopTheme>> themes;
		for (auto themeName : { L"old", L"new" })
		{
			auto theme = Ptr(new DesktopTheme);
			theme->name = themeName;
			for (vint series = 0; series < 3; series++)
			{
				auto animation = Ptr(new ThemeAnimation);
				animation->name = L"series" + itow(series);
				for (vint frame = 1; frame <= 2; frame++)
					animation->frames.Add(FilePath(L"C:\\synthetic-artwork") / themeName / (animation->name + L"_" + itow(frame) + L".png"));
				theme->animations.Add(animation);
			}
			themes.Add(theme);
		}
		ThemePlayback playback(themes[0], 12345);
		ThemePlayback uninterrupted(themes[1], 12345);
		auto originalFrame = playback.CurrentFrame();
		ExpectDesktopFailure([&] { playback.SetTheme(nullptr); });
		auto invalid = Ptr(new DesktopTheme);
		ExpectDesktopFailure([&] { playback.SetTheme(invalid); });
		invalid->animations.Add(nullptr);
		ExpectDesktopFailure([&] { playback.SetTheme(invalid); });
		invalid->animations.Clear();
		invalid->animations.Add(Ptr(new ThemeAnimation));
		ExpectDesktopFailure([&] { playback.SetTheme(invalid); });
		TEST_ASSERT(playback.CurrentFrame().GetFullPath() == originalFrame.GetFullPath());
		for (vint change = 0; change < 32; change++)
		{
			// Switch after two complete playthroughs and the first frame of the third.
			for (vint frame = 0; frame < 5; frame++) playback.Advance();
			// Completing a series consumes the same next random choice as changing theme.
			for (vint frame = 0; frame < 6; frame++) uninterrupted.Advance();
			playback.SetTheme(themes[1]);
			TEST_ASSERT(playback.CurrentFrame().GetName() == playback.CurrentAnimation() + L"_1.png");
			for (vint frame = 0; frame < 6; frame++)
			{
				TEST_ASSERT(playback.CurrentFrame().GetFullPath() == uninterrupted.CurrentFrame().GetFullPath());
				playback.Advance();
				uninterrupted.Advance();
			}
		}
	});
}
