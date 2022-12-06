#include "utils.h"
#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <gnutls/crypto.h>
#include <iostream>
#include <cassert>
#include <thread>
#include <mutex>
#include "main.h"
#include "warn.h"
#include "exc_error.h"
#include "utils_iface.h"

using std::cout;
using std::endl;
using std::flush;
using std::pair;
using std::string;
using std::vector;
using std::mutex;
using std::ostream;
using std::ifstream;
using std::string_view;
using std::ostringstream;
using std::unique_ptr;
using std::initializer_list;
typedef std::lock_guard<mutex> lock;

int createfile(const string& filename)
{
	while (prog_status == ProgramStatus::work) {
		int fd = creat(filename.c_str(), S_IRUSR | S_IWUSR);
		if (fd != -1)
			return fd;
		if (errno != EINTR)
			throw exc_errno("Error create file", filename);
	}
	throw exc_error();
}

int openfile(const string& filename, int flags)
{
	while (prog_status == ProgramStatus::work) {
		int fd = open(filename.c_str(), flags, 0644);
		if (fd != -1)
			return fd;
		if (errno != EINTR)
			throw exc_errno("Error open file", filename);
	}
	throw exc_error();
}

void closefile(int& fd, const string& filename)
{
	if (fd == -1)
		return;
	for (;;) {
		int x = close(fd);
		if (!x)
			break;
		if (errno != EINTR) {
			fd = -1;
			throw exc_errno("Error close", filename);
		}
	}
	fd = -1;
}

ssize_t readfile(int fd, void * buf, size_t size, const string& filename)
{
	char * dst = (char *)buf;
	char * const end = dst + size;
	while (dst < end && prog_status == ProgramStatus::work) {
		ssize_t x = read(fd, dst, end - dst);
		if (x > 0)
			dst += x;
		else if (!x)
			break;
		else if (errno != EINTR)
			throw exc_errno("Error read", filename);
	}
	if (prog_status != ProgramStatus::work)
		throw exc_error();
	return dst - (char *)buf;
}

ssize_t readsome(int fd, void * buf, size_t size, const string& filename)
{
	char * dst = (char *)buf;
	char * const end = dst + size;
	while (prog_status == ProgramStatus::work) {
		ssize_t x = read(fd, dst, end - dst);
		if (x >= 0) {
			dst += x;
			break;
		} else if (errno != EINTR)
			throw exc_errno("Error read", filename);
	}
	if (prog_status != ProgramStatus::work)
		throw exc_error();
	return dst - (char *)buf;
}

void writefile(int fd, const void * buf, size_t size, const string& filename)
{
	const char * src = (const char *)buf;
	const char * end = src + size;
	while (src < end && prog_status == ProgramStatus::work) {
		ssize_t x =  write(fd, src, end - src);
		if (x > 0)
			src += x;
		else if (errno != EINTR)
			throw exc_errno("Error write to", filename);
	}
	if (prog_status != ProgramStatus::work)
		throw exc_error();
}

ssize_t read_nb(int fd, void * buf, size_t size, const string& filename)
{
	char * dst = (char *)buf;
	char * const end = dst + size;
	while (dst < end && prog_status == ProgramStatus::work) {
		ssize_t x =  read(fd, dst, end - dst);
		if (x > 0)
			dst += x;
		else if (!x)
			break;
		else if (errno != EAGAIN)
			break;
		else if (errno != EINTR)
			throw exc_errno("Error read", filename);
	}
	if (prog_status != ProgramStatus::work)
		throw exc_error();
	return dst - (char *)buf;
}

ssize_t write_nb(int fd, void * buf, size_t size, const string& filename)
{
	const char * src = (const char *)buf;
	const char * end = src + size;
	while (src < end && prog_status == ProgramStatus::work) {
		ssize_t x =  write(fd, src, end - src);
		if (x > 0)
			src += x;
		else if (errno != EAGAIN)
			break;
		else if (errno != EINTR)
			throw exc_errno("Error write to", filename);
	}
	if (prog_status != ProgramStatus::work)
		throw exc_error();
	return src - (char *)buf;
}

