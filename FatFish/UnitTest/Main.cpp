#include <Vlpp.h>

using namespace vl;

int wmain(int argc, wchar_t* argv[])
{
	auto result = unittest::UnitTest::RunAndDisposeTests(argc, argv);
	FinalizeGlobalStorage();
#ifdef VCZH_CHECK_MEMORY_LEAKS
	unittest::UnitTest::DumpMemoryLeak(argc, argv);
	if (_CrtDumpMemoryLeaks()) return 1;
#endif
	return result;
}
