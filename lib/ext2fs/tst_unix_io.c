/*
 * tst_unix_io.c --- unit tests for unix_io.c and io_manager.c
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_PTHREAD
#include <pthread.h>
#include <sys/wait.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

static int test_count;
static int fail_count;

#define CHECK(cond, msg) do { \
	test_count++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
		fail_count++; \
	} \
} while (0)

#define CHECK_RC(rc, expected, msg) do { \
	errcode_t _rc = (rc); \
	errcode_t _exp = (expected); \
	test_count++; \
	if (_rc != _exp) { \
		fprintf(stderr, "FAIL [%s:%d]: %s (got %ld, expected %ld)\n", \
			__FILE__, __LINE__, msg, (long) _rc, (long) _exp); \
		fail_count++; \
	} \
} while (0)

/*
 * Suite 1: io_manager.c generic dispatchers and io_channel_alloc_buf().
 */
static unsigned long mock_last_blk32;
static int mock_opt_count;

static errcode_t mock_read_blk32(io_channel ch EXT2FS_ATTR((unused)),
				 unsigned long block,
				 int count EXT2FS_ATTR((unused)),
				 void *buf)
{
	mock_last_blk32 = block;
	memset(buf, 0x5a, 16);
	return 0;
}

static errcode_t mock_write_blk32(io_channel ch EXT2FS_ATTR((unused)),
				  unsigned long block,
				  int count EXT2FS_ATTR((unused)),
				  const void *buf EXT2FS_ATTR((unused)))
{
	mock_last_blk32 = block;
	return 0;
}

static errcode_t mock_set_option(io_channel ch EXT2FS_ATTR((unused)),
				 const char *option, const char *arg)
{
	mock_opt_count++;
	if (strcmp(option, "fail") == 0)
		return EXT2_ET_INVALID_ARGUMENT;
	if (strcmp(option, "flag_only") == 0 && arg != NULL)
		return EXT2_ET_INVALID_ARGUMENT;
	if (strcmp(option, "key") == 0 && (!arg || strcmp(arg, "val") != 0))
		return EXT2_ET_INVALID_ARGUMENT;
	return 0;
}

