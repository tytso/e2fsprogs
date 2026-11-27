/*
 * tst_atexit.c --- unit tests for atexit.c
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"
#include "ext2fsP.h"

static int test_count;
static int fail_count;
static int pipe_write_fd = -1;

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

static void exit_cb_alpha(void *data)
{
	const char *tag = (const char *) data;

	if (pipe_write_fd >= 0 && tag) {
		if (write(pipe_write_fd, tag, 1) < 0)
			return;
	}
}

static void exit_cb_beta(void *data)
{
	const char *tag = (const char *) data;

	if (pipe_write_fd >= 0 && tag) {
		if (write(pipe_write_fd, tag, 1) < 0)
			return;
	}
}

static void test_invalid_and_empty_ops(void)
{
	int dummy = 42;

	CHECK_RC(ext2fs_add_exit_fn(NULL, &dummy), EXT2_ET_INVALID_ARGUMENT,
		 "add_exit_fn(NULL) rejected");
	CHECK_RC(ext2fs_remove_exit_fn(NULL, &dummy), EXT2_ET_INVALID_ARGUMENT,
		 "remove_exit_fn(NULL) rejected");
	CHECK_RC(ext2fs_remove_exit_fn(exit_cb_alpha, &dummy), 0,
		 "remove_exit_fn on empty list is a no-op");
}

#ifndef _WIN32
/*
 * Read everything written to the pipe until the child exits and the
 * pipe is closed.  The exit callbacks each write() one byte, so a
 * single read() may return only some of them.
 */
static ssize_t read_pipe(int fd, char *buf, size_t size)
{
	size_t	n = 0;
	ssize_t	r;

	while (n < size) {
		r = read(fd, buf + n, size - n);
		if (r < 0 && errno == EINTR)
			continue;
		if (r < 0)
			return -1;
		if (r == 0)
			break;
		n += r;
	}
	return n;
}

