#pragma once
#include <string>
#include <json/json.h>
#include "cryptkey.h"

// Create file like creat(2) but with correct signals processing
int createfile(const std::string& filename);

// Open file like open(2) but with correct signals processing
int openfile(const std::string& filename, int flags);

// Close file like close(2) but with correct signals processing
void closefile(int& fd, const std::string& filename);

/* Read from file like read(2) but with correct signals processing
 * Return 0 on EOF */
ssize_t readfile(int fd, void * buf, size_t size, const std::string& filename);

/* Like readfile() but may read less than required. Used for fill cache
 * in network connections. Return read size */
ssize_t readsome(int fd, void * buf, size_t size, const std::string& filename);

// Write to file like write(2) but with correct signals processing
void writefile(int fd, const void * buf, size_t size, const std::string& filename);

// Same as readfile() but used in non-blocking mode
ssize_t read_nb(int fd, void * buf, size_t size, const std::string& filename);

/* Same as writefile() but used in non-blocking mode
 * Return number of written bytes */
ssize_t write_nb(int fd, void * buf, size_t size, const std::string& filename);

// Split string_view in two parts by first space(s) found
std::pair<std::string_view, std::string_view> split2(std::string_view);

// Print json to string without unneeded indentations and newlines
std::string compact_string(const Json::Value&);

// Print json to ostream
void write_string(const Json::Value&, std::ostream&);

/* Return string value by key from json. If value is not string
 * print warning, remove value from json and return empty string */
std::string asString(const Json::Value&, const std::string&);

bool asBool(const Json::Value&, bool defult = false);

/* Execute program with arguments. Return false on errors.
 * Write return status to second parameter if specified. */
bool exec_prog(std::initializer_list<std::string> params, int * status = nullptr);

// Same as exec_prog, pass 'input' as input to program
bool exec_prog_input(std::initializer_list<std::string> params, const std::string& input);

// Same as exec_prog, store output in 'output'
bool exec_prog_output(std::vector<std::string>& params, std::string& output, const std::string& workdir);
bool exec_prog_output(std::initializer_list<std::string> params, std::string& output, const std::string& workdir = "");

/* Execute program. Give program access to input and output, wait untill program ends*/
bool exec_prog_interactive(std::initializer_list<std::string> params);

/* Function to work with lines like /etc/shadow. Check if line 'find_user' has username found in where_search.
 * If line is found, return pointer to that line. Return nullptr otherwise. */
std::string * contain_user(const std::string& find_user, std::vector<std::string>& where_search, const std::vector<size_t>& nick_sizes);

// return line by username password from /etc/shadow or return empty line
std::string find_pwd_line(std::string_view username);

// sleep for 0.1 second
void nsleep(unsigned x = 1);

// Set read/write timeouts for socket
void socket_timeouts(int fd);

// Connect to unix socket
int create_unix_socket_client_fd(const std::string& filename);

/* Split string by spaces (tabs, new tines, etc). Spaces in quoted parts are
 * ingored. Special characters can be escaped outside of quoted parts and
 * inside double-quoted (") parts. Escapes inside single quoted parts (')
 * are ignored and parts printed as is without any transformation
 * If keep_empty is false, empty strings will be emitted from result
 * If delimiter specified, it will be user instead if spaces */
std::vector<std::string> splitq(std::string_view src, bool keep_empty = false, char delimiter = 0);

/* Quote string so that it can be restored to original by dequote_string()
 * or interpreted as single part and restored by splitq() */
std::string quote_string(std::string_view src);

// Simple string splitter, do not reescape or use quoted strings. Remove empty elements
std::vector<std::string_view> split(std::string_view str, bool keep_empty = false, char delim = ' ');

// Read from fd until EOT.
bool getline(int fd, std::string& res);

// send new-line terminated command to unix soket unix_fd and get returned line of text without trailing newline
std::string unix_getline(int fd, const std::string& cmd);

// convert vector
std::vector<std::string> as_strings(std::vector<std::string_view>&& from);

void print_list(std::ostream& os, const std::vector<std::string>& list, char delim);

void cdhome();

void encrypt(void *, size_t, const CryptKey&, const Nonce&);
void decrypt(void *, size_t, const CryptKey&, const Nonce&);
void decrypt_to(const void * from, void * to, size_t, const CryptKey&, const Nonce&);

template <typename T>
void encrypt(T& t, const CryptKey& k, const Nonce& n)
{
	encrypt(&t, sizeof(t), k, n);
}

template <typename T>
void decrypt(T& t, const CryptKey& k, const Nonce& n)
{
	decrypt(&t, sizeof(t), k, n);
}

template <typename T>
void decrypt_to(T& from, T& to, const CryptKey& k, const Nonce& n)
{
	decrypt_to(&from, &to, sizeof(T), k, n);
}

const char EOT = '\04';
