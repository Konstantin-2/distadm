#include "core.h"
#include <error.h>
#include <errno.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <libintl.h>
#include <string>
#include <iostream>
#include <ostream>
#include <fstream>
#include <map>
#include <string_view>
#include <utility>
#include <thread>
#include <algorithm>
#include <filesystem>
#include "exc_error.h"
#include "utils.h"
#include "warn.h"
#include "cmd_local.h"
#include "incm.h"
#include "main.h"
#define _(STRING) gettext(STRING)
// Compile with `pkg-config --libs readline`
// On Debian instll libreadline-dev

/* This file contain functions executed for interactive commands
 * (from command line) on local node (daemon).
 * Most of them generate json commands to execute on all nodes in group */

namespace fs = std::filesystem;
using std::map;
using std::max;
using std::set;
using std::cout;
using std::endl;
using std::move;
using std::sort;
using std::pair;
using std::flush;
using std::vector;
using std::ifstream;
using std::string;
using std::string_view;
using std::ostream;
using std::thread;
using std::exception;

static int unix_fd = -1;

enum class Complt {
	none,
	node,
	user,
	loc_file,
	my_file
};

// Convert string to ingeter. Used for switch in completition function
static Complt complt(string_view str)
{
	static map<string_view, Complt> list = {
		{"delnode", Complt::node},
		{"deluser", Complt::user},
		{"addfile", Complt::loc_file},
		{"delfile", Complt::my_file},
		{"join-group", Complt::loc_file},
		{"finalize-invite", Complt::loc_file},
		{"read-packet", Complt::loc_file},
		{"write-packet", Complt::loc_file},
		{"write-online-invite", Complt::loc_file},
		{"write-offline-invite", Complt::loc_file},
		{"deldir", Complt::my_file}
	};

	auto p = list.find(str);
	return p != list.end() ? p->second : Complt::none;
}

// return number of word in string where ptr points (char offset)
static size_t word_number(char * str, size_t ptr)
{
	size_t res = 0;
	size_t i = 0;
	for (;;) {
		while (i < ptr && isspace(str[i])) i++;
		if (i == ptr) return res;
		while (i < ptr && !isspace(str[i])) i++;
		if (i == ptr) return res;
		res++;
	}
}

// send input from unix_fd to cout until EOT. Return false on EOF
static bool unix_cout()
{
	if(unix_fd == -1)
		return false;
	char buf[0x1000];
	bool end = false;
	while (!end) {
		ssize_t x = read(unix_fd, buf, sizeof(buf));
		if (!x)
			return false;
		if (x < 0)
			error(errno, errno, "read");
		if (buf[x - 1] == EOT) {
			end = true;
			x--;
		}
		cout.write(buf, x);
	}
	cout << endl;
	return true;
}

// generator_ functions used by readline()
static char * generator_cmdnames(const char * text, int state)
{
	static size_t len;
	static decltype(incm_list)::const_iterator ptr;
	if (!state) {
		ptr = incm_list.begin();
		len = strlen(text);
	}
	while (ptr != incm_list.end()) {
		const char* name = (ptr++->first).c_str();
		if (strncmp (name, text, len) == 0)
			return (strdup(name));
	}
	return nullptr;
}

static char * generator_nodenames(const char * text, int state)
{
	static size_t len, i;
	static vector<string> names;
	if (!state) {
		i = 0;
		len = strlen(text);
		if (unix_fd == -1)
			names = core->get_nodenames();
		else {
			string s = unix_getline(unix_fd, "listnodes\n");
			names = as_strings(split(s, false, ' '));
		}
	}
	while (i < names.size()) {
		const char* name = names[i++].c_str();
		if (strncmp (name, text, len) == 0)
			return strdup(name);
	}
	return nullptr;
}

static char * generator_usernames(const char * text, int state)
{
	static size_t len, i;
	static vector<string> names;
	if (!state) {
		i = 0;
		len = strlen(text);
		if (unix_fd == -1)
			names = core->get_usernames();
		else {
			string s = unix_getline(unix_fd, "listusers\n");
			names = as_strings(split(s, false, ' '));
		}
	}
	while (i < names.size()) {
		const char* name = names[i++].c_str();
		if (strncmp (name, text, len) == 0) {
			return strdup(name);
		}
	}
	return nullptr;
}

