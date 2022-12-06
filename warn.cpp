#include <set>
#include <iostream>
#include "warn.h"

using std::set;
using std::pair;
using std::cout;
using std::endl;
using std::string;
using std::ostream;

Warn warn;
thread_local std::iostream * warn_thread_local = nullptr;

WarnTmp::~WarnTmp()
{
	if (!ok)
		return;
	if (warn_thread_local)
		*warn_thread_local << buf.str() << endl;
	else
		cout << buf.str() << endl;
}

WarnTmp get_warnln(const char* file, int line)
{
	return std::move(warn << file << ':' << line << ':');
}