void delfile(const string& filename)
{
	int ret = unlink(filename.c_str());
	if (ret)
		warn << "Error delete file" << ' ' << filename << ':' << strerror(errno);
}

void renamefile(const string& oldfilename, const string& newfilename)
{
	int ret = rename(oldfilename.c_str(), newfilename.c_str());
	if (ret)
		throw exc_errno("Error rename file", oldfilename, "to", newfilename);
}

pair<string_view, string_view> split2(string_view str)
{
	while (!str.empty() && isspace(str.front())) str.remove_prefix(1);
	auto ptr1 = str.data();
	while (!str.empty() && !isspace(str.front())) str.remove_prefix(1);
	auto ptr2 = str.data();
	while (!str.empty() && isspace(str.front())) str.remove_prefix(1);
	auto ptr3 = str.data();
	return pair {string_view {ptr1, ptr2}, string_view {ptr3, str.end()}};
}

string compact_string(const Json::Value& json)
{
	ostringstream o;
	unique_ptr<Json::StreamWriter> writer(json_cbuilder.newStreamWriter());
	writer->write(json, &o);
	return o.str();
}

void write_string(const Json::Value& json, ostream& o)
{
	unique_ptr<Json::StreamWriter> writer(json_builder.newStreamWriter());
	writer->write(json, &o);
}

string asString(const Json::Value& js, const string& key)
{
	try {
		return js[key].asString();
	} catch (const Json::LogicError&) {
		return "";
	}
}

bool exec_prog(initializer_list<string> params, int * st)
{
	if (!params.size())
		return false;
	int pip[2];
	if (pipe(pip))
		error(1, errno, "Error make pipe");
	pid_t chldpid = fork();
	if (chldpid == -1)
		error(1, errno, "Can't fork");
	if (!chldpid) {
		vector<char *> args;
		args.reserve(params.size() + 1);
		for(const string& s : params)
			args.push_back((char *)s.data());
		args.push_back(0);

		close(STDIN_FILENO);
		if (dup2(pip[0], STDIN_FILENO) == -1)
			error(1, errno, "Error dup2()");
		close(pip[0]);
		close(pip[1]);

		execvp(args[0], args.data());
		error(1, errno, "exec error %s", args[0]);
	}
	close(pip[0]);
	close(pip[1]);
	int status;
	waitpid(chldpid, &status, 0);
	if (st) *st = status;
	return !status;
}

bool exec_prog_input(initializer_list<string> params, const string& input)
{
	vector<string> par = params;
	if (!params.size())
		return false;
	int pip[2];
	if (pipe(pip))
		error(1, errno, "Error make pipe");
	pid_t chldpid = fork();
	if (chldpid == -1)
		error(1, errno, "Can't fork");
	if (!chldpid) {
		vector<char *> args;
		args.reserve(params.size() + 1);
		for(const string& s : params)
			args.push_back((char *)s.data());
		args.push_back(0);

		close(STDIN_FILENO);
		if (dup2(pip[0], STDIN_FILENO) == -1)
			error(1, errno, "Error dup2()");
		close(pip[0]);
		close(pip[1]);

		execvp(args[0], args.data());
		error(1, errno, "exec error %s", args[0]);
	}
	close(pip[0]);
	writefile(pip[1], input.data(), input.size(), par[0]);
	close(pip[1]);
	int status;
	waitpid(chldpid, &status, 0);
	return !status;
}