static void test_fork_lifo_and_slot_reuse(void)
{
	int	pfd[2];
	pid_t	pid;
	int	status = 0;
	ssize_t	n;
	char	buf[16];

	/*
	 * Child 1: register (alpha,"1"), (beta,"2"), (alpha,"3"),
	 * verify duplicate (alpha,"1") fails with EXT2_ET_FILE_EXISTS,
	 * remove non-existent (beta,"9") (no-op), remove (beta,"2"),
	 * then add (beta,"4") reusing the vacated trailing slot.
	 * On normal exit(0), handle_exit() runs LIFO -> writes "431".
	 */
	if (pipe(pfd) < 0) {
		perror("pipe");
		fail_count++;
		return;
	}
	fflush(stdout);
	fflush(stderr);
	pid = fork();
	if (pid < 0) {
		perror("fork");
		close(pfd[0]);
		close(pfd[1]);
		fail_count++;
		return;
	}
	if (pid == 0) {
		close(pfd[0]);
		pipe_write_fd = pfd[1];

		if (ext2fs_add_exit_fn(exit_cb_alpha, (void *) "1") != 0)
			_exit(10);
		if (ext2fs_add_exit_fn(exit_cb_beta, (void *) "2") != 0)
			_exit(11);
		/* Same callback function with distinct data is allowed */
		if (ext2fs_add_exit_fn(exit_cb_alpha, (void *) "3") != 0)
			_exit(12);
		/* Exact duplicate (func, data) rejected */
		if (ext2fs_add_exit_fn(exit_cb_alpha, (void *) "1") !=
		    EXT2_ET_FILE_EXISTS)
			_exit(13);
		/* Removing non-matching data leaves existing items intact */
		if (ext2fs_remove_exit_fn(exit_cb_beta, (void *) "9") != 0)
			_exit(14);
		/* Remove middle entry (beta, "2") */
		if (ext2fs_remove_exit_fn(exit_cb_beta, (void *) "2") != 0)
			_exit(15);
		/* Reuse vacated trailing slot with (beta, "4") */
		if (ext2fs_add_exit_fn(exit_cb_beta, (void *) "4") != 0)
			_exit(16);

		exit(0);
	}
	close(pfd[1]);
	memset(buf, 0, sizeof(buf));
	n = read_pipe(pfd[0], buf, sizeof(buf) - 1);
	close(pfd[0]);
	CHECK(waitpid(pid, &status, 0) == pid, "waitpid child 1");
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
	      "child 1 exited with status 0");
	CHECK(n == 3 && strcmp(buf, "431") == 0,
	      "atexit callbacks fired in LIFO order '431' after slot reuse");

	/*
	 * Child 2: register "A", "B", "C", remove "B" leaving trailing NULL
	 * slot unrefilled; handle_exit() skips NULL slot and writes "CA".
	 */
	if (pipe(pfd) < 0) {
		perror("pipe");
		fail_count++;
		return;
	}
	fflush(stdout);
	fflush(stderr);
	pid = fork();
	if (pid < 0) {
		perror("fork");
		close(pfd[0]);
		close(pfd[1]);
		fail_count++;
		return;
	}
	if (pid == 0) {
		close(pfd[0]);
		pipe_write_fd = pfd[1];

		if (ext2fs_add_exit_fn(exit_cb_alpha, (void *) "A") != 0 ||
		    ext2fs_add_exit_fn(exit_cb_beta, (void *) "B") != 0 ||
		    ext2fs_add_exit_fn(exit_cb_alpha, (void *) "C") != 0 ||
		    ext2fs_remove_exit_fn(exit_cb_beta, (void *) "B") != 0)
			_exit(20);

		exit(0);
	}
	close(pfd[1]);
	memset(buf, 0, sizeof(buf));
	n = read_pipe(pfd[0], buf, sizeof(buf) - 1);
	close(pfd[0]);
	CHECK(waitpid(pid, &status, 0) == pid, "waitpid child 2");
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
	      "child 2 exited with status 0");
	CHECK(n == 2 && strcmp(buf, "CA") == 0,
	      "handle_exit skips trailing NULL slot and runs 'CA'");

	/*
	 * Child 3: register "A", "B", "C", remove "A" and "B" so that
	 * two slots are free, then register and remove "D".  "D" must
	 * really be removed, so handle_exit() only writes "C".
	 */
	if (pipe(pfd) < 0) {
		perror("pipe");
		fail_count++;
		return;
	}
	fflush(stdout);
	fflush(stderr);
	pid = fork();
	if (pid < 0) {
		perror("fork");
		close(pfd[0]);
		close(pfd[1]);
		fail_count++;
		return;
	}
	if (pid == 0) {
		static char	a[] = "A", b[] = "B", c[] = "C", d[] = "D";

		close(pfd[0]);
		pipe_write_fd = pfd[1];

		if (ext2fs_add_exit_fn(exit_cb_alpha, a) != 0 ||
		    ext2fs_add_exit_fn(exit_cb_beta, b) != 0 ||
		    ext2fs_add_exit_fn(exit_cb_alpha, c) != 0 ||
		    ext2fs_remove_exit_fn(exit_cb_alpha, a) != 0 ||
		    ext2fs_remove_exit_fn(exit_cb_beta, b) != 0 ||
		    ext2fs_add_exit_fn(exit_cb_beta, d) != 0 ||
		    ext2fs_remove_exit_fn(exit_cb_beta, d) != 0)
			_exit(30);

		exit(0);
	}
	close(pfd[1]);
	memset(buf, 0, sizeof(buf));
	/* Read until EOF, in case the callbacks' writes arrive separately */
	n = 0;
	while (n < (ssize_t) sizeof(buf) - 1) {
		ssize_t r = read(pfd[0], buf + n, sizeof(buf) - 1 - n);

		if (r <= 0)
			break;
		n += r;
	}
	close(pfd[0]);
	CHECK(waitpid(pid, &status, 0) == pid, "waitpid child 3");
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
	      "child 3 exited with status 0");
	if (n != 1 || strcmp(buf, "C") != 0)
		fprintf(stderr, "child 3 exit callbacks wrote '%s'\n", buf);
	CHECK(n == 1 && strcmp(buf, "C") == 0,
	      "removed callback 'D' not run after slot reuse, 'C' run");
}
#endif

int main(int argc EXT2FS_ATTR((unused)), char **argv EXT2FS_ATTR((unused)))
{
	add_error_table(&et_ext2_error_table);

	test_invalid_and_empty_ops();
#ifndef _WIN32
	test_fork_lifo_and_slot_reuse();
#endif

	if (fail_count) {
		fprintf(stderr, "tst_atexit: %d/%d checks FAILED\n",
			fail_count, test_count);
		return 1;
	}
	printf("tst_atexit: all %d checks passed\n", test_count);
	return 0;
}
