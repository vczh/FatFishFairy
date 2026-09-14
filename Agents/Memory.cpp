#include "Memory.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace fatfish
{
	using namespace vl;
	using namespace vl::collections;
	using namespace vl::filesystem;

	Ptr<List<WString>> SplitMemoryLines(const WString& text)
	{
		auto lines = Ptr(new List<WString>);
		vint start = 0;
		for (vint i = 0; i < text.Length(); i++)
		{
			if (text[i] == L'\r' || text[i] == L'\n')
			{
				lines->Add(text.Sub(start, i - start));
				if (text[i] == L'\r' && i + 1 < text.Length() && text[i + 1] == L'\n') i++;
				start = i + 1;
			}
		}
		lines->Add(text.Sub(start, text.Length() - start));
		return lines;
	}

	WString JoinMemoryLines(const List<WString>& lines)
	{
		return stream::GenerateToStream([&](stream::StreamWriter& writer)
		{
			for (vint i = 0; i < lines.Count(); i++)
			{
				if (i > 0) writer.WriteChar(L'\n');
				writer.WriteString(lines[i]);
			}
		});
	}

	void CheckMemoryDiskEntry(const FilePath& path)
	{
		auto attributes = GetFileAttributesW(path.GetFullPath().Buffer());
		if (attributes == INVALID_FILE_ATTRIBUTES)
		{
			auto error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return;
			throw Exception(L"无法检查记忆路径：" + path.GetFullPath());
		}
		if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			throw Exception(L"记忆路径不允许符号链接、目录联接或其他重解析点。");
		}
		if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			// A hard link would allow an otherwise confined write to modify an outside file.
			auto handle = CreateFileW(path.GetFullPath().Buffer(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (handle == INVALID_HANDLE_VALUE) throw Exception(L"无法检查记忆文件。");
			BY_HANDLE_FILE_INFORMATION information;
			auto success = GetFileInformationByHandle(handle, &information);
			CloseHandle(handle);
			if (!success) throw Exception(L"无法检查记忆文件属性。");
			if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || information.nNumberOfLinks > 1)
			{
				throw Exception(L"记忆文件不允许符号链接或硬链接。");
			}
		}
	}