static void test_io_manager_dispatch_and_alloc(void)
{
	struct struct_io_manager	mgr;
	struct struct_io_channel	ch;
	char				buf[64];
	void				*abuf = NULL;

	memset(&mgr, 0, sizeof(mgr));
	mgr.magic = EXT2_ET_MAGIC_IO_MANAGER;
	mgr.name = "mock32";
	mgr.read_blk = mock_read_blk32;
	mgr.write_blk = mock_write_blk32;

	memset(&ch, 0, sizeof(ch));
	ch.magic = EXT2_ET_MAGIC_IO_CHANNEL;
	ch.manager = &mgr;
	ch.block_size = 1024;

	/* Unimplemented optional operations */
	CHECK_RC(io_channel_set_options(&ch, NULL), 0, "NULL opts no-op");
	CHECK_RC(io_channel_set_options(&ch, "key=val"),
		 EXT2_ET_INVALID_ARGUMENT, "missing set_option rejected");
	CHECK_RC(io_channel_write_byte(&ch, 0, 4, buf),
		 EXT2_ET_UNIMPLEMENTED, "missing write_byte -> UNIMPLEMENTED");
	CHECK_RC(io_channel_discard(&ch, 0, 1),
		 EXT2_ET_UNIMPLEMENTED, "missing discard -> UNIMPLEMENTED");
	CHECK_RC(io_channel_zeroout(&ch, 0, 1),
		 EXT2_ET_UNIMPLEMENTED, "missing zeroout -> UNIMPLEMENTED");
	CHECK_RC(io_channel_cache_readahead(&ch, 0, 1),
		 EXT2_ET_OP_NOT_SUPPORTED,
		 "missing cache_readahead -> OP_NOT_SUPPORTED");

	/* 32-bit fallback vs >= 2^32 rejection */
	mock_last_blk32 = 0;
	memset(buf, 0, sizeof(buf));
	CHECK_RC(io_channel_read_blk64(&ch, 42ULL, -16, buf), 0,
		 "32-bit read_blk fallback");
	CHECK(mock_last_blk32 == 42UL && (unsigned char) buf[0] == 0x5a,
	      "read_blk32 invoked");
	CHECK_RC(io_channel_write_blk64(&ch, 99ULL, -16, buf), 0,
		 "32-bit write_blk fallback");
	CHECK(mock_last_blk32 == 99UL, "write_blk32 invoked");

	CHECK_RC(io_channel_read_blk64(&ch, 1ULL << 32, 1, buf),
		 EXT2_ET_IO_CHANNEL_NO_SUPPORT_64,
		 "read_blk64 >= 2^32 rejected on 32-bit manager");
	CHECK_RC(io_channel_write_blk64(&ch, 1ULL << 32, 1, buf),
		 EXT2_ET_IO_CHANNEL_NO_SUPPORT_64,
		 "write_blk64 >= 2^32 rejected on 32-bit manager");

	/* Multi-option '&' parsing in io_channel_set_options */
	mgr.set_option = mock_set_option;
	mock_opt_count = 0;
	CHECK_RC(io_channel_set_options(&ch, "key=val&flag_only"), 0,
		 "multi-option '&' parsing");
	CHECK(mock_opt_count == 2, "both options dispatched");
	CHECK_RC(io_channel_set_options(&ch, "key=val&fail=1&flag_only"),
		 EXT2_ET_INVALID_ARGUMENT, "mid-chain option error propagated");
	CHECK(mock_opt_count == 4, "stopped immediately on error");

	/* io_channel_alloc_buf: count == 0, > 0, < 0, and aligned */
	CHECK_RC(io_channel_alloc_buf(&ch, 0, &abuf), 0, "alloc_buf count=0");
	memset(abuf, 0x11, 1024);
	ext2fs_free_mem(&abuf);

	CHECK_RC(io_channel_alloc_buf(&ch, 3, &abuf), 0, "alloc_buf count=3");
	memset(abuf, 0x22, 3072);
	ext2fs_free_mem(&abuf);

	CHECK_RC(io_channel_alloc_buf(&ch, -256, &abuf), 0,
		 "alloc_buf count=-256");
	memset(abuf, 0x33, 256);
	ext2fs_free_mem(&abuf);

	ch.align = 4096;
	CHECK_RC(io_channel_alloc_buf(&ch, -128, &abuf), 0,
		 "alloc_buf aligned 4096");
	CHECK((((uintptr_t) abuf) & 4095U) == 0, "buffer is 4096-aligned");
	memset(abuf, 0x44, 4096);
	ext2fs_free_mem(&abuf);

	printf("  test_io_manager_dispatch_and_alloc: ok\n");
}

static int create_temp_image(char *tmpl, size_t size)
{
	int fd;

	strcpy(tmpl, "/tmp/tst_unix_io.XXXXXX");
	fd = mkstemp(tmpl);
	if (fd < 0) {
		perror("mkstemp");
		exit(1);
	}
	if (ftruncate(fd, (off_t) size) < 0) {
		perror("ftruncate");
		close(fd);
		unlink(tmpl);
		exit(1);
	}
	return fd;
}

/*
 * Suite 2: unix_io_manager / unixfd_io_manager open, refcount, stat helpers.
 */
