#pragma once
#include <vector>
#include <string>
#include <json/json.h>

/* Object of this class keep lines from /etc/shadow file. These lines contains
 * info about username and password. Such line is named 'line' here and username
 * is named 'nick'. Adding and removeing lines leads to adding and removing real
 * logins in operation system (OS). */

struct Usernames {
	struct User {
		User(const std::string& l) : line(l) {};
		std::string line;
		std::string nick;
	};

	Usernames() = default;

	// Fill object. Used in load(). Does not change real logins
	Usernames(const Json::Value&);

	// Return array of lines. Used in save()
	Json::Value as_json() const;

	// Extract 'nick' from 'line'
	static std::string get_nick(const std::string& line);

	/* By default only 'lines' are filled cause 'nicks' are used rarely.
	 * So this function extract and fill 'nicks' for every stored line */
	void fill_nicks();

	/* Add new line to object or update existing line with same nick.
	 * Also add/update real login in OS */
	void add(const std::string&);

	// Del user from stored lines and from operation system
	void del(const std::string& nick);

	/* Replace lines with new lines from parameter.
	 * If user exists in 'users' but not in parameter, it will be deleted
	 * (also from OS),
	 * If user exists in parameter but not in 'users', it will be added
	 * (also to OS),
	 * Is user exists in 'users' and parameter, it will be updated
	 * (also password in OS changed).
	 * Parameter becomes invalid after this function called */
	void replace(Usernames&&);

	// Delete all lines from object (also their logins from OS). Use with caution!
	void clear();

	// Storage itself
	std::vector<User> users;

private:
	/* Add line to users or update existing line with same nick.
	 * Do not modify system users. Return true if record was added,
	 * false if updated. */
	bool add_ns(const std::string&);

	/* Update user passwords in OS. */
	void updpwd() const;

	// Return pointer to line in 'users' by nick or nullptr if record not found
	const std::string * find(const std::string_view) const;

	// Sets to true after fill_nicks() is called
	bool nicks_filled = false;
};
