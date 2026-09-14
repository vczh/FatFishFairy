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

class FairyDesktopWindow : public fatfish::ui::FairyWindow, public INativeControllerListener
{
private:
	FilePath                                envFolder;
	ThemePlayback                           playback;
	Dictionary<WString, Ptr<INativeImage>>   images;
	Nullable<Point>                         dragStart;
	vuint64_t                               lastFrameTime = 0;

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

		auto events = GetBoundsComposition()->GetEventReceiver();
		events->mouseDown.AttachMethod(this, &FairyDesktopWindow::OnLeftButtonDown);
		events->mouseMove.AttachMethod(this, &FairyDesktopWindow::OnMouseMove);
		events->mouseUp.AttachMethod(this, &FairyDesktopWindow::OnMouseUp);
		lastFrameTime = GetTickCount64();
		GetCurrentController()->CallbackService()->InstallListener(this);
	}

	~FairyDesktopWindow()
	{
		GetCurrentController()->CallbackService()->UninstallListener(this);
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
