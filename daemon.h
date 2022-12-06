#pragma once
#include <list>
#include <condition_variable>
#include <thread>
#include <ext/stdio_filebuf.h>
#include "coremt.h"

struct TCPheloCrypted {
	Nonce nonce;
	UUID random;
	TCPHeloMsg msg;
	SHA1 hash;
	size_t trash_size;
	char trash[0x400];
};

struct TCPheloNB  {
	TCPheloNB(const TCPHeloMsg& hc, const IN6ad& a, CryptKey& k);
	TCPheloNB(TCPheloNB&&) = default;
	TCPheloNB& operator=(TCPheloNB&&) = default;
	TCPheloCrypted p_in, p_out;
	IN6ad ad;
	CryptKey * key;
	size_t size_to_write;
	size_t written_size = 0;
	size_t read_size = 0;
	bool complete = false;
	bool decrypted_read = false;

	void on_read(pollfd&);
	void on_write(pollfd&);
};

struct TCPhelo : TCPHeloMsg {
	TCPhelo(IN6ad&);
	TCPconn conn;
};

struct ThreadCV {
	std::mutex mtx;
	std::condition_variable cv;
	bool done = false;
};

// unix socket connection
struct UnixSession {
	UnixSession(int fd);
	~UnixSession();
	void operator=(const UnixSession&) = delete;
	void operator=(UnixSession&&) = delete;
	void run();

	bool stop = false;
private:
	bool getline(std::string&) const;
	int fd;
	std::thread thr;
	pthread_t thr_id;
};

struct Daemon : CoreMT, virtual CoreBase {
	Daemon(Config&);

	void notify() const override;
	void daemon();

private:
	TCPHeloMsg client_connect(TCPconn&);
	void client_main_loop(ThreadCV *);
	void server_main_loop(ThreadCV *);
	void client_act();
	void server_act();
	void daemon_run();
	void clear_usl();
	void recv_unix(int ufd);
	void recv_unix_lp(int ufd);

	/* Called when UDP message arrived on specified listener socket and interface */
	void recv_udp(int fd, const IFName&);
	void recv_udp_v1(const UDPmessage_v1&, const IFName&, sockaddr_in6&);

	// Open listener on specified interface name and it's index
	int open_udp_listen_socket(const char * if_name, unsigned if_idx) const;

	int open_tcp_listen_socket(const char * if_name, bool dev_specified) const;

	bool serve(const TCPHeloMsg&, const IN6ad& ad, int fd);

	// Thread where daemon running. Used for notify it
	pthread_t thr_id;

	// Server part
	ThreadCV server;
	int pass_server_fd;
	std::string pass_server_fname; // remote node network address
	std::atomic_bool server_busy;
	TCPHeloMsg serv_helo; // this is what remote node send to our server

	// Client part
	ThreadCV client;

	// Unix socket part
	std::list<UnixSession> usl;
};

struct ThreadCtrl {
	ThreadCtrl(ThreadCV *, void(Daemon::*)(ThreadCV *));
	~ThreadCtrl();
	void run();

	void(Daemon::* func)(ThreadCV *);
	ThreadCV * tcv;
	pthread_t thr_id;
	std::thread thr;
};

struct TCPsession_v1 {
	TCPsession_v1(TCPconn& c);
	~TCPsession_v1();
	bool initialize();
	void initialize_me();
	void initialize_that();
	void client_main();
	void server_main();
	bool xchg_bool(bool);

	TCPconn& conn;
	OCCstream fcout;
	ICCstream fcin;
	bool remote_initialized;
	UUID remote_id;
};

extern Daemon * dmn;