static void test_unix_open_and_unixfd(void)
{
	char			path[64], fd_str[32];
	char			wbuf[1024], rbuf[1024];
	int			fd, reopen_fd;
	io_channel		io = NULL;
	ext2fs_struct_stat	st1, st2;

	CHECK_RC(unix_io_manager->open(NULL, 0, &io),
		 EXT2_ET_BAD_DEVICE_NAME, "unix_open NULL name");
	CHECK_RC(unix_io_manager->open("/tmp/nonexistent_e2fs_tst_xyz_999",
				       0, &io),
		 ENOENT, "unix_open nonexistent path");
	CHECK_RC(unixfd_io_manager->open("-1", 0, &io), EBADF,
		 "unixfd_open invalid fd");

	fd = create_temp_image(path, 64 * 1024);

	CHECK(ext2fs_stat(path, &st1) == 0, "ext2fs_stat");
	CHECK(ext2fs_fstat(fd, &st2) == 0, "ext2fs_fstat");
	CHECK(st1.st_ino == st2.st_ino && st2.st_size == 64 * 1024,
	      "stat/fstat match tempfile");

	reopen_fd = ext2fs_open_file(path, O_RDONLY, 0);
	CHECK(reopen_fd >= 0, "ext2fs_open_file");
	if (reopen_fd >= 0)
		close(reopen_fd);

	snprintf(fd_str, sizeof(fd_str), "%d", fd);
	CHECK_RC(unixfd_io_manager->open(fd_str, 0, &io), 0, "unixfd_open");
	memset(wbuf, 0xa5, sizeof(wbuf));
	CHECK_RC(io_channel_write_blk64(io, 3, 1, wbuf), 0,
		 "unixfd write blk 3");
	io_channel_bumpcount(io);
	CHECK_RC(io_channel_close(io), 0, "close decrements refcount 2 -> 1");
	CHECK_RC(io_channel_close(io), 0, "final close flushes without closing fd");
	/* Verify caller's fd is still open and has flushed data */
	CHECK(fcntl(fd, F_GETFL) != -1, "unixfd_io_manager left fd open");
	memset(rbuf, 0, sizeof(rbuf));
	CHECK(pread(fd, rbuf, sizeof(rbuf), 3 * 1024) == 1024 &&
	      memcmp(wbuf, rbuf, sizeof(rbuf)) == 0,
	      "unixfd close flushed dirty block to underlying fd");

	close(fd);
	unlink(path);
	printf("  test_unix_open_and_unixfd: ok\n");
}

/*
 * Suite 3: Write-back cache hits/misses, LRU eviction, grow/shrink cache
 *          resizing, writethrough mode, multi-block & negative-count I/O,
 *          and set_blksize.
 */
