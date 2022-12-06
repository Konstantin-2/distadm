#include "daemon.h"
#include <error.h>
#include <errno.h>
#include <signal.h>
#include <ifaddrs.h>
#include <bsd/string.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <libintl.h>
#include <iostream>
#include <cassert>
#include "alarmer.h"
#include "warn.h"
#include "showdebug.h"
#include "main.h"
#include "exc_error.h"
#include "utils_iface.h"
#include "utils.h"
#define _(STRING) gettext(STRING)

using std::cout;
using std::endl;
using std::min;
using std::move;
using std::flush;
using std::map;
using std::vector;
using std::mutex;
using std::string;
using std::thread;
using std::exception;
using std::uniform_int_distribution;
typedef std::lock_guard<mutex> lock;
typedef std::unique_lock<mutex> ulock;

Daemon * dmn;

static IFName get_ifname(int sock_fd)
{
	struct sockaddr_in6 addr;
	struct ifaddrs* ifaddr;
	struct ifaddrs* ifa;
	socklen_t addr_len = sizeof (addr);
	int x = getsockname(sock_fd, (struct sockaddr*)&addr, &addr_len);
	if (x == -1)
		error(errno, errno, "getsockname");
	x = getifaddrs(&ifaddr);
	if (x == -1)
		error(errno, errno, "getifaddrs");

	IFName res;
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
		if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6) {
			struct sockaddr_in6* inaddr = (struct sockaddr_in6*)ifa->ifa_addr;
			if (!memcmp(&inaddr->sin6_addr, &addr.sin6_addr, sizeof(addr.sin6_addr)) && ifa->ifa_name) {
				res = ifa->ifa_name;
				break;
			}
		}
	freeifaddrs(ifaddr);
	return res;
}

// Return network interface names and theri indices
static map<IFName, unsigned> if_indices()
{
	map<IFName, unsigned> res;
	struct if_nameindex * ifn = if_nameindex();
	if (!ifn)
		error(errno, errno, "if_nameindex");
	struct if_nameindex * i = ifn;
	while (i->if_name) {
		res[i->if_name] = i->if_index;
		i++;
	}
	if_freenameindex(ifn);
	return res;
}

// ===== MsgRequest =====

MsgRequest::MsgRequest(const UUID& id, size_t num) : MsgId(id, num)
{
	dmn->add_msg_request(*this);
}

MsgRequest::~MsgRequest()
{
	dmn->del_msg_request(*this);
}

MsgRequest::operator bool() const
{
	return node_id;
}

// ===== Daemon =====

TCPheloNB::TCPheloNB(const TCPHeloMsg& hm, const IN6ad& a, CryptKey& k) :
	ad(a), key(&k)
{
	p_out.nonce.random(rnd);
	p_out.random.random(rnd);
	p_out.msg = hm;
	size_t trash_size = p_out.trash_size = uniform_int_distribution<size_t>(0UL, sizeof(TCPheloCrypted::trash))(rd);
	SHA1::write(&p_out, offsetof(TCPheloCrypted, trash), p_out.hash);
	::encrypt(&p_out.random, offsetof(TCPheloCrypted, trash) - offsetof(TCPheloCrypted, random), k, p_out.nonce);
	size_to_write = offsetof(TCPheloCrypted, trash) + trash_size;
}

void TCPheloNB::on_read(pollfd& pfd)
{
	size_t to_read = offsetof(TCPheloCrypted, trash);
	if (read_size >= offsetof(TCPheloCrypted, trash))
		to_read += p_in.trash_size;
	ssize_t x = read(pfd.fd, (char *)&p_in + read_size, to_read - read_size);
	if (x > 0) {
		read_size += x;
		if (read_size < to_read)
			return;
		if (!decrypted_read) {
			::decrypt(&p_in.random, sizeof(TCPheloCrypted) - offsetof(TCPheloCrypted, trash) - offsetof(TCPheloCrypted, random), *key, p_in.nonce);
			decrypted_read = true;
			if (p_in.trash_size > sizeof(p_in.trash))
				pfd.revents |= POLLERR;
			if (p_in.trash_size)
				return;
		}
		if (SHA1::check(&p_in, offsetof(TCPheloCrypted, trash), p_in.hash)) {
			pfd.events &= ~POLLIN;
			complete = true;
		} else
			pfd.revents |= POLLERR;
	} else if (!x)
		pfd.revents |= POLLHUP;
	else if (errno != EAGAIN && errno != EINTR)
		pfd.revents |= POLLERR;
}

