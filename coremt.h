#pragma once
#include <mutex>
#include "corenet.h"

// Only purpose of this class is to prevent multithread runs of base class

struct CoreMT : private CoreNet {
	CoreMT(Config&);

	void load();
	void save(bool force = false);
	void pending_commands();
	void broadcast_helo();
	void broadcast_bye();
	in6_addr ipv6_group() const;
	IN6ad addr_to_connect(bool server_busy, const UUID& conn_id);
	void read_net_initializer(ICCstream&, OCCstream&, const UUID& remote_id);
	void write_net_initializer(ICCstream&, OCCstream&);
	void update_matrix(const Matrix&);
	MsgRequest request_message_from_node(const UUID& remote_node) const;
	void add_msg_request(const MsgId&);
	void del_msg_request(const MsgId&);
	void after_read(ICCstream&, const Msg&);
	void after_write(OCCstream&, const Msg&);
	void add_cmd(Msg&& cmd);
	void write_matrix(OCCstream&) const;
	const Msg * find_command(const MsgId&) const;
	void update_my_hash();
	bool interactive_exec(const std::string&, std::ostream&); // return false for disconnect
protected:
	bool check_msg_cnt(const UUID&, size_t);
	void add_node(const UUID&, const IN6ad&, const SHA256& hash, bool initialized);
	void update_node_hash(const UUID&, const SHA256&);
	void del_addr(const IN6ad&);
	TCPHeloMsg get_tcp_helo() const;
	void remove_old_commands();
	void update_info();
	bool node_known(const UUID&) const;
	void write_av(int fd);
	void cwd() const;
	bool node_alive(const UUID&) const;
	void del_self();
private:
	// Used to prevent unsafe multithread access to core
	mutable std::mutex mtx;
};
