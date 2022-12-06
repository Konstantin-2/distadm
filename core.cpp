#include "core.h"
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/un.h>
#include <gnutls/crypto.h>
#include <libintl.h>
#include <memory>
#include <iostream>
#include <sstream>
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include "main.h"
#include "utils_iface.h"
#include "utils.h"
#include "tmpdir.h"
#include "exc_error.h"
#include "locdatetime.h"
#include "warn.h"
#include "showdebug.h"
#define _(STRING) gettext(STRING)

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using std::min;
using std::max;
using std::set;
using std::cout;
using std::endl;
using std::cin;
using std::copy;
using std::find;
using std::flush;
using std::map;
using std::sort;
using std::move;
using std::vector;
using std::string;
using std::string_view;
using std::ifstream;
using std::ofstream;
using std::exception;
using std::unique_ptr;
using std::istringstream;
using std::ostringstream;
using std::mutex;
using std::uniform_int_distribution;
using std::chrono::system_clock;
typedef std::lock_guard<mutex> lock;

Core * core;

/* Used in read/write_packet(), read/write _ online/offline _ invite(),*/
const short protocol_version = 1;

const map<string, NodeStatus> distadm_statuses = {
	{"uninitialized", NodeStatus::uninitialized},
	{"partially initialized", NodeStatus::part_init},
	{"work", NodeStatus::work},
	{"inviter", NodeStatus::inviter},
	{"deleting", NodeStatus::deleting},
	{"deleted", NodeStatus::deleted}
};

const map<NodeStatus, string> distadm_statuses_str = {
	{NodeStatus::uninitialized, "uninitialized"},
	{NodeStatus::part_init, "partially initialized"},
	{NodeStatus::work, "work"},
	{NodeStatus::inviter, "inviter"},
	{NodeStatus::deleting, "deleting"},
	{NodeStatus::deleted, "deleted"}
};

static bool check_no_file(const string& filename, string str, bool force_yes)
{
	if (force_yes)
		return true;
	if (!ifstream(filename))
		return true;
	str += ' ';
	str += _("Do you want to re-initialize?");
	return prompt_yn(str);
}

static void create_safe_file(const string& filename)
{
	int fd = creat(filename.c_str(), S_IRUSR | S_IWUSR);
	if (fd < 0)
		throw exc_errno(_("Error create file"), filename);
	close(fd);
}

template <typename T>
static void check_proto_version(T& f)
{
	short pv;
	f.read(&pv, sizeof(pv));
	if (pv != protocol_version)
		throw exc_error(_("Bad protocol version"));
}

void new_group(Config& cfg)
{
	try {
		Core core(cfg);
		core.create_group();
		core.save(true);
		exec_prog({"systemctl", "restart", "distadm"});
	} catch (const exception& ex) {
		print_message(_(ex.what()));
	}
}

// Return false on cancel
bool join_group(Config& cfg, const string& filename)
{
	string password = readpasswd(_("Enter password for invite file"));
	Core core(cfg);
	if (core.read_invite(filename, password)) {
		core.save(true);
		exec_prog({"systemctl", "restart", "distadm"});
		return true;
	} else {
		print_message(_("Error read file"));
		return false;
	}
}

// ===== Node =====

Node::Node(const Json::Value& json)
{
	const Json::Value& jrow = json["row"];
	if (jrow.isArray()) {
		Json::ArrayIndex n = jrow.size();
		matrix_row.resize(n);
		for (Json::ArrayIndex i = 0; i < n; i++)
			matrix_row[i] = jrow[i].asUInt64();
	}
	if (json["cmd"].isUInt64())
		command_to_exec = json["cmd"].asUInt64();
	if (json["netmsgcnt"].isUInt64())
		netmsgcnt = json["netmsgcnt"].asUInt64();
	if (json["proto-ver"].isUInt64())
		proto_ver = json["proto-ver"].asUInt64();
}

void Node::update(Json::Value& val) const
{
	Json::Value jrow = Json::arrayValue;
	for(size_t x : matrix_row)
		jrow.append(x);
	val["row"] = move(jrow);
	val["cmd"] = command_to_exec;
	val["netmsgcnt"] = netmsgcnt;
	val["proto-ver"] = proto_ver;
}

// ===== MsgId =====

MsgId::MsgId(const UUID& id, size_t num) : node_id(id), msg_number(num)
{
}

MsgId::MsgId(const Json::Value& src) :
	node_id(src["author_id"]),
	msg_number(src["number"].asUInt64())
{
}

bool MsgId::operator<(const MsgId& that) const
{
	return node_id < that.node_id || (node_id == that.node_id && msg_number < that.msg_number);
}

// ===== Msg =====

Msg::Msg(const MsgId& id) : MsgId(id)
{
}

Msg::Msg(const UUID& id, size_t num) : MsgId(id, num)
{
}

Msg::Msg(const Json::Value& src) : MsgId(src), value(src["value"])
{
	if (!valid()) {
		value["old-name"] = value["name"];
		value["name"] = "BAD MESSAGE";
		warn << _("Bad command found") << ' ' << src["value"];
		return;
	}
	const Json::Value& d = src["depends"];
	for (auto i = d.begin(); i != d.end(); i++) {
		UUID id = i.key();
		if (!id)
			continue;
		const Json::Value& v = *i;
		if (v.isUInt64())
			depends[id] = v.asUInt64();
	}
}

Json::Value Msg::as_json() const
{
	Json::Value res;
	res["author_id"] = string(node_id);
	res["number"] = msg_number;
 	res["value"] = value;
 	Json::Value& d = res["depends"];
 	for(const auto& i : depends)
		d[string(i.first)] = i.second;
	return res;
}

size_t Msg::total_size() const
{
	size_t size = compact_string(as_json()).size();
	if (value["name"] != "addfile")
		return size;
	const Json::Value& jfrom = value["from"];
	const Json::Value& jto = value["to"];
	if (jfrom.isInt64() && jto.isInt64()) {
		size_t from = jfrom.asInt64();
		size_t to = jto.asInt64();
		size += to - from;
		return size;
	}
	const Json::Value& fn = value["filename"];
	if (fn.isString())
		size += fs::file_size(value["filename"].asString());
	return size;
}

bool Msg::valid() const
{
	if (!value["name"].isString())
		return false;
	if (value["name"] == "addfile") {
		if (!value["filename"].isString())
			return false;
		const Json::Value& jfrom = value["from"];
		const Json::Value& jto = value["to"];
		if (value["from"].isInt64() && value["to"].isInt64()) {
			size_t from = jfrom.asInt64();
			size_t to = jto.asInt64();
			if (from > to)
				return false;
		}
	}
	return true;
}

