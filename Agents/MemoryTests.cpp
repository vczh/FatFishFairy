#include "Memory.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>

namespace fatfish
{
	using namespace vl;
	using namespace vl::collections;
	using namespace vl::filesystem;

	void RequireMemoryTest(bool condition, const WString& message)
	{
		if (!condition) throw Exception(L"Memory test failed: " + message);
	}

	void ExpectMemoryFailure(const Func<void()>& action)
	{
		bool rejected = false;
		try
		{
			action();
		}
		catch (const Exception&)
		{
			rejected = true;
		}
		RequireMemoryTest(rejected, L"An unsafe or invalid operation was accepted.");
	}

	void CreateMemoryTestJunction(const FilePath& junction, const FilePath& target)
	{
		RequireMemoryTest(Folder(junction).Create(false), L"Create junction directory.");
		auto handle = CreateFileW(junction.GetFullPath().Buffer(), GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		RequireMemoryTest(handle != INVALID_HANDLE_VALUE, L"Open junction directory.");
		struct JunctionData
		{
			DWORD tag;
			WORD dataLength;
			WORD reserved;
			WORD substituteOffset;
			WORD substituteLength;
			WORD printOffset;
			WORD printLength;
			wchar_t names[4096];
		} data = {};
		auto substitute = L"\\??\\" + target.GetFullPath();
		auto display = target.GetFullPath();
		auto count = substitute.Length() + display.Length() + 2;
		RequireMemoryTest(count <= 4096, L"Junction test path length.");
		data.tag = IO_REPARSE_TAG_MOUNT_POINT;
		data.substituteLength = static_cast<WORD>(substitute.Length() * sizeof(wchar_t));
		data.printOffset = static_cast<WORD>((substitute.Length() + 1) * sizeof(wchar_t));
		data.printLength = static_cast<WORD>(display.Length() * sizeof(wchar_t));
		data.dataLength = static_cast<WORD>(8 + count * sizeof(wchar_t));
		memcpy(data.names, substitute.Buffer(), data.substituteLength);
		memcpy(data.names + substitute.Length() + 1, display.Buffer(), data.printLength);
		DWORD returned;
		auto created = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data, data.dataLength + 8, nullptr, 0, &returned, nullptr);
		CloseHandle(handle);
		RequireMemoryTest(created != FALSE, L"Create junction reparse point.");
	}

