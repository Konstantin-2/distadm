#include "ccstream.h"
#include <sys/stat.h>
#include <libintl.h>
#include <iostream>
#include <cassert>
#include <error.h>
#include "utils.h"
#include "main.h"
#include "exc_error.h"
#include "warn.h"
#include "showdebug.h"
#define _(STRING) gettext(STRING)

using std::copy;
using std::cout;
using std::endl;
using std::string;
using std::min;
using std::exception;
using std::istringstream;

bool dump_crypt = true;
static void dump(const string& str, const void * buf, size_t size)
{
	if (!dump_crypt)
		return;
	cout << str;
	const unsigned char * ptr = (const unsigned char *)buf;
	cout << std::hex;
	for (size_t i = 0; i < size; i++)
		cout << ' ' << (unsigned)*ptr++;
	cout << endl;
}

// ===== Fstream =====

Fstream::Fstream(const string& fname) : filename(fname), net(false)
{
}

Fstream::Fstream(int f, const string& fname, bool n) : filename(fname), fd(f), net(n)
{
}

Fstream::Fstream(Fstream&& src) :
	filename(src.filename),
	fd(src.fd),
	net(src.net)
{
}

Fstream::~Fstream()
{
	try {
		close();
	} catch (const exception& exc) {
		warn << exc.what();
	}
}

Fstream Fstream::create(const string& fname)
{
	Fstream res;
	res.fd = ::createfile(fname);
	res.filename = fname;
	return res;
}

Fstream Fstream::open(const string& fname, int mode)
{
	Fstream res;
	res.fd = ::openfile(fname, mode);
	res.filename = fname;
	return res;
}

Fstream Fstream::openrw(const string& fname)
{
	Fstream res;
	res.fd = ::openfile(fname, O_RDWR);
	res.filename = fname;
	return res;
}

Fstream Fstream::use(int fd, const string& fname)
{
	Fstream res;
	res.fd = fd;
	res.filename = fname;
	return res;
}

off_t Fstream::filesize() const
{
	struct stat st;
	int ret = fstat(fd, &st);
	if (ret)
		throw exc_errno(_("Error read"), filename);
	return st.st_size;
}

void Fstream::close()
{
	if (fd == -1)
		return;
	closefile(fd, filename);
	fd = -1;
}

void Fstream::seek(off_t x)
{
	off_t ret = lseek(fd, x, SEEK_SET);
	if (ret != x)
		throw exc_errno(_("Error read"), filename);
}

// ===== Istream =====

Istream::Istream(Fstream& b) : StreamBase(b)
{
	ptr = cache;
	cache_end = cache;
}

Istream::~Istream()
{
	try {
		close();
	} catch (const exception& exc) {
		warn << exc.what();
	}
}

void Istream::read(void * buf, size_t size)
{
	char * dst_ptr = (char *)buf;
	char * const dst_end = dst_ptr + size;
	if (size < sizeof(cache)) {
		while (dst_ptr < dst_end) {
			if (ptr == cache_end)
				fillcache();
			ssize_t msize = min(dst_end - dst_ptr, cache_end - ptr);
			memcpy(dst_ptr, ptr, msize);
			ptr += msize;
			dst_ptr += msize;
		}
	} else {
		ssize_t msize = cache_end - ptr;
		memcpy(dst_ptr, ptr, msize);
		dst_ptr += msize;
		ptr = cache;
		cache_end = cache;
		size -= msize;
		ssize_t x = readfile(base.fd, dst_ptr, size, base.filename);
		if (x < (ssize_t)size)
			throw exc_errno(_("Damaged file"), base.filename);
	}
}

Nonce Istream::read_nonce()
{
	Nonce res;
	read(&res, sizeof(res));
	return res;
}

void Istream::peek(void * buf, size_t size)
{
	assert(size <= sizeof(cache));
	if (cache_end - ptr < (ssize_t)size)
		fillcache();
	if (cache_end - ptr < (ssize_t)size)
		throw exc_errno(_("Damaged file"), base.filename);
	copy((char *)buf, (char *)buf + size, ptr);
}

off_t Istream::tell()
{
	off_t x = lseek(base.fd, 0, SEEK_CUR);
	if (x == -1)
		throw exc_errno(_("Error read"), base.filename);
	return x + ptr - cache_end;
}

void Istream::close()
{
	if (ptr == cache_end)
		return;
	off_t ret = lseek(base.fd, ptr - cache_end, SEEK_CUR);
	if (ret == -1)
		throw exc_errno(_("Error read"), base.filename);
	cache_end = ptr;
}

