#include "utils.h"
#include "coremt.h"

using std::mutex;
using std::move;
using std::string;
using std::ostream;
using std::vector;
typedef std::lock_guard<mutex> lock;

/* Every function here should contain only two lines" lock mutex and call
 * function from base class */

CoreMT::CoreMT(Config&c) : CoreBase(c), CoreNet(c)
{
}

void CoreMT::load()
{
	lock lck(mtx);
	Core::load();
}

void CoreMT::save(bool force)
{
	lock lck(mtx);
	Core::save(force);
}

void CoreMT::pending_commands()
{
	lock lck(mtx);
	CoreNet::pending_commands();
}

void CoreMT::broadcast_helo()
{
	lock lck(mtx);
	CoreNet::broadcast_helo();
}

void CoreMT::broadcast_bye()
{
	lock lck(mtx);
	CoreNet::broadcast_bye();
}

in6_addr CoreMT::ipv6_group() const
{
	lock lck(mtx);
	return CoreNet::ipv6_group();
}

IN6ad CoreMT::addr_to_connect(bool server_busy, const UUID& conn_id)
{
	lock lck(mtx);
	return CoreNet::addr_to_connect(server_busy, conn_id);
}

bool CoreMT::check_msg_cnt(const UUID& id, size_t cnt)
{
	lock lck(mtx);
	return Core::check_msg_cnt(id, cnt);
}

void CoreMT::add_node(const UUID& id, const IN6ad& ad, const SHA256& hash, bool initialized)
{
	lock lck(mtx);
	return CoreNet::add_node(id, ad, hash, initialized);
}

void CoreMT::del_addr(const IN6ad& ad)
{
	lock lck(mtx);
	return CoreNet::del_addr(ad);
}

TCPHeloMsg CoreMT::get_tcp_helo() const
{
	lock lck(mtx);
	return CoreNet::get_tcp_helo();
}

void CoreMT::read_net_initializer(ICCstream& fin, OCCstream& fout, const UUID& remote_id)
{
	lock lck(mtx);
	CoreNet::read_net_initializer(fin, fout, remote_id);
}

void CoreMT::write_net_initializer(ICCstream& fin, OCCstream& fout)
{
	lock lck(mtx);
	CoreNet::write_net_initializer(fin, fout);
}

void CoreMT::update_matrix(const Matrix& m)
{
	lock lck(mtx);
	Core::update_matrix(m);
}

MsgRequest CoreMT::request_message_from_node(const UUID& remote_node) const
{
	lock lck(mtx);
	return CoreNet::request_message_from_node(remote_node);
}

void CoreMT::add_msg_request(const MsgId& cmd)
{
	lock lck(mtx);
	downloading_msgs.insert(cmd);
}

void CoreMT::del_msg_request(const MsgId& cmd)
{
	lock lck(mtx);
	downloading_msgs.erase(cmd);
}

void CoreMT::after_read(ICCstream& f, const Msg& cmd)
{
	lock lck(mtx);
	CoreNet::after_read(f, cmd);
}

void CoreMT::after_write(OCCstream& f, const Msg& cmd)
{
	lock lck(mtx);
	CoreNet::after_write(f, cmd);
}

void CoreMT::add_cmd(Msg&& cmd)
{
	lock lck(mtx);
	CoreNet::add_cmd(move(cmd));
}

void CoreMT::write_matrix(OCCstream& f) const
{
	lock lck(mtx);
	nodes.write(f);
}

const Msg * CoreMT::find_command(const MsgId& id) const
{
	lock lck(mtx);
	return CoreNet::find_command(id);
}

void CoreMT::remove_old_commands()
{
	lock lck(mtx);
	CoreNet::remove_old_commands();
}

void CoreMT::update_my_hash()
{
	lock lck(mtx);
	CoreNet::update_my_hash();
}

bool CoreMT::interactive_exec(const string& cmd, ostream& os)
{
	lock lck(mtx);
	return CoreNet::interactive_exec(splitq(cmd), os);
}

void CoreMT::update_info()
{
	lock lck(mtx);
	CoreNet::update_info();
}

void CoreMT::update_node_hash(const UUID& id, const SHA256& hash)
{
	lock lck(mtx);
	CoreNet::update_node_hash(id, hash);
}

bool CoreMT::node_known(const UUID& id) const
{
	lock lck(mtx);
	return CoreNet::node_known(id);
}

void CoreMT::write_av(int fd)
{
	lock lck(mtx);
	Core::write_av(fd);
}

void CoreMT::cwd() const
{
	lock lck(mtx);
	Core::cwd();
}

bool CoreMT::node_alive(const UUID& id) const
{
	lock lck(mtx);
	return Core::node_alive(id);
}

void CoreMT::del_self()
{
	lock lck(mtx);
	Core::del_self();
}
