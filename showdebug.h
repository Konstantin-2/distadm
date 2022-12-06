#pragma once
#include <string>
#include <sstream>

struct DebugTmp : std::ostream {
	DebugTmp() : std::ostream(&buf) { ok = true; };
	DebugTmp(DebugTmp&& src) : std::ostream(&buf), buf(std::move(src.buf)) {
		src.ok = false;
	}
	~DebugTmp();
private:
	std::stringbuf buf;
	bool ok;
};

struct Debug {
	template <typename T>
	DebugTmp operator<<(T x)
	{
		DebugTmp res;
		res << x;
		return res;
	}
};

extern Debug debug;
extern bool print_debug;
DebugTmp get_debugln(const char* file, int line);
#define debugln get_debugln(__FILE__, __LINE__)
