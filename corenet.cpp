#include "corenet.h"
#include <error.h>
#include <bsd/string.h>
#include <sys/stat.h>
#include <libintl.h>
#include <iostream>
#include <cassert>
#include "main.h"
#include "utils.h"
#include "showdebug.h"
#define _(STRING) gettext(STRING)

using std::cout;
using std::endl;
using std::vector;
using std::string;
using std::ostringstream;
using std::uniform_int_distribution;

CoreNet::CoreNet(Config& c) : CoreBase(c), Core(c)
{
}

UDPcrypted CoreNet::broadcast_helo_v1()
{
	UDPcrypted buf;
	buf.msg.v1.version = 1;
	buf.msg.v1.counter = my_node ? my_node->netmsgcnt++ : 0;
	buf.msg.v1.group_id = group_id;
	buf.msg.v1.node_id = my_id;
	buf.msg.v1.node_hash = calc_my_hash();
	buf.msg.v1.message =
		status == NodeStatus::uninitialized ? UDPmessage_v1::not_initialized : UDPmessage_v1::helo;
	return buf;
}

in6_addr CoreNet::ipv6_group() const
{
	in6_addr res;
	res.s6_addr[0] = 0xff;
	res.s6_addr[1] = 0x12;
	memcpy(res.s6_addr + 2, &group_id, sizeof(res) - 2);
	return res;
}

void CoreNet::broadcast_helo()
{
	debug << "Broadcast helo (" << cfg.listen.size() << " interfaces)";
	UDPcrypted buf;
	size_t size;
	switch (max_proto_ver()) {
	case 1:
		buf = broadcast_helo_v1();
		size = sizeof(Nonce) + sizeof(UDPmessage_v1);
		break;
	default:
		error(1, 0, _("Bad protocol version"));
	}
	buf.encrypt(crypt_key);
	for (const IFName& addr : cfg.listen)
		broadcast(addr.dev, buf, size);
	save(true);
}

void CoreNet::broadcast_bye()
{
	debug << "Broadcast bye";
	UDPcrypted buf;
	size_t size;
	switch (max_proto_ver()) {
	case 1:
		buf = broadcast_helo_v1();
		buf.msg.v1.message = UDPmessage_v1::Command::bye;
		size = sizeof(Nonce) + sizeof(UDPmessage_v1);
		break;
	default:
		error(1, 0, _("Bad protocol version"));
	}
	buf.encrypt(crypt_key);
	for (const IFName& addr : cfg.listen)
		broadcast(addr.dev, buf, size);
	save(true);
}

void CoreNet::broadcast(const char * ifn, const UDPcrypted& buf, size_t size) const
{
	int s;
	if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		error(errno, errno, "socket UDP error");
	ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifn, sizeof(ifr.ifr_name));
	int res = setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
	if (res)
		return;
	int broadcastPermission = 1;
	if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (void *) &broadcastPermission, sizeof(broadcastPermission)) < 0)
		error(errno, errno, "setsockopt UDP error");
	struct sockaddr_in6 si;
	memset(&si, 0, sizeof(si));
	si.sin6_family = AF_INET6;
	si.sin6_addr = ipv6_group();
	si.sin6_port = htons(cfg.port);
	sendto(s, &buf, size, 0, (struct sockaddr *)&si, sizeof(si));
	close(s);
}

void CoreNet::update_my_hash()
{
	if (!my_node)
		return;
	SHA256 old_hash = my_node->hash;
	SHA256 new_hash = calc_my_hash();
	if (old_hash != new_hash) {
		debug << "My hash changed, " << new_hash.partial();
		my_node->hash = new_hash;
		broadcast_helo();
	}
}

void CoreNet::pending_commands()
{
	Core::pending_commands();
	update_my_hash();
}

void CoreNet::add_node(const UUID& id, const IN6ad& ad, const SHA256& hash, bool initialized)
{
	debug << "Add node";
	auto p = nodes.find(id);
	if (p != nodes.end()) {
		p->second.hash = hash;
		p->second.initialized = initialized;
		ips[ad] = &p->second;
	} else
		ips[ad] = nullptr;
	print_hashes();
}

void CoreNet::del_addr(const IN6ad& ad)
{
	auto p = ips.find(ad);
	if (p != ips.end())
		ips.erase(p);
}

bool CoreNet::need_communicate(const Node& n) const
{
	return my_node && (n.matrix_row != my_node->matrix_row || (n.hash && n.hash != my_node->hash));
}

IN6ad CoreNet::addr_to_connect(bool server_busy, const UUID& conn_id)
{
	print_hashes();
	vector<IN6ad> addrs;
	for(auto& n : nodes) {
		if (server_busy && n.first == conn_id)
			n.second.interesting = Node::Intersting::no;
		else
			n.second.interesting = Node::Intersting::unknown;
	}
	for (auto& n : ips) {
		if (!n.second) {
			addrs.push_back(n.first);
			continue;
		}
		if (n.second->interesting == Node::Intersting::unknown) {
			if ((status == NodeStatus::uninitialized || status == NodeStatus::part_init) && !n.second->initialized)
				n.second->interesting = Node::Intersting::no;
			else if (need_communicate(*n.second))
				n.second->interesting = Node::Intersting::yes;
			else
				n.second->interesting = Node::Intersting::no;
		}
		if (n.second->interesting == Node::Intersting::yes)
			addrs.push_back(n.first);
	}
	if (addrs.empty())
		return IN6ad::none();
	uniform_int_distribution<size_t> dist(0, addrs.size() - 1);
	return addrs[dist(rd)];

	IN6ad res = addrs[dist(rd)];
	debug << res.name();
	if (!ips[res]) {
		debug << "new node";
	} else {
		const Node& n = *ips[res];
		if (n.hash && n.hash != my_node->hash)
			debug << "different hash, " << my_node->hash.partial() << '/' << n.hash.partial();
		else
			debug << "different row";
	}
	return res;
}

TCPHeloMsg CoreNet::get_tcp_helo() const
{
	TCPHeloMsg res;
	res.node_id = my_id;
	res.node_hash = my_node ? my_node->hash : SHA256();
	res.msg_cnt = my_node ? my_node->netmsgcnt++ : 0;
	res.version = max_proto_ver();
	res.initialized = !need_initialize();
	return res;
}

MsgRequest CoreNet::request_message_from_node(const UUID& remote_node) const
{
	MsgRequest res;
	if (!my_node)
		return res;
	res.node_id.clear();
	res.msg_number = 0;
	auto n = nodes.find(remote_node);
	assert(n != nodes.end());
	vector<UUID> uuids;
	uuids.reserve(my_node->matrix_row.size());
	for(auto i : nodes)
		uuids.push_back(i.first);
	for (size_t j = 0; j < my_node->matrix_row.size(); j++)
		if (n->second.matrix_row[j] > my_node->matrix_row[j]) {
			if (downloading_msgs.contains(MsgId(uuids[j], my_node->matrix_row[j])))
				continue;
			res.node_id = uuids[j];
			res.msg_number = my_node->matrix_row[j];
			break;
		}
	return res;
}

void CoreNet::delnoderecord(const UUID& id)
{
	const auto p = nodes.find(id);
	if (p != nodes.end())
		erase_if(ips, [&p](const auto& x) { return x.second == &p->second; });
	Core::delnoderecord(id);
}

void CoreNet::print_hashes() const
{
	if (!print_debug)
		return;
	ostringstream str;
	size_t s = ips.size();
	for (auto& i : ips) {
		str << s << '.' << i.first.name() << ':';
		if (i.second)
			str << i.second->hash.partial();
		str << '\n';
	}
	cout << str.str();
}
