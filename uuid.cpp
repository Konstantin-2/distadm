#include <fstream>
#include <cstring>
#include "uuid.h"

using std::string;
using std::ifstream;

UUID::UUID(const Json::Value& src)
{
	operator=(src);
}

UUID::UUID(const std::string& str)
{
	operator=(str);
}

UUID& UUID::operator=(const string& str)
{
	int res = uuid_parse(str.c_str(), uuid);
	if (res < 0)
		clear();
	return *this;
}

UUID& UUID::operator=(const Json::Value& src)
{
	if (src.isString())
		operator=(src.asString());
	else
		clear();
	return *this;
}

void UUID::clear()
{
	memset(uuid, 0, sizeof(uuid));
}

UUID::operator string() const
{
	char str[37];
	uuid_unparse_upper(uuid, str);
	return str;
}

UUID::operator bool() const
{
	for(auto x : uuid)
		if (x)
			return true;
	return false;
}

void UUID::random(ifstream& rnddevfile)
{
	do
		rnddevfile.read((char*)this, sizeof(*this));
	while (!*this);
}

std::strong_ordering UUID::operator<=>(const UUID& that) const
{
	int res = memcmp(uuid, that.uuid, sizeof(uuid));
	if (res < 0) return std::strong_ordering::less;
	else if (res == 0) return std::strong_ordering::equal;
	else return std::strong_ordering::greater;
}

bool UUID::operator==(const UUID& that) const
{
	return !memcmp(uuid, that.uuid, sizeof(uuid));
}

UUID UUID::none()
{
	UUID res;
	res.clear();
	return res;
}