void TCPheloNB::on_write(pollfd& pfd)
{
	size_t to_write = size_to_write - written_size;
	ssize_t x = write(pfd.fd, (char *)&p_out + written_size, to_write);
	if (x > 0) {
		written_size += x;
		if (written_size != to_write)
			return;
		pfd.events &= ~POLLOUT;
		complete = true;
		fsync(pfd.fd);
	} else if (!x)
		pfd.revents |= POLLHUP;
	else if (errno != EAGAIN && errno != EINTR)
		pfd.revents |= POLLERR;
}

// ===== TCPsession_v1 =====

TCPsession_v1::TCPsession_v1(TCPconn& c) :
	conn(c),
	fcout(c.fout, dmn->crypt_key),
	fcin(c.fin, dmn->crypt_key)
{
}

TCPsession_v1::~TCPsession_v1()
{
	try {
		fcout.close();
		fcin.close();
	} catch (const exception& exc) {
		warn << exc.what();
	}
}

void TCPsession_v1::initialize_me()
{
	dmn->read_net_initializer(fcin, fcout, remote_id);
}

void TCPsession_v1::initialize_that()
{
	dmn->write_net_initializer(fcin, fcout);
}

bool TCPsession_v1::initialize()
{
	if (dmn->need_initialize() && !remote_initialized)
		throw exc_error();
	if (dmn->need_initialize()) {
		initialize_me();
		return true;
	} else if (!remote_initialized) {
		initialize_that();
		return true;
	}
	return false;
}

void TCPsession_v1::client_main()
{
	dmn->update_matrix(Matrix::read(fcin));
	while (prog_status == ProgramStatus::work) {
		MsgRequest req = dmn->request_message_from_node(remote_id);
		debug << "Request message " << string(req.node_id) << '/' << req.msg_number;
		fcout.write(&req, sizeof(req));
		fcout.write_hash();
		fcout.flush_net();
		if (!req)
			break;

		Msg c(fcin.read_json());
		if (c.node_id != req.node_id || c.msg_number != req.msg_number)
			throw exc_error("Bad responce");
		fcin.check_hash();
		dmn->after_read(fcin, c);
		dmn->add_cmd(move(c));
	}
}

void TCPsession_v1::server_main()
{
	dmn->write_matrix(fcout);
	fcout.flush_net();
	while (prog_status == ProgramStatus::work) {
		MsgRequest req;
		fcin.read(&req, sizeof(req));
		fcin.check_hash();
		if (!req)
			break;
		debug << "Asked for command uuid=" << string(req.node_id) << ", N= " << req.msg_number;
		const Msg * c = dmn->find_command(req);
		if (c == nullptr)
			throw exc_error("Requested command not found");
		debug << "Send command uuid=" << string(c->node_id) << ", N= " << c->msg_number;
		fcout.write_json(c->as_json());
		fcout.write_hash();
		dmn->after_write(fcout, *c);
		fcout.flush_net();
	}
}

bool TCPsession_v1::xchg_bool(bool b)
{
	char ch = b;
	fcout.write(&ch, sizeof(ch));
	fcout.write_hash();
	fcout.flush_net();
	fcin.read(&ch, sizeof(ch));
	fcin.check_hash();
	return ch;
}


// ===== ThreadCtrl =====

ThreadCtrl::ThreadCtrl(ThreadCV * cv, void (Daemon::* f)(ThreadCV *)) :
	func(f),
	tcv(cv),
	thr(&ThreadCtrl::run, this)
{
	tcv->done = false;
}

ThreadCtrl::~ThreadCtrl()
{
	if (!thr.joinable())
		return;
	alarm_thread(thr_id);
	while (!tcv->done) {
		tcv->cv.notify_all();
		nsleep();
	}
	thr.join();
	alarm_stop(thr_id);
}

void ThreadCtrl::run()
{
	thr_id = pthread_self();
	(dmn->*func)(tcv);
}

// ===== UnixSession =====

UnixSession::UnixSession(int conn_fd) :
	fd(conn_fd)
{
	thr = thread(&UnixSession::run, this);
}

UnixSession::~UnixSession()
{
	stop = true;
	alarm_thread(thr_id);
	thr.join();
	alarm_stop(thr_id);
}

