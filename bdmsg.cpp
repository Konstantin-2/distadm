// BDCmd = Before Delete Commands
#include "core.h"
#include <algorithm>
#include <filesystem>
#include "warn.h"

using std::map;
using std::move;
using std::find;
using std::string;
using std::exception;
namespace fs = std::filesystem;

/* This file contain functions called on each command to delete
 * Command is to delete when it is executed and known to all other nodes */

static map<string, void (Core::*)(const Msg&)> bdm_list = {
	{"delnode", &Core::bdm_delnode},
	{"delfile", &Core::bdm_delfile},
	{"deldir", &Core::bdm_deldir}
};

void Core::before_delete_message(const Msg& msg)
{
	string msgstr = msg.value["name"].asString();
	auto p = bdm_list.find(msgstr);
	if (p != bdm_list.end())
		try {
			(this->*p->second)(msg);
		} catch(const exception& e) {
			warn << "Error process before-delete-command" << ' ' << msgstr << ':' << e.what();
		}
}

void Core::bdm_delfile(const Msg& msg)
{
	const Json::Value& jfn = msg.value["filename"];
	string fn = jfn.asString();
	string filename = cfg.filesdir() + '/' + fn;
	fs::remove(filename);
	auto p = find(filenames.begin(), filenames.end(), fn);
	if (p != filenames.end())
		filenames.erase(p);
}

void Core::bdm_deldir(const Msg& msg)
{
	string dn = msg.value["dirname"].asString();
	string dirname = cfg.filesdir() + '/' + dn;
	fs::remove_all(dirname);
}

void Core::bdm_delnode(const Msg& msg)
{
	UUID id = msg.value["val"];
	if (!id || (id == my_id && nodes.size() > 1))
		return;

	Json::Value newcmd;
	newcmd["name"] = "delnoderecord";
	newcmd["val"] = string(id);
	NodeStatus tmp = status;
	status = NodeStatus::work;
	create_command(move(newcmd));
	status = tmp;
}
