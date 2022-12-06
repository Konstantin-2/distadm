#include "core.h"
#include <error.h>
#include <errno.h>
#include <sys/wait.h>
#include <libintl.h>
#include <iostream>
#include "showdebug.h"
#include "locdatetime.h"
#include "warn.h"
#include "utils.h"
#define _(STRING) gettext(STRING)

using std::map;
using std::move;
using std::vector;
using std::string;
using std::exception;

/* This file contains functions executed on commands spread between nodes */

static map<string, void (Core::*)(const Msg&)> cmd_ints = {
	{"BAD MESSAGE", nullptr},
	{"addnode", &Core::exec_addnode},
	{"sethostname", &Core::exec_sethostname},
	{"delnode", &Core::exec_delnode},
	{"delnoderecord", &Core::exec_delnoderecord},
	{"addfile", &Core::exec_addfile},
	{"delfile", nullptr},
	{"deldir", nullptr},
	{"online", &Core::exec_online},
	{"exec", &Core::exec_exec},
	{"executed", &Core::exec_executed},
	{"delexec", &Core::exec_delexec},
	{"dellog", &Core::exec_dellog},
	{"antivirus", &Core::exec_antivirus},
	{"adduser", &Core::exec_adduser},
	{"deluser", &Core::exec_deluser},
	{"smart", &Core::exec_smart}
};

void Core::exec(const Msg& cmd)
{
	string cmdstr = cmd.value["name"].asString();
	debug << "Exec " << cmdstr;
	auto p = cmd_ints.find(cmdstr);
	if (p != cmd_ints.end()) {
		if (p->second) {
			try {
				(this->*p->second)(cmd);
			} catch(const exception& e) {
				warnln << _("Error execute command") << ' ' << cmdstr << ':' << e.what();
			}
		}
	} else
		warn << "Unknown command" << ' ' <<  cmdstr;
}

//========== Command executors ==========

void Core::exec_addnode(const Msg& cmd)
{
	Json::Value val = cmd.value["val"];
	UUID id = val;
	auto n = nodes.find(id);
	if (n == nodes.end())
		nodes.resize(id, nullptr, max_proto_ver());
	else if (id != my_id)
		return;
	else if (!valid_node)
		valid_node = true;
	else {
		status = NodeStatus::uninitialized;
		warn << _("Node UUID collision found. You should reinitialize this node.");
	}
}

void Core::exec_sethostname(const Msg& cmd)
{
	state_nodes[string(cmd.node_id)]["hostname"] = cmd.value["val"];
}

void Core::exec_delnode(const Msg& cmd)
{
	UUID id = cmd.value["val"];
	if (!id || id != my_id)
		return;
	status = NodeStatus::deleting;
}

void Core::exec_delnoderecord(const Msg& cmd)
{
	UUID id = cmd.value["val"];
	if (!id)
		return;
	string name = nodename(id);
	if (id == my_id)
		del_self();
	else
		nodes.del(id);
	if (!node_alive(id))
		return;
	string str_id = string(id);
	Json::Value rec;
	rec["date"] = string(loctime(time(nullptr)));
	rec["text"] = string(_("Node can be deleted")) + ": " + str_id + " (" + name +  ')';
	rec["type"] = "deadnode";
	rec["deaduuid"] = str_id;
	state["log"].append(rec);
}

void Core::exec_online(const Msg& cmd)
{
	state_nodes[string(cmd.node_id)]["online"] = cmd.value["val"];
}

void Core::exec_addfile(const Msg& cmd)
{
	filenames.insert(cmd.value["filename"].asString());
}

void Core::exec_exec(const Msg& cmd)
{
	vector<string> params;
	const Json::Value& par = cmd.value["exec"];
	string str;
	for (Json::Value::ArrayIndex i = 0; i < par.size(); i++)
		if (par[i].isString()) {
			if (!str.empty())
				str += ' ';
			str += par[i].asString();
		}
	params.push_back("sh");
	params.push_back("-c");
	params.push_back(str);
	string out;
	if (exec_prog_output(params, out, cfg.filesdir())) {
		Json::Value newcmd;
		newcmd["name"] = "executed";
		newcmd["cmd"] = str;
		newcmd["date"] = string(loctime(time(nullptr)));
		newcmd["output"] = out;
		create_command(move(newcmd));
	} else
		warn << _("Error execute command") << ' ' << params[0];
}

void Core::exec_executed(const Msg& cmd)
{
	Json::Value rec;
	rec["cmd"] = cmd.value["cmd"];
	rec["uuid"] = string(cmd.node_id);
	rec["date"] = cmd.value["date"];
	rec["output"] = cmd.value["output"];
	state["exec"].append(rec);
}

void Core::exec_delexec(const Msg& cmd)
{
	string filter = cmd.value["filter"].asString();
	Json::Value& elist = state["exec"];
	if (filter.empty()) {
		elist = Json::arrayValue;
		return;
	}

	Json::Value removed;
	for (Json::Value::ArrayIndex i = 0; i < elist.size(); i++)
		if (elist[i]["cmd"] == filter)
			elist.removeIndex(i--, &removed);
}

void Core::exec_dellog(const Msg& cmd)
{
	state["log"].clear();
}

void Core::exec_antivirus(const Msg& cmd)
{
	Json::Value rec;
	rec["updated"] = cmd.value["updated"];
	rec["scanned"] = cmd.value["scanned"];
	rec["found"] = cmd.value["found"];
	state_nodes[string(cmd.node_id)]["antivirus"] = move(rec);
}

void Core::exec_adduser(const Msg& cmd)
{
	string line = cmd.value["val"].asString();
	users.add(line);
}

void Core::exec_deluser(const Msg& cmd)
{
	string nick = cmd.value["val"].asString();
	users.del(nick);
}
