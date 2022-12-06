#pragma once
#include <atomic>
#include "core.h"

struct TCPHeloMsg {
	UUID node_id;
	SHA256 node_hash;
	size_t msg_cnt;
	short version;
	bool initialized;
};

// Atomically keeps downloading_msgs flag (RAII)
struct MsgRequest : MsgId {
	MsgRequest() = default;
	MsgRequest(const UUID&, size_t);
	MsgRequest(const MsgRequest&) = delete;
	MsgRequest(MsgRequest&&);
	~MsgRequest();
	MsgRequest& operator=(const MsgRequest&) = delete;
	MsgRequest& operator=(MsgRequest&&);
	operator bool() const;
};

struct CoreNet : Core {
	CoreNet(Config& c);

protected:
	// Get UPD message to send
	UDPcrypted broadcast_helo_v1();

	// return ipv6 group based no group-id
	in6_addr ipv6_group() const;

	// broadcast udp-helo message on specified network interface
	void broadcast(const char * ifname, const UDPcrypted&, size_t) const;

	// broadcast udp-helo message on available network interfaces
	void broadcast_helo();
	void broadcast_bye();

	void update_my_hash();

	// Add info about node based on UPD message
	void add_node(const UUID& id, const IN6ad& ad, const SHA256& hash, bool initialized);

	// Del network address
	void del_addr(const IN6ad& ad);

	// Return true if there is some reason to communicate with parameter node
	bool need_communicate(const Node& n) const;

	// return network address to connect by client or empry address
	IN6ad addr_to_connect(bool server_busy, const UUID& conn_id);

	TCPHeloMsg get_tcp_helo() const;

	void delnoderecord(const UUID&) override;

	MsgRequest request_message_from_node(const UUID& remote_node) const;

	// Execute new commands, remove old, update hash, send UPD
	void pending_commands();

	void print_hashes() const;

	// Network address and it's node
	std::map<IN6ad, Node*> ips;

	std::set<MsgId> downloading_msgs;

};
