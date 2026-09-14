#define GAC_HEADER_USE_NAMESPACE
#include "UI/Source/FatFishUI.h"
#include "../../Agents/Desktop.h"
#include <GacUI.Windows.h>
#include <Skins/DarkSkin/DarkSkin.h>

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

class FairyDesktopWindow : public fatfish::ui::FairyWindow
{
private:
	FilePath                                envFolder;
	ThemePlayback                           playback;
	Dictionary<WString, Ptr<INativeImage>>   images;
	GuiToolstripMenu*                       contextMenu = nullptr;
	vuint64_t                               lastFrameTime = 0;

	void OnLeftButtonDown(GuiGraphicsComposition* sender, GuiMouseEventArgs& arguments)
	{
		if (arguments.button != NativeMouseButton::Left) return;
		arguments.handled = true;
		// Hand dragging to Windows so capture and mixed-DPI monitor transitions work normally.
		auto nativeWindow = GetNativeWindow();
		auto hwnd = GetWindowsForm(nativeWindow)->GetWindowHandle();
		nativeWindow->ReleaseCapture();
		// GacUI routes nonclient clicks back to mouseDown in custom-frame mode.
		DefWindowProcW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, GetMessagePos());
		auto bounds = nativeWindow->GetBounds();
		SaveDesktopPosition(envFolder, { bounds.x1.value, bounds.y1.value });
	}

	void OnRightButtonUp(GuiGraphicsComposition* sender, GuiMouseEventArgs& arguments)
	{
		if (arguments.button != NativeMouseButton::Right) return;
		arguments.handled = true;
		contextMenu->ShowPopup(this, Point(arguments.x, arguments.y));
	}

public:
	FairyDesktopWindow(const FilePath& environment, Ptr<DesktopTheme> theme, vuint64_t seed)
		: envFolder(environment)
		, playback(theme, seed)
	{
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
		fairyImage->SetImage(images[playback.CurrentFrame().GetFullPath()], 0);
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

		contextMenu = new GuiToolstripMenu(theme::ThemeName::Menu, this);
		AddControlHostComponent(contextMenu);
		auto exitItem = new GuiToolstripButton(theme::ThemeName::MenuItemButton);
		exitItem->SetText(L"退出");
		exitItem->Clicked.AttachLambda([this](GuiGraphicsComposition*, GuiEventArgs&)
		{
			Close();
		});
		contextMenu->GetToolstripItems().Add(exitItem);
		auto events = GetBoundsComposition()->GetEventReceiver();
		events->mouseDown.AttachMethod(this, &FairyDesktopWindow::OnLeftButtonDown);
		events->mouseUp.AttachMethod(this, &FairyDesktopWindow::OnRightButtonUp);
		AddAnimation(IGuiAnimation::CreateAnimation([this](vuint64_t milliseconds)
		{
			if (milliseconds - lastFrameTime >= 1000)
			{
				// Keep every frame visible for a full second, even if the UI was briefly busy.
				lastFrameTime = milliseconds;
				playback.Advance();
				fairyImage->SetImage(images[playback.CurrentFrame().GetFullPath()], 0);
			}
		}));
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
	FairyDesktopWindow window(envFolder, themes[0], seed);
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
