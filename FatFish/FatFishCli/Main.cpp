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
	{
		auto selfTest = false;
		auto once = false;
		WString repository;
		for (vint i = 1; i < argc; i++)
		{
			auto argument = WString(argv[i]);
			if (argument == L"--self-test")
			{
				selfTest = true;
			}
			else if (argument == L"--once")
			{
				once = true;
			}
			else if (argument == L"--repo-root" && i + 1 < argc)
			{
				repository = WString(argv[++i]);
			}
			else if (argument == L"--help")
			{
				Console::WriteLine(L"FatFishCli [--once | --self-test] [--repo-root PATH]");
				Console::WriteLine(L"ENTER: observe all monitors and run the fairy. ESC: exit.");
				return 0;
			}
			else
			{
				Console::WriteLine(L"Unknown or incomplete option: " + argument);
				return 2;
			}
		}
		if (selfTest && once)
		{
			Console::WriteLine(L"Choose either --once or --self-test.");
			return 2;
		}
		if (selfTest)
		{
			RunSelfTests();
			Console::WriteLine(L"All offline tests passed.");
		}
		else
		{
			auto root = repository.Length() == 0 ? FairyApplication::FindRepositoryRoot() : FilePath(repository);
			FairyApplication application(root);
			vl::glr::json::Parser outputParser;
			application.ResponseReceived.Add(Func<void(bool, const WString&)>([&](bool vision, const WString& message)
			{
				Console::WriteLine(FormatAgentResponse(vision, message, outputParser));
			}));
			if (once)
			{
				application.RunRound();
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
						application.RunRound();
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
	return 0;
}