void UnixSession::run()
{
	thr_id = pthread_self();
	try {
		__gnu_cxx::stdio_filebuf<char> filebuf_out(fd, std::ios::out);
		std::iostream os(&filebuf_out);
		warn_thread_local = &os;
		string line;
		bool ok = true;
		while (ok && !stop && getline(line) && prog_status == ProgramStatus::work) {
			ok = dmn->interactive_exec(line, os);
			dmn->pending_commands();
			dmn->notify();
			if (ok)
				os << EOT << flush;
		}
		dmn->save();
		warn_thread_local = nullptr;
	} catch (const exception& exc) {
		warn << exc.what();
	}
	stop = true;
}

bool UnixSession::getline(string& res) const
{
	if(fd == -1)
		return false;
	res.clear();
	char buf[0x1000];
	bool eof = false;
	while (!stop) {
		ssize_t x = read(fd, buf, sizeof(buf));
		if (errno == EINTR)
			continue;
		if (x <= 0) {
			eof = true;
			break;
		}
		res.append(buf, x);
		if (buf[x - 1] == '\n') {
			res.pop_back();
			break;
		}
	}
	return !eof || !res.empty();
}

// ===== Daemon =====

Daemon::Daemon(Config& c) : CoreBase(c), CoreMT(c), server_busy(false)
{
	dmn = this;
	thr_id = pthread_self();
}

void Daemon::notify() const
{
	alarm_thread(thr_id);
}

int Daemon::open_udp_listen_socket(const char * if_name, unsigned if_idx) const
{
	int s = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
	if (s == -1)
		error(errno, errno, "socket UDP error");

	struct ipv6_mreq mreq;
	mreq.ipv6mr_multiaddr = ipv6_group();
	mreq.ipv6mr_interface = if_idx;
	setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq));
	int res;

	if (if_name) {
		ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		strlcpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));
		res = setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
		if (res) {
			close(s);
			error(0, errno, "setsockopt UDP error, bind to deivce %s", if_name);
			return -1;
		}
	}

	int reuse = true;
	res = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	if (res)
		error(errno, errno, "setsockopt UDP error");
	sockaddr_in6 si;
	memset(&si, 0, sizeof(si));
	si.sin6_family = AF_INET6;
	si.sin6_port = htons(cfg.port);
	si.sin6_addr = in6addr_any;
	res = bind(s, (struct sockaddr*)&si, sizeof(si));
	if (res < 0) {
		close(s);
		error(0, errno, "bind UDP error");
		return -1;
	}
	return s;
}

int Daemon::open_tcp_listen_socket(const char * if_name, bool dev_specified) const
{
	int s = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (s == -1)
		error(errno, errno, "socket TCP error");

	if (dev_specified) {
		ifreq ifr;
		memset(&ifr, 0, sizeof(ifr));
		strlcpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));
		int res = setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
		if (res) {
			close(s);
			error(0, errno, "setsockopt TCP error, bind to deivce %s", if_name);
			return -1;
		}
	}

	int reuse = true;
	int res = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	if (res)
		error(errno, errno, "setsockopt TCP error");
	sockaddr_in6 si;
	memset(&si, 0, sizeof(si));
	si.sin6_family = AF_INET6;
	si.sin6_port = htons(cfg.port);
	si.sin6_addr = in6addr_any;
	res = bind(s, (struct sockaddr*)&si, sizeof(si));
	if (res < 0) {
		close(s);
		error(0, errno, "bind TCP error");
		return -1;
	}
	res = ::listen(s, 0x10);
	return s;
}

void Daemon::recv_udp_v1(const UDPmessage_v1& msg, const IFName& if_name, sockaddr_in6& sa)
{
	if (msg.group_id != group_id || msg.node_id == my_id) {
		debug << "ignore this UDP";
		return;
	}
	if (!check_msg_cnt(msg.node_id, msg.counter))
		debug << "Possible UDP spoofing detected";
	IN6ad ad(sa.sin6_addr, if_name);
	switch(msg.message) {
	case UDPmessage_v1::Command::not_initialized:
		if (need_initialize())
			break;
	case UDPmessage_v1::Command::helo:
		add_node(msg.node_id, ad, msg.node_hash, msg.message == UDPmessage_v1::Command::helo);
		client.cv.notify_one();
		break;
	case UDPmessage_v1::Command::bye:
		del_addr(ad);
		break;
	default:
		break;
	}
}

