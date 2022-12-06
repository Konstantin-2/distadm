#include <cstring>
#include <sstream>
#include <string>
#include <json/json.h>
#include "sha.h"

using std::ostringstream;
using std::string;
using std::vector;

// ===== SHA1 =====

SHA1::SHA1(const void * buf, size_t size)
{
	SHA1_CTX sctx;
	SHA1Init(&sctx);
	SHA1Update(&sctx, (const uint8_t *)buf, size);
	SHA1Final((uint8_t *)hash, &sctx);
}

bool SHA1::operator==(const SHA1& that) const
{
	return !memcmp(hash, that.hash, sizeof(hash));
}

void SHA1::clear()
{
	memset(hash, 0, sizeof(hash));
}

void SHA1::write(const void * ptr, size_t size, SHA1& hash)
{
	hash.clear();
	hash = SHA1(ptr, size);
}

bool SHA1::check(void * ptr, size_t size, SHA1& hash)
{
	SHA1 tmp = hash;
	hash.clear();
	hash = SHA1(ptr, size);
	return hash == tmp;
}

// ===== SHA256 =====

bool SHA256::operator==(const SHA256& that) const
{
	return !memcmp(hash, that.hash, sizeof(hash));
}

SHA256::SHA256(const string& src)
{
	for_string(src);
}

SHA256::SHA256(const Json::Value& src)
{
	static Json::StreamWriterBuilder builder;
	builder["indentation"] = "";
	std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
	ostringstream os;
	writer->write(src, &os);
	string str = os.str();
	for_string(str);
}

void SHA256::for_string(const string& src)
{
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, (const uint8_t *)src.data(), src.size());
	SHA256_Final(hash, &ctx);
}

void SHA256::from_string(const string& src)
{
	size_t i = 0;
	auto ch = src.begin();
	while (i < sizeof(hash) && ch != src.end()) {
		char ch1 = ch != src.end() ? *ch++  : '0';
		char ch2 = ch != src.end() ? *ch++  : '0';
		ch1 -= ch1 < 'A' ? '0' : 'A' + 10;
		ch2 -= ch2 < 'A' ? '0' : 'A' + 10;
		hash[i++] = (ch1 << 4) | ch2;
	}
	while (i < sizeof(hash))
		hash[i++] = 0;
}

void SHA256::operator=(const string& src)
{
	from_string(src);
}

SHA256::operator string() const
{
	string res;
	res.reserve(sizeof(hash) * 2);
	for(size_t i = 0; i < sizeof(hash); i++) {
		char ch1 = hash[i] & 0xF;
		char ch2 = (hash[i] >> 4) & 0xF;
		ch1 += ch1 < 10 ? '0' : 'A' - 10;
		ch2 += ch2 < 10 ? '0' : 'A' - 10;
		res.push_back(ch1);
		res.push_back(ch2);
	}
	return res;
}

string SHA256::partial() const
{
	string res;
	res.reserve(sizeof(hash) * 2);
	for(size_t i = 0; i < 8; i++) {
		char ch1 = hash[i] & 0xF;
		char ch2 = (hash[i] >> 4) & 0xF;
		ch1 += ch1 < 10 ? '0' : 'A' - 10;
		ch2 += ch2 < 10 ? '0' : 'A' - 10;
		res.push_back(ch1);
		res.push_back(ch2);
	}
	return res;
}

SHA256::operator bool() const
{
	for (size_t i = 0; i < sizeof(hash); i++)
		if(hash[i])
			return true;
	return false;
}

void SHA256::clear()
{
	memset(hash, 0, sizeof(hash));
}