// ===== Matrix =====

Node& Matrix::operator[](const UUID& id)
{
	auto p = find(id);
	if (p == end())
		throw exc_error(_("Bad UUID"));
	return p->second;
}

void Matrix::create(const UUID& id)
{
	(map<UUID, Node>::operator[])(id);
}

Matrix Matrix::read(ICCstream& f)
{
	size_t R;
	f.read(&R, sizeof(R));
	f.check_hash();

	vector<UUID> uuids(R);
	vector<size_t> mtx(R * R);
	vector<size_t> netmsgcnt(R);
	vector<short> proto_ver(R);
	f.read(uuids.data(), R * sizeof(UUID));
	f.read(mtx.data(), R * R * sizeof(size_t));
	f.read(netmsgcnt.data(), R * sizeof(size_t));
	f.read(proto_ver.data(), R * sizeof(short));
	f.check_hash();
	Matrix res;
	for (size_t i = 0; i < R; i++) {
		const UUID& id = uuids[i];
		Node& n = (*(map<UUID, Node>*)&res)[id];
		n.matrix_row.resize(R);
		copy(&mtx[i * R], &mtx[i * R + R], n.matrix_row.data());
		n.netmsgcnt = netmsgcnt[i];
		n.proto_ver = proto_ver[i];
	}
	return res;
}

Matrix Matrix::read_vld(ICCstream& f)
{
	Matrix res = read(f);
	size_t R = res.size();
	vector<size_t> cmd_to_exec(R);
	f.read(cmd_to_exec.data(), R * sizeof(size_t));
	auto n = res.begin();
	for (size_t i = 0; i < R; i++, n++)
		n->second.command_to_exec = cmd_to_exec[i];
	f.check_hash();
	return res;
}

void Matrix::write(OCCstream& f) const
{
	size_t R = size();
	write(f, R);
}

void Matrix::write(OCCstream& f, size_t R) const
{
	f.write(&R, sizeof(R));
	f.write_hash();

	vector<UUID> uuids;
	vector<size_t> mtx;
	vector<size_t> netmsgcnt;
	vector<short> proto_ver;
	uuids.reserve(R);
	mtx.reserve(R * R);
	netmsgcnt.reserve(R);
	proto_ver.reserve(R);
	for (const auto& n : *this) {
		uuids.push_back(n.first);
		for (size_t x : n.second.matrix_row)
			mtx.push_back(x);
		netmsgcnt.push_back(n.second.netmsgcnt);
		proto_ver.push_back(n.second.proto_ver);
	}
	f.write(uuids.data(), R * sizeof(UUID));
	f.write(mtx.data(), R * R * sizeof(size_t));
	f.write(netmsgcnt.data(), R * sizeof(size_t));
	f.write(proto_ver.data(), R * sizeof(short));
	f.write_hash();
}

void Matrix::write_vld(OCCstream& f) const
{
	size_t R = size();
	write(f, R);
	vector<size_t> cmd_to_exec;
	cmd_to_exec.reserve(R);
	for (auto& n : *this)
		cmd_to_exec.push_back(n.second.command_to_exec);
	f.write(cmd_to_exec.data(), R * sizeof(size_t));
	f.write_hash();
}

bool Matrix::update(const Matrix& src)
{
	bool updated = false;
	size_t M = size();
	vector<UUID> my_ids;
	my_ids.reserve(M);
	for (const auto& n : *this)
		my_ids.push_back(n.first);

	size_t R = src.size();
	vector<UUID> remote_ids;
	remote_ids.reserve(R);
	for (const auto& n : src)
		remote_ids.push_back(n.first);

	// Preparation to update : cell indexed 'i' in my row has source 'r_idx[i]' or -1
	vector<size_t> r_idx; // indexes in remote row
	r_idx.reserve(M);

	size_t m = 0;
	size_t r = 0;
	while (m < M && r < R) {
		while (r < R && remote_ids[r] < my_ids[m])
			r++;
		if (r < R && remote_ids[r] == my_ids[m])
			r_idx.push_back(r++);
		else
			r_idx.push_back(-1UL);
		m++;
	}
	while (r_idx.size() < M) r_idx.push_back(-1UL);

	m = 0;
	r = 0;
	auto rptr = src.begin();
	for (auto& n : *this) {
		if (r_idx[m] != -1UL) {
			while (r < r_idx[m]) rptr++, r++;
			size_t * my_row = n.second.matrix_row.data();
			const size_t * remote_row = rptr->second.matrix_row.data();
			for (size_t i = 0; i < M; i++)
				if (r_idx[i] != -1UL) {
					updated |= my_row[i] < remote_row[r_idx[i]];
					my_row[i] = max(my_row[i], remote_row[r_idx[i]]);
				}
			updated |= n.second.netmsgcnt < rptr->second.netmsgcnt;
			n.second.netmsgcnt = max(n.second.netmsgcnt, rptr->second.netmsgcnt);
			n.second.proto_ver = max(n.second.proto_ver, rptr->second.proto_ver);
		}
		m++;
	}
	return updated;
}

void Matrix::resize(const UUID& remote_id, const Node* from_node, short proto_ver)
{
	vector<UUID> vid = {remote_id};
	resize(vid, from_node, proto_ver);
}

void Matrix::resize(vector<UUID>& remote_ids, const Node* from_node, short proto_ver)
{
	sort(remote_ids.begin(), remote_ids.end());
	vector<UUID> my_ids;
	for (const auto& n : *this)
		my_ids.push_back(n.first);
	size_t M = my_ids.size(); // number of my nodes

	size_t R = remote_ids.size(); // number of remote nodes

	vector<UUID> all_ids;
	all_ids.reserve(R + M);
	set_union(my_ids.begin(), my_ids.end(), &remote_ids[0], &remote_ids[R], back_inserter(all_ids));
	size_t U = all_ids.size(); // number of united nodes

	vector<ssize_t> m_idx(U); // indexes in my row
	{
		size_t m = 0;
		for (size_t i = 0; i < U; i++)
			if (m < M)
				m_idx[i] = my_ids[m] == all_ids[i] ? m++ : -1L;
			else
				m_idx[i] = -1L;
	}

	// Unite: fill 'uni' matrix
	vector<size_t> uni(U * U); //united matrix
	for (size_t i = 0; i < U; i++) {
		const size_t * my_row = nullptr;
		if (m_idx[i] != -1L)
			my_row = (map<UUID, Node>::operator[])(my_ids[m_idx[i]]).matrix_row.data();
		else if (from_node)
			my_row = from_node->matrix_row.data();

		for (size_t j = 0; j < U; j++) {
			size_t a = 0;
			if (my_row && m_idx[j] != -1L)
				a = my_row[m_idx[j]];
			uni[i * U + j] = a;
		}
	}

	// Save results
	for (size_t i = 0; i < U; i++)
		(map<UUID, Node>::operator[])(all_ids[i]).matrix_row = vector(&uni[i * U], &uni[i * U + U]);
	for (const UUID& id : remote_ids) {
		Node& n = (map<UUID, Node>::operator[])(id);
		n.proto_ver = proto_ver;
	}
}