void Daemon::recv_udp(int fd, const IFName& if_name)
{
	UDPcrypted buf;
	sockaddr_storage ss;
	memset(&ss, 0, sizeof(ss));
	socklen_t sl = sizeof(ss);
	ssize_t x = recvfrom(fd, &buf, sizeof(buf), 0, (struct sockaddr *) &ss, &sl);
	if (ss.ss_family != AF_INET6)
		return;

	char addrname[INET6_ADDRSTRLEN] { '\0' };
	sockaddr_in6& sa = *(sockaddr_in6*)&ss;
	inet_ntop(ss.ss_family, &sa.sin6_addr, addrname, sizeof(addrname));
	debug << "received UDP from " << addrname;

	buf.decrypt(crypt_key);
	switch (buf.msg.base.version) {
	case 1:
		if (x == sizeof(Nonce) + sizeof(UDPmessage_v1))
			recv_udp_v1(buf.msg.v1, if_name, sa);
		else
			warn << "Bad UDP";
		break;
	default:
		warn << "Unknown UDP";
		return;
	}
}

void Daemon::client_act()
{
	debug << "Client act";
	while(prog_status == ProgramStatus::work) {
		unsigned timeout = uniform_int_distribution<unsigned>(1, 8)(rd);
		nsleep(timeout);
		IN6ad ad = addr_to_connect(server_busy, serv_helo.node_id);
		if (!ad || prog_status != ProgramStatus::work)
			break;

		try {
			debug << "Try connect to " << ad.name();
			TCPconn conn(ad, cfg.port);
			TCPHeloMsg helo = client_connect(conn);
			update_node_hash(helo.node_id, helo.node_hash);

			switch (helo.version) {
			case 1: {
					TCPsession_v1 sess(conn);
					sess.remote_id = helo.node_id;
					sess.remote_initialized = helo.initialized;
					if (sess.initialize())
						break;
					if (!sess.xchg_bool(node_alive(helo.node_id)) && status == NodeStatus::deleting) {
						del_self();
						break;
					}
					if (!node_known(helo.node_id)) {
						warn << "Unknown remote node " << string(helo.node_id);
						del_addr(ad);
						break;
					}
					sess.client_main();
					sess.server_main();
					sess.client_main();
				}
				break;
			default:
				throw exc_error("Bad protocol version");
			}
		} catch (const exception& exc) {
			del_addr(ad);
			warn << "Server disconnect: " << exc.what();
		}

		if (prog_status != ProgramStatus::work)
			return;
		try {
			pending_commands();
			save();
		} catch (const exception& exc) {
			warn << exc.what();
		}
		debug << "Client complete";
	}
}

void Daemon::client_main_loop(ThreadCV * t)
{
	ulock lk(t->mtx);
	while (prog_status == ProgramStatus::work) {
		t->cv.wait(lk);
		if (prog_status != ProgramStatus::work)
			break;
		client_act();
	}
	t->done = true;
}

void Daemon::server_act()
{
	try {
		TCPconn conn(pass_server_fd, pass_server_fname);
		update_node_hash(serv_helo.node_id, serv_helo.node_hash);
		switch(serv_helo.version) {
		case 1: {
				TCPsession_v1 sess(conn);
				sess.remote_id = serv_helo.node_id;
				sess.remote_initialized = serv_helo.initialized;
				if (sess.initialize())
					break;
				if (!sess.xchg_bool(node_alive(serv_helo.node_id)) && status == NodeStatus::deleting) {
					del_self();
					break;
				}
				if (!node_known(serv_helo.node_id)) {
					warn << "Unknown remote node " << string(serv_helo.node_id);
					break;
				}
				sess.server_main();
				sess.client_main();
				sess.server_main();
			}
			break;
		default:
			throw exc_error(_("Bad protocol version"));
		}
	} catch (const exception& exc) {
		warn << "Client disconnected: " << exc.what();
	}

	if (prog_status != ProgramStatus::work)
		return;
	try {
		pending_commands();
		save();
	} catch (const exception& exc) {
		warn << exc.what();
	}
	server_busy = false;
	debug << "Server complete";
}

bool Daemon::serve(const TCPHeloMsg& p_in, const IN6ad& ad, int fd)
{
	add_node(p_in.node_id, ad, p_in.node_hash, p_in.initialized);
	bool free = false;
	bool ok = server_busy.compare_exchange_weak(free, true);
	if (!ok) {
		debug << "I'm busy, disconnect";
		closefile(fd, ad.name());
		client.cv.notify_one();
		return false;
	}
	if (!check_msg_cnt(p_in.node_id, p_in.msg_cnt)) {
		debug << "Possible TCP spoofing detected";
		//closefile(fd, ad.name());
		//throw exc_error("Possible network spoofing detected");
	}
	pass_server_fd = fd;
	pass_server_fname = ad.name();
	serv_helo = p_in;
	int oldfl = fcntl(fd, F_GETFL);
	if (oldfl < 0)
		error(errno, errno, "fcntl");
	if (fcntl(fd, F_SETFL, oldfl & ~O_NONBLOCK) < 0)
		error(errno, errno, "fcntl");
	server.cv.notify_one();
	return true;
}