	void RunMemoryTests()
	{
		wchar_t tempFolder[MAX_PATH + 1];
		wchar_t tempName[MAX_PATH + 1];
		auto tempLength = GetTempPathW(MAX_PATH, tempFolder);
		RequireMemoryTest(tempLength > 0 && tempLength < MAX_PATH, L"Find temporary directory.");
		RequireMemoryTest(GetTempFileNameW(tempFolder, L"fff", 0, tempName) != 0, L"Reserve unique temporary name.");
		auto sandbox = FilePath(tempName);
		auto prefix = FilePath(tempFolder).GetFullPath() + WString::FromChar(FilePath::GetPathDelimiter());
		RequireMemoryTest(sandbox.GetFullPath().Length() > prefix.Length()
			&& Locale::Invariant().CompareOrdinalIgnoreCase(sandbox.GetFullPath().Left(prefix.Length()), prefix) == 0,
			L"Temporary test directory must remain under the system temporary directory.");
		RequireMemoryTest(File(sandbox).Delete() && Folder(sandbox).Create(false), L"Create temporary test directory.");
		auto memoryRoot = sandbox / L"memory";
		auto outsideRoot = sandbox / L"outside";
		RequireMemoryTest(Folder(outsideRoot).Create(false), L"Create outside sentinel directory.");
		auto outsideFile = outsideRoot / L"sentinel.txt";
		RequireMemoryTest(File(outsideFile).WriteAllText(L"outside sentinel", true, stream::BomEncoder::Utf8), L"Write outside sentinel.");

		{
			MemoryStore store(memoryRoot);
			RequireMemoryTest(store.Read(L"INDEX.MD") == L"# 记忆索引\n", L"Create and preserve the index.");
			RequireMemoryTest(store.NormalizePath(L"People\\用户.md") == L"people/用户.md", L"Normalize separators and case.");
			store.Write(L"People\\用户.md", L"喜欢 C++\r\n中文记忆\n");
			RequireMemoryTest(store.Read(L"PEOPLE/用户.MD") == L"喜欢 C++\n中文记忆\n", L"Unicode content and line ending normalization.");
			RequireMemoryTest(File(memoryRoot / L"people/用户.md").ReadAllTextByBom() == store.Read(L"people/用户.md"), L"Disk and memory content agree.");
			store.Write(L"empty.txt", L"");
			RequireMemoryTest(store.Read(L"empty.txt") == L"", L"Empty file round trip.");
			store.Write(L"lines.txt", L"\n\nlast");
			RequireMemoryTest(store.Read(L"lines.txt") == L"\n\nlast", L"Leading empty lines and missing trailing newline.");
			store.Write(L"carriage.txt", L"one\rtwo\r\n\r");
			RequireMemoryTest(store.Read(L"carriage.txt") == L"one\ntwo\n\n", L"Normalize CR, CRLF and trailing empty lines.");

			List<MemoryMatch> matches;
			store.Search(L"中文", matches);
			RequireMemoryTest(matches.Count() == 1 && matches[0].path == L"people/用户.md" && matches[0].line == 2 && matches[0].text == L"中文记忆", L"Search returns file, line and text.");
			store.Search(L"PEOPLE", matches);
			RequireMemoryTest(matches.Count() == 1 && matches[0].line == 0 && matches[0].text == L"", L"Search matches paths without case sensitivity.");
			store.Search(L"LAST", matches);
			RequireMemoryTest(matches.Count() == 1 && matches[0].line == 3, L"Search matches lines without case sensitivity.");
			store.Search(L"", matches, 1);
			RequireMemoryTest(matches.Count() == 1, L"Search respects its result limit.");
			ExpectMemoryFailure([&] { store.Search(L"", matches, 0); });
			ExpectMemoryFailure([&] { store.Search(L"", matches, 1001); });

			// Changing disk state must not change cached reads or searches until the next load.
			RequireMemoryTest(File(memoryRoot / L"people/用户.md").WriteAllText(L"external change", true, stream::BomEncoder::Utf8), L"Change cached file externally.");
			RequireMemoryTest(store.Read(L"people/用户.md") == L"喜欢 C++\n中文记忆\n", L"Read uses the dictionary.");
			store.Search(L"中文", matches);
			RequireMemoryTest(matches.Count() == 1, L"Search uses the dictionary.");
			MemoryStore reloaded(memoryRoot);
			RequireMemoryTest(reloaded.Read(L"people/用户.md") == L"external change", L"Restart loads persisted files.");

			const wchar_t* invalidPaths[] =
			{
				L"", L".", L"..", L"../outside/sentinel.txt", L"..\\outside\\sentinel.txt",
				L"a/../b", L"a/./b", L"a//b", L"a/", L"/outside", L"\\outside",
				L"C:\\outside", L"C:outside", L"\\\\server\\share\\file", L"\\\\?\\C:\\outside",
				L"Index.md:stream", L"a/NUL.txt", L"con", L"CON .txt", L"COM1.md", L"lpt².txt",
				L"conin$", L"file.", L"file ", L"a/.. /b", L"a?b", L"a*b", L"a|b", L"a\nb",
			};
			for (auto path : invalidPaths)
			{
				ExpectMemoryFailure([&] { store.Read(path); });
				ExpectMemoryFailure([&] { store.Write(path, L"forbidden"); });
				ExpectMemoryFailure([&] { store.Delete(path); });
			}
			const wchar_t embeddedZero[] = { L'a', 0, L'b' };
			ExpectMemoryFailure([&] { store.Write(WString::CopyFrom(embeddedZero, 3), L"forbidden"); });
			Array<wchar_t> longPath(300);
			for (vint i = 0; i < longPath.Count(); i++) longPath[i] = L'a';
			ExpectMemoryFailure([&] { store.Write(WString::CopyFrom(&longPath[0], longPath.Count()), L"forbidden"); });
			ExpectMemoryFailure([&] { store.Delete(L"INDEX.md"); });
			ExpectMemoryFailure([&] { store.Read(L"missing.md"); });
			ExpectMemoryFailure([&] { store.Write(L"people", L"file over directory"); });
			ExpectMemoryFailure([&] { store.Write(L"lines.txt/child", L"directory over file"); });

			store.Write(L"prune/inner/first.txt", L"first");
			store.Write(L"prune/inner/second.txt", L"second");
			store.Delete(L"prune/inner/first.txt");
			RequireMemoryTest(Folder(memoryRoot / L"prune/inner").Exists(), L"Keep nonempty parent directory.");
			store.Delete(L"prune/inner/second.txt");
			RequireMemoryTest(!Folder(memoryRoot / L"prune").Exists() && Folder(memoryRoot).Exists(), L"Prune empty ancestors without deleting memory root.");

			auto junction = memoryRoot / L"escape";
			store.Write(L"escape/sentinel.txt", L"cached before junction");
			RequireMemoryTest(File(junction / L"sentinel.txt").Delete() && Folder(junction).Delete(false), L"Replace cached directory with junction.");
			CreateMemoryTestJunction(junction, outsideRoot);
			ExpectMemoryFailure([&] { store.Write(L"escape/sentinel.txt", L"forbidden"); });
			ExpectMemoryFailure([&] { store.Write(L"escape/new/deep.txt", L"forbidden"); });
			ExpectMemoryFailure([&] { store.Delete(L"escape/sentinel.txt"); });
			RequireMemoryTest(store.Read(L"escape/sentinel.txt") == L"cached before junction", L"Read never follows a replacement junction.");
			ExpectMemoryFailure([&] { MemoryStore unsafe(memoryRoot); });
			RequireMemoryTest(RemoveDirectoryW(junction.GetFullPath().Buffer()) != FALSE, L"Remove junction itself without traversing it.");

			store.Write(L"linked.txt", L"old cache");
			RequireMemoryTest(File(memoryRoot / L"linked.txt").Delete(), L"Prepare hard link replacement.");
			RequireMemoryTest(CreateHardLinkW((memoryRoot / L"linked.txt").GetFullPath().Buffer(), outsideFile.GetFullPath().Buffer(), nullptr) != FALSE, L"Create outside hard link.");
			ExpectMemoryFailure([&] { store.Write(L"linked.txt", L"forbidden"); });
			ExpectMemoryFailure([&] { store.Delete(L"linked.txt"); });
			ExpectMemoryFailure([&] { MemoryStore unsafe(memoryRoot); });
			RequireMemoryTest(File(memoryRoot / L"linked.txt").Delete(), L"Remove hard link itself.");
			RequireMemoryTest(File(outsideFile).ReadAllTextByBom() == L"outside sentinel" && !Folder(outsideRoot / L"new").Exists(), L"Unsafe operations never change outside files.");
			List<WString> paths;
			store.ListFiles(paths);
			RequireMemoryTest(paths.Contains(WString(L"index.md")) && !paths.Contains(WString(L"prune/inner/second.txt")), L"List reflects writes and deletions.");
		}
		// The unique target was checked above; every link created by these tests has been removed.
		RequireMemoryTest(Folder(sandbox).Delete(true), L"Remove temporary test directory.");
	}
}
