#define GAC_HEADER_USE_NAMESPACE
#include "UI/Source/FatFishUI.h"
#include "../../Agents/Desktop.h"
#include <GacUI.Windows.h>
#include <Skins/DarkSkin/DarkSkin.h>
#include <CommCtrl.h>

// Use the current system controls and TOOLINFO layout for the speech bubble.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace vl::collections;
using namespace vl::filesystem;
using namespace vl::presentation::windows;
using namespace fatfish;

class FairySkinPlugin : public Object, public IGuiPlugin
{
public:

	GUI_PLUGIN_NAME(FatFish_DefaultSkin)
	{
		GUI_PLUGIN_DEPEND(GacGen_DarkSkinResourceLoader);
	}

	void Load(bool controllerUnrelatedPlugins, bool controllerRelatedPlugins) override
	{
		RegisterTheme(Ptr(new darkskin::Theme));
	}

	void Unload(bool controllerUnrelatedPlugins, bool controllerRelatedPlugins) override
	{
	}
};
GUI_REGISTER_PLUGIN(FairySkinPlugin)

class FairyDesktopWindow : public fatfish::ui::FairyWindow, public INativeControllerListener
{
private:
	FilePath                                envFolder;
	const List<Ptr<DesktopTheme>>&           themes;
	vint                                    selectedTheme;
	ThemePlayback                           playback;
	Dictionary<WString, Ptr<INativeImage>>   images;
	List<GuiToolstripButton*>                themeItems;
	Nullable<Point>                         dragStart;
	vuint64_t                               lastFrameTime = 0;
	HWND                                    speechBubble = nullptr;
	TOOLINFOW                               speechTool = {};
	Nullable<NativePoint>                   speechLayoutTarget;
	vint                                    speechStemX = 0;

	void LoadThemeImages(Ptr<DesktopTheme> theme)
	{
		images.Clear();
		for (auto animation : theme->animations)
		{
			for (auto&& path : animation->frames)
			{
				auto image = GetCurrentController()->ImageService()->CreateImageFromFile(path.GetFullPath());
				if (!image || image->GetFrameCount() != 1 || image->GetFrame(0)->GetSize() != Size(384, 384))
				{
					throw Exception(L"Theme frame must be a 384x384 image: " + path.GetFullPath());
				}
				images.Add(path.GetFullPath(), image);
			}
		}
	}

	void SelectTheme(vint index)
	{
		if (index == selectedTheme) return;
		LoadThemeImages(themes[index]);
		SaveSelectedDesktopTheme(envFolder, themes[index]->name);
		playback.SetTheme(themes[index]);
		selectedTheme = index;
		fairyImage->SetImage(images[playback.CurrentFrame().GetFullPath()], 0);
		lastFrameTime = GetTickCount64();
		for (vint i = 0; i < themeItems.Count(); i++)
		{
			themeItems[i]->SetSelected(i == selectedTheme);
			themeItems[i]->SetShortcutText(i == selectedTheme ? L"✓" : L"");
		}
	}