bool exec_prog_output(vector<string>& params, string& output, const string& workdir)
{
	output.clear();
	if (!params.size())
		return false;
	int pip[2];
	if (pipe(pip))
		error(1, errno, "Error make pipe");
	pid_t chldpid = fork();
	if (chldpid == -1)
		error(1, errno, "Can't fork");
	if (!chldpid) {
		if (!workdir.empty()) {
			int res = chdir(workdir.c_str());
			if (res)
				error(0, errno, "chdir %s", workdir.c_str());
		}

		vector<char *> args;
		args.reserve(params.size() + 1);
		for(const string& s : params)
			args.push_back((char *)s.data());
		args.push_back(0);

		close(STDOUT_FILENO);
		if (dup2(pip[1], STDOUT_FILENO) == -1)
			error(1, errno, "Error dup2()");
		close(pip[0]);
		close(pip[1]);

		execvp(args[0], args.data());
		error(1, errno, "exec error %s", args[0]);
	}
	close(pip[1]);

	char buf[0x1000];
	for (;;) {
		ssize_t x = read(pip[0], buf, sizeof(buf));
		if (x > 0) {
			output.append(buf, x);
			continue;
		}
		if (x < 0)
			warn << "Error exec cmd" << ' ' << params[0];
		break;
	}
	close(pip[0]);
	int status;
	waitpid(chldpid, &status, 0);
	return !status;
}

bool exec_prog_output(initializer_list<string> params, string& output, const string& workdir)
{
	vector<string> par = params;
	return exec_prog_output(par, output, workdir);
}

bool exec_prog_interactive(initializer_list<string> params)
{
	if (!params.size())
		return false;
	pid_t chldpid = fork();
	if (chldpid == -1)
		error(1, errno, "Can't fork");
	if (!chldpid) {
		vector<char *> args;
		args.reserve(params.size() + 1);
		for(const string& s : params)
			args.push_back((char *)s.data());
		args.push_back(0);
		execvp(args[0], args.data());
		error(1, errno, "exec error %s", args[0]);
	}
	int status;
	waitpid(chldpid, &status, 0);
	return !status;
}

string * contain_user(const string& find_user, vector<string>& where_search, const vector<size_t>& nick_sizes)
{
	assert(where_search.size() == nick_sizes.size());
	for (size_t i = 0; i < where_search.size(); i++)
		// compare begin of line with username and colon (:) after username
		if (find_user.size() > nick_sizes[i] && !memcmp(find_user.data(), where_search[i].data(), nick_sizes[i] + 1))
			return &where_search[i];
	return nullptr;
}

// find line with user password from /etc/shadow
string find_pwd_line(string_view username)
{
	string line;
	ifstream f("/etc/shadow");
	if (!f) {
		warn << "Error read" << " /etc/shadow";
		return "";
	}
	size_t s = username.size();
	while(getline(f, line)) {
		if (line.size() <= s || memcmp(line.data(), username.data(), s) || line[s] != ':')
			continue;
		return line;
	}
	return "";
}

void encrypt(void * buf, size_t size, const CryptKey& key, const Nonce& nonce)
{
	const gnutls_cipher_algorithm_t algo = GNUTLS_CIPHER_AES_256_CFB8;
	gnutls_cipher_hd_t ctx;
	gnutls_datum_t iv_d, key_d;
	key_d.data = (unsigned char *)&key;
	key_d.size = sizeof(key);
	iv_d.data = (unsigned char *)&nonce;
	iv_d.size = sizeof(nonce);
	assert(gnutls_cipher_get_iv_size(algo) == sizeof(nonce));
	assert(gnutls_cipher_get_key_size(algo) == sizeof(key));
	int res = gnutls_cipher_init(&ctx, algo, &key_d, &iv_d);
	if (res < 0)
		error(res, 0, "GNU TLS error %s", gnutls_strerror(res));
	res = gnutls_cipher_encrypt(ctx, buf, size);
	if (res < 0)
		error(res, 0, "GNU TLS error %s", gnutls_strerror(res));
	gnutls_cipher_deinit(ctx);
}