static void test_cache_lru_resize_and_writethrough(void)
{
	char		path[64];
	char		blk[4096], rblk[4096], multi[6 * 1024], rmulti[6 * 1024];
	int		fd, i;
	io_channel	io = NULL;
	io_stats	stats = NULL;
	unsigned long	hits_before;

	fd = create_temp_image(path, 256 * 1024);
	CHECK_RC(unix_io_manager->open(path, IO_FLAG_RW, &io), 0,
		 "unix_open RW");

	/* Write blocks 0..3 into cache, read back for cache hits */
	for (i = 0; i < 4; i++) {
		memset(blk, 0x10 + i, 1024);
		CHECK_RC(io_channel_write_blk64(io, i, 1, blk), 0,
			 "cached write");
	}
	CHECK_RC(io->manager->get_stats(io, &stats), 0, "get_stats");
	hits_before = stats->cache_hits;
	for (i = 0; i < 4; i++) {
		memset(rblk, 0, 1024);
		CHECK_RC(io_channel_read_blk64(io, i, 1, rblk), 0,
			 "cached read");
		memset(blk, 0x10 + i, 1024);
		CHECK(memcmp(blk, rblk, 1024) == 0, "cached read matches");
	}
	CHECK(stats->cache_hits >= hits_before + 4, "cache hits incremented");

	/* Write 16 blocks (> DEFAULT_CACHE_SIZE 8) to trigger LRU eviction */
	for (i = 0; i < 16; i++) {
		memset(blk, 0x30 + i, 1024);
		CHECK_RC(io_channel_write_blk64(io, i, 1, blk), 0,
			 "LRU write");
	}
	/* Grow cache to 16 (activates golden-ratio cache_hash), write 20 blks */
	CHECK_RC(io_channel_set_options(io, "cache_blocks=16"), 0,
		 "grow_cache to 16");
	for (i = 0; i < 20; i++) {
		memset(blk, 0x50 + i, 1024);
		CHECK_RC(io_channel_write_blk64(io, i, 1, blk), 0,
			 "hashed cache write");
	}
	/* Shrink cache to 4 (flushes & frees slots) and verify all 20 blocks */
	CHECK_RC(io_channel_set_options(io, "cache_blocks=4"), 0,
		 "shrink_cache to 4");
	CHECK_RC(io_channel_set_options(io, "cache_blocks=0"),
		 EXT2_ET_INVALID_ARGUMENT, "cache_blocks=0 rejected");
	for (i = 0; i < 20; i++) {
		memset(rblk, 0, 1024);
		CHECK_RC(io_channel_read_blk64(io, i, 1, rblk), 0,
			 "read after resize");
		memset(blk, 0x50 + i, 1024);
		CHECK(memcmp(blk, rblk, 1024) == 0,
		      "data intact across grow/shrink");
	}

	/* Writethrough writes immediately to underlying fd without flush */
	io->flags |= CHANNEL_FLAGS_WRITETHROUGH;
	memset(blk, 0xee, 1024);
	CHECK_RC(io_channel_write_blk64(io, 25, 1, blk), 0,
		 "writethrough write");
	memset(rblk, 0, 1024);
	CHECK(pread(fd, rblk, 1024, 25 * 1024) == 1024 &&
	      memcmp(blk, rblk, 1024) == 0,
	      "writethrough visible immediately on raw fd");
	io->flags &= ~CHANNEL_FLAGS_WRITETHROUGH;

	/* Direct multi-block (> WRITE_DIRECT_SIZE) and negative byte count */
	for (i = 0; i < (int) sizeof(multi); i++)
		multi[i] = (char) (i & 0xff);
	CHECK_RC(io_channel_write_blk64(io, 30, 6, multi), 0,
		 "direct 6-block write");
	memset(rmulti, 0, sizeof(rmulti));
	CHECK_RC(io_channel_read_blk64(io, 30, 6, rmulti), 0,
		 "direct 6-block read");
	CHECK(memcmp(multi, rmulti, sizeof(multi)) == 0,
	      "direct 6-block roundtrip");

	memset(blk, 0x7c, 512);
	CHECK_RC(io_channel_write_blk64(io, 40, -512, blk), 0,
		 "negative byte-count write (-512)");
	memset(rblk, 0, 512);
	CHECK_RC(io_channel_read_blk64(io, 40, -512, rblk), 0,
		 "negative byte-count read (-512)");
	CHECK(memcmp(blk, rblk, 512) == 0, "-512 byte roundtrip");

	/* Switch blocksize 1024 -> 4096 */
	CHECK_RC(io_channel_set_blksize(io, 4096), 0, "set_blksize 4096");
	memset(blk, 0x9b, 4096);
	CHECK_RC(io_channel_write_blk64(io, 12, 1, blk), 0, "write 4K blk 12");
	CHECK_RC(io_channel_flush(io), 0, "flush 4K");
	memset(rblk, 0, 4096);
	CHECK_RC(io_channel_read_blk64(io, 12, 1, rblk), 0, "read 4K blk 12");
	CHECK(memcmp(blk, rblk, 4096) == 0, "4K block roundtrip");
	CHECK(stats->bytes_read > 0 && stats->bytes_written > 0,
	      "io_stats tracked bytes_read and bytes_written");

	CHECK_RC(io_channel_close(io), 0, "close io");
	close(fd);
	unlink(path);
	printf("  test_cache_lru_resize_and_writethrough: ok\n");
}

/*
 * Suite 4: offset=, cache=off/on, IO_FLAG_FORCE_BOUNCE, write_byte,
 *          zeroout, discard, readahead, error handlers, and IO_FLAG_THREADS.
 */
static int read_err_called;
static int write_err_called;
static errcode_t last_write_err;

static errcode_t test_read_err_cb(io_channel ch EXT2FS_ATTR((unused)),
				  unsigned long block EXT2FS_ATTR((unused)),
				  int count EXT2FS_ATTR((unused)),
				  void *data EXT2FS_ATTR((unused)),
				  size_t size EXT2FS_ATTR((unused)),
				  int actual_bytes EXT2FS_ATTR((unused)),
				  errcode_t error)
{
	read_err_called++;
	return error;
}

