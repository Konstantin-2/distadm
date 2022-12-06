#include <cstring>
#include <cassert>
#include <filesystem>
#include <error.h>
#include <arpa/inet.h>
#include <bsd/string.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <gnutls/crypto.h>
#include "exc_error.h"
#include "network.h"
#include "utils.h"
#include "warn.h"
#include "main.h"

namespace fs = std::filesystem;
using std::string;
using std::string_view;
using std::uniform_int_distribution;

static int connect_to(const IN6ad& ad, int port)
{
	int connfd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (connfd < 0)
		error(errno, errno, "socket() failed");
	ifreq ifr;
	strlcpy(ifr.ifr_name, ad.if_name.dev, sizeof(ifr.ifr_name));
	int res = setsockopt(connfd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
	if (res)
		throw exc_error(errno, "Error setsockopt");
	socket_timeouts(connfd);
	struct sockaddr_in6 si;
	memset(&si, 0, sizeof(si));
	si.sin6_family = AF_INET6;
	si.sin6_port = htons(port);
	si.sin6_addr = ad.addr;
	res = connect(connfd, (struct sockaddr*)&si, sizeof(si));
	if (res < 0) {
		closefile(connfd, ad.name());
		throw exc_errno("Error connect");
	}
	return connfd;
}

// ===== IFName =====

IFName::IFName()
{
	dev[0] = '\0';
}

IFName::IFName(const char * str)
{
	init_from(str);
}

IFName::IFName(string_view sv)
{
	init_from(sv.data());
}

const char * IFName::operator=(const char * str)
{
	init_from(str);
	return str;
}

bool IFName::operator==(const IFName& that) const
{
	return !strncmp(dev, that.dev, sizeof(dev));
}

bool IFName::operator<(const IFName& that) const
{
	return strncmp(dev, that.dev, sizeof(dev)) < 0;
}

IFName::operator bool() const
{
	return dev[0];
}

void IFName::init_from(const char * str)
{
	strlcpy(dev, str, sizeof(dev));
}

// ===== IN6addr =====

IN6addr::IN6addr(const in6_addr& addr)
{
	memcpy(this, &addr, sizeof(addr));
}

bool IN6addr::operator==(const in6_addr& that) const
{
	return !memcmp(this, &that, sizeof(that));
}

bool IN6addr::operator<(const in6_addr& that) const
{
	return memcmp(this, &that, sizeof(that)) < 0;
}

// ===== IN6ad =====

IN6ad::IN6ad(const in6_addr& a, const IFName& n) : addr(a), if_name(n)
{
}

bool IN6ad::operator==(const IN6ad& that) const
{
	return addr == that.addr && if_name == that.if_name;
}

bool IN6ad::operator<(const IN6ad& that) const
{
	return addr < that.addr || (addr == that.addr && if_name < that.if_name);
}

IN6ad::operator bool() const
{
	for (auto& x : addr.s6_addr)
		if (x)
			return true;
	return false;
}

IN6ad IN6ad::none()
{
	IN6ad res;
	memset(&res.addr, 0, sizeof(res.addr));
	return res;
}

string IN6ad::name() const
{
	char addrname[INET6_ADDRSTRLEN] { '\0' };
	inet_ntop(AF_INET6, &addr, addrname, sizeof(addrname));
	return string(addrname) + ':' + if_name.dev;
}

// ===== UnixServerSocket =====

UnixServerSocket::UnixServerSocket(const string& fname, bool hp) :
	filename(fname),
	high_priveleged(hp)
{
	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1)
		error(errno, errno, "socket");

	int reuse = true;
	int res = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	if (res)
		error(errno, errno, "setsockopt Unix error");

	sockaddr_un sa;
	sa.sun_family = AF_UNIX;
	memset(sa.sun_path, 0, sizeof(sa.sun_path));
	fs::remove(filename);
	memcpy(sa.sun_path, filename.data(), filename.size());
	res = bind(fd, (struct sockaddr *)&sa, sizeof(sa.sun_family) + filename.size());
	if (res == -1)
		error(errno, errno, "bind %s", filename.c_str());
	res = chmod(filename.c_str(), hp ? 0600 : 0666);
	res = listen(fd, 1);
	if (res == -1)
		error(errno, errno, "listen");
}

UnixServerSocket::~UnixServerSocket()
{
	close(fd);
	fs::remove(filename);
}

// ===== UDPmessage =====

void UDPcrypted::encrypt(const CryptKey& key)
{
	switch (msg.base.version) {
	case 1:
		SHA1::write(msg.v1, &UDPmessage_v1::hash);
		break;
	default:
		error(1, 0, "Bad UDP message version");
	}
	nonce.random(rnd);
	::encrypt(msg, key, nonce);
}

void UDPcrypted::decrypt(const CryptKey& key)
{
	::decrypt(msg ,key, nonce);
	switch (msg.base.version) {
	case 1:
		if (!SHA1::check(msg.v1, &UDPmessage_v1::hash))
			msg.base.version = -1;
		break;
	default:
		msg.base.version = -1;
	}
}

// ===== TCPconn =====

TCPconn::TCPconn(const IN6ad& ad, int port) :
	fs(connect_to(ad, port), ad.name(), true),
	fin(fs),
	fout(fs)
{
}

TCPconn::TCPconn(int fd, const string& name) :
	fs(fd, name, true),
	fin(fs),
	fout(fs)
{
}

/* ad = addr-to-connect
 * tcp-helo-base = connect(ad)
 * case version 1:
 * 		tcp-helo-1.read
 * 		v1.client-main
 * */