void decrypt(void * buf, size_t size, const CryptKey& key, const Nonce& nonce)
{
	const gnutls_cipher_algorithm_t algo = GNUTLS_CIPHER_AES_256_CFB8;
	gnutls_cipher_hd_t ctx;
	gnutls_datum_t iv_d, key_d;
	key_d.data = (unsigned char *)&key;
	key_d.size = sizeof(key);
	iv_d.data = (unsigned char *)&nonce;
	iv_d.size = sizeof(nonce);
	assert(gnutls_cipher_get_iv_size(algo) == sizeof(nonce));
	assert(gnutls_cipher_get_key_size(algo) == sizeof(key));
	int res = gnutls_cipher_init(&ctx, algo, &key_d, &iv_d);
	if (res < 0)
		error(res, 0, "GNU TLS error %s", gnutls_strerror(res));
	res = gnutls_cipher_decrypt(ctx, buf, size);
	if (res < 0)
		error(res, 0, "GNU TLS error %s", gnutls_strerror(res));
	gnutls_cipher_deinit(ctx);
}

void decrypt_to(const void * from, void * to, size_t size, const CryptKey& key, const Nonce& nonce)
{
	const gnutls_cipher_algorithm_t algo = GNUTLS_CIPHER_AES_256_CFB8;
	gnutls_cipher_hd_t ctx;
	gnutls_datum_t iv_d, key_d;
	key_d.data = (unsigned char *)&key;
	key_d.size = sizeof(key);
	iv_d.data = (unsigned char *)&nonce;
	iv_d.size = sizeof(nonce);
	assert(gnutls_cipher_get_iv_size(algo) == sizeof(nonce));
	assert(gnutls_cipher_get_key_size(algo) == sizeof(key));
	int res = gnutls_cipher_init(&ctx, algo, &key_d, &iv_d);
	if (res < 0)
		error(res, 0, "GNU TLS error %s", gnutls_strerror(res));
	res = gnutls_cipher_decrypt2(ctx, from, size, to, size);
	if (res < 0)
		error(res, 0, "GNU TLS error %s", gnutls_strerror(res));
	gnutls_cipher_deinit(ctx);
}

void nsleep(unsigned x)
{
	unsigned y = x / 10;
	x %= 10;
	static const timespec t {y, x * 100000000};
	nanosleep(&t, nullptr);
}

static bool isdelimiter(char ch, char delimiter)
{
	return delimiter ? ch == delimiter : isspace(ch);
}
/* Dequote source or part of it: escaped chars (see source below) replaced
 * by their values, single-quoted parte returned as is
 * stop when deimiter found or space (new line, tab, etc) if no delimiter specified
 * when return, 'ptr' points to this value or 'end' */
static string dequote_string(string_view::const_iterator& ptr, string_view::const_iterator end, char delimiter)
{
	bool inq = false;
	string res;
	while (ptr != end) {
		if (*ptr == '\'' && !inq) {
			auto ptr2 = ptr + 1;
			while(ptr2 != end && *ptr2 != '\'') ptr2++;
			res.append(ptr + 1, ptr2);
			if (ptr2 != end)
				ptr2++;
			ptr = ptr2;
		} else if (*ptr == '"') {
			ptr++;
			inq = !inq;
			continue;
		} else if(!inq && isdelimiter(*ptr, delimiter))
			break;
		else if(*ptr == '\\') {
			ptr++;
			if (ptr == end)
				break;
			switch(*ptr) {
			case '\\':
				res += '\\';
				break;
			case 'b':
				res += '\b';
				break;
			case 'e':
				res += '\e';
				break;
			case 'f':
				res += '\f';
				break;
			case 'n':
				res += '\n';
				break;
			case 'r':
				res += '\r';
				break;
			case 't':
				res += '\t';
				break;
			case 'v':
				res += '\v';
				break;
			case ' ':
				res += ' ';
				break;
			case '"':
				res += '"';
				break;
			case '0':
				{
					char ch = 0;
					if (++ptr != end && isdigit(*ptr)) {
						ch += *ptr - '0';
						if (++ptr != end && isdigit(*ptr)) {
							ch = ch * 10 + *ptr - '0';
						} else ptr--;
					} else ptr--;
					res += ch;
				}
				break;
			default:
				res += '\\';
				res += *ptr;
				break;
			}
			ptr++;
		} else if (inq) {
			auto ptr2 = ptr + 1;
			while (ptr2 != end && *ptr2 != '"' && *ptr2 != '\\') ptr2++;
			res.append(ptr, ptr2);
			ptr = ptr2;
		} else {
			auto ptr2 = ptr + 1;
			while (ptr2 != end && !isdelimiter(*ptr2, delimiter) && *ptr2 != '\'' && *ptr2 != '"' && *ptr2 != '\\') ptr2++;
			res.append(ptr, ptr2);
			ptr = ptr2;
		}
	}
	return res;
}