static errcode_t test_write_err_cb(io_channel ch EXT2FS_ATTR((unused)),
				   unsigned long block EXT2FS_ATTR((unused)),
				   int count EXT2FS_ATTR((unused)),
				   const void *data EXT2FS_ATTR((unused)),
				   size_t size EXT2FS_ATTR((unused)),
				   int actual_bytes EXT2FS_ATTR((unused)),
				   errcode_t error)
{
	write_err_called++;
	last_write_err = error;
	return error;
}

#ifdef HAVE_PTHREAD
struct io_worker_arg {
	io_channel		io;
	unsigned long long	start_blk;
	unsigned char		seed;
	int			failed;
};

static void *io_worker_fn(void *arg)
{
	struct io_worker_arg	*wa = (struct io_worker_arg *) arg;
	unsigned char		wbuf[1024], rbuf[1024];
	int			i;

	for (i = 0; i < 16; i++) {
		memset(wbuf, wa->seed + i, sizeof(wbuf));
		if (io_channel_write_blk64(wa->io, wa->start_blk + i, 1,
					   wbuf) != 0) {
			wa->failed = 1;
			return NULL;
		}
		memset(rbuf, 0, sizeof(rbuf));
		if (io_channel_read_blk64(wa->io, wa->start_blk + i, 1,
					  rbuf) != 0 ||
		    memcmp(wbuf, rbuf, sizeof(wbuf)) != 0) {
			wa->failed = 1;
			return NULL;
		}
	}
	return NULL;
}
#endif

