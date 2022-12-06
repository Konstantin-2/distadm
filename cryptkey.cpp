#include <cstring>
#include "cryptkey.h"

using std::ifstream;

void CryptKey::random(ifstream& f)
{
	for(;;) {
		f.read((char*)this, sizeof(*this));
		for(auto x : data)
			if (x)
				return;
	}
}

bool CryptKey::operator==(const CryptKey& that) const
{
	return !memcmp(data, that.data, sizeof(data));
}