void Istream::fillcache()
{
	size_t msize = cache_end - ptr;
	memmove(cache, ptr, msize);
	ptr = cache;
	cache_end = cache + msize;
	size_t rsize = sizeof(cache) - msize;

	ssize_t x = readsome(base.fd, cache_end, rsize, base.filename);
	if (!x)
		throw exc_error(_("Error read"), base.filename);
	cache_end += x;
}

// ===== Ostream =====

Ostream::Ostream(Fstream& b) : StreamBase(b)
{
	ptr = cache;
}

Ostream::~Ostream()
{
	try {
		close();
	} catch (const exception& exc) {
		warn << exc.what();
	}
}

void Ostream::write(const void * from, size_t size)
{
	const char * src_ptr = (const char *)from;
	const char * const src_end = src_ptr + size;
	if (size < sizeof(cache)) {
		while (src_ptr < src_end) {
			if (ptr == dst_end)
				flush_cache();
			ssize_t msize = min(src_end - src_ptr, dst_end - ptr);
			memcpy(ptr, src_ptr, msize);
			ptr += msize;
			src_ptr += msize;
		}
	} else {
		flush_cache();
		writefile(base.fd, from, size, base.filename);
	}
}

void Ostream::write(const Nonce& nonce)
{
	write(&nonce, sizeof(nonce));
}

void Ostream::flush_cache()
{
	writefile(base.fd, cache, ptr - cache, base.filename);
	ptr = cache;
}

void Ostream::flush_net()
{
	if (!base.net)
		return;
	flush_cache();
	fsync(base.fd);
}

off_t Ostream::tell()
{
	off_t x = lseek(base.fd, 0, SEEK_CUR);
	if (x == -1)
		throw exc_errno(_("Error read"), base.filename);
	return x + ptr - cache;
}

void Ostream::close()
{
	if (!ptr)
		return;
	flush_cache();
	ptr = nullptr;
}

// ===== ICstream =====

ICstream::ICstream(Istream& b, const CryptKey& key) :
	base(b)
{
	Nonce nonce;
	base.read(&nonce, sizeof(nonce));
	init(key, nonce);
}

ICstream::ICstream(Istream& b, const CryptKey& key, const Nonce& nonce) :
	base(b)
{
	init(key, nonce);
}

void ICstream::init(const CryptKey& key, const Nonce& nonce)
{
	const gnutls_cipher_algorithm_t algo = GNUTLS_CIPHER_AES_256_CFB8;
	gnutls_datum_t iv_d, key_d;
	key_d.data = (unsigned char *)&key;
	key_d.size = sizeof(key);
	iv_d.data = (unsigned char *)&nonce;
	iv_d.size = sizeof(nonce);
	assert(gnutls_cipher_get_iv_size(algo) == sizeof(nonce));
	assert(gnutls_cipher_get_key_size(algo) == sizeof(key));
	int res = gnutls_cipher_init(&cctx, algo, &key_d, &iv_d);
	if (res < 0)
		error(1, 0, "GNU TLS error %s", gnutls_strerror(res));

	SHA1Init(&sctx);
	SHA1Update(&sctx, (const uint8_t *)&nonce, sizeof(nonce));
	UUID rndid;
	read(&rndid, sizeof(rndid));
}

ICstream::~ICstream()
{
	if (!cctx)
		return;
	gnutls_cipher_deinit(cctx);
	cctx = nullptr;
}

void ICstream::read(void * buf, size_t size)
{
	read_nh(buf, size);
	SHA1Update(&sctx, (const uint8_t *)buf, size);
}

void ICstream::read_nh(void * buf, size_t size)
{
	char * dst = (char *) buf;
	char * const dst_end = dst + size;
	while (dst < dst_end) {
		if (base.ptr == base.cache_end)
			base.fillcache();
		ssize_t csize = min(dst_end - dst, base.cache_end - base.ptr);
		int ret = gnutls_cipher_decrypt2(cctx, base.ptr, csize, dst, csize);
		if (ret < 0)
			error(1, 0, "GNU TLS error %s", gnutls_strerror(ret));
		//dump("RCrypt:", base.ptr, csize);
		//dump("RPlain:", dst, csize);
		dst += csize;
		base.ptr += csize;
	}
}

void ICstream::check_hash()
{
	char buf1[SHA1_DIGEST_LENGTH];
	SHA1Final((uint8_t *)buf1, &sctx);
	SHA1Init(&sctx);
	SHA1Update(&sctx, (const uint8_t *)&buf1, sizeof(buf1));
	char buf2[SHA1_DIGEST_LENGTH];
	read_nh(&buf2, sizeof(buf2));
	if (memcmp(buf1, buf2, sizeof(buf1)))
		throw exc_error(_("Damaged file"), base.base.filename);
}

