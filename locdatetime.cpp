#include <time.h>
#include "locdatetime.h"

using std::string;
using std::ostream;

static void datetime_add(string& s, const time_t t)
{
	char buf[19];//xxxx-xx-xx xx:xx:xx\0
	struct tm st;
	localtime_r(&t, &st);
	st.tm_mon++;
	st.tm_year += 1900;
	buf[3] = st.tm_year % 10 + '0';
	st.tm_year /= 10;
	buf[2] = st.tm_year % 10 + '0';
	st.tm_year /= 10;
	buf[1] = st.tm_year % 10 + '0';
	buf[0] = st.tm_year / 10 + '0';
	buf[4] = '.';
	buf[5] = st.tm_mon / 10 + '0';
	buf[6] = st.tm_mon % 10 + '0';
	buf[7] = '.';
	buf[8] = st.tm_mday / 10 + '0';
	buf[9] = st.tm_mday % 10 + '0';
	buf[10] = ' ';
	buf[11] = st.tm_hour / 10 + '0';
	buf[12] = st.tm_hour % 10 + '0';
	buf[13] = ':';
	buf[14] = st.tm_min / 10 + '0';
	buf[15] = st.tm_min % 10 + '0';
	buf[16] = ':';
	buf[17] = st.tm_sec / 10 + '0';
	buf[18] = st.tm_sec % 10 + '0';
	s.append(buf, sizeof(buf));
}

static void date_add(string& s, const time_t t)
{
	char buf[10];//xxxx-xx-xx
	struct tm st;
	localtime_r(&t, &st);
	st.tm_mon++;
	st.tm_year += 1900;
	buf[3] = st.tm_year % 10 + '0';
	st.tm_year /= 10;
	buf[2] = st.tm_year % 10 + '0';
	st.tm_year /= 10;
	buf[1] = st.tm_year % 10 + '0';
	buf[0] = st.tm_year / 10 + '0';
	buf[4] = '.';
	buf[5] = st.tm_mon / 10 + '0';
	buf[6] = st.tm_mon % 10 + '0';
	buf[7] = '.';
	buf[8] = st.tm_mday / 10 + '0';
	buf[9] = st.tm_mday % 10 + '0';
	s.append(buf, sizeof(buf));
}

ostream& operator<<(ostream& s, loctime h)
{
	string str;
	datetime_add(str, h.t);
	s << str;
	return s;
}

string operator+(const string& s, loctime h)
{
	string res = s;
	datetime_add(res, h.t);
	return res;
}

string& operator+(string&& s, loctime h)
{
	datetime_add(s, h.t);
	return s;
}

string& operator+=(string& s, loctime h)
{
	datetime_add(s, h.t);
	return s;
}

loctime::operator string() const
{
	string str;
	datetime_add(str, t);
	return str;
}

ostream& operator<<(ostream& s, locdate h)
{
	string str;
	date_add(str, h.t);
	s << str;
	return s;
}

string operator+(const string& s, locdate h)
{
	string res = s;
	date_add(res, h.t);
	return res;
}

string& operator+(string&& s, locdate h)
{
	date_add(s, h.t);
	return s;
}

string& operator+=(string& s, locdate h)
{
	date_add(s, h.t);
	return s;
}

locdate::operator string() const
{
	string str;
	date_add(str, t);
	return str;
}