/***********************************************************************
MemoryStore
***********************************************************************/

	MemoryStore::MemoryStore(const FilePath& memoryRoot)
		: root(memoryRoot)
	{
		if (root.IsRoot() || root.GetFolder().IsRoot())
		{
			throw Exception(L"记忆目录不能是文件系统根目录。");
		}
		ValidateDiskPath(root);
		if (!Folder(root).Exists() && !Folder(root).Create(true))
		{
			throw Exception(L"无法创建记忆目录。");
		}
		LoadFolder(root);
		if (!files.Keys().Contains(WString(L"index.md"))) Write(L"Index.md", L"# 记忆索引\n");
	}

	WString MemoryStore::NormalizePath(const WString& path) const
	{
		if (path.Length() == 0) throw Exception(L"记忆路径不能为空。");
		WString normalized;
		vint start = 0;
		for (vint i = 0; i <= path.Length(); i++)
		{
			if (i == path.Length() || path[i] == L'/' || path[i] == L'\\')
			{
				auto part = path.Sub(start, i - start);
				if (part.Length() == 0 || part == L"." || part == L".." || part[part.Length() - 1] == L'.' || part[part.Length() - 1] == L' ')
				{
					throw Exception(L"记忆路径必须是相对文件路径；不允许空路径段、点路径段、末尾空格或末尾句点。");
				}
				part = Locale::Invariant().ToLower(part);
				auto dot = part.IndexOf(L'.');
				auto base = dot < 0 ? part : part.Left(dot);
				while (base.Length() > 0 && base[base.Length() - 1] == L' ') base = base.Left(base.Length() - 1);
				auto reserved = base == L"con" || base == L"prn" || base == L"aux" || base == L"nul" || base == L"conin$" || base == L"conout$" || base == L"clock$";
				if (base.Length() == 4 && (base.Left(3) == L"com" || base.Left(3) == L"lpt"))
				{
					auto digit = base[3];
					reserved |= (digit >= L'1' && digit <= L'9') || digit == L'¹' || digit == L'²' || digit == L'³';
				}
				if (reserved) throw Exception(L"记忆路径不允许 Windows 设备名称。");
				if (normalized.Length() > 0) normalized += L"/";
				normalized += part;
				start = i + 1;
			}
			else if (path[i] < L' ' || path[i] == L':' || path[i] == L'<' || path[i] == L'>' || path[i] == L'"' || path[i] == L'|' || path[i] == L'?' || path[i] == L'*')
			{
				throw Exception(L"记忆路径包含非法字符。");
			}
		}
		auto prefix = root.GetFullPath() + WString::FromChar(FilePath::GetPathDelimiter());
		auto joined = prefix + normalized;
		if (joined.Length() >= MAX_PATH) throw Exception(L"记忆路径过长。");
		// FilePath also expands short names by consulting the disk. Keep reads purely in memory
		// by using the lexical Win32 expansion here; mutations use FilePath after validation.
		wchar_t buffer[MAX_PATH + 1];
		auto length = GetFullPathNameW(joined.Buffer(), MAX_PATH + 1, buffer, nullptr);
		if (length == 0 || length > MAX_PATH) throw Exception(L"无法展开记忆路径。");
		auto absolute = WString(buffer);
		if (absolute.Length() <= prefix.Length() || Locale::Invariant().CompareOrdinalIgnoreCase(absolute.Left(prefix.Length()), prefix) != 0)
		{
			throw Exception(L"记忆路径不能超出记忆目录。");
		}
		return normalized;
	}

	void MemoryStore::ValidateDiskPath(const FilePath& path) const
	{
		List<FilePath> ancestors;
		auto current = path;
		while (!current.IsRoot())
		{
			ancestors.Add(current);
			current = current.GetFolder();
		}
		for (vint i = ancestors.Count() - 1; i >= 0; i--)
		{
			CheckMemoryDiskEntry(ancestors[i]);
			if (i > 0 && ancestors[i].IsFile()) throw Exception(L"记忆路径的父路径必须是目录。");
		}
	}

	void MemoryStore::LoadFolder(const FilePath& folder)
	{
		ValidateDiskPath(folder);
		List<File> diskFiles;
		List<Folder> diskFolders;
		if (!Folder(folder).GetFiles(diskFiles) || !Folder(folder).GetFolders(diskFolders))
		{
			throw Exception(L"无法列举记忆目录。");
		}
		for (auto file : diskFiles)
		{
			ValidateDiskPath(file.GetFilePath());
			auto relative = root.GetRelativePathFor(file.GetFilePath());
			if (relative.Length() >= 2 && relative.Left(2) == L".\\") relative = relative.Right(relative.Length() - 2);
			auto key = NormalizePath(relative);
			if (files.Keys().Contains(key)) throw Exception(L"记忆文件路径存在大小写冲突。");
			WString content;
			stream::BomEncoder::Encoding encoding;
			bool containsBom;
			if (!file.ReadAllTextWithEncodingTesting(content, encoding, containsBom)) throw Exception(L"无法读取记忆文件：" + key);
			files.Add(key, SplitMemoryLines(content));
		}
		for (auto&& child : diskFolders)
		{
			auto relative = root.GetRelativePathFor(child.GetFilePath());
			if (relative.Length() >= 2 && relative.Left(2) == L".\\") relative = relative.Right(relative.Length() - 2);
			NormalizePath(relative);
			LoadFolder(child.GetFilePath());
		}
	}

	WString MemoryStore::Read(const WString& path) const
	{
		auto key = NormalizePath(path);
		if (!files.Keys().Contains(key)) throw Exception(L"记忆文件不存在：" + key);
		return JoinMemoryLines(*files[key].Obj());
	}

	void MemoryStore::Write(const WString& path, const WString& content)
	{
		auto key = NormalizePath(path);
		auto absolute = root / key;
		ValidateDiskPath(absolute);
		if (absolute.IsFolder()) throw Exception(L"不能把记忆目录覆盖为文件。");
		auto parent = Folder(absolute.GetFolder());
		if (!parent.Exists() && !parent.Create(true)) throw Exception(L"无法创建记忆文件的目录。");
		ValidateDiskPath(absolute);
		auto lines = SplitMemoryLines(content);
		if (!File(absolute).WriteAllText(JoinMemoryLines(*lines.Obj()), true, stream::BomEncoder::Utf8))
		{
			throw Exception(L"无法写入记忆文件：" + key);
		}
		files.Set(key, lines);
	}

	void MemoryStore::Delete(const WString& path)
	{
		auto key = NormalizePath(path);
		if (key == L"index.md") throw Exception(L"不能删除记忆索引 Index.md。");
		if (!files.Keys().Contains(key)) throw Exception(L"记忆文件不存在：" + key);
		auto absolute = root / key;
		ValidateDiskPath(absolute);
		if (!File(absolute).Delete()) throw Exception(L"无法删除记忆文件：" + key);
		files.Remove(key);
		auto parent = absolute.GetFolder();
		while (Locale::Invariant().CompareOrdinalIgnoreCase(parent.GetFullPath(), root.GetFullPath()) != 0)
		{
			ValidateDiskPath(parent);
			List<File> children;
			List<Folder> folders;
			if (!Folder(parent).GetFiles(children) || !Folder(parent).GetFolders(folders)) throw Exception(L"无法检查记忆目录。");
			if (children.Count() > 0 || folders.Count() > 0) break;
			if (!Folder(parent).Delete(false)) throw Exception(L"无法删除空的记忆目录。");
			parent = parent.GetFolder();
		}
	}

	void MemoryStore::Search(const WString& query, List<MemoryMatch>& results, vint limit) const
	{
		if (limit < 1 || limit > 1000) throw Exception(L"搜索结果数量必须介于 1 和 1000 之间。");
		results.Clear();
		for (vint i = 0; i < files.Count(); i++)
		{
			auto&& path = files.Keys()[i];
			auto&& lines = *files.Values()[i].Obj();
			if (query.Length() == 0 || Locale::Invariant().FindFirst(path, query, Locale::IgnoreCase).key >= 0)
			{
				results.Add({ path, 0, WString::Empty });
				if (results.Count() == limit) return;
			}
			for (vint line = 0; line < lines.Count(); line++)
			{
				if (query.Length() == 0 || Locale::Invariant().FindFirst(lines[line], query, Locale::IgnoreCase).key >= 0)
				{
					results.Add({ path, line + 1, lines[line] });
					if (results.Count() == limit) return;
				}
			}
		}
	}

	void MemoryStore::ListFiles(List<WString>& paths) const
	{
		paths.Clear();
		CopyFrom(paths, files.Keys());
	}
}