Matrix Matrix::load_nodes(const Json::Value& src)
{
	Matrix res;
	if (src.isObject())
		for (Json::ValueConstIterator it = src.begin(); it != src.end(); ++it)
			res.emplace(it.key(), *it);
	size_t len = res.size();
	for (const auto& n : res)
		if (n.second.matrix_row.size() != len)
			throw exc_error(_("Node file damaged"));
	return res;
}

void Matrix::del(const UUID& id)
{
	auto p = find(id);
	if (p == end())
		return;
	size_t d = distance(begin(), p);
	erase(p);
	for (auto& i : *this)
		i.second.matrix_row.erase(i.second.matrix_row.begin() + d);
}

ssize_t Matrix::node_offset(const UUID& id) const
{
	auto n = find(id);
	if (n == end())
		return -1;
	return distance(begin(), n);
}

// ===== Core =====

CoreBase::CoreBase(Config& c) : cfg(c)
{
}

bool CoreBase::load()
{
	const string filename = cfg.workdir() + "/group-id";
	ifstream f(filename);
	if (!f)
		return false;
	f.read((char *)&crypt_key, sizeof(crypt_key));
	f.read((char *)&group_id, sizeof(group_id));
	if (!f)
		return false;
	return true;
}

bool CoreBase::need_initialize() const
{
	return status == NodeStatus::uninitialized || status == NodeStatus::part_init;
}

string CoreBase::unix_socket_name() const
{
	return string(RUN_DIR "/distadm-") + string(group_id);
}

string CoreBase::unix_socket_name_lp() const
{
	return string(RUN_DIR "/distadm-lp-") + string(group_id);
}

// ===== Core =====

Core::Core(Config& c) : CoreBase(c)
{
	core = this;
}

void Core::load()
{
	cfg.load();
	bool created = load_group_id();
	if (created)
		return;
	const Json::Value js = load_local_state();
	if (js.isObject()) {
		my_id = js["local-id"];
		status = load_status(js["status"]);
		state_nodes = js["nodes"];
		state = js["state"];
		nodes = Matrix::load_nodes(state_nodes);
		messages = load_messages(js["commands"]);
		users = Usernames(js["users"]);
		filenames = load_filenames(js["filenames"]);
		invite_id = js["invite-id"];
		valid_node = asBool(js["valid-node"]);
	}
	auto p = nodes.find(my_id);
	if (p == nodes.end())
		return;
	my_node = &nodes[my_id];
	my_node->hash = calc_my_hash();
	my_node->proto_ver = protocol_version;
	check_matrix();
}

bool Core::load_group_id()
{
	const string filename = cfg.workdir() + "/group-id";
	ifstream f(filename);
	if (f) {
		f.read((char *)&crypt_key, sizeof(crypt_key));
		f.read((char *)&group_id, sizeof(group_id));
		if (!f)
			throw exc_errno(_("Error read file"), filename);
		return false;
	} else {
		if (!prompt_yn(_("Group not initialized. Do you want to initialize it?")))
			throw exc_error(_("Group not initialized"));
		create_group();
		save(true);
		return true;
	}
}

bool Core::create_group()
{
	crypt_key.random(rnd);
	group_id.random(rnd);
	my_id.random(rnd);
	nodes.create(my_id);
	my_node = &nodes[my_id];
	my_node->hash = calc_my_hash();
	my_node->matrix_row.push_back(0);
	my_node->proto_ver = protocol_version;

	const string filename = cfg.workdir() + "/group-id";
	if (!check_no_file(filename, _("Group already initialized."), cfg.force_yes))
		return false;
	save_group_id(filename);
	cfg.force_yes = true;
	status = NodeStatus::work;
	valid_node = true;
	return true;
}

void Core::save_group_id(const string& filename) const
{
	create_safe_file(filename);
	ofstream of(filename);
	if (!of)
		throw exc_errno(_("Error open file"), filename);
	of.write((char*)&crypt_key, sizeof(crypt_key));
	of.write((char*)&group_id, sizeof(group_id));
	if (!of)
		throw exc_errno(_("Error write file"), filename);
}

Json::Value Core::load_local_state()
{
	try {
		return load_local_state(cfg.workdir() + "/node");
	} catch (const exception&) {}
	warn << _("Save file damaged. Load from backup.");
	load_from_bkup = true;
	return load_local_state(cfg.workdir() + "/node~");
}

Json::Value Core::load_local_state(const string& filename)
{
	Json::Value res;
	ifstream f(filename);
	if (f) {
		f >> res;
		if (!f)
			throw exc_errno(_("Error read file"), filename);
	} else {
		if (!prompt_yn(("Node file not found. Do you want to create new?")))
			throw exc_error(_("Node file not found"));
		my_id.clear();
		status = NodeStatus::uninitialized;
	}
	return res;
}

set<Msg> Core::load_messages(const Json::Value& src)
{
	set<Msg> res;
	if (src.isArray()) {
		const Json::ArrayIndex n = src.size();
		for (Json::ArrayIndex i = 0; i < n; i++)
			res.emplace(src[i]);
	}
	return res;
}

set<string> Core::load_filenames(const Json::Value& src)
{
	set<string> res;
	if(src.isArray()) {
		const Json::ArrayIndex n = src.size();
		for (Json::ArrayIndex i = 0; i < n; i++) {
			const Json::Value& r = src[i];
			if (r.isString())
				res.insert(r.asString());
			else
				throw exc_error(_("Node file damaged"));
		}
	}
	return res;
}

