#include <filesystem>
#include "tmpdir.h"

using std::string;
namespace fs = std::filesystem;

TmpDir::TmpDir(const string& pathname) : path(pathname)
{
	if (fs::exists(path))
		fs::remove_all(path);
	fs::create_directory(path);
}

TmpDir::~TmpDir()
{
	fs::remove_all(path);
}

void TmpDir::store_files(const std::string& destpath)
{
	fs::rename(path, destpath);
}