static void test_options_bounce_byte_zero_discard_threads(void)
{
	char		path[64];
	unsigned char	buf[1024], rbuf[1024];
	int		fd, i, all_zero;
	io_channel	io = NULL;
	errcode_t	rc;

	fd = create_temp_image(path, 32 * 1024);

	/* offset= and cache= options + IO_FLAG_FORCE_BOUNCE */
	CHECK_RC(unix_io_manager->open(path, IO_FLAG_RW | IO_FLAG_FORCE_BOUNCE,
				       &io), 0, "open RW | FORCE_BOUNCE");
	CHECK_RC(io_channel_set_options(io, "offset=xyz"),
		 EXT2_ET_INVALID_ARGUMENT, "invalid offset rejected");
	CHECK_RC(io_channel_set_options(io, "cache=bogus"),
		 EXT2_ET_INVALID_ARGUMENT, "invalid cache arg rejected");
	CHECK_RC(io_channel_set_options(io, "unknown_opt=1"),
		 EXT2_ET_INVALID_ARGUMENT, "unknown option rejected");
	CHECK_RC(io_channel_set_options(io, "offset=4096&cache=off"), 0,
		 "set offset=4096 and cache=off");

	memset(buf, 0xaa, sizeof(buf));
	CHECK_RC(io_channel_write_blk64(io, 0, 1, buf), 0,
		 "bounce write blk 0 at offset 4096");
	/* Partial sub-block bounce write (-200 bytes) */
	memset(buf, 0xbb, 200);
	CHECK_RC(io_channel_write_blk64(io, 0, -200, buf), 0,
		 "bounce sub-block write -200");
	memset(rbuf, 0, sizeof(rbuf));
	CHECK_RC(io_channel_read_blk64(io, 0, 1, rbuf), 0,
		 "bounce read blk 0");
	CHECK(rbuf[0] == 0xbb && rbuf[199] == 0xbb && rbuf[200] == 0xaa,
	      "bounce RMW preserved surrounding bytes");
	/* Verify bytes 0..4095 before offset were untouched */
	CHECK(pread(fd, rbuf, 1024, 0) == 1024 && rbuf[0] == 0,
	      "bytes prior to offset=4096 untouched");

	CHECK_RC(io_channel_set_options(io, "offset=0&cache=on"), 0,
		 "reset offset=0 and cache=on");

	/* io_channel_write_byte */
	io->align = 512;
	CHECK_RC(io_channel_write_byte(io, 10, 4, "ABCD"),
		 EXT2_ET_UNIMPLEMENTED, "write_byte with align != 0 rejected");
	io->align = 0;
	CHECK_RC(io_channel_write_byte(io, 500, 10, "0123456789"), 0,
		 "io_channel_write_byte");
	memset(rbuf, 0, sizeof(rbuf));
	CHECK_RC(io_channel_read_blk64(io, 0, 1, rbuf), 0,
		 "read back write_byte block");
	CHECK(memcmp(rbuf + 500, "0123456789", 10) == 0,
	      "write_byte payload verified");

	/* io_channel_zeroout (flush before zeroout!) & NOZEROOUT flag */
	memset(buf, 0xdd, sizeof(buf));
	CHECK_RC(io_channel_write_blk64(io, 2, 1, buf), 0, "write blk 2");
	CHECK_RC(io_channel_flush(io), 0, "flush before zeroout");
	rc = io_channel_zeroout(io, 2, 1);
	CHECK(rc == 0 || rc == EXT2_ET_UNIMPLEMENTED,
	      "zeroout succeeded or cleanly reported UNIMPLEMENTED");
	if (rc == 0) {
		io_channel_set_options(io, "cache=off");
		memset(rbuf, 0xff, sizeof(rbuf));
		CHECK_RC(io_channel_read_blk64(io, 2, 1, rbuf), 0,
			 "read zeroed blk 2");
		all_zero = 1;
		for (i = 0; i < 1024; i++)
			if (rbuf[i] != 0)
				all_zero = 0;
		CHECK(all_zero, "zeroout zeroed block on disk");
	}
	io->flags |= CHANNEL_FLAGS_NOZEROOUT;
	CHECK_RC(io_channel_zeroout(io, 2, 1), EXT2_ET_UNIMPLEMENTED,
		 "CHANNEL_FLAGS_NOZEROOUT honored");

	/* io_channel_discard & NODISCARD flag, plus cache_readahead */
	CHECK_RC(io_channel_flush(io), 0, "flush before discard");
	rc = io_channel_discard(io, 3, 1);
	CHECK(rc == 0 || rc == EXT2_ET_UNIMPLEMENTED,
	      "discard succeeded or cleanly reported UNIMPLEMENTED");
	io->flags |= CHANNEL_FLAGS_NODISCARD;
	CHECK_RC(io_channel_discard(io, 3, 1), EXT2_ET_UNIMPLEMENTED,
		 "CHANNEL_FLAGS_NODISCARD honored");

	rc = io_channel_cache_readahead(io, 0, 4);
	CHECK(rc == 0 || rc == EXT2_ET_OP_NOT_SUPPORTED,
	      "cache_readahead succeeded or OP_NOT_SUPPORTED");

	CHECK_RC(io_channel_close(io), 0, "close RW io");

	/* Error callbacks: read past EOF and write on RO channel */
	CHECK_RC(unix_io_manager->open(path, IO_FLAG_NOCACHE, &io), 0,
		 "open RO | NOCACHE");
	io->read_error = test_read_err_cb;
	io->write_error = test_write_err_cb;
	read_err_called = 0;
	write_err_called = 0;
	memset(rbuf, 0x77, sizeof(rbuf));
	CHECK_RC(io_channel_read_blk64(io, 500, 1, rbuf),
		 EXT2_ET_SHORT_READ, "short read past EOF");
	CHECK(read_err_called == 1 && rbuf[0] == 0,
	      "read_error callback invoked and buffer zero-filled");
	CHECK_RC(io_channel_write_blk64(io, 0, 1, buf), EBADF,
		 "write on RO fd returns EBADF");
	CHECK(write_err_called == 1, "write_error callback invoked");
	CHECK_RC(io_channel_close(io), 0, "close RO io");

	/*
	 * Cached write-back LRU eviction failure must propagate the error
	 * both to channel->write_error and as unix_write_blk64()'s return
	 * value instead of silently returning 0.
	 */
	CHECK_RC(unix_io_manager->open(path, 0, &io), 0, "open RO cached");
	io->write_error = test_write_err_cb;
	write_err_called = 0;
	last_write_err = 0;
	memset(buf, 0x66, sizeof(buf));
	for (i = 0; i < 8; i++)
		CHECK_RC(io_channel_write_blk64(io, i, 1, buf), 0,
			 "fill RO write-back cache");
	CHECK_RC(io_channel_write_blk64(io, 8, 1, buf), EBADF,
		 "dirty LRU eviction error returned from write_blk64");
	CHECK(write_err_called == 1 && last_write_err == EBADF,
	      "write_error received EBADF on dirty cache eviction");
	(void) io_channel_close(io);

#ifdef HAVE_PTHREAD
	/* Concurrent multi-threaded R/W under IO_FLAG_THREADS */
	{
		pthread_t		t1, t2;
		struct io_worker_arg	a1, a2;

		CHECK_RC(unix_io_manager->open(path,
					       IO_FLAG_RW | IO_FLAG_THREADS,
					       &io), 0,
			 "open RW | IO_FLAG_THREADS");
		a1.io = io;
		a1.start_blk = 0;
		a1.seed = 0x20;
		a1.failed = 0;
		a2.io = io;
		a2.start_blk = 16;
		a2.seed = 0x80;
		a2.failed = 0;

		CHECK(pthread_create(&t1, NULL, io_worker_fn, &a1) == 0,
		      "spawn worker 1");
		CHECK(pthread_create(&t2, NULL, io_worker_fn, &a2) == 0,
		      "spawn worker 2");
		pthread_join(t1, NULL);
		pthread_join(t2, NULL);
		CHECK(!a1.failed && !a2.failed,
		      "concurrent IO_FLAG_THREADS workers succeeded");
		CHECK_RC(io_channel_close(io), 0, "close threaded io");
	}

	/*
	 * Verify flush_cached_blocks() with IO_FLAG_THREADS + write_error
	 * does not self-deadlock on CACHE_MTX (both normal flush and
	 * FLUSH_NOLOCK via set_blksize).  Run in a child with alarm(1) so
	 * a deadlock fails cleanly instead of hanging the test suite.
	 */
	{
		pid_t	pid;
		int	status = 0;

		fflush(NULL);
		pid = fork();
		if (pid == 0) {
			io_channel tio = NULL;

			alarm(1);
			if (unix_io_manager->open(path, IO_FLAG_THREADS,
						  &tio) != 0)
				_exit(2);
			tio->write_error = test_write_err_cb;
			write_err_called = 0;
			memset(buf, 0x55, sizeof(buf));
			if (io_channel_write_blk64(tio, 0, 1, buf) != 0 ||
			    io_channel_write_blk64(tio, 1, 1, buf) != 0)
				_exit(3);
			if (io_channel_flush(tio) != EBADF ||
			    write_err_called != 2)
				_exit(4);
			if (io_channel_write_blk64(tio, 2, 1, buf) != 0)
				_exit(5);
			if (io_channel_set_blksize(tio, 2048) != EBADF ||
			    write_err_called != 3)
				_exit(6);
			if (io_channel_close(tio) != 0)
				_exit(7);
			_exit(0);
		}
		CHECK(pid > 0 && waitpid(pid, &status, 0) == pid,
		      "waitpid for threaded flush write_error child");
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
		      "IO_FLAG_THREADS flush + set_blksize with write_error did not deadlock");
	}
#endif

	close(fd);
	unlink(path);
	printf("  test_options_bounce_byte_zero_discard_threads: ok\n");
}

int main(int argc EXT2FS_ATTR((unused)), char *argv[] EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);
	printf("Running tst_unix_io unit tests...\n");

	test_io_manager_dispatch_and_alloc();
	test_unix_open_and_unixfd();
	test_cache_lru_resize_and_writethrough();
	test_options_bounce_byte_zero_discard_threads();

	remove_error_table(&et_ext2_error_table);
	printf("tst_unix_io: %d checks run, %d failed\n",
	       test_count, fail_count);
	return fail_count ? 1 : 0;
}
