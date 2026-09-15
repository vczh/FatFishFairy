#include "../../Agents/Runtime.h"
#include "../../Agents/Output.h"
#include <VlppOS.Windows.h>
#include <conio.h>

using namespace vl;
using namespace vl::console;
using namespace vl::filesystem;
using namespace fatfish;

int wmain(int argc, wchar_t* argv[])
{
	SetConsoleOutputCP(CP_UTF8);
	Console::Enable();
	Console::SetTitle(L"FatFishCli");
	auto exitCode = 0;
	{
		auto once = false;
		WString repository;
		for (vint i = 1; i < argc; i++)
		{
			auto argument = WString(argv[i]);
			if (argument == L"--once")
			{
				once = true;
			}
			else if (argument == L"--repo-root" && i + 1 < argc)
			{
				repository = WString(argv[++i]);
			}
			else if (argument == L"--help")
			{
				Console::WriteLine(L"FatFishCli [--once] [--repo-root PATH]");
				Console::WriteLine(L"ENTER: observe all monitors and run the fairy. ESC: exit.");
				return 0;
			}
			else
			{
				Console::WriteLine(L"Unknown or incomplete option: " + argument);
				return 2;
			}
		}
		FilePath root;
		if (repository.Length() == 0)
		{
			collections::Array<wchar_t> executable(32768);
			auto length = GetModuleFileNameW(nullptr, &executable[0], static_cast<DWORD>(executable.Count()));
			if (length == 0 || length >= static_cast<DWORD>(executable.Count())) throw Exception(L"Cannot locate executable.");
			auto executableFolder = FilePath(WString::CopyFrom(&executable[0], length)).GetFolder();
			// Match the output folders in FatFish/Common.props for Debug and Release.
#ifdef _WIN64
			root = executableFolder / L"../../.."; // FatFish/x64/<Configuration>
#else
			root = executableFolder / L"../.."; // FatFish/<Configuration>
#endif
		}
		else
		{
			root = FilePath(repository);
		}
		auto envFolder = root / L"env";
		auto memoryFolder = root / L"memory";
		auto characterFile = root / L"themes" / L"loli_maid" / L"Character.md";
		FairyApplication application(envFolder, memoryFolder, [characterFile]()
		{
			return LoadCharacterPrompt(characterFile, characterFile);
		});
		vl::glr::json::Parser outputParser;
		application.ResponseReceived.Add(Func<void(bool, const WString&)>([&](bool vision, const WString& message)
		{
			Console::WriteLine(FormatAgentResponse(vision, message, outputParser));
		}));
		if (once)
		{
			try
			{
				application.RunRound();
			}
			catch (const FairyContextRecoveryExhausted& error)
			{
				Console::WriteLine(L"Fairy round cancelled: " + error.Message());
				exitCode = 1;
			}
		}
		else
		{
			Console::WriteLine(L"FatFishCli: ENTER to observe and respond; ESC to exit.");
			while (true)
			{
				auto key = _getwch();
				if (key == 27) break;
				if (key == 0 || key == 0xE0)
				{
					_getwch();
				}
				else if (key == L'\r')
				{
					Console::WriteLine(L"Observing...");
					try
					{
						application.RunRound();
					}
					catch (const FairyContextRecoveryExhausted& error)
					{
						Console::WriteLine(L"Fairy round cancelled: " + error.Message());
						Console::WriteLine(L"Press ENTER for a new observation, or ESC to exit.");
					}
				}
			}
		}
	}
	FinalizeGlobalStorage();
#ifdef VCZH_CHECK_MEMORY_LEAKS
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
	if (_CrtDumpMemoryLeaks()) return 1;
#endif
	return exitCode;
}
