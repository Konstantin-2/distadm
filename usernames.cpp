#include <fstream>
#include <filesystem>
#include "utils.h"
#include "usernames.h"

using std::vector;
using std::string;
using std::string_view;
using std::ifstream;
using std::ofstream;
namespace fs = std::filesystem;

Usernames::Usernames(const Json::Value& src)
{
	if(!src.isArray())
		return;
	for (Json::Value::ArrayIndex i = 0; i < src.size(); i++) {
		const Json::Value& r = src[i];
		if (r.isString()) {
			string str = r.asString();
			users.emplace_back(str);
		}
	}
}

Json::Value Usernames::as_json() const
{
	Json::Value res = Json::arrayValue;
	for(const auto& s : users)
		res.append(s.line);
	return res;
}

string Usernames::get_nick(const string& line)
{
	size_t len = line.find(':');
	if (!len || len == string::npos)
		return "";
	else
		return string(line.data(), len);
}

void Usernames::fill_nicks()
{
	if (nicks_filled)
		return;
	for (User& u : users)
		u.nick = get_nick(u.line);
	nicks_filled = true;
}

void Usernames::add(const std::string& line)
{
	fill_nicks();
	bool ret = add_ns(line);
	if (ret)
		exec_prog({"adduser", string(Usernames::get_nick(line))});
	updpwd();
}

void Usernames::del(const std::string& nick)
{
	fill_nicks();
	for (auto p = users.begin(); p != users.end(); p++)
		if (p->nick == nick) {
			users.erase(p);
			break;
		}
	exec_prog({"deluser", "--remove-home", nick});
	updpwd();
}

void Usernames::replace(Usernames&& src)
{
	fill_nicks();
	src.fill_nicks();

	// Remove old users
	for (const auto& p : users)
		if (!src.find(p.nick))
			exec_prog({"deluser", "--remove-home", string(p.nick)});

	// Add new users
	for (const auto& p : src.users)
		if (!find(p.nick))
			exec_prog({"adduser", string(p.nick)});

	users = move(src.users);
	updpwd();
}

void Usernames::clear()
{
	for (const auto& p : users)
		exec_prog({"deluser", "--remove-home", string(p.nick)});
	users.clear();
}

bool Usernames::add_ns(const std::string& line)
{
	string_view nick = get_nick(line);
	for (auto& i : users)
		if (i.nick == nick) {
			i.line = line;
			i.nick = get_nick(i.line);
			return false;
		}
	users.emplace_back(line);
	users.back().nick = get_nick(users.back().line);
	return true;
}

void Usernames::updpwd() const
{
	fs::remove("/etc/shadow-");
	fs::rename("/etc/shadow", "/etc/shadow-");
	ifstream ifs("/etc/shadow-");
	ofstream ofs("/etc/shadow");
	string line;
	while (getline(ifs, line)) {
		const string * nline = find(Usernames::get_nick(line));
		ofs << (nline ? *nline : line) << '\n';
	}
}

const string * Usernames::find(const string_view nick) const
{
	for (auto& i : users)
		if (i.nick == nick)
			return (string *)&i.line;
	return nullptr;
}
