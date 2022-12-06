#include "incm.h"
#include <libintl.h>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include "utils.h"
#include "exc_error.h"
#define _(STRING) gettext(STRING)

namespace fs = std::filesystem;
using std::endl;
using std::cout;
using std::set;
using std::map;
using std::min;
using std::max;
using std::pair;
using std::move;
using std::sort;
using std::vector;
using std::string;
using std::ostream;

map<string, void (Core::*)(ostream&, vector<string>&)> incm_list = {
	{"help", &Core::incm_help},
	{"status", &Core::incm_status},
	{"local-id", &Core::incm_local_id},
	{"delnode", &Core::incm_delnode},
	{"listnodes", &Core::incm_listnodes},
	{"nodesinfo", &Core::incm_nodesinfo},
	{"listonline", &Core::incm_listonline},
	{"addfile", &Core::incm_addfile},
	{"delfile", &Core::incm_delfile},
	{"deldir", &Core::incm_deldir},
	{"dellog", &Core::incm_dellog},
	{"exec", &Core::incm_exec},
	{"showexec", &Core::incm_showexec},
	{"showlog", &Core::incm_showlog},
	{"delexec", &Core::incm_delexec},
	{"antivirus", &Core::incm_antivirus},
	{"adduser", &Core::incm_adduser},
	{"deluser", &Core::incm_deluser},
	{"listusers", &Core::incm_listusers},
	{"listfiles", &Core::incm_listfiles},
	{"write-offline-invite", &Core::incm_write_off_invite},
	{"write-online-invite", &Core::incm_write_on_invite},
	{"join-group", &Core::incm_join},
	{"finalize-invite", &Core::incm_fin_invite},
	{"cancel-invite", &Core::incm_cancel_invite},
	{"read-packet", &Core::incm_read_packet},
	{"write-packet", &Core::incm_write_packet},
	{"queue", &Core::incm_queue},
	{"stored-commands", &Core::incm_stored_commands},
	{"exit", nullptr}
};

void Core::incm_status(ostream& os, vector<string>& str)
{
	os << status_string();
}

void Core::incm_local_id(ostream& os, vector<string>& str)
{
	os << string(my_id) << '/' << string(group_id);
}

void Core::incm_delnode(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2) {
		os << _("Node not specified");
		return;
	}
	UUID id = nodebyname(param[1]);
	if (!id) {
		os << _("Node not found");
		return;
	}

	Json::Value cmd;
	string id_s = id;
	cmd["name"] = "delnode";
	cmd["val"] = id_s;
	create_command(move(cmd));

	if (param.size() > 2 && param[2] == "force" && id != my_id) {
		Json::Value cmd;
		cmd["name"] = "delnoderecord";
		cmd["val"] = id_s;
		create_command(move(cmd));
	}
	os << _("Node queued to delete") << ": " << id_s;
}

void Core::incm_listnodes(std::ostream& os, vector<string>& param)
{
	print_list(os, get_nodenames(), ' ');
}

void Core::incm_addfile(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	fs::path abs_fn(cfg.filesdir());
	if (param.size() > 2)
		abs_fn /= param[2];
	abs_fn /= fs::path(param[1]).filename();
	string rel_fn = abs_fn.lexically_relative(cfg.filesdir());

	if (cfg.files_granularity == -1UL)  {
		Json::Value cmd;
		cmd["name"] = "addfile";
		cmd["filename"] = rel_fn;
		create_command(move(cmd));
	} else {
		off_t fsize = fs::file_size(abs_fn);
		if (fsize == -1L)
			throw exc_error("Error read file size", rel_fn);
		size_t j = (fsize - 1) / cfg.files_granularity;
		for (size_t i = 0; i < j; i++) {
			Json::Value cmd;
			cmd["from"] = i * cfg.files_granularity;
			cmd["to"] = (i + 1) * cfg.files_granularity;
			cmd["name"] = "addfile";
			cmd["filename"] = rel_fn;
			create_command(move(cmd));
		}
		Json::Value cmd;
		cmd["from"] = j * cfg.files_granularity;
		cmd["to"] = fsize;
		cmd["name"] = "addfile";
		cmd["filename"] = rel_fn;
		create_command(move(cmd));
	}
	os << _("File added");
}