NodeStatus Core::load_status(const Json::Value& src)
{
	if (!src.isString())
		throw exc_error(_("Node file damaged"));
	string str = src.asString();
	auto s = distadm_statuses.find(str);
	if (s != distadm_statuses.end())
		return s->second;
	else
		return NodeStatus::uninitialized;
}

void Core::save(bool force)
{
	if (!need_save && !force)
		return;
	need_save = false;
	Json::Value json;
	json["local-id"] = string(my_id);
	json["valid-node"] = valid_node;
	json["status"] = status_string();
	json["nodes"] = save_nodes();
	json["state"] = state;
	json["commands"] = save_commands();
	json["users"] = users.as_json();
	json["filenames"] = save_filenames();
	if (status == NodeStatus::inviter)
		json["invite-id"] = string(invite_id);
	const string filename = cfg.workdir() + "/node";
	if (!load_from_bkup)
		try {
			fs::rename(filename, filename + '~');
		} catch(...){}
	create_safe_file(filename);
	ofstream f(filename);
	if (!f)
		throw exc_errno(_("Error open file"), filename);
	write_string(json, f);
	if (!f)
		throw exc_errno(_("Error save file"), filename);
	debug << _("Saved");
}


GroupIdPacket Core::read_group_id(Istream& ff, const std::string& passwd)
{
	Nonce nonce;
	ff.read(&nonce, sizeof(nonce));

	CryptKey pwd_key;
	gnutls_datum_t iv_pwd, iv_salt;
	iv_pwd.data = (unsigned char *)passwd.data();
	iv_pwd.size = passwd.size();
	iv_salt.data = (unsigned char *)&nonce;
	iv_salt.size = sizeof(nonce);
	int ret = gnutls_pbkdf2(GNUTLS_MAC_SHA256, &iv_pwd, &iv_salt, pbkdf2_iter_count, (void *)&pwd_key, sizeof(pwd_key));
	if (ret < 0)
		error(1, 0, "GNU TLS error %s", gnutls_strerror(ret));

	GroupIdPacket nid;
	ICstream f(ff, pwd_key, nonce);
	short pv;
	f.read(&pv, sizeof(pv));
	f.read(&nid, sizeof(nid));
	f.check_hash();
	if (pv != protocol_version)
		throw exc_error(_("Bad protocol version"));
	return nid;
}

void Core::write_group_id(Ostream& of, const string& passwd) const
{
	GroupIdPacket nid;
	nid.key = crypt_key;
	nid.group_id = group_id;

	Nonce nonce;
	nonce.random(rnd);

	CryptKey pwd_key;
	gnutls_datum_t iv_pwd, iv_salt;
	iv_pwd.data = (unsigned char *)passwd.data();
	iv_pwd.size = passwd.size();
	iv_salt.data = (unsigned char *)&nonce;
	iv_salt.size = sizeof(nonce);
	int ret = gnutls_pbkdf2(GNUTLS_MAC_SHA256, &iv_pwd, &iv_salt, pbkdf2_iter_count, (void *)&pwd_key, sizeof(pwd_key));
	if (ret < 0)
		error(1, 0, "GNU TLS error %s", gnutls_strerror(ret));

	of.write(&nonce, sizeof(nonce));
	OCstream f(of, pwd_key, nonce);
	f.write(&protocol_version, sizeof(protocol_version));
	f.write(&nid, sizeof(nid));
	f.write_hash();
}

set<Msg> Core::read_messages(ICCstream& f)
{
	size_t cmd_size;
	set<Msg> res;
	f.read(&cmd_size, sizeof(cmd_size));
	f.check_hash();
	for (size_t i = 0; i < cmd_size; i++) {
		Json::Value j = f.read_json();
		res.emplace(j);
	}
	f.check_hash();
	return res;
}

void Core::write_messages(OCCstream& f) const
{
	size_t cmd_size = messages.size();
	f.write(&cmd_size, sizeof(cmd_size));
	f.write_hash();
	for (const Msg& m : messages)
		f.write_json(m.as_json());
	f.write_hash();
}

void Core::write_online_invite(const string& filename, const string& passwd) const
{
	Fstream b = Fstream::create(filename);
	Ostream f(b);
	write_group_id(f, passwd);
}

bool Core::write_offline_invite(const string& filename, const string& passwd)
{
	if (status == NodeStatus::inviter) {
		if (!prompt_yn(_("Node already write invite. Do you want to cancel previous invite and write new one?")))
			return false;
	} else if (status != NodeStatus::work)
		throw exc_error(_("Node is not initialized"));
	Fstream b = Fstream::create(filename);
	Ostream f1(b);
	write_group_id(f1, passwd);
	OCCstream f(f1, crypt_key);
	invite_id.random(rnd);
	f.write(&invite_id, sizeof(invite_id));
	write_initializer_v1(f);
	status = NodeStatus::inviter;
	return true;
}

void Core::write_initializer_v1(OCCstream& f)
{
	f.write(&my_id, sizeof(my_id));
	nodes.write_vld(f);
	f.write_json(state_nodes);
	f.write_json(state);
	write_messages(f);
	f.write_json(users.as_json());
	f.write_json(save_filenames());
	for(const string& fn : filenames)
		f.write_file(cfg.filesdir() + '/' + fn);
}

bool Core::read_invite(const string& filename, const string& passwd)
{
	const string local_filename = cfg.workdir() + "/node";
	if (!check_no_file(local_filename, _("Node file already exists."), cfg.force_yes))
		return false;
	try {
		read_offline_invite(filename, passwd);
		return true;
	} catch (const exception&) {}

	try {
		read_online_invite(filename, passwd);
		return true;
	} catch (const exception&) {}

	return false;
}

void Core::read_online_invite(const string& filename, const string& passwd)
{
	Fstream b = Fstream::open(filename);
	Istream f(b);
	GroupIdPacket nid = read_group_id(f, passwd);
	crypt_key = nid.key;
	group_id = nid.group_id;
	my_id.clear();
	status = NodeStatus::uninitialized;
	state_nodes = Json::Value();
	state = Json::Value();
	nodes.clear();
	messages.clear();
	users.clear();
	filenames.clear();
	fs::remove_all(cfg.filesdir());
	save_group_id(cfg.workdir() + "/group-id");
}

