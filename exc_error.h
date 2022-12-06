#pragma once
#include <string>
#include <sstream>
#include <string.h>

/* Exception class. Construct string from parameters.
 * Separate them by spaces. */
struct exc_error : std::exception {
	template <class... Args> exc_error(Args... args)
	{
		std::ostringstream o;
		int dummy[] __attribute__ ((unused)) = { 0, ((void) addstr(o, std::forward<Args>(args)),0)... };
		msg = std::move(o).str();
	}

	const char * what() const noexcept override { return msg.c_str(); }
protected:
	template <typename T> void addstr(std::ostringstream& o, T t)
	{
//		if (!o.view().empty())
			o << ' ';
		o << t;
	}

	std::string msg;
};

/* As 'exc_error'. Also write strerror(errno) at the end. */
struct exc_errno : exc_error {
	template <class... Args> exc_errno(Args... args)
	{
		std::ostringstream o;
		int dummy[] __attribute__((unused)) = { 0, ((void) addstr(o, std::forward<Args>(args)),0)... };
		o << ": " << strerror(errno);
		msg = std::move(o).str();
	}
};