// ===== OCstream =====

OCstream::OCstream(Ostream& b, const CryptKey& key) :
	base(b)
{
	Nonce nonce;
	nonce.random(rnd);
	base.write(&nonce, sizeof(nonce));
	init(key, nonce);
	base.flush_net();
}

OCstream::OCstream(Ostream& b, const CryptKey& key, const Nonce& nonce) :
	base(b)
{
	init(key, nonce);
}

void OCstream::init(const CryptKey& key, const Nonce& nonce)
{
	const gnutls_cipher_algorithm_t algo = GNUTLS_CIPHER_AES_256_CFB8;
	gnutls_datum_t iv_d, key_d;
	key_d.data = (unsigned char *)&key;
	key_d.size = sizeof(key);
	iv_d.data = (unsigned char *)&nonce;
	iv_d.size = sizeof(nonce);
	assert(gnutls_cipher_get_iv_size(algo) == sizeof(nonce));
	assert(gnutls_cipher_get_key_size(algo) == sizeof(key));
	int res = gnutls_cipher_init(&cctx, algo, &key_d, &iv_d);
	if (res < 0)
		error(1, 0, "GNU TLS error %s", gnutls_strerror(res));

	SHA1Init(&sctx);
	SHA1Update(&sctx, (const uint8_t *)&nonce, sizeof(nonce));
	UUID rndid;
	rndid.random(rnd);
	write(&rndid, sizeof(rndid));
}

OCstream::~OCstream()
{
	if (!cctx)
		return;
	gnutls_cipher_deinit(cctx);
	cctx = nullptr;
}

void OCstream::write(const void * data, size_t size)
{
	write_nc(data, size);
	SHA1Update(&sctx, (const uint8_t *)data, size);
}

void OCstream::write_nc(const void * data, size_t size)
{
	const char * src = (const char *) data;
	const char * const src_end = src + size;
	while (src < src_end) {
		if (base.ptr == base.dst_end)
			base.flush_cache();
		ssize_t csize = min(src_end - src, base.dst_end - base.ptr);
		int ret = gnutls_cipher_encrypt2(cctx, src, csize, base.ptr, csize);
		if (ret < 0)
			error(1, 0, "GNU TLS error %s", gnutls_strerror(ret));
		//dump("WPlain:", src, csize);
		//dump("WCrypt:", base.ptr, csize);
		src += csize;
		base.ptr += csize;
	}
}

void OCstream::write_hash()
{
	char buf[SHA1_DIGEST_LENGTH];
	SHA1Final((uint8_t *)buf, &sctx);
	SHA1Init(&sctx);
	SHA1Update(&sctx, (const uint8_t *)&buf, sizeof(buf));
	write_nc(&buf, sizeof(buf));
}

// ===== ICCstream =====

ICCstream::ICCstream(Istream& b, const CryptKey& key) :
	ICstream(b, key)
{
	ptr = cache;
	cache_end = cache;
	fillcache();

	zctx.zalloc = nullptr;
	zctx.zfree = nullptr;
	zctx.opaque = nullptr;
	zctx.next_in = (Bytef *)ptr;
	zctx.avail_in = cache_end - ptr;
	int ret = inflateInit(&zctx);
	if (ret != Z_OK)
		throw exc_error(_("Damaged file"), base.base.filename, zctx.msg);
	SHA1Init(&sctx);
	UUID rndid;
	read(&rndid, sizeof(rndid));
}

void ICCstream::fillcache()
{
	assert(cache_end == ptr);
	ptr = cache;
	cache_end = cache;
	if (base.cache_end == base.ptr)
		base.fillcache();
	ssize_t rsize = min(base.cache_end - base.ptr, (ssize_t)sizeof(cache));
	ICstream::read_nh(cache, rsize);
	cache_end = cache + rsize;
}

ICCstream::~ICCstream()
{
	try {
		close();
	} catch (const exception& exc) {
		warn << exc.what();
	}
}

