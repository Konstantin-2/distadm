#pragma once
#include <string>

//yyyy.mm.dd hh:mm:ss (+ timezone)
struct loctime {
	loctime(const time_t x) : t(x) {};
	operator std::string() const;
	const time_t t;
};

//yyyy.mm.dd (+ timezone)
struct locdate {
	locdate(const time_t x) : t(x) {};
	operator std::string() const;
	const time_t t;
};

std::string operator+(const std::string&, loctime);
std::string& operator+(std::string&&, loctime);
std::string& operator+=(std::string&, loctime);
std::ostream& operator<<(std::ostream&, loctime);

std::string operator+(const std::string&, locdate);
std::string& operator+(std::string&&, locdate);
std::string& operator+=(std::string&, locdate);
std::ostream& operator<<(std::ostream&, locdate);