void Daemon::server_main_loop(ThreadCV * t)
{
	ulock lk(t->mtx);
	while (prog_status == ProgramStatus::work) {
		t->cv.wait(lk);
		if (prog_status != ProgramStatus::work)
			break;
		server_act();
	}
	t->done = true;
}

TCPHeloMsg Daemon::client_connect(TCPconn& conn)
{
	TCPheloCrypted p_out;
	p_out.nonce.random(rnd);
	p_out.random.random(rnd);
	p_out.msg = get_tcp_helo();
	size_t trash_size = p_out.trash_size = uniform_int_distribution<size_t>(0UL, sizeof(TCPheloCrypted::trash))(rd);
	short proto_ver = p_out.msg.version;
	SHA1::write(&p_out, offsetof(TCPheloCrypted, trash), p_out.hash);
	::encrypt(&p_out.random, offsetof(TCPheloCrypted, trash) - offsetof(TCPheloCrypted, random), crypt_key, p_out.nonce);
	conn.fout.write(&p_out, offsetof(TCPheloCrypted, trash) + trash_size);
	conn.fout.flush_net();

	TCPheloCrypted p_in;
	conn.fin.read(&p_in, offsetof(TCPheloCrypted, trash));
	::decrypt(&p_in.random, offsetof(TCPheloCrypted, trash) - offsetof(TCPheloCrypted, random), crypt_key, p_in.nonce);
	if (p_in.trash_size > sizeof(TCPheloCrypted::trash))
		throw exc_error();
	SHA1::check(&p_in, offsetof(TCPheloCrypted, trash), p_in.hash);
	conn.fin.read(p_in.trash, p_in.trash_size);

	p_in.msg.version = min(proto_ver, p_in.msg.version);
	return p_in.msg;
}

void Daemon::clear_usl()
{
	erase_if(usl, [](const UnixSession& s) { return s.stop; });
}

void Daemon::recv_unix(int ufd)
{
	int fd = accept(ufd, nullptr, nullptr);
	if (fd < 0)
		error(0, errno, "accept");
	else
		usl.emplace_back(fd);
}

void Daemon::recv_unix_lp(int ufd)
{
	int fd = accept(ufd, nullptr, nullptr);
	if (fd < 0) {
		error(0, errno, "accept");
		return;
	}
	write_av(fd);
	closefile(fd, "Unix low priveleged socket");
}

void Daemon::daemon()
{
	cwd();
	use_console = false;
	while (prog_status != ProgramStatus::exit) {
		try {
			daemon_run();
		} catch (const exception& exc) {
			warn << exc.what();
		}
	}
	broadcast_bye();
}