void Core::read_offline_invite(const string& filename, const string& passwd)
{
	Fstream b = Fstream::openrw(filename);
	Istream f1(b);
	GroupIdPacket nid = read_group_id(f1, passwd);
	crypt_key = nid.key;
	group_id = nid.group_id;
	ICCstream f(f1, crypt_key);
	UUID tmp;
	f.read(&tmp, sizeof(tmp));
	UUID from_id = read_initializer_v1(f);
	f.close();
	save_group_id(cfg.workdir() + "/group-id");
	status = NodeStatus::part_init;
	off_t eod_pos = f1.tell();
	off_t fsize = f1.base.filesize();
	TrailerUUIDs trl;
	if (fsize > eod_pos)
		trl = read_trailer_uuids(f1, fsize - eod_pos);
	else
		trl.nonce.random(rnd);
	for (;;) {
		my_id.random(rnd);
		if (nodes.find(my_id) != nodes.end())
			continue;
		if (find(trl.uuids.begin(), trl.uuids.end(), my_id) == trl.uuids.end())
			break;
	}
	auto p = nodes.find(from_id);
	if (p == nodes.end())
		throw exc_error(_("Bad invite"));
	nodes.resize(my_id, &p->second, max_proto_ver());
	trl.uuids.push_back(my_id);
	f1.close();
	b.seek(eod_pos);
	Ostream of(b);
	write_trailer_uuids(of, trl);
}

UUID Core::read_initializer_v1(ICCstream& f)
{
	UUID from_id;
	f.read(&from_id, sizeof(from_id));
	Matrix mtx = Matrix::read_vld(f);
	Json::Value stn = f.read_json();
	Json::Value stt = f.read_json();
	set<Msg> cmds = read_messages(f);
	Usernames usrs(f.read_json());
	set<string> filnames = load_filenames(f.read_json());
	TmpDir tmpdir(cfg.tmpfilesdir());
	for(const string& fn : filnames)
		f.read_to_file(tmpdir.path + '/' + fn);

	state_nodes = stn;
	state = stt;
	nodes = mtx;
	messages = cmds;
	users.replace(move(usrs));
	filenames = filnames;
	tmpdir.store_files(cfg.filesdir());
	cwd();
	return from_id;
}

void Core::read_net_initializer(ICCstream& sin, OCCstream& sout, const UUID& remote_id)
{
	if (!my_id)
		my_id.random(rnd);
	UUID from_id = read_initializer_v1(sin);
	char ok;
	do {
		while (nodes.find(my_id) != nodes.end())
			my_id.random(rnd);
		sout.write(&my_id, sizeof(my_id));
		sout.write_hash();
		sout.flush_net();
		sin.read(&ok, sizeof(ok));
		sin.check_hash();
	} while (!ok);
	auto p = nodes.find(from_id);
	if (p == nodes.end())
		throw exc_error(_("Bad invite"));
	nodes.resize(my_id, &p->second, max_proto_ver());
	status = NodeStatus::work;
	my_node = &nodes[my_id];
}

void Core::finalize_offline_invitation(const string& filename)
{
	if (status != NodeStatus::inviter) {
		warn << _("This node is not inviter");
		return;
	}
	Fstream b = Fstream::open(filename);
	b.seek(sizeof(Nonce) + sizeof(UUID) + sizeof(short) + sizeof(GroupIdPacket) + SHA1_DIGEST_LENGTH);

	Istream f1(b);
	ICCstream f(f1, crypt_key);

	UUID init_id;
	f.read(&init_id, sizeof(init_id));
	if (invite_id != init_id)
		throw exc_error(_("Wrong file"));
	f.read(&init_id, sizeof(init_id));
	Matrix::read_vld(f);
	f.read_json(); // state_nodes
	f.read_json(); // state
	read_messages(f);
	Usernames(f.read_json()); // users
	set<string> filnames = load_filenames(f.read_json());
	for(size_t i = 0; i < filnames.size(); i++)
		f.skip_file();
	f.close();

	off_t eod_pos = f1.tell();
	off_t fsize = f1.base.filesize();
	if (fsize == eod_pos) {
		warn << _("No new nodes");
		status = NodeStatus::work;
		return;
	}
	TrailerUUIDs trl = read_trailer_uuids(f1, fsize - eod_pos);
	f1.close();

	for (const UUID& id : trl.uuids)
		if (nodes.find(id) != nodes.end())
			throw exc_error(_("UUID collision"), _("Damaged file"), filename);

	for (const UUID& id : trl.uuids) {
		Json::Value cmd;
		cmd["name"] = "addnode";
		cmd["val"] = string(id);
		create_command(move(cmd), false);
	}
	pending_commands();
	status = NodeStatus::work;
}

void Core::finalize_offline_invitation()
{
	status = NodeStatus::work;
}

TrailerUUIDs Core::read_trailer_uuids(Istream& f, size_t size) const
{
	TrailerUUIDs res;
	f.read(&res.nonce, sizeof(res.nonce));
	ICstream ff(f, crypt_key, res.nonce);
	size_t cnt = (size - sizeof(Nonce) - sizeof(UUID) - SHA1_DIGEST_LENGTH) / sizeof(UUID);
	res.uuids.resize(cnt);
	for (size_t i = 0; i < cnt; i++)
		ff.read(&res.uuids[i], sizeof(UUID));
	ff.check_hash();
	return res;
}

void Core::write_trailer_uuids(Ostream& f, const TrailerUUIDs& trl) const
{
	f.write(trl.nonce);
	OCstream ff(f, crypt_key, trl.nonce);
	for (const UUID& id : trl.uuids)
		ff.write(&id, sizeof(id));
	ff.write_hash();
}

SHA256 Core::calc_my_hash() const
{
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	for(const auto& i : nodes) {
		SHA256_Update(&ctx, (const uint8_t *)&i.first, sizeof(i.first));
		SHA256_Update(&ctx, (const uint8_t *)i.second.matrix_row.data(), i.second.matrix_row.size() * sizeof(decltype(i.second.matrix_row)::value_type));
	}
	SHA256 res;
	SHA256_Final(res.hash, &ctx);
	return res;
}

const string& Core::status_string() const
{
	auto p = distadm_statuses_str.find(status);
	if (p != distadm_statuses_str.end())
		return p->second;
	else
		throw exc_error("Internal error: bad node status");
}

Json::Value& Core::save_nodes()
{
	Json::Value res;
	for(const auto& p : nodes) {
		string id = p.first;
		res[id] = state_nodes[id];
		p.second.update(res[id]);
	}
	return state_nodes = res;
}

Json::Value Core::save_filenames() const
{
	Json::Value res = Json::arrayValue;
	for(const string& s : filenames)
		res.append(s);
	return res;
}