static char * generator_force(const char * text, int state)
{
	static size_t len;
	static bool b;
	if (!state) {
		b = true;
		len = strlen(text);
	}
	if (b) {
		const char* n = "force";
		b = false;
		if (strncmp (n, text, len) == 0)
			return (strdup(n));
	}
	return nullptr;
}

static int files_dir_readline(char ** s)
{
	free(*s);
	*s = strdup(core->cfg.filesdir().c_str());
	return 1;
}

char * deq(char * text, int ch)
{
	cout << "deq:" << text << ':' << ch << endl;
	return strdup(text);
}

char * enq(char * text, int x, char * ch)
{
	cout << "enq:" << text << ':' << x << endl;
	return strdup(text);
}

static char ** my_readline_cf(const char * text, int start, int end)
{
	rl_attempted_completion_over = 1; // no filename completition
	rl_directory_rewrite_hook = nullptr;
	rl_filename_quoting_desired = 1;
	size_t deep = word_number(rl_line_buffer, start);
	if (!deep)
		return rl_completion_matches(text, generator_cmdnames);
	auto [cmd, par] = split2(rl_line_buffer);
	if (deep == 1) {
		switch(complt(cmd)) {
		case Complt::node:
			return rl_completion_matches(text, generator_nodenames);
		case Complt::user:
			return rl_completion_matches(text, generator_usernames);
		case Complt::my_file:
			rl_directory_rewrite_hook = files_dir_readline;
		case Complt::loc_file:
			rl_filename_dequoting_function = &deq;
			rl_filename_quoting_function = &enq;
			rl_attempted_completion_over = 0;
		default:
			break;
		}
	} else if (deep == 2 && cmd == "delnode")
		return rl_completion_matches(text, generator_force);
	return nullptr;
}

bool Core::interactive_online_exec(vector<string>& cmd, int fd)
{
	bool ok = local_preprocess(cmd, cfg);
	if (!ok)
		return true;
	string str;
	for (const string& s : cmd) {
		if (!str.empty())
			str += ' ';
		str.append(s);
	}
	str += '\n';
	write(fd, str.data(), str.size());
	return unix_cout();
}

void Core::interactive_online(vector<string>& cmd, int fd)
{
	unix_fd = fd;
	Fstream cfd(fd, "unix socket", true);
	string line;
	bool ok;
	if (!cmd.empty()) {
		ok = interactive_online_exec(cmd, fd);
	} else {
		cout << "Online mode" << endl;
		rl_attempted_completion_function = (rl_completion_func_t*)my_readline_cf;
		ok = true;
		while (ok) {
			char * l = readline("distadm: ");
			if (!l)
				break;
			add_history(l);
			vector<string> cmdl = splitq(l);
			free (l);
			ok = interactive_online_exec(cmdl, fd);
		}
		rl_clear_history();
	}
	cfd.close();
	prog_status = ProgramStatus::exit;
}

bool Core::interactive_offline_exec(vector<string>& cmd)
{
	bool ok = local_preprocess(cmd, cfg);
	if (!ok)
		return true;
	ok = interactive_exec(cmd, cout);
	pending_commands();
	save();
	cout << endl;
	return ok;
}

void Core::interactive_offline(vector<string>& cmd)
{
	update_info();
	pending_commands();
	if (!cmd.empty())
		interactive_offline_exec(cmd);
	else {
		cout << "Offline mode" << endl;
		rl_attempted_completion_function = (rl_completion_func_t*)my_readline_cf;
		bool ok = true;
		while (ok && prog_status == ProgramStatus::work) {
			char * l = readline("distadm: ");
			if (!l)
				break;
			add_history(l);
			vector<string> cmdl = splitq(l);
			free (l);
			ok = interactive_offline_exec(cmdl);
		}
		rl_clear_history();
	}
}

bool Core::interactive_exec(vector<string>& cmd, ostream& os)
{
	try {
		if (cmd.empty())
			return true;
		if (cmd[0] == "exit")
			return false;
		auto p = incm_list.find(string(cmd[0]));
		if (p != incm_list.end())
			(this->*p->second)(os, cmd);
		else if (!cmd.empty())
			os << _("Not implemented");
	} catch (const exception& exc) {
		warn << exc.what();
	}
	os << flush;
	return true;
}

bool Core::interactive_exec(vector<string>&& cmd, ostream& os)
{
	return interactive_exec(cmd, os);
}
