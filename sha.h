// Compile with: `pkg-config libmd`
// On Debian install libmd-dev
#pragma once
#include <string>
#include <sha1.h>
#include <sha256.h>
#include <json/value.h>

// Object with size of SHA1_DIGEST_LENGTH which store some hash and has additional functions
struct SHA1 {
	SHA1() = default;
	SHA1(const void *, size_t);
	bool operator==(const SHA1&) const;

	// Set hash to zeroes
	void clear();

	/* Calculate hash for object pointed by 1st parameter with length of 2d
	 * parameter, store result to 3rd paremeter. 3rd parameter cleared before
	 * calculations. */
	static void write(const void *, size_t, SHA1&);

	// Same as write() before
	template <typename T>
	static void write(T& t, SHA1 T::* p)
	{
		(t.*p).clear();
		t.*p = SHA1(&t, sizeof(t));
	}

	/* Calculate hash for object pointed by 1st parameter with length of 2d
	 * parameter, compare result with 3rd paremeter. 3rd parameter cleared before
	 * calculations. */
	static bool check(void *, size_t, SHA1&);

	// Same as check() before
	template <typename T>
	static bool check(T& t, SHA1 T::* p)
	{
		SHA1 tmp = t.*p;
		(t.*p).clear();
		t.*p = SHA1(&t, sizeof(t));
		return t.*p == tmp;
	}

	// Hash itself
	char hash[SHA1_DIGEST_LENGTH];
};

// Object with size of SHA256_DIGEST_LENGTH which store some hash and has additional functions
struct SHA256 {
	SHA256() = default;
	SHA256(const SHA256&) = default;
	SHA256(const std::string&); // calculate hash for parameter
	SHA256(const Json::Value&); // calculate hash for parameter
	void operator=(const std::string&); // read hash from hex string
	bool operator==(const SHA256&) const;
	operator std::string() const; // Save has as hex string
	std::string partial() const; // print some partial of hash. Used for debugging
	operator bool() const; // Check hash is not zeroes
	void clear(); // Set hash to zeroes
	unsigned char hash[SHA256_DIGEST_LENGTH]; // Hash itself
private:
	void for_string(const std::string&); // calculate hash for parameter
	void from_string(const std::string&); // read hash from hex string
};