void Core::incm_delfile(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	Json::Value cmd;
	cmd["name"] = "delfile";
		cmd["filename"] = string(param[1]);
	create_command(move(cmd));
	os << _("File queued to delete");
}

void Core::incm_deldir(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	Json::Value cmd;
	cmd["name"] = "deldir";
		cmd["dirname"] = string(param[1]);
	create_command(move(cmd));
	os << _("Directory queued to delete");
}

void Core::incm_listonline(std::ostream& os, vector<string>& param)
{
	const Json::Value& src = state_nodes;
	set<string> res;
	for (auto i = src.begin(); i != src.end(); i++)
		res.insert(nodename(*i, i.key().asString()) + ' ' + asString(*i, "online"));
	bool first = true;
	for (const string& p : res) {
		if (!first) os << '\n';
		else first = false;
		os << p;
	}
}

void Core::incm_exec(std::ostream& os, vector<string>& param)
{
	Json::Value cmd;
	cmd["name"] = "exec";
	Json::Value& params = cmd["exec"];
	for (size_t i = 1; i< param.size(); i++)
		params.append(param[i]);
	create_command(move(cmd));
	os << _("Command queued to execute") << '\n';
}

void Core::incm_showexec(std::ostream& os, vector<string>& param)
{
	const Json::Value& from = state["exec"];
	size_t size = from.size();
	const string& filter = param.size() > 1 ? param[1] : "";
	bool hasfilter = !filter.empty();
	for (Json::Value::ArrayIndex i = 0; i < size; i++) {
		const Json::Value& rec = from[i];
		if (hasfilter && rec["cmd"] != filter)
			continue;
		UUID id = rec["uuid"];
		os << "=========================\n" << _("Command") << ": ";
		os << rec["cmd"].asString() << '\n' << _("Node") << ": ";
		os << nodename(id) << '\n' << _("Date") << ": ";
		os << rec["date"].asString() << "\n-------------------------\n";
		string out = rec["output"].asString();
		if (out.empty() || out.back() != '\n')
			out += '\n';
		os << out;
	}
}

void Core::incm_showlog(std::ostream& os, vector<string>& param)
{
	const Json::Value& from = state["log"];
	size_t size = from.size();
	for (Json::Value::ArrayIndex i = 0; i < size; i++) {
		const Json::Value& rec = from[i];
		UUID id = rec["uuid"];
		os << rec["date"].asString() << ' ' << nodename(id) << ':' << rec["text"] << '\n';
	}
}

void Core::incm_delexec(std::ostream& os, vector<string>& param)
{
	Json::Value cmd;
	cmd["name"] = "delexec";
	if (param.size() > 1)
		cmd["filter"] = param[1];
	create_command(move(cmd));
	os << _("Results are cleared");
}

void Core::incm_dellog(std::ostream& os, vector<string>& param)
{
	Json::Value cmd;
	cmd["name"] = "dellog";
	create_command(move(cmd));
	os << _("Log is cleared");
}

void Core::incm_antivirus(std::ostream& os, vector<string>& param)
{
	const Json::Value& log = state_nodes;
	bool first = true;
	for (auto i = log.begin(); i != log.end(); i++) {
		UUID id = i.key();
		if (!id)
			continue;
		if (!first) os << '\n';
		else first = false;
		const Json::Value& v = (*i)["antivirus"];
		os << nodename(id) << " updated: " << v["updated"] << ", scanned: " << v["scanned"];
		if (v["found"] == true) os << " FOUND";
	}
}