	void UpdateSpeechPosition()
	{
		if (!speechBubble) return;
		auto bounds = GetNativeWindow()->GetBounds();
		auto screen = GetRelatedScreen();
		if (!screen) screen = GetCurrentController()->ScreenService()->GetScreen(vint(0));
		auto workArea = screen->GetClientBounds();
		// Windows has no flag for stem direction. At the monitor's bottom edge it
		// lays out the native balloon above its target, with the stem pointing down.
		// Move that complete shape above the fairy without changing its layout.
		NativePoint layoutTarget(workArea.x1.value + workArea.Width().value / 2, screen->GetBounds().y2.value - 1);
		if (!speechLayoutTarget || speechLayoutTarget.Value() != layoutTarget)
		{
			SendMessageW(speechBubble, TTM_TRACKPOSITION, 0, MAKELPARAM(layoutTarget.x.value, layoutTarget.y.value));
			RECT layout;
			if (!GetWindowRect(speechBubble, &layout)) throw Exception(L"Cannot measure system speech bubble stem.");
			speechStemX = layoutTarget.x.value - layout.left;
			speechLayoutTarget = layoutTarget;
		}
		RECT bubble;
		if (!GetWindowRect(speechBubble, &bubble)) throw Exception(L"Cannot measure system speech bubble.");
		auto width = bubble.right - bubble.left;
		auto height = bubble.bottom - bubble.top;
		auto x = bounds.x1.value + bounds.Width().value / 2 - speechStemX;
		auto y = bounds.y1.value - height;
		if (x + width > workArea.x2.value) x = workArea.x2.value - width;
		if (x < workArea.x1.value) x = workArea.x1.value;
		if (y + height > workArea.y2.value) y = workArea.y2.value - height;
		if (y < workArea.y1.value) y = workArea.y1.value;
		if (!SetWindowPos(speechBubble, nullptr, static_cast<int>(x), static_cast<int>(y), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
		{
			throw Exception(L"Cannot position system speech bubble.");
		}
	}

	void OnWindowOpened(GuiGraphicsComposition* sender, GuiEventArgs& arguments)
	{
		if (speechBubble) return;
		INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_BAR_CLASSES };
		if (!InitCommonControlsEx(&controls)) throw Exception(L"Cannot initialize system speech bubbles.");
		auto hwnd = GetWindowsForm(GetNativeWindow())->GetWindowHandle();
		speechBubble = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, TOOLTIPS_CLASSW, nullptr,
			WS_POPUP | TTS_BALLOON | TTS_ALWAYSTIP | TTS_NOPREFIX,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (!speechBubble) throw Exception(L"Cannot create system speech bubble.");
		speechTool.cbSize = sizeof(speechTool);
		speechTool.uFlags = TTF_IDISHWND | TTF_TRACK;
		speechTool.hwnd = hwnd;
		speechTool.uId = reinterpret_cast<UINT_PTR>(hwnd);
		static wchar_t greeting[] = L"Hello, world!";
		speechTool.lpszText = greeting;
		if (!SendMessageW(speechBubble, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&speechTool)))
		{
			throw Exception(L"Cannot set system speech bubble text.");
		}
		SendMessageW(speechBubble, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&speechTool));
		UpdateSpeechPosition();
	}

	void OnLeftButtonDown(GuiGraphicsComposition* sender, GuiMouseEventArgs& arguments)
	{
		if (arguments.button != NativeMouseButton::Left) return;
		arguments.handled = true;
		dragStart = Point(arguments.x, arguments.y);
	}

	void OnMouseMove(GuiGraphicsComposition* sender, GuiMouseEventArgs& arguments)
	{
		if (!arguments.left)
		{
			dragStart.Reset();
			return;
		}
		if (!dragStart) return;
		arguments.handled = true;
		auto nativeWindow = GetNativeWindow();
		auto bounds = nativeWindow->GetBounds();
		// Moving the window resets the relative cursor position, so keep the original anchor.
		bounds.Move(nativeWindow->Convert(Size(arguments.x - dragStart.Value().x, arguments.y - dragStart.Value().y)));
		nativeWindow->SetBounds(bounds);
	}

	void OnMouseUp(GuiGraphicsComposition* sender, GuiMouseEventArgs& arguments)
	{
		if (arguments.button == NativeMouseButton::Left && dragStart)
		{
			arguments.handled = true;
			dragStart.Reset();
			auto bounds = GetNativeWindow()->GetBounds();
			SaveDesktopPosition(envFolder, { bounds.x1.value, bounds.y1.value });
		}
		else if (arguments.button == NativeMouseButton::Right)
		{
			arguments.handled = true;
			contextMenu->ShowPopup(this, Point(arguments.x, arguments.y));
		}
	}

protected:
	void Moved() override
	{
		fatfish::ui::FairyWindow::Moved();
		UpdateSpeechPosition();
	}