Json::Value Core::save_commands() const
{
	Json::Value res;
	for(const auto& p : messages)
		res.append(p.as_json());
	return res;
}

void Core::create_command(Json::Value&& json, bool add_depends)
{
	if (status != NodeStatus::work && status != NodeStatus::inviter) {
		warn << _("Node is not initialized");
		return;
	}
	Msg cmd(my_id, my_node->matrix_row[nodes.node_offset(my_id)]++);
	cmd.value = move(json);
	debug << _("New command") << ' ' << cmd.value["name"];
	if (add_depends)
		for (const auto& n : nodes)
			cmd.depends[n.first] = n.second.command_to_exec;
	messages.insert(move(cmd));
	need_save = true;
}

bool Core::execute_pending_commands()
{
	bool res = false;
	debug << "Execute pending commands";
	for (;;) {
		const Msg * cmdp = command_to_exec();
		if (!cmdp)
			break;
		debug << "Exec: " << cmdp->value["name"];
		const Msg& cmd = *cmdp;
		exec(cmd);
		mark_as_executed(cmd);
		need_save = true;
		res = true;
	}
	return res;
}

const Msg * Core::command_to_exec() const
{
	vector<const Msg *> vres;
	for(const Msg& m : messages) {
		auto n = nodes.find(m.node_id);
		if (n == nodes.end())
			continue;
		if (n->second.command_to_exec != m.msg_number)
			continue;
		bool ok = true;
		for (const auto& i : m.depends) {
			auto p = nodes.find(i.first);
			if (p == nodes.end())
				continue;
			if (p->second.command_to_exec < i.second) {
				ok = false;
				break;
			}
		}
		if (ok)
			vres.push_back(&m);
	}
	if (vres.empty())
		return nullptr;
	uniform_int_distribution<size_t> dist(0, vres.size() - 1);
	return vres[dist(rd)];
}

void Core::mark_as_executed(const Msg& cmd)
{
	auto p = nodes.find(cmd.node_id);
	if (p == nodes.end())
		return;
	if (p->second.command_to_exec != cmd.msg_number) {
		warnln << "Internal error: executed command number is wrong";
		return;
	}
	p->second.command_to_exec++;
}

void Core::remove_old_commands()
{
	if (status == NodeStatus::inviter)
		return;
	set<UUID> ign_nodes;
	for (const Msg& cmd : messages)
		if (cmd.value["name"] == "delnode" && cmd.value["force"] == true)
			ign_nodes.insert(cmd.value["val"]);
	vector<size_t> level(nodes.size(), -1UL);
	size_t i = 0;
	for (const auto& n : nodes) {
		if (ign_nodes.contains(n.first))
			continue;
		for (size_t j = 0; j < n.second.matrix_row.size(); j++)
			level[j] = min(level[j], n.second.matrix_row[j]);
		level[i] = min(level[i], n.second.command_to_exec);
		i++;
	}
	map<UUID, size_t> b;
	i = 0;
	for (const auto& n : nodes)
		b[n.first] = level[i++];

	for (const Msg& m : messages)
		if (nodes.find(m.node_id) == nodes.end() || m.msg_number < b[m.node_id]) {
			try {
				before_delete_message(m);
			} catch(const exception& e) {
				warnln << _("Bad command found") << ' ' << e.what();
			}
			m.delete_flag = true;
			need_save = true;
		} else
			m.delete_flag = false;

	erase_if(messages, [&b](const Msg& m) { return m.delete_flag; });
}

void Core::del_self()
{
	status = NodeStatus::deleted;
	prog_status = ProgramStatus::exit;
	save();
	notify();
}

void Core::notify() const
{
}

vector<const Msg*> Core::commands_to_write(const set<UUID>& dest_nodes) const
{
	size_t size = nodes.size();
	vector<size_t> min_nums(size);
	for (size_t i = 0; i < size; i++)
		min_nums[i] = my_node->matrix_row[i];
	for (const UUID& id : dest_nodes) {
		const auto& n = nodes.find(id);
		if (n == nodes.end()) {
			warnln << "Internal error" << ' ' << __FILE__ << ':' << __LINE__;
			continue;
		}
		const vector<size_t>& src = n->second.matrix_row;
		for (size_t i = 0; i < size; i++)
			min_nums[i] = min(min_nums[i], src[i]);
	}
	map<UUID, size_t> id_n;
	size_t i = 0;
	for (const auto& n : nodes)
		id_n[n.first] = i++;

	vector<const Msg*> res;
	for (const Msg& m : messages) {
		const auto& n = id_n.find(m.node_id);
		if (n == id_n.end()) {
			warnln << _("Bad command found");
			continue;
		}
		if (m.msg_number < min_nums[n->second])
			res.push_back(&m);
	}

	return res;
}

void Core::after_write(OCCstream& f, const Msg& cmd) const
{
	if (cmd.value["name"] != "addfile")
		return;
	const Json::Value& jfrom = cmd.value["from"];
	const Json::Value& jto = cmd.value["to"];
	string fn = cfg.filesdir() + '/' + cmd.value["filename"].asString();
	if (jfrom.isInt64() && jto.isInt64()) {
		size_t from = jfrom.asInt64();
		size_t to = jto.asInt64();
		Fstream fr = Fstream::open(fn);
		f.write_file(fr.fd, fn, from, to);
	} else
		f.write_file(fn);
}

void Core::after_read(ICCstream& f, const Msg& cmd)
{
	if (cmd.value["name"] != "addfile")
		return;
	const Json::Value& jfrom = cmd.value["from"];
	const Json::Value& jto = cmd.value["to"];
	string fn = cfg.filesdir() + '/' + cmd.value["filename"].asString();
	fs::path dir(fn);
	dir.remove_filename();
	fs::create_directories(dir);
	if (jfrom.isInt64() && jto.isInt64()) {
		Fstream tmpfile = Fstream::open(cfg.filesdir().c_str(), O_RDWR | O_TMPFILE);
		size_t from = jfrom.asInt64();
		size_t to = jto.asInt64();
		f.read_to_tempfile(tmpfile.fd);
		lseek(tmpfile.fd, 0, SEEK_SET);
		copy_file_segment(tmpfile.fd, fn, from, to);
	} else
		f.read_to_file(fn);
}

