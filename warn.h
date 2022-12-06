#pragma once
#include <string>
#include <sstream>

/* As "cout << .. << .. " but thread save, i.e. simultaneous writes from
 * different threads won't be mixed and be printed in sequence */


struct WarnTmp : std::ostream {
	WarnTmp() : std::ostream(&buf) { ok = true; };
	WarnTmp(WarnTmp&& src) : std::ostream(&buf), buf(std::move(src.buf)) {
		src.ok = false;
	}
	~WarnTmp();
private:
	std::stringbuf buf;
	bool ok;
};

struct Warn {
	template <typename T>
	WarnTmp operator<<(T x)
	{
		WarnTmp res;
		res << x;
		return res;
	}
};

struct NullOut : std::ostream {
	template <typename T>
	NullOut& operator<<(T x)
	{
		return *this;
	}
};

/* Usage as cout:
 * warn << "some string" << some_number << ...
 * 'endl' will be appened automatically */
extern Warn warn;

// Thread local redirection. Used for write infos and errors to sockets
extern thread_local std::iostream * warn_thread_local;

/* Also write filename and line number. Used for debugging. Usage as warn:
 * warnln << "some string" << some_number << ... */
WarnTmp get_warnln(const char* file, int line);
#define warnln get_warnln(__FILE__, __LINE__)
