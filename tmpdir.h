#pragma once
#include <string>

/* Provides RAII access to temporary directory
 * This directory is created on constructor
 * and reccursively erased on destructor. */

struct TmpDir {
	TmpDir(const std::string& pathname);
	~TmpDir();

	// Move files from temporary path to 'destpath'
	void store_files(const std::string& destpath);

	// Temp path name
	const std::string path;
};
