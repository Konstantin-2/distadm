#pragma once
#include <string>
#include <poll.h>
#include <zlib.h>
#include <net/if.h>
#include <netinet/in.h>
#include "sha.h"
#include "ccstream.h"

// Network interface symbolic name ("eth0", ...). No name ("\0") means false
struct IFName {
	IFName();
	IFName(const IFName&) = default;
	IFName(const char *);
	IFName(std::string_view);
	const char * operator=(const char *);
	bool operator==(const IFName&) const;
	bool operator<(const IFName&) const;
	IFName& operator=(const IFName&) = default;
	IFName& operator=(IFName&&) = default;
	operator bool() const;
	char dev[IFNAMSIZ];
private:
	void init_from(const char *);
};

// ipv6 address (128-bits)
struct IN6addr : in6_addr {
	IN6addr() = default;
	IN6addr(const IN6addr&) = default;
	IN6addr(const in6_addr&);
	bool operator==(const in6_addr&) const;
	bool operator<(const in6_addr&) const;
};

// ipv6 address + network interface name
struct IN6ad {
	IN6ad() = default;
	IN6ad(const IN6ad&) = default;
	IN6ad(IN6ad&&) = default;
	IN6ad(const in6_addr&, const IFName&);
	IN6ad& operator=(const IN6ad&) = default;
	IN6ad& operator=(IN6ad&&) = default;
	bool operator==(const IN6ad&) const;
	bool operator<(const IN6ad&) const;
	operator bool() const;
	std::string name() const;
	static IN6ad none();
	IN6addr addr;
	IFName if_name;
};

/* Socket to communicate with local processes
 * High privelegied means fully interactive process
 * Low proveleged daemon only writes info to socket and disconnect after that
 * so low-proveleged users has no ability to tell something to daemon */
struct UnixServerSocket {
	UnixServerSocket(const std::string&, bool high_priveleged);
	UnixServerSocket(const UnixServerSocket&) = delete;
	void operator=(const UnixServerSocket&) = delete;
	~UnixServerSocket();
	std::string filename;
	int fd;
	bool high_priveleged;
};

// Foresee for future interprocess commmunication protocol changes
struct UDPmessageBase {
	short version;
};

// UDP message which should be crypted
struct UDPmessage_v1 : UDPmessageBase {
	enum Command : char {
		helo = 0,
		bye = 1,
		not_initialized = 2 // I'm not initialized, help me!
	};

	Command message;

	/* Each udp message of node should has it's own incremental number
	 * (see Node::udp_cnt) to prevent udp spoofing */
	size_t counter;

	UUID group_id;
	UUID node_id;

	// If two nodes has different hash, they should communicate to each other
	SHA256 node_hash;

	// Hash of this message
	SHA1 hash;
};

union UDPmessage {
	UDPmessageBase base;
	UDPmessage_v1 v1;
};

// Crypted UDP message
struct UDPcrypted {
	Nonce nonce;
	UDPmessage msg;
	void encrypt(const CryptKey& key);
	void decrypt(const CryptKey& key);
};

struct TCPconn {
	TCPconn(const IN6ad&, int port); // connect to address and port
	TCPconn(int fd, const std::string&); // use opened socket and it's name
	Fstream fs;
	Istream fin;
	Ostream fout;
};
