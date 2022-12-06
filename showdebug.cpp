#include <iostream>
#include "showdebug.h"

using std::cout;
using std::endl;
using std::string;

Debug debug;
bool print_debug = false;

DebugTmp::~DebugTmp()
{
	if (!ok)
		return;
	if (!print_debug)
		return;
	cout << buf.str() << endl;
}

DebugTmp get_debugln(const char* file, int line)
{
	return std::move(debug << file << ':' << line << ':');
}
