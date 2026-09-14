#include "../../Agents/Memory.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>

using namespace fatfish;
using namespace vl;
using namespace vl::collections;
using namespace vl::filesystem;

namespace
{
	void ExpectMemoryFailure(const Func<void()>& action)
	{
		TEST_EXCEPTION(action(), Exception, [](const Exception&) {});
	}

	void CreateMemoryTestJunction(const FilePath& junction, const FilePath& target)
	{
		TEST_ASSERT(Folder(junction).Create(false)); // Create junction directory.
		auto handle = CreateFileW(junction.GetFullPath().Buffer(), GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		TEST_ASSERT(handle != INVALID_HANDLE_VALUE); // Open junction directory.
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
		TEST_ASSERT(count <= 4096); // Junction test path length.
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
		TEST_ASSERT(created != FALSE); // Create junction reparse point.
	}
}

TEST_FILE
{
	TEST_CASE(L"Memory persists and searches content while enforcing filesystem boundaries")
	{
		wchar_t tempFolder[MAX_PATH + 1];
		wchar_t tempName[MAX_PATH + 1];
		auto tempLength = GetTempPathW(MAX_PATH, tempFolder);
		TEST_ASSERT(tempLength > 0 && tempLength < MAX_PATH); // Find temporary directory.
		TEST_ASSERT(GetTempFileNameW(tempFolder, L"fff", 0, tempName) != 0); // Reserve unique temporary name.
		auto sandbox = FilePath(tempName);
		auto prefix = FilePath(tempFolder).GetFullPath() + WString::FromChar(FilePath::GetPathDelimiter());
		TEST_ASSERT(sandbox.GetFullPath().Length() > prefix.Length()
			&& Locale::Invariant().CompareOrdinalIgnoreCase(sandbox.GetFullPath().Left(prefix.Length()), prefix) == 0); // Temporary test directory must remain under the system temporary directory.
		TEST_ASSERT(File(sandbox).Delete() && Folder(sandbox).Create(false)); // Create temporary test directory.
		auto memoryRoot = sandbox / L"memory";
		auto outsideRoot = sandbox / L"outside";
		TEST_ASSERT(Folder(outsideRoot).Create(false)); // Create outside sentinel directory.
		auto outsideFile = outsideRoot / L"sentinel.txt";
		TEST_ASSERT(File(outsideFile).WriteAllText(L"outside sentinel", true, stream::BomEncoder::Utf8)); // Write outside sentinel.

		{
			MemoryStore store(memoryRoot);
			TEST_ASSERT(store.Read(L"INDEX.MD") == L"# 记忆索引\n"); // Create and preserve the index.
			TEST_ASSERT(store.NormalizePath(L"People\\用户.md") == L"people/用户.md"); // Normalize separators and case.
			store.Write(L"People\\用户.md", L"喜欢 C++\r\n中文记忆\n");
			TEST_ASSERT(store.Read(L"PEOPLE/用户.MD") == L"喜欢 C++\n中文记忆\n"); // Unicode content and line ending normalization.
			TEST_ASSERT(File(memoryRoot / L"people/用户.md").ReadAllTextByBom() == store.Read(L"people/用户.md")); // Disk and memory content agree.
			store.Write(L"empty.txt", L"");
			TEST_ASSERT(store.Read(L"empty.txt") == L""); // Empty file round trip.
			store.Write(L"lines.txt", L"\n\nlast");
			TEST_ASSERT(store.Read(L"lines.txt") == L"\n\nlast"); // Leading empty lines and missing trailing newline.
			store.Write(L"carriage.txt", L"one\rtwo\r\n\r");
			TEST_ASSERT(store.Read(L"carriage.txt") == L"one\ntwo\n\n"); // Normalize CR, CRLF and trailing empty lines.

			List<MemoryMatch> matches;
			store.Search(L"中文", matches);
			TEST_ASSERT(matches.Count() == 1 && matches[0].path == L"people/用户.md" && matches[0].line == 2 && matches[0].text == L"中文记忆"); // Search returns file, line and text.
			store.Search(L"PEOPLE", matches);
			TEST_ASSERT(matches.Count() == 1 && matches[0].line == 0 && matches[0].text == L""); // Search matches paths without case sensitivity.
			store.Search(L"LAST", matches);
			TEST_ASSERT(matches.Count() == 1 && matches[0].line == 3); // Search matches lines without case sensitivity.
			store.Search(L"", matches, 1);
			TEST_ASSERT(matches.Count() == 1); // Search respects its result limit.
			ExpectMemoryFailure([&] { store.Search(L"", matches, 0); });
			ExpectMemoryFailure([&] { store.Search(L"", matches, 1001); });

			// Changing disk state must not change cached reads or searches until the next load.
			TEST_ASSERT(File(memoryRoot / L"people/用户.md").WriteAllText(L"external change", true, stream::BomEncoder::Utf8)); // Change cached file externally.
			TEST_ASSERT(store.Read(L"people/用户.md") == L"喜欢 C++\n中文记忆\n"); // Read uses the dictionary.
			store.Search(L"中文", matches);
			TEST_ASSERT(matches.Count() == 1); // Search uses the dictionary.
			MemoryStore reloaded(memoryRoot);
			TEST_ASSERT(reloaded.Read(L"people/用户.md") == L"external change"); // Restart loads persisted files.

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
			TEST_ASSERT(Folder(memoryRoot / L"prune/inner").Exists()); // Keep nonempty parent directory.
			store.Delete(L"prune/inner/second.txt");
			TEST_ASSERT(!Folder(memoryRoot / L"prune").Exists() && Folder(memoryRoot).Exists()); // Prune empty ancestors without deleting memory root.

			auto junction = memoryRoot / L"escape";
			store.Write(L"escape/sentinel.txt", L"cached before junction");
			TEST_ASSERT(File(junction / L"sentinel.txt").Delete() && Folder(junction).Delete(false)); // Replace cached directory with junction.
			CreateMemoryTestJunction(junction, outsideRoot);
			ExpectMemoryFailure([&] { store.Write(L"escape/sentinel.txt", L"forbidden"); });
			ExpectMemoryFailure([&] { store.Write(L"escape/new/deep.txt", L"forbidden"); });
			ExpectMemoryFailure([&] { store.Delete(L"escape/sentinel.txt"); });
			TEST_ASSERT(store.Read(L"escape/sentinel.txt") == L"cached before junction"); // Read never follows a replacement junction.
			ExpectMemoryFailure([&] { MemoryStore unsafe(memoryRoot); });
			TEST_ASSERT(RemoveDirectoryW(junction.GetFullPath().Buffer()) != FALSE); // Remove junction itself without traversing it.

			store.Write(L"linked.txt", L"old cache");
			TEST_ASSERT(File(memoryRoot / L"linked.txt").Delete()); // Prepare hard link replacement.
			TEST_ASSERT(CreateHardLinkW((memoryRoot / L"linked.txt").GetFullPath().Buffer(), outsideFile.GetFullPath().Buffer(), nullptr) != FALSE); // Create outside hard link.
			ExpectMemoryFailure([&] { store.Write(L"linked.txt", L"forbidden"); });
			ExpectMemoryFailure([&] { store.Delete(L"linked.txt"); });
			ExpectMemoryFailure([&] { MemoryStore unsafe(memoryRoot); });
			TEST_ASSERT(File(memoryRoot / L"linked.txt").Delete()); // Remove hard link itself.
			TEST_ASSERT(File(outsideFile).ReadAllTextByBom() == L"outside sentinel" && !Folder(outsideRoot / L"new").Exists()); // Unsafe operations never change outside files.
			List<WString> paths;
			store.ListFiles(paths);
			TEST_ASSERT(paths.Contains(WString(L"index.md")) && !paths.Contains(WString(L"prune/inner/second.txt"))); // List reflects writes and deletions.
		}
		// The unique target was checked above; every link created by these tests has been removed.
		TEST_ASSERT(Folder(sandbox).Delete(true)); // Remove temporary test directory.
	});
}