bool has_free_space(int fd, size_t needed)
{
	needed += sizeof(Ostream::cache) + 0x100; // Add a bit more on a safe size
	struct statvfs buf;
	int ret = fstatvfs(fd, &buf);
	if (ret)
		error(errno, errno, "Error fstatvfs");
	return needed < buf.f_bsize * buf.f_bfree;
}

void Core::write_packet(const string& filename) const
{
	switch(max_proto_ver()) {
	case 1:
		write_packet_v1(filename);
		break;
	default:
		throw exc_error("Unsupported protocol version");
	}
}

void Core::write_packet_v1(const string& filename) const
{
	Fstream f1 = Fstream::create(filename);
	Ostream f2(f1);
	OCCstream f3(f2, crypt_key);
	f3.write(&protocol_version, sizeof(protocol_version));
	nodes.write(f3);
	for (const Msg& m : messages) {
		if (cfg.chk_free_space && !has_free_space(f1.fd, m.total_size()))
			break;
		f3.write_json(m.as_json());
		after_write(f3, m);
	}
	f3.write_json(Json::Value());
}

void Core::read_packet(const string& filename)
{
	Fstream f1 = Fstream::open(filename);
	Istream f2(f1);
	ICCstream f3(f2, crypt_key);
	short pv;
	f3.read(&pv, sizeof(pv));
	switch(pv) {
	case 1:
		read_packet_v1(f3);
		break;
	default:
		throw exc_error(_("Bad protocol version"));
	}
}

void Core::read_packet_v1(ICCstream& f3)
{
	Matrix mtx = Matrix::read(f3);
	if (status == NodeStatus::part_init) {
		auto r = mtx.find(my_id);
		if (r == mtx.end())
			throw exc_error(_("Node is not initialized"));
		status = NodeStatus::work;
		update_info();
	}
	need_save |= nodes.update(mtx);
	check_matrix();
	for (;;) {
		Json::Value j = f3.read_json();
		if (j.empty())
			break;
		Msg c(j);
		after_read(f3, c);
		add_cmd(move(c));
	}
}

void Core::add_cmd(Msg&& cmd)
{
	UUID id = cmd.node_id;
	ssize_t x = nodes.node_offset(cmd.node_id);
	if (x == -1) {
		warnln << _("Bad command found");
		return;
	}
	messages.insert(move(cmd));
	need_save = true;
	if (!my_node)
		return;
	size_t& c = my_node->matrix_row[x];
	while(messages.find(Msg(id, c)) != messages.end())
		c++;
}

short Core::max_proto_ver() const
{
	short res = protocol_version;
	for (auto& n : nodes)
		res = min(res, n.second.proto_ver);
	return res;
}

vector<string> Core::get_nodenames() const
{
	vector<string> res;
	for (const auto&n : nodes) {
		string id = string(n.first);
		const string name = asString(state_nodes[id], "hostname");
		res.push_back(name.empty() ? id : name);
	}
	return res;
}

vector<string> Core::get_usernames()
{
	vector<string> res;
	users.fill_nicks();
	for (const Usernames::User& u : users.users)
		res.emplace_back(u.nick);
	return res;
}

vector<string> Core::get_filenames() const
{
	vector<string> res;
	for (const string& u : filenames)
		res.emplace_back(u);
	return res;
}

string Core::nodename(const UUID& id) const
{
	string sid = string(id);
	const string name = asString(state_nodes[sid], "hostname");
	return name.empty() ? sid : name;
}

string Core::nodename(const Json::Value& src, const string& id) const
{
	const string name = asString(src, "hostname");
	return name.empty() ? id : name;
}

UUID Core::nodebyname(const string& str) const
{
	UUID id = string(str);
	auto p = nodes.find(id);
	if (p != nodes.end())
		return id;

	for (auto i = state_nodes.begin(); i != state_nodes.end(); i++) {
		const Json::Value& v = *i;
		if (v["hostname"] != str || !i.key().isString())
			continue;
		id = i.key().asString();
		p = nodes.find(id);
		if (p != nodes.end())
			return id;
	}

	return UUID::none();
}

void Core::interactive(vector<string>& cmd)
{
	int fd = create_unix_socket_client_fd();
	if (fd != -1)
		interactive_online(cmd, fd);
	else
		interactive_offline(cmd);
}

int Core::create_unix_socket_client_fd() const
{
	return ::create_unix_socket_client_fd(unix_socket_name());
}

bool Core::check_msg_cnt(const UUID& id, size_t netmsgcnt)
{
	auto p = nodes.find(id);
	if (p == nodes.end())
		return false;
	if (p->second.netmsgcnt >= netmsgcnt)
		return false;
	p->second.netmsgcnt = netmsgcnt;
	return true;

}

const Msg * Core::find_command(const MsgId& id) const
{
	auto p = messages.find(Msg(id));
	if (p == messages.end())
		return nullptr;
	return &*p;
}

void Core::delnoderecord(const UUID& id)
{
	nodes.del(id);
}

void Core::write_net_initializer(ICCstream& sin, OCCstream& sout)
{
	write_initializer_v1(sout);
	sout.flush_net();
	char ok;
	UUID new_id;
	do {
		sin.read(&new_id, sizeof(new_id));
		sin.check_hash();
		ok = nodes.find(new_id) == nodes.end();
		sout.write(&ok, sizeof(ok));
		sout.write_hash();
		sout.flush_net();
	} while (!ok);
	Json::Value cmd;
	cmd["name"] = "addnode";
	cmd["val"] = string(new_id);
	create_command(move(cmd), false);
	pending_commands();
}

void Core::update_info()
{
	if (status != NodeStatus::work && status != NodeStatus::inviter)
		return;
	static auto start = system_clock::now() - 2min;
	auto now = system_clock::now();
	std::chrono::duration<double> diff = now - start;
	if (now - start < 1min)
		return;
	update_hostname();
	update_online();
	update_antivirus();
	update_smart();
	start = now;
}

void Core::update_hostname()
{
	if (status != NodeStatus::work && status != NodeStatus::inviter)
		return;
	const string old_hostname = asString(state_nodes[string(my_id)], "hostname");
	char buf[HOST_NAME_MAX + 1];
	int res = gethostname(buf, sizeof(buf));
	if (res)
		error(errno, errno, "gethostname");
	buf[HOST_NAME_MAX] = 0;
	const string new_hostname = buf;
	if (old_hostname == new_hostname)
		return;
	Json::Value cmd;
	cmd["name"] = "sethostname";
	cmd["val"] = new_hostname;
	state_nodes[string(my_id)]["hostname"] = new_hostname;
	create_command(move(cmd), false);
}

