#include "config.h"
#include <libintl.h>
#include <error.h>
#include <errno.h>
#include <ifaddrs.h>
#include <fstream>
#include <filesystem>
#include "utils.h"
#include "warn.h"
#define _(STRING) gettext(STRING)

namespace fs = std::filesystem;
using std::vector;
using std::string;
using std::string_view;
using std::ifstream;
using std::ofstream;
using std::istringstream;
using std::exception;

static vector<string> default_iface_names()
{
	vector<string> res;
	ifaddrs* addrs;
	int x = getifaddrs(&addrs);
	if (x)
		error(errno, errno, "getifaddrs error");
	for (ifaddrs* i = addrs; i; i = i->ifa_next)
		if (i->ifa_addr
			&& i->ifa_addr->sa_family == AF_INET6
			&& i->ifa_name
			&& (i->ifa_flags & IFF_UP)
			&& !(i->ifa_flags & IFF_LOOPBACK)
		)
			res.push_back(i->ifa_name);
	freeifaddrs(addrs);
	return res;
}

// ========== Config ===========

void Config::load()
{
	load_common();
	load_home();

	if (listen.empty()) {
		for (string_view i : default_iface_names())
			listen.insert(i);
		listen_specified = false;
	} else
		listen_specified = true;
}

bool Config::process_line(string_view s1, string_view s2)
{
	if (s1 == "workdir") {
		workdir_str = s2;
		filesdir_str = string(s2) + "/files";
		tmpfilesdir_str = string(s2) + "/tmp";
		return true;
	} else if (s1 == "check-free-space") {
		chk_free_space = s2 == "true" || s2 == "True" || s2 == "on" || s2 == "1";
	} else if (s1 == "port") {
		int x = 0;
		string s(s2);
		istringstream(s) >> x;
		if (x) port = x;
		return true;
	} else if (s1 == "files-granularity") {
		size_t x = 0;
		char ch = '\0';
		string s(s2);
		istringstream(s) >> x >> ch;
		switch (ch) {
		case '\0':
			break;
		case 'K':
		case 'k':
			x *= 0x400UL;
			break;
		case 'M':
		case 'm':
			x *= 0x400UL * 0x400;
			break;
		case 'G':
		case 'g':
			x *= 0x400UL * 0x400 * 0x400;
			break;
		case 'T':
		case 't':
			x *= 0x400UL * 0x400 * 0x400 * 0x400;
			break;
		case 'P':
		case 'p':
			x *= 0x400UL * 0x400 * 0x400 * 0x400 * 0x400;
			break;
		default:
			return false;
		}
		if (x) files_granularity = x;
		return true;
	} else if (s1 == "listen") {
		for (string_view i : split(s2))
			if (!i.empty())
				listen.insert(i);
		return true;
	} else if (s1 == "antivirus-scan-date-file") {
		av_scan_date_file = s2;
		return true;
	} else if (s1 == "antivirus-scan-date-file-date") {
		av_scan_date_file_date = s2;
		return true;
	} else if (s1 == "antivirus-scan-date-exec") {
		av_scan_date_exec = s2;
		return true;
	} else if (s1 == "antivirus-update-date-file") {
		av_update_date_file = s2;
		return true;
	} else if (s1 == "antivirus-update-date-file-date") {
		av_update_date_file_date = s2;
		return true;
	} else if (s1 == "antivirus-update-date-exec") {
		av_update_date_exec = s2;
		return true;
	} else if (s1 == "antivirus-results-file") {
		av_results_file = s2;
		return true;
	}
	return false;
}

void Config::load_common()
{
	ifstream f(config_filename);
	if (f) {
		string line;
		while (getline(f, line)) {
			auto item = split2(line);
			if (item.first.empty() || item.first[0] == '#')
				continue;
			bool ok = process_line(item.first, item.second);
			if(!ok)
				warn << _("Unrecognized line in config file") << ": " << line;
		}
	} else
		warn << _("Cannot open config file") << ' ' << config_filename;
	try {
		fs::create_directories(filesdir());
	} catch (const exception&) {}
}

void Config::load_home()
{
	char * home = getenv("HOME");
	if (!home)
		return;
	home_file = home;
	home_file += "/.config/distadm";
	ifstream f(home_file);
	if (!f) return;

	string line;
	while (getline(f, line)) {
		auto item = split2(line);
		if (item.first == "packet-file") {
			packet_file = item.second;
			break;
		}
	}
}

void Config::set_packet_file(const string& fn)
{
	if (packet_file == fn)
		return;
	packet_file = fn;
	ofstream f(home_file);
	if (!f)
		return;
	f << "packet-file " << fn;
}

const string& Config::workdir() const
{
	return workdir_str;
}

const string& Config::filesdir() const
{
	return filesdir_str;
}

const string& Config::tmpfilesdir() const
{
	return tmpfilesdir_str;
}
