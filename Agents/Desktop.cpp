#include "Desktop.h"
#include "Json.h"

using namespace vl;
using namespace vl::collections;
using namespace vl::filesystem;
using namespace vl::glr;

namespace fatfish
{
	Ptr<json::JsonObject> ReadDesktopJson(const FilePath& path, json::Parser& parser)
	{
		WString text;
		stream::BomEncoder::Encoding encoding;
		bool containsBom = false;
		if (!File(path).ReadAllTextWithEncodingTesting(text, encoding, containsBom))
			throw Exception(L"Cannot read desktop metadata: " + path.GetFullPath());
		auto object = ParseJson(text, parser).Cast<json::JsonObject>();
		if (!object) throw Exception(L"Desktop metadata must be a JSON object: " + path.GetFullPath());
		SortedList<WString> names;
		for (auto field : object->fields)
		{
			if (names.Contains(field->name.value)) throw Exception(L"Duplicate desktop metadata field: " + field->name.value);
			names.Add(field->name.value);
		}
		return object;
	}

	vint ReadDesktopCoordinate(Ptr<json::JsonObject> object, const WString& name)
	{
		auto value = GetField(object, name);
		if (!value) return 0;
		auto number = value.Cast<json::JsonNumber>();
		bool success = false;
		auto coordinate = number ? wtoi_test(number->content.value, success) : 0;
		if (!success) throw Exception(L"Desktop coordinates must be signed 32-bit integers: " + name);
		return coordinate;
	}

	Ptr<json::JsonObject> ReadDesktopConfig(const FilePath& envFolder, json::Parser& parser)
	{
		auto path = envFolder / L"config.json";
		if (Folder(path).Exists()) throw Exception(L"config.json must be a file.");
		auto object = File(path).Exists() ? ReadDesktopJson(path, parser) : Ptr(new json::JsonObject);
		ReadDesktopCoordinate(object, L"windowX");
		ReadDesktopCoordinate(object, L"windowY");
		return object;
	}

	DesktopPosition LoadDesktopPosition(const FilePath& envFolder)
	{
		json::Parser parser;
		auto object = ReadDesktopConfig(envFolder, parser);
		return { ReadDesktopCoordinate(object, L"windowX"), ReadDesktopCoordinate(object, L"windowY") };
	}

	void SaveDesktopPosition(const FilePath& envFolder, DesktopPosition position)
	{
		if (position.x < -2147483647LL - 1 || position.x > 2147483647LL
			|| position.y < -2147483647LL - 1 || position.y > 2147483647LL)
			throw Exception(L"Desktop coordinates must be signed 32-bit integers.");
		json::Parser parser;
		// Re-read when dragging ends so unrelated settings changed since startup survive.
		auto object = ReadDesktopConfig(envFolder, parser);
		SetInteger(object, L"windowX", position.x);
		SetInteger(object, L"windowY", position.y);
		if (!Folder(envFolder).Exists() && !Folder(envFolder).Create(true))
			throw Exception(L"Cannot create the supplied environment folder.");
		if (!File(envFolder / L"config.json").WriteAllText(json::JsonToString(object), true, stream::BomEncoder::Utf8))
			throw Exception(L"Cannot save config.json.");
	}

	void ValidateDesktopId(const WString& name, SortedList<WString>& names)
	{
		if (name.Length() == 0) throw Exception(L"Theme and animation IDs cannot be empty.");
		for (vint i = 0; i < name.Length(); i++)
		{
			auto c = name[i];
			if (!((L'a' <= c && c <= L'z') || (L'A' <= c && c <= L'Z')
				|| (L'0' <= c && c <= L'9') || c == L'_' || c == L'-'))
				throw Exception(L"Theme and animation IDs must be safe single path segments.");
		}
		auto normalized = wlower(name);
		if (names.Contains(normalized)) throw Exception(L"Theme and animation IDs must be unique ignoring case.");
		names.Add(normalized);
	}

	void LoadDesktopThemes(const FilePath& themesFolder, List<Ptr<DesktopTheme>>& themes)
	{
		json::Parser parser;
		auto catalog = ReadDesktopJson(themesFolder / L"theme.json", parser);
		if (catalog->fields.Count() == 0) throw Exception(L"The theme catalog cannot be empty.");
		List<Ptr<DesktopTheme>> loaded;
		SortedList<WString> themeNames;
		for (auto field : catalog->fields)
		{
			ValidateDesktopId(field->name.value, themeNames);
			auto displayName = field->value.Cast<json::JsonString>();
			if (!displayName || displayName->content.value.Length() == 0)
				throw Exception(L"Every theme requires a nonempty display name.");
			auto theme = Ptr(new DesktopTheme);
			theme->name = field->name.value;
			theme->displayName = displayName->content.value;
			auto themeFolder = themesFolder / theme->name;
			auto index = ReadDesktopJson(themeFolder / L"index.json", parser);
			if (index->fields.Count() == 0) throw Exception(L"A theme must contain at least one animation.");
			SortedList<WString> animationNames;
			for (auto animationField : index->fields)
			{
				ValidateDesktopId(animationField->name.value, animationNames);
				auto countNode = animationField->value.Cast<json::JsonNumber>();
				bool success = false;
				auto count = countNode ? wtoi_test(countNode->content.value, success) : 0;
				if (!success || count <= 0) throw Exception(L"Animation frame counts must be positive integers.");
				auto animation = Ptr(new ThemeAnimation);
				animation->name = animationField->name.value;
				for (vint frame = 0; frame < count; frame++)
				{
					auto path = themeFolder / (animation->name + L"_" + itow(frame + 1) + L".png");
					if (!File(path).Exists()) throw Exception(L"Missing animation frame: " + path.GetFullPath());
					animation->frames.Add(path);
				}
				theme->animations.Add(animation);
			}
			loaded.Add(theme);
		}
		themes = std::move(loaded);
	}

	ThemePlayback::ThemePlayback(Ptr<DesktopTheme> desktopTheme, vuint64_t seed)
		: theme(desktopTheme)
		, random(seed)
	{
		if (!theme || theme->animations.Count() == 0) throw Exception(L"Playback requires a theme with animations.");
		for (auto animation : theme->animations)
			if (!animation || animation->frames.Count() == 0) throw Exception(L"Playback requires nonempty animations.");
		SelectAnimation();
	}

	void ThemePlayback::SelectAnimation()
	{
		// Vlpp provides no random-number generator. Keep independent seeded state per player.
		std::uniform_int_distribution<vint> select(0, theme->animations.Count() - 1);
		animationIndex = select(random);
		frameIndex = 0;
		completedPlaythroughs = 0;
	}

	const FilePath& ThemePlayback::CurrentFrame() const
	{
		return theme->animations[animationIndex]->frames[frameIndex];
	}

	const WString& ThemePlayback::CurrentAnimation() const
	{
		return theme->animations[animationIndex]->name;
	}

	void ThemePlayback::Advance()
	{
		if (++frameIndex < theme->animations[animationIndex]->frames.Count()) return;
		frameIndex = 0;
		if (++completedPlaythroughs == 3) SelectAnimation();
	}
}