public:
	void GlobalTimer() override
	{
		auto milliseconds = GetTickCount64();
		if (milliseconds - lastFrameTime >= 1000)
		{
			// Keep every frame visible for a full second, even if the UI was briefly busy.
			lastFrameTime = milliseconds;
			playback.Advance();
			fairyImage->SetImage(images[playback.CurrentFrame().GetFullPath()], 0);
		}
	}

	// GuiMain owns the catalog for the entire lifetime of this window.
	FairyDesktopWindow(const FilePath& environment, const List<Ptr<DesktopTheme>>& desktopThemes, vuint64_t seed)
		: envFolder(environment)
		, themes(desktopThemes)
		, selectedTheme(LoadSelectedDesktopTheme(envFolder, themes))
		, playback(themes[selectedTheme], seed)
	{
		LoadThemeImages(themes[selectedTheme]);
		fairyImage->SetImage(images[playback.CurrentFrame().GetFullPath()], 0);
		auto menu = themeMenuItem->EnsureToolstripSubMenu();
		for (vint i = 0; i < themes.Count(); i++)
		{
			auto item = new GuiToolstripButton(theme::ThemeName::MenuItemButton);
			item->SetText(themes[i]->displayName);
			item->SetAutoSelection(false);
			item->SetSelected(i == selectedTheme);
			// DarkSkin has no selected menu glyph; use its trailing text slot.
			item->SetShortcutText(i == selectedTheme ? L"✓" : L"");
			item->Clicked.AttachLambda([this, i](GuiGraphicsComposition* sender, GuiEventArgs& arguments)
			{
				SelectTheme(i);
			});
			menu->GetToolstripItems().Add(item);
			themeItems.Add(item);
		}
		SetTopMost(true);
		SetShowInTaskBar(false);
		ForceCalculateSizeImmediately();
		auto position = LoadDesktopPosition(envFolder);
		SetBounds(NativePoint(static_cast<int>(position.x), static_cast<int>(position.y)), Size(384, 384));

		auto hwnd = GetWindowsForm(GetNativeWindow())->GetWindowHandle();
		SetLastError(ERROR_SUCCESS);
		auto previousStyle = SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
		if ((previousStyle == 0 && GetLastError() != ERROR_SUCCESS) || !SetLayeredWindowAttributes(hwnd, RGB(0, 255, 0), 255, LWA_COLORKEY))
		{
			throw Exception(L"Cannot enable desktop fairy transparency.");
		}

		auto events = GetBoundsComposition()->GetEventReceiver();
		events->mouseDown.AttachMethod(this, &FairyDesktopWindow::OnLeftButtonDown);
		events->mouseMove.AttachMethod(this, &FairyDesktopWindow::OnMouseMove);
		events->mouseUp.AttachMethod(this, &FairyDesktopWindow::OnMouseUp);
		WindowOpened.AttachMethod(this, &FairyDesktopWindow::OnWindowOpened);
		lastFrameTime = GetTickCount64();
		GetCurrentController()->CallbackService()->InstallListener(this);
	}

	~FairyDesktopWindow()
	{
		GetCurrentController()->CallbackService()->UninstallListener(this);
		if (speechBubble) DestroyWindow(speechBubble);
	}
};

void GuiMain()
{
	Array<wchar_t> executable(32768);
	auto length = GetModuleFileNameW(nullptr, &executable[0], static_cast<DWORD>(executable.Count()));
	if (length == 0 || length >= static_cast<DWORD>(executable.Count())) throw Exception(L"Cannot locate executable.");
	auto executableFolder = FilePath(WString::CopyFrom(&executable[0], length)).GetFolder();
	// Match FatFishCli and Common.props in both Debug and Release.
#ifdef _WIN64
	auto root = executableFolder / L"../../.."; // FatFish/x64/<Configuration>
#else
	auto root = executableFolder / L"../.."; // FatFish/<Configuration>
#endif
	auto envFolder = root / L"env";
	auto themesFolder = root / L"themes";
	List<Ptr<DesktopTheme>> themes;
	LoadDesktopThemes(themesFolder, themes);
	std::random_device entropy;
	auto seed = (static_cast<vuint64_t>(entropy()) << 32) | entropy();
	FairyDesktopWindow window(envFolder, themes, seed);
	GetApplication()->Run(&window);
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	auto result = SetupWindowsDirect2DRenderer();
#ifdef VCZH_CHECK_MEMORY_LEAKS
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
	if (_CrtDumpMemoryLeaks()) return 1;
#endif
	return result;
}