void ICCstream::close()
{
	if (!initialized)
		return;
	if (!eof) {
		ssize_t cnt = 0;
		Bytef buf[0x1000];
		for (;;) {
			if (ptr == cache_end)
				fillcache();
			zctx.next_in = (Bytef *)ptr;
			zctx.avail_in = min(cache_end - ptr, (ssize_t)UINT_MAX); // zlib support 4 bytes only
			zctx.next_out = buf;
			zctx.avail_out = sizeof(buf);
			int err = inflate(&zctx, Z_NO_FLUSH);
			cnt += zctx.next_out - buf;
			//dump("Comp:", ptr, (char*)zctx.next_in - ptr);
			ptr = (char *)zctx.next_in;
			if (err == Z_STREAM_END)
				break;
			if (err != Z_OK)
				throw exc_error(_("Damaged file"), base.base.filename, zctx.msg);
		}
		if (cnt)
			debug << cnt << " compressed bytes left";
	}
	int err = inflateEnd(&zctx);
	if (err != Z_OK)
		error(1, 0, "Zlib error");

	base.ptr -= cache_end - ptr;
	assert(base.ptr >= base.cache);
	initialized = false;
}

void ICCstream::read(void * buf, size_t size)
{
	read_nc(buf, size);
	SHA1Update(&sctx, (const uint8_t *)buf, size);
}

void ICCstream::read_nc(void * buf, size_t size)
{
	zctx.next_out = (Bytef *)buf;
	Bytef * const end_out = zctx.next_out + size;
	//cout << "Read " << size;
	while (zctx.next_out < end_out) {
		if (ptr == cache_end)
			fillcache();
		zctx.next_in = (Bytef *)ptr;
		zctx.avail_in = min(cache_end - ptr, (ssize_t)UINT_MAX);
		zctx.avail_out = min(end_out - zctx.next_out, (ssize_t)UINT_MAX);
		//char * c = (char *)zctx.next_out;
		int err = inflate(&zctx, Z_NO_FLUSH);
		//dump("Comp:", ptr, (char*)zctx.next_in - ptr);
		//dump("Raw:", c, (char*)zctx.next_out - c);
		ptr = (char *)zctx.next_in;
		if (err == Z_OK)
			continue;
		if (err == Z_STREAM_END)
			eof = true;
		if (zctx.next_out < end_out)
			throw exc_error(_("Damaged file"), base.base.filename, zctx.msg);
	}
}

Json::Value ICCstream::read_json()
{
	size_t size;
	read(&size, sizeof(size));
	check_hash();
	string str;
	str.resize(size);
	read(str.data(), size);
	check_hash();
	Json::Value res;
	istringstream(str) >> res;
	return res;
}

void ICCstream::read_to_tempfile(int fd)
{
	char buf[0x10000];
	off_t wsize;
	read(&wsize, sizeof(wsize));
	check_hash();
	while (wsize) {
		ssize_t rsize = min((off_t)sizeof(buf), wsize);
		read(buf, rsize);
		writefile(fd, buf, rsize, "temporary file");
		wsize -= rsize;
	}
	check_hash();
}

void ICCstream::read_to_file(const string& filename)
{
	Fstream f = Fstream::create(filename);
	char buf[0x10000];
	off_t wsize;
	read(&wsize, sizeof(wsize));
	check_hash();
	while (wsize) {
		ssize_t rsize = min((off_t)sizeof(buf), wsize);
		read(buf, rsize);
		writefile(f.fd, buf, rsize, filename);
		wsize -= rsize;
	}
	check_hash();
}

void ICCstream::skip_file()
{
	char buf[0x10000];
	off_t wsize;
	read(&wsize, sizeof(wsize));
	check_hash();
	while (wsize) {
		ssize_t rsize = min((off_t)sizeof(buf), wsize);
		read(buf, rsize);
	}
	check_hash();
}

void ICCstream::check_hash()
{
	char buf1[SHA1_DIGEST_LENGTH];
	SHA1Final((uint8_t *)buf1, &sctx);
	SHA1Init(&sctx);
	SHA1Update(&sctx, (const uint8_t *)&buf1, sizeof(buf1));
	char buf2[SHA1_DIGEST_LENGTH];
	read_nc(&buf2, sizeof(buf2));
	if (memcmp(buf1, buf2, sizeof(buf1)))
		throw exc_error(_("Damaged file"), base.base.filename);
}

// ===== OCCstream =====

OCCstream::OCCstream(Ostream& b, const CryptKey& key) :
	OCstream(b, key)
{
	zctx.zalloc = nullptr;
	zctx.zfree = nullptr;
	zctx.opaque = nullptr;
	int err = deflateInit(&zctx, Z_DEFAULT_COMPRESSION);
	if (err != Z_OK)
		error(1, 0, "Zlib error %s", zctx.msg);
	SHA1Init(&sctx);
	UUID rndid;
	rndid.random(rnd);
	write(&rndid, sizeof(rndid));
	flush_net();
}

