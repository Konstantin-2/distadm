#include "cmd_local.h"
#include <libintl.h>
#include <map>
#include <vector>
#include <string>
#include <filesystem>
#include "warn.h"
#include "showdebug.h"
#include "core.h"
#include "utils.h"
#include "utils_iface.h"
#include "exc_error.h"
#define _(STRING) gettext(STRING)

namespace fs = std::filesystem;
using std::map;
using std::string;
using std::string_view;
using std::vector;
using std::exception;

static bool local_addfile(vector<string>& cmd, Config& cfg)
{
	if (cmd.size() < 2) {
		warn << _("File not specified");
		return false;
	}
	fs::path p_from = fs::absolute(cmd[1]);
	if (!fs::is_regular_file(p_from)) {
		warn << _("File not found");
		return false;
	}
	fs::path dst_filename = cfg.filesdir();
	if (cmd.size() > 2) {
		dst_filename = dst_filename / cmd[2];
		fs::create_directory(dst_filename);
	}
	dst_filename = dst_filename / p_from.filename();
	fs::remove(dst_filename);
	fs::copy(p_from, dst_filename);
	return true;
}

static bool local_adduser(vector<string>& cmd, Config& cfg)
{
	if (cmd.size() < 2) {
		warn << _("Username not specified");
		return false;
	}
	if (cmd.size() > 2 && cmd[2] == "quiet")
		return true;

	bool ok = exec_prog_interactive({"adduser", cmd[1]});
	if (ok)
		return true;

	exec_prog({"deluser", "--remove-home", cmd[1]});
	warn << _("User not added");
	return false;
}

static bool local_write_invite(vector<string>& cmd, Config& cfg)
{
	if (cmd.size() < 2) {
		warn << _("File not specified");
		return false;
	}
	if (cmd.size() < 3)
		cmd.emplace_back();
	cmd[1] = fs::absolute(cmd[1]);
	cmd[2] = readpasswd2();
	return true;
}

static bool local_read_invite(vector<string>& cmd, Config& cfg)
{
	if (cmd.size() < 2) {
		warn << _("File not specified");
		return false;
	}
	if (!fs::is_regular_file(cmd[1])) {
		warn << _("File not found");
		return false;
	}
	if (cmd.size() < 3)
		cmd.emplace_back();
	cmd[1] = fs::absolute(cmd[1]);
	if (cmd[0] == "join-group")
		cmd[2] = readpasswd(_("Enter password"));
	return true;
}

static map<string, bool(*)(vector<string>& cmd, Config&)> local_list = {
	{"addfile", &local_addfile},
	{"adduser", &local_adduser},
	{"write-offline-invite", &local_write_invite},
	{"write-online-invite", &local_write_invite},
	{"join-group", &local_read_invite},
	{"finalize-invite", &local_read_invite}
};

/* Preprocess command before send to daemon
 * return false if command is bad */
bool local_preprocess(vector<string>& cmd, Config& cfg)
{
	try {
		if (cmd.empty())
			return true;
		auto p = local_list.find(string(cmd[0]));
		if (p != local_list.end())
			return (p->second)(cmd, cfg);
		else
			return true;
	} catch (const exception& exc) {
		warn << exc.what();
	}
	return false;
}
