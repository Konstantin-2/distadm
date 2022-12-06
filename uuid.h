#pragma once
#include <string>
#include <uuid/uuid.h>
#include <json/value.h>

// Compile with -luuid
// On Debian install uuid-dev

// 16 bytes UUID
struct UUID {
	// Create uninitialized UUID (contains trash)
	UUID() = default;

	// Try to initialize from parameter. Self-zeroing on errors
	UUID(const std::string&);
	UUID(const Json::Value&); // parameter should be json string
	UUID& operator=(const std::string&);
	UUID& operator=(const Json::Value&);

	// Return no-UUID (all bytes are zeroes)
	static UUID none();

	// Check if UUID is not zeroed
	operator bool() const;

	// Return UUID as hex string
	operator std::string() const;

	// Write all bytes zeroes
	void clear();

	void random(std::ifstream& dev_urandom);
	std::strong_ordering operator<=>(const UUID&) const;
	bool operator==(const UUID&) const;
private:
	uuid_t uuid;
};