vector<string> splitq(string_view src, bool keep_empty, char delimiter)
{
	vector<string> res;
	auto ptr = src.begin();
	auto end = src.end();
	while (ptr != end && isspace(*ptr)) ptr++;
	while (ptr != end) {
		string str = dequote_string(ptr, end, delimiter);
		if (keep_empty || !str.empty())
			res.push_back(str);
		while (ptr != end && isspace(*ptr)) ptr++;
	}
	return res;
}

string quote_string(string_view src)
{
	bool need_quote = false;
	for (char ch : src)
		if (isspace(ch) || ch == '\'' || ch == '"' || ch == '\\') {
			need_quote = true;
			break;
		}
	if (!need_quote)
		return string(src);
	ostringstream res;
	res << '"';
	for (char ch : src) {
		if (ch == '\\' || ch == '"')
			res << '\\';
		res << ch;
	}
	res << '"';
	return res.str();
}

vector<string_view> split(string_view strv, bool keep_empty, char delim)
{
	vector<string_view> output;
	size_t first = 0;
	while (first < strv.size())
	{
		const auto second = strv.find_first_of(delim, first);
		if (keep_empty || first != second)
			output.emplace_back(strv.substr(first, second-first));
		if (second == string_view::npos)
			break;
		first = second + 1;
	}
	return output;
}

void socket_timeouts(int fd)
{
	struct timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

bool asBool(const Json::Value& src, bool defult)
{
	if (src.isBool())
		return src.asBool();
	else
		return defult;
}


int create_unix_socket_client_fd(const string& filename)
{
	int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd == -1)
		error(errno, errno, "socket");
	sockaddr_un sa;
	sa.sun_family = AF_UNIX;
	memset(sa.sun_path, 0, sizeof(sa.sun_path));
	memcpy(sa.sun_path, filename.data(), filename.size());
	int res = connect(fd, (struct sockaddr*)&sa, sizeof(sa.sun_family) + filename.size());
	if (res != -1)
		return fd;
	close(fd);
	return -1;
}

bool getline(int fd, string& res)
{
	if(fd == -1)
		return false;
	res.clear();
	char buf[0x1000];
	bool eof = false;
	for(;;) {
		ssize_t x = read(fd, buf, sizeof(buf));
		if (!x) {
			eof = true;
			break;
		}
		if (x < 0)
			error(errno, errno, "read");
		res.append(buf, x);
		if (buf[x - 1] == EOT) {
			res.pop_back();
			return true;
		}
	}
	return !eof || !res.empty();
}

string unix_getline(int fd, const string& cmd)
{
	static mutex mtx;
	lock lck(mtx);
	if(fd == -1)
		return string();
	write(fd, cmd.data(), cmd.size());
	string res;
	getline(fd, res);
	return res;
}

vector<string> as_strings(vector<string_view>&& from)
{
	vector<string> res;
	res.reserve(from.size());
	for (const auto& s : from)
		res.push_back(string(s));
	return res;
}

void print_list(ostream& os, const vector<string>& list, char delim)
{
	bool first = true;
	for (const string& n : list) {
		if (!first) os << delim;
		else first = false;
		os << quote_string(n);
	}
}
