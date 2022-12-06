#pragma once
#include <fstream>
#include "uuid.h"

typedef UUID Nonce;

// Key to crypt messages and streams

struct CryptKey {
	CryptKey() = default;
	CryptKey(const CryptKey&) = default;
	CryptKey(CryptKey&&) = default;
	CryptKey& operator=(const CryptKey&) = default;
	CryptKey& operator=(CryptKey&&) = default;
	void random(std::ifstream&);
	bool operator==(const CryptKey&) const;

	unsigned char data[32];
};
