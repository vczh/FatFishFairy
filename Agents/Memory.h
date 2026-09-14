#ifndef FATFISH_MEMORY_H
#define FATFISH_MEMORY_H

#include <VlppOS.h>

namespace fatfish
{
	struct MemoryMatch
	{
		vl::WString												path;
		vl::vint													line = 0;
		vl::WString												text;
	};

	class MemoryStore
	{
	private:
		vl::filesystem::FilePath										root;
		vl::collections::Dictionary<vl::WString,
			vl::Ptr<vl::collections::List<vl::WString>>>				files;

		void														LoadFolder(const vl::filesystem::FilePath& folder);
		void														ValidateDiskPath(const vl::filesystem::FilePath& path) const;

	public:
		// Tool errors are reported as vl::Exception. Reads and searches use the cached lines only.
																		MemoryStore(const vl::filesystem::FilePath& memoryRoot);
		vl::WString													NormalizePath(const vl::WString& path) const;
		vl::WString													Read(const vl::WString& path) const;
		void														Write(const vl::WString& path, const vl::WString& content);
		void														Delete(const vl::WString& path);
		void														Search(const vl::WString& query, vl::collections::List<MemoryMatch>& results, vl::vint limit = 100) const;
		void														ListFiles(vl::collections::List<vl::WString>& paths) const;
	};

	extern void RunMemoryTests();
}

#endif