void Core::update_online()
{
	if (status != NodeStatus::work && status != NodeStatus::inviter)
		return;
	string dt = string(locdate(time(nullptr)));
	string online = asString(state_nodes[string(my_id)], "online");
	if (online == dt)
		return;
	Json::Value cmd;
	cmd["name"] = "online";
	cmd["val"] = dt;
	state_nodes[string(my_id)]["online"] = dt;
	create_command(move(cmd), false);
}

/* Return string with:
 * 'filetime' file touched, if 'filetime' is not empty, or
 * 'filecontent' first string if 'filecontent' is not empty, or
 * first string of result of exec sh 'exec_cmd', if 'exec_cmd' is not empty, or
 * empty string */
static string get_av_string(const string& filetime, const string& filecontent, const string& exec_cmd, const string& homedir)
{
	if (!filetime.empty()) {
		struct stat st;
		int res = stat(filetime.c_str(), &st);
		if (!res)
			return loctime(st.st_mtim.tv_sec);
	}
	if (!filecontent.empty()) {
		string res;
		ifstream(filecontent) >> res;
		vector<string_view> v = split(res, false, '\n');
		if (!v.empty())
			return string(v[0]);
	}
	if (!exec_cmd.empty()) {
		vector<string> params;
		params.push_back("sh");
		params.push_back("-c");
		params.push_back(move(exec_cmd));
		string out;
		if (exec_prog_output(params, out, homedir)) {
			vector<string_view> v = split(out, false, '\n');
			if (!v.empty())
				return string(v[0]);
		}
	}
	return "";
}

void Core::update_antivirus()
{
	string updated;
	string scanned;
	string s_my_id(my_id);
	bool found = false;

	scanned = get_av_string(cfg.av_scan_date_file_date, cfg.av_scan_date_file, cfg.av_scan_date_exec, cfg.filesdir());
	updated = get_av_string(cfg.av_update_date_file_date, cfg.av_update_date_file, cfg.av_update_date_exec, cfg.filesdir());
	if (updated.empty())
		updated = asString(state_nodes[s_my_id]["antivirus"], "updated");
	struct stat st;
	int res = stat(cfg.av_results_file.c_str(), &st);
	if (!res)
		found = st.st_size;

	Json::Value& rec = state_nodes[string(my_id)]["antivirus"];
	if (rec["updated"] == updated && rec["scanned"] == scanned && rec["found"] == found)
		return;
	Json::Value cmd;
	cmd["name"] = "antivirus";
	cmd["updated"] = updated;
	cmd["scanned"] = scanned;
	cmd["found"] = found;
	state_nodes[s_my_id]["antivirus"] = cmd;
	create_command(move(cmd), false);
}

void Core::update_smart()
{
	string str;
	bool ok = exec_prog_output({"smartctl", "--scan"}, str);
	if (!ok) {
		warn << _("smartctl not found");
		return;
	}
	vector<string_view> lines = split(str, false, '\n');
	for (const string_view& line : lines) {
		auto items = split2(line);
		string dev(items.first);
		int status = 0;
		ok = exec_prog({"smartctl", "-Hq", "silent", dev}, &status);
		if (ok || !(status & 0x10))
			continue;

		Json::Value& rec = state_nodes[string(my_id)]["smart"];
		if (rec == false)
			return;
		Json::Value cmd;
		cmd["name"] = "smart";
		cmd["status"] = false;
		create_command(move(cmd), false);
		return;
	}
	Json::Value& rec = state_nodes[string(my_id)]["smart"];
	if (rec == true)
		return;
	Json::Value cmd;
	cmd["name"] = "smart";
	cmd["status"] = true;
	create_command(move(cmd), false);
}

void Core::pending_commands()
{
	execute_pending_commands();
	remove_old_commands();
	for (;;) {
		bool ret = execute_pending_commands();
		if (!ret)
				break;
		remove_old_commands();
	}
}

void Core::update_node_hash(const UUID& id, const SHA256& hash)
{
	auto p = nodes.find(id);
	if (p != nodes.end())
		p->second.hash = hash;
}

bool Core::node_known(const UUID& id) const
{
	return nodes.find(id) != nodes.end();
}

void Core::write_av(int fd)
{
	string s;
	const Json::Value& src = state_nodes;
	for (auto i = src.begin(); i != src.end(); i++) {
		const Json::Value& antivir = (*i)["antivirus"];
		s = move(s) + nodename(*i, i.key().asString())
			+ '\t' + asString(*i, "online")
			+ '\t' + asString(antivir, "updated")
			+ '\t' + asString(antivir, "scanned")
			+ '\t' + asString(antivir, "found")
			+ '\n';
	}
	writefile(fd, s.data(), s.size(), "unix low priveleged socket");
}

void Core::update_matrix(const Matrix& m)
{
	need_save |= nodes.update(m);
}

void Core::cwd() const
{
	string newdir = cfg.filesdir();
	int res = chdir(newdir.c_str());
	if (res)
		error(0, errno, "chdir %s", newdir.c_str());
}

bool Core::node_alive(const UUID& id) const
{
	const Json::Value& log = state["log"];
	string str_id = string(id);
	for (auto i = log.begin(); i != log.end(); i++) {
		if ((*i)["type"] != "deadnode")
			continue;
		if ((*i)["deaduuid"] == str_id)
			return false;
	}
	return true;
}

void Core::exec_smart(const Msg& cmd)
{
	state_nodes[string(cmd.node_id)]["smart"] = move(cmd.value["status"]);
}

void Core::check_matrix()
{
	if (!my_node)
		return;
	vector<size_t> minrow = my_node->matrix_row;
	for (auto& node : nodes) {
		for (size_t i = 0; i < minrow.size(); i++)
			minrow[i] = min(minrow[i], node.second.matrix_row[i]);
	}
	size_t i = -1UL;
	for (auto& node : nodes) {
		i++;
		for (size_t j = node.second.command_to_exec; j < my_node->matrix_row[i]; j++) {
			if (messages.find(Msg(node.first, j)) != messages.end())
				continue;
			if (j >= minrow[i] && my_node != &node.second) {
				my_node->matrix_row[i] = j;
				break;
			}
			Json::Value json;
			json["name"] = "BAD MESSAGE";
			Msg cmd(node.first, j);
			cmd.value = move(json);
			messages.insert(move(cmd));
			need_save = true;
		}
	}
}