void Core::incm_adduser(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	string pwd = find_pwd_line(param[1]);
	if (pwd.empty())
		return;

	Json::Value cmd;
	cmd["name"] = "adduser";
	cmd["val"] = pwd;
	create_command(move(cmd));
	os << _("User added");
}

void Core::incm_deluser(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	Json::Value cmd;
	cmd["name"] = "deluser";
	cmd["val"] = param[1];
	create_command(move(cmd));
	os << _("User queued to delete");
}

void Core::incm_listusers(std::ostream& os, vector<string>& param)
{
	print_list(os, get_usernames(), ' ');
}

void Core::incm_write_off_invite(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	if (param.size() < 3)
		param.emplace_back();
	bool ok = write_offline_invite(param[1], param[2]);
	if (ok)
		os << _("Invite written");
	else
		os << _("Invite is not written");
}

void Core::incm_write_on_invite(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	if (param.size() < 3)
		param.emplace_back();
	write_online_invite(param[1], param[2]);
	os << _("Invite written");
}

void Core::incm_join(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	if (param.size() < 3)
		param.emplace_back();
	read_invite(param[1], param[2]);
	os << _("Node joined to group");
}

void Core::incm_fin_invite(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	finalize_offline_invitation(param[1]);
	os << _("Invitation finalized");
}

void Core::incm_cancel_invite(std::ostream& os, vector<string>& param)
{
	if (param.size() < 1)
		return;
	finalize_offline_invitation();
	os << _("Invitation cancelled");
}

void Core::incm_read_packet(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	read_packet(param[1]);
	os << _("Packet read");
}

void Core::incm_write_packet(std::ostream& os, vector<string>& param)
{
	if (param.size() < 2)
		return;
	write_packet(param[1]);
	os << _("Packet written");
}

void Core::incm_queue(std::ostream& os, vector<string>& param)
{
	vector<pair<UUID, size_t>> res;
	size_t s = nodes.size();
	vector<size_t> maxrow(s);
	for (const auto& n : nodes)
		for (size_t i = 0; i < s; i++)
			maxrow[i] = max(maxrow[i], n.second.matrix_row[i]);
	for (const auto& n : nodes) {
		size_t d = 0;
			for (size_t i = 0; i < s; i++)
				d += maxrow[i] - n.second.matrix_row[i];
		if (d)
			res.emplace_back(n.first, d);
	}
	if (res.empty())
		return;
	sort(res.begin(), res.end(), [](const auto& a, const auto& b) {
		return a.second > b.second;
	});
	bool first = true;
	for (const auto& i : res) {
		if (first) first = false;
		else os << '\n';
		os << nodename(i.first) << ' ' << i.second;
	}
}

void Core::incm_nodesinfo(std::ostream& os, vector<string>& param)
{
	vector<size_t> exec;
	exec.reserve(nodes.size());
	for (auto& n : nodes)
		exec.push_back(n.second.command_to_exec);
	for (auto& n : nodes) {
		string sid = string(n.first);
		const string name = asString(state_nodes[sid], "hostname");
		const string online = asString(state_nodes[sid], "online");

		os << string(n.first) << '\t' << name << '\t' << online << '\t';
		vector<size_t> row = n.second.matrix_row;
		if (n.first != my_id)
			for (size_t x : row)
				os << x << '\t';
		else
			for (size_t i = 0; i < min(row.size(), exec.size()); i++)
				os << exec[i] << '/' << row[i] << '\t';

		os << n.second.netmsgcnt << '\n';
	}
}

void Core::incm_listfiles(std::ostream& os, vector<string>& param)
{
	print_list(os, get_filenames(), '\n');
}

void Core::incm_stored_commands(std::ostream& os, vector<string>& param)
{
	Json::Value json = save_commands();
	write_string(json, os);
}

void Core::incm_help(ostream& os, vector<string>& str)
{
	os << _("Available commands") << ":\n";
	for (const auto& i : incm_list)
		os << i.first << '\n';
}