void Daemon::daemon_run()
{
	warn << "DAEMON RUN";
	if (status == NodeStatus::deleted) {
		warn << "This node is deleted";
		pthread_kill(thr_id, SIGTERM);
		return;
	}
	if (!group_id)
		error(1, 0, "Distadm-network not initialized");
	prog_status = ProgramStatus::work;
	pending_commands();
	save();

	UnixServerSocket usshp(unix_socket_name(), true);
	UnixServerSocket usslp(unix_socket_name_lp(), false);
	vector<pollfd> pollfds; // system + udp-listener + tcp-listener + tls_sessions
	vector<IFName> udp_ifn; // udp-listener: network interface names for file descriptors
	vector<TCPheloNB> tcpnb;
	const size_t sys_idx = 2; // one unix socket
	size_t udp_idx;
	size_t tcp_idx;
	pollfds.push_back(pollfd { usshp.fd, POLLIN, 0 });
	pollfds.push_back(pollfd { usslp.fd, POLLIN, 0 });

	{
		int i = sys_idx;
		map<IFName, unsigned> indices = if_indices();
		for(const IFName& x : cfg.listen) {
			auto p = indices.find(x);
			if (p == indices.end())
				continue;
			int fd = open_udp_listen_socket(p->first.dev, p->second);
			if (fd < 0)
				continue;
			udp_ifn.push_back(x);
			pollfds.push_back(pollfd { fd, POLLIN, 0 });
			i++;
		}
		udp_idx = i;
	}
	if(cfg.listen_specified) {
		int i = udp_idx;
		for(const IFName& x : cfg.listen) {
			int fd = open_tcp_listen_socket(x.dev, true);
			if (fd < 0)
				continue;
			pollfds.push_back(pollfd { fd, POLLIN, 0 });
			i++;
		}
		tcp_idx = i;
	} else {
		int i = udp_idx;
		int fd = open_tcp_listen_socket(0, false);
		if (fd >= 0) {
			pollfds.push_back(pollfd { fd, POLLIN, 0 });
			i++;
		}
		tcp_idx = i;
	}
	ThreadCtrl srv(&server, &Daemon::server_main_loop);
	ThreadCtrl clnt(&client, &Daemon::client_main_loop);


	sleep(1);
	broadcast_helo();

	while (prog_status == ProgramStatus::work) {
		int ready = poll(pollfds.data(), pollfds.size(), 60 * 60 * 1000);
		if (prog_status != ProgramStatus::work)
				break;
		if (ready < 0) {
			if (errno == EINTR) {
				alarm_stop(thr_id);
				clear_usl();
			}
			else
				error(errno, errno, "poll");
		}
		for(size_t i = 0; i < pollfds.size(); i++) {
			if (pollfds[i].revents & POLLIN) {
				if (!i)
					recv_unix(pollfds[i].fd);
				else if (i < sys_idx) {
					update_info();
					pending_commands();
					recv_unix_lp(pollfds[i].fd);
				} else if (i >= sys_idx && i < udp_idx)
					recv_udp(pollfds[i].fd, udp_ifn[i - sys_idx]);
				else if (i < tcp_idx) {
					sockaddr_in6 sa;
					socklen_t sa_size = sizeof(sa);
					int connfd = accept4(pollfds[i].fd, (struct sockaddr*)&sa, &sa_size, SOCK_NONBLOCK);
					if (connfd >= 0 && sa_size == sizeof(sa)) {
						char addrname[INET6_ADDRSTRLEN] { '\0' };
						inet_ntop(sa.sin6_family, &sa.sin6_addr, addrname, sizeof(addrname));
						debug << "received TCP from " << addrname;
						socket_timeouts(connfd);
						IN6ad ad(sa.sin6_addr, get_ifname(connfd));
						pollfds.emplace_back(pollfd {connfd, POLLIN | POLLOUT, 0});
						tcpnb.emplace_back(dmn->get_tcp_helo(), move(ad), crypt_key);
					} else
						pollfds[i].revents = POLLERR;
				} else
					tcpnb[i - tcp_idx].on_read(pollfds[i]);
				pollfds[i].revents &= ~POLLIN;
			}
			if (pollfds[i].revents & POLLOUT) {
				tcpnb[i - tcp_idx].on_write(pollfds[i]);
				pollfds[i].revents &= ~POLLOUT;
			}

			if (pollfds[i].revents & (POLLERR | POLLHUP)) {
				if (i < sys_idx)
					error(errno, errno, "poll unix socket");
				else if (i < udp_idx) {
					close(pollfds[i].fd);
					udp_ifn.erase(udp_ifn.begin() + i - sys_idx);
					pollfds.erase(pollfds.begin() + i);
					udp_idx--;
					tcp_idx--;
				} else if (i < tcp_idx) {
					close(pollfds[i].fd);
					pollfds.erase(pollfds.begin() + i);
					tcp_idx--;
				} else {
					tcpnb.erase(tcpnb.begin() + i - tcp_idx);
					close(pollfds[i].fd);
					pollfds.erase(pollfds.begin() + i);
				}
			}
		}
		for (size_t i = 0; i < tcpnb.size(); i++) {
			if (pollfds[i + tcp_idx].events & (POLLIN | POLLOUT))
				continue;
			serve(tcpnb[i].p_in.msg, tcpnb[i].ad, pollfds[i + tcp_idx].fd);
			tcpnb.erase(tcpnb.begin() + i);
			pollfds.erase(pollfds.begin() + i + tcp_idx);
			i--;
		}
		if (status == NodeStatus::work || status == NodeStatus::inviter) {
			update_info();
			pending_commands();
		}
	}
	for (size_t i = sys_idx; i < pollfds.size(); i++)
		close(pollfds[i].fd);
}
