#pragma once
#include <zlib.h>
#include <fcntl.h>
#include <gnutls/crypto.h>
#include <sha1.h>
#include <string>
#include "cryptkey.h"

// Complessed Crypted files (or network sockets)

/* 1. File descriptor may refers to regular file or network socket.
 *    Network functions like accept() use numeric file descriptors
 * 2. There is no way to use iostreams with numeric file descriptors.
 *    So this class is implemented.
 * 3. FILE struct is not used cause it is unobvious with signal processing.
 * 4. close() functions allow raise exceptions and inperrupt process on errors
 *    during deinitialization. So it is not advisable to close files in
 *    destructors. */

extern bool dump_crypt;

struct Fstream {
	// Store filename (for error messages)
	Fstream(const std::string& filename);

	/* Store opened file descriptor and filename (for error messages)
	 * Set net to true if fd is network socket */
	Fstream(int fd, const std::string& filename, bool net);

	Fstream(Fstream&&);

	~Fstream();

	static Fstream create(const std::string& filename);
	static Fstream open(const std::string& filename, int node = O_RDONLY);
	static Fstream openrw(const std::string& filename);
	static Fstream use(int fd, const std::string& filename);

	off_t filesize() const;
	void seek(off_t);
	void close();
	std::string filename;
	int fd = -1; // File descriptor
	bool net = false; // file is network socket
private:
	Fstream() = default;
};

struct StreamBase {
	StreamBase(Fstream& b) : base(b) {}
	Fstream& base;
};

// Input cached file (similar to FILE struct)
struct Istream : StreamBase {
	Istream(Fstream&);
	Istream(const Istream&) = delete;
	Istream(Istream&&) = delete;
	Istream& operator=(const Istream&) = delete;
	Istream& operator=(Istream&&) = delete;
	~Istream();

	// Read data from file
	void read(void *, size_t);
	Nonce read_nonce();

	/* Read data but not move pointer. Size should not be greater than cache */
	void peek(void *, size_t);

	// Get offset of read pointer
	off_t tell();

	// Throw exception if called at EOF
	void fillcache();

	void close();

	char cache[0x10000];
	char * ptr;
	char * cache_end;
};

// Output cached file
struct Ostream : StreamBase {
	Ostream(Fstream&);
	Ostream(const Ostream&) = delete;
	Ostream(Ostream&&) = delete;
	Ostream& operator=(const Ostream&) = delete;
	Ostream& operator=(Ostream&&) = delete;
	~Ostream();

	// Write data to file
	void write(const void *, size_t);
	void write(const Nonce&);

	// Drop cache to file
	void flush_cache();

	// If file descriptor is network sosket, drop cache and call fsync()
	void flush_net();

	// Get offset of write pointer
	off_t tell();

	void close();

	char cache[0x10000];
	char * ptr;
	char * const dst_end = cache + sizeof(cache);
};

// Input Crypted file. Also calculate hash for read data
struct ICstream {
	ICstream(Istream&, const CryptKey&);
	ICstream(Istream&, const CryptKey&, const Nonce&);
	ICstream(const ICstream&) = delete;
	ICstream(ICstream&&) = delete;
	ICstream& operator=(const ICstream&) = delete;
	ICstream& operator=(ICstream&&) = delete;
	~ICstream();
	Istream& base;

	void read(void *, size_t);
	void read_nh(void *, size_t); // Read but not calc hash

	/* Read hash object and compare with current state.
	 * Throw error if hash is wrong */
	void check_hash();
private:
	void init(const CryptKey&, const Nonce&);
	gnutls_cipher_hd_t cctx = nullptr;
	SHA1_CTX sctx;
};

// Output Crypted file. Also calculate hash for written data
struct OCstream {
	OCstream(Ostream&, const CryptKey&);
	OCstream(Ostream&, const CryptKey&, const Nonce&);
	OCstream(const OCstream&) = delete;
	OCstream(OCstream&&) = delete;
	OCstream& operator=(const OCstream&) = delete;
	OCstream& operator=(OCstream&&) = delete;
	~OCstream();
	Ostream& base;

	void write(const void *, size_t);
	void write_nc(const void *, size_t); // Write but not calc hash

	// Write current state hash
	void write_hash();
private:
	void init(const CryptKey&, const Nonce&);
	gnutls_cipher_hd_t cctx = nullptr;
	SHA1_CTX sctx;
};

// Intput Complessed Crypted file (data is compressed and after that is crypted)
struct ICCstream : ICstream {
	ICCstream(Istream&, const CryptKey&);
	ICCstream(const ICCstream&) = delete;
	ICCstream(ICCstream&&) = delete;
	ICCstream& operator=(const ICCstream&) = delete;
	ICCstream& operator=(ICCstream&&) = delete;
	~ICCstream();

	// Read data (decrypt, decompress and after that read)
	void read(void *, size_t);
	void read_nc(void *, size_t); // Read but not calc hash
	Json::Value read_json();
	void read_to_file(const std::string& filename);
	void read_to_tempfile(int fd);
	void skip_file();

	// Read hash and compare with calculated
	void check_hash();

	// Deinitialize and close file descriptor
	void close();
private:
	void fillcache();
	char cache[0x1000]; // decrypted but compressed data
	char * ptr;
	char * cache_end;
	z_stream zctx;
	SHA1_CTX sctx;
	bool initialized = true;
	bool eof = false;
};

// Output Complessed Crypted file
struct OCCstream : OCstream {
	OCCstream(Ostream&, const CryptKey&);
	OCCstream(const OCCstream&) = delete;
	OCCstream(OCCstream&&) = delete;
	OCCstream& operator=(const OCCstream&) = delete;
	OCCstream& operator=(OCCstream&&) = delete;
	~OCCstream();

	// Compress, crypt and write data to file descriptor
	void write(const void *, size_t);
	void write_nc(const void *, size_t);
	void write_json(const Json::Value&);
	void write_file(int fd, const std::string& filename, off_t from, off_t to);
	void write_file(const std::string& filename);

	// Write hash of all written data to file descriptor
	void write_hash();

	// Drop all caches to file and call sync()
	void flush_net();

	void close();
private:
	void flush_cache();
	char cache[0x1000]; // decrypted compressed data
	char * ptr;
	z_stream zctx;
	SHA1_CTX sctx;
	bool initialized = true;
};

// Copy data from begin of 'from_fd' to 'to_filename', write bytes from 'from' untill 'to'
void copy_file_segment(int from_fd, const std::string& to_filename, off_t from, off_t to);