OCCstream::~OCCstream()
{
	try {
		close();
	} catch (const exception& exc) {
		warn << exc.what();
	}
}

void OCCstream::write(const void * from, size_t size)
{
	write_nc(from, size);
	SHA1Update(&sctx, (const uint8_t *)from, size);
}

void OCCstream::write_nc(const void * from, size_t size)
{
	Bytef buf[0x1000];
	assert(sizeof(buf) <= UINT_MAX);
	zctx.next_in = (Bytef*)from;
	const unsigned char * const end_in = zctx.next_in + size;
	//cout << "Write " << size << ' ';
	//dump("Raw:", from, size);
	while (zctx.next_in < end_in) {
		zctx.avail_in = min(end_in - zctx.next_in, (ssize_t)UINT_MAX);
		zctx.next_out = buf;
		zctx.avail_out = sizeof(buf);
		int ret = deflate(&zctx, Z_NO_FLUSH);
		if (ret != Z_OK)
			error(1, 0, "Zlib deflate error %s", zctx.msg);
		size_t s = zctx.next_out - buf;
		//dump("Comp:", buf, s);
		OCstream::write_nc(buf, s);
	}
}

void OCCstream::close()
{
	if (!initialized)
		return;
	Bytef buf[0x1000];
	assert(sizeof(buf) <= UINT_MAX);
	zctx.next_in = nullptr;
	zctx.avail_in = 0;
	int ret;
	do {
		zctx.next_out = buf;
		zctx.avail_out = sizeof(buf);
		ret = deflate(&zctx, Z_FINISH);
		if (ret != Z_OK && ret != Z_STREAM_END)
			error(1, 0, "Zlib deflate error %s", zctx.msg);
		size_t s = zctx.next_out - buf;
		//dump("Comp:", buf, s);
		OCstream::write_nc(buf, s);
	} while(ret != Z_STREAM_END);
	int err = deflateEnd(&zctx);
	if (err != Z_OK)
		error(1, 0, "Zlib error");
	initialized = false;
	base.flush_net();
}

void OCCstream::flush_net()
{
	if (!base.base.net)
		return;
	Bytef buf[0x1000];
	assert(sizeof(buf) <= UINT_MAX);
	zctx.next_in = nullptr;
	zctx.avail_in = 0;
	zctx.next_out = buf;
	zctx.avail_out = sizeof(buf);
	int err = deflate(&zctx, Z_SYNC_FLUSH);
	if (err != Z_OK)
		error(1, 0, "Zlib deflate error %s", zctx.msg);
	size_t s = zctx.next_out - buf;
	//dump("Comp:", buf, s);
	OCstream::write_nc(buf, s);
	base.flush_net();
}

void OCCstream::write_json(const Json::Value& json)
{
	string str = compact_string(json);
	size_t size = str.size();
	write(&size, sizeof(size));
	write_hash();
	write(str.data(), size);
	write_hash();
}

void OCCstream::write_file(int fd, const string& fname, off_t from, off_t to)
{
	char buf[0x10000];
	lseek(fd, from, SEEK_SET);
	off_t wsize = to - from;
	write(&wsize, sizeof(wsize));
	write_hash();
	while (wsize) {
		ssize_t rsize = min((off_t)sizeof(buf), wsize);
		ssize_t x = readfile(fd, buf, rsize, fname);
		if (x < rsize)
			throw exc_error("Unexpected end of file", fname);
		write(buf, rsize);
		wsize -= rsize;
		flush_net();
	}
	write_hash();
}

void OCCstream::write_file(const string& filename)
{
	Fstream f = Fstream::open(filename);
	write_file(f.fd, filename, 0, f.filesize());
}

void OCCstream::write_hash()
{
	char buf[SHA1_DIGEST_LENGTH];
	SHA1Final((uint8_t *)buf, &sctx);
	SHA1Init(&sctx);
	SHA1Update(&sctx, (const uint8_t *)&buf, sizeof(buf));
	write_nc(&buf, sizeof(buf));
}

void copy_file_segment(int from_fd, const string& to_filename, off_t from, off_t to)
{
	Fstream out = Fstream::open(to_filename, O_WRONLY | O_CREAT);
	out.seek(from);
	off_t size = to - from;
	char buf[0x100000];
	while (size) {
		off_t csize = min(size, (off_t)sizeof(buf));
		readfile(from_fd, buf, csize, "temporary file");
		writefile(out.fd, buf, csize, to_filename);
		size -= csize;
	}
}
