/*
 * e4crypt.c - ext4 encryption management utility
 *
 * Copyright (c) 2014 Google, Inc.
 *	SHA512 implementation from libtomcrypt.
 *
 * Authors: Michael Halcrow <mhalcrow@google.com>,
 *	Ildar Muslukhov <ildarm@google.com>
 */

#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE
#endif

#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <asm/unistd.h>

#include "ext2fs/ext2_fs.h"

/* special process keyring shortcut IDs */
#define KEY_SPEC_THREAD_KEYRING		-1
#define KEY_SPEC_PROCESS_KEYRING	-2
#define KEY_SPEC_SESSION_KEYRING	-3
#define KEY_SPEC_USER_KEYRING		-4
#define KEY_SPEC_USER_SESSION_KEYRING	-5
#define KEY_SPEC_GROUP_KEYRING		-6

#define KEYCTL_GET_KEYRING_ID		0
#define KEYCTL_DESCRIBE			6
#define KEYCTL_SEARCH			10

typedef __s32 key_serial_t;

#define EXT4_KEY_REF_STR_BUF_SIZE ((EXT4_KEY_DESCRIPTOR_SIZE * 2) + 1)

static long keyctl(int cmd, ...)
{
	va_list va;
	unsigned long arg2, arg3, arg4, arg5;

	va_start(va, cmd);
	arg2 = va_arg(va, unsigned long);
	arg3 = va_arg(va, unsigned long);
	arg4 = va_arg(va, unsigned long);
	arg5 = va_arg(va, unsigned long);
	va_end(va);
	return syscall(__NR_keyctl, cmd, arg2, arg3, arg4, arg5);
}

static const char *hexchars = "0123456789abcdef";
static const size_t hexchars_size = 16;

#define SHA512_LENGTH 64
#define EXT2FS_KEY_TYPE_LOGON "logon"
#define EXT2FS_KEY_DESC_PREFIX "ext4:"
#define EXT2FS_KEY_DESC_PREFIX_SIZE 5

#define MSG_USAGE \
"Usage:\te4crypt -a -n salt [ -k keyring ] [ path ...  ]\n" \
"\te4crypt -s policy path ...\n"

#define EXT4_IOC_ENCRYPTION_POLICY      _IOW('f', 19, struct ext4_encryption_policy)

static int is_path_valid(int argc, char *argv[], int path_start_index)
{
	int x;
	int valid = 1;

	if (path_start_index == argc) {
		printf("At least one path option must be provided.\n");
		return 0;
	}

	for (x = path_start_index; x < argc; x++) {
		int ret = access(argv[x], W_OK);
		if (ret) {
			  printf("%s: %s\n", strerror(errno), argv[x]);
			  valid = 0;
		}
	}

	return valid;
}

static int hex2byte(const char *hex, size_t hex_size, char *bytes,
		    size_t bytes_size)
{
	int x;
	char *h, *l;

	if (hex_size % 2)
		return -EINVAL;
	for (x = 0; x < hex_size; x += 2) {
		h = memchr(hexchars, hex[x], hexchars_size);
		if (!h)
			return -EINVAL;
		l = memchr(hexchars, hex[x + 1], hexchars_size);
		if (!l)
			return -EINVAL;
		if ((x >> 1) >= bytes_size)
			return -EINVAL;
		bytes[x >> 1] = (((unsigned char)(h - hexchars) << 4) +
				 (unsigned char)(l - hexchars));
	}
	return 0;
}

static void set_policy(const char key_descriptor[EXT4_KEY_REF_STR_BUF_SIZE],
		       int argc, char *argv[], int path_start_index)
{
	struct stat st;
	struct ext4_encryption_policy policy;
	int fd;
	int x;
	int rc;

	if (!is_path_valid(argc, argv, path_start_index)) {
		printf("Invalid path.\n");
		exit(1);
	}

	if (!key_descriptor || 
	    (strlen(key_descriptor) != (EXT4_KEY_DESCRIPTOR_SIZE * 2))) {
		printf("Invalid key descriptor [%s]. Valid characters are "
		       "0-9 and a-f, lower case. Length must be %d.\n",
		       key_descriptor, (EXT4_KEY_DESCRIPTOR_SIZE * 2));
		exit(1);
	}

	for (x = path_start_index; x < argc; x++) {
		stat(argv[x], &st);
		if (!S_ISDIR(st.st_mode)) {
			printf("You may only set policy on directories.\n");
			exit(1);
		}
		policy.version = 0;
		policy.contents_encryption_mode =
			EXT4_ENCRYPTION_MODE_AES_256_XTS;
		policy.filenames_encryption_mode =
			EXT4_ENCRYPTION_MODE_AES_256_CBC;
		if (hex2byte(key_descriptor, (EXT4_KEY_DESCRIPTOR_SIZE * 2),
			     policy.master_key_descriptor,
			     EXT4_KEY_DESCRIPTOR_SIZE)) {
			printf("Invalid key descriptor [%s]. Valid characters "
			       "are 0-9 and a-f, lower case.\n",
			       key_descriptor);
			exit(1);
		}
		fd = open(argv[x], O_DIRECTORY);
		if (fd == -1) {
			printf("Cannot open directory [%s]: [%s].\n", argv[x],
			       strerror(errno));
			exit(1);
		}
		rc = ioctl(fd, EXT4_IOC_ENCRYPTION_POLICY, &policy);
		close(fd);
		if (rc) {
			printf("Error [%s] setting policy.\nThe key descriptor "
			       "[%s] may not match the existing encryption "
			       "context for directory [%s].\n",
			       strerror(errno), key_descriptor, argv[x]);
			exit(1);
		}
		printf("Key with descriptor [%s%s] successfully applied "
		       "to directory [%s].\n", EXT2FS_KEY_DESC_PREFIX,
		       key_descriptor, argv[x]);
	}
}

static void pbkdf2_sha512(const char *passphrase, const char *salt, int count,
			  char derived_key[EXT4_MAX_KEY_SIZE])
{
	size_t salt_size = strlen(salt);
	size_t passphrase_size = strlen(passphrase);
	char buf[SHA512_LENGTH + EXT4_MAX_PASSPHRASE_SIZE] = {0};
	char tempbuf[SHA512_LENGTH] = {0};
	char final[SHA512_LENGTH] = {0};
	char saltbuf[EXT4_MAX_SALT_SIZE + EXT4_MAX_PASSPHRASE_SIZE] = {0};
	int actual_buf_len = SHA512_LENGTH + passphrase_size;
	int actual_saltbuf_len = EXT4_MAX_SALT_SIZE + passphrase_size;
	int x, y;
	__u32 *final_u32 = (__u32 *)final;
	__u32 *temp_u32 = (__u32 *)tempbuf;

	if (passphrase_size > EXT4_MAX_PASSPHRASE_SIZE) {
		printf("Salt size is %d; max is %d.\n", passphrase_size,
		       EXT4_MAX_PASSPHRASE_SIZE);
		exit(1);
	}
	if (salt_size > EXT4_MAX_SALT_SIZE) {
		printf("Salt size is %d; max is %d.\n", salt_size,
		       EXT4_MAX_SALT_SIZE);
		exit(1);
	}
	assert(EXT4_MAX_KEY_SIZE <= SHA512_LENGTH);

	if (hex2byte(salt, strlen(salt), saltbuf, sizeof(saltbuf))) {
		printf("Invalid salt hex value: [%s]. Valid characters are "
		       "0-9 and a-f, lower case.\n", salt);
		exit(1);
	}
	memcpy(&saltbuf[EXT4_MAX_SALT_SIZE], passphrase, passphrase_size);

	memcpy(&buf[SHA512_LENGTH], passphrase, passphrase_size);

	for (x = 0; x < count; ++x) {
		if (x == 0) {
			ext2fs_sha512(saltbuf, actual_saltbuf_len, tempbuf);
		} else {
			/*
			 * buf: [previous hash || passphrase]
			 */
			memcpy(buf, tempbuf, SHA512_LENGTH);
			ext2fs_sha512(buf, actual_buf_len, tempbuf);
		}
		for (y = 0; y < (sizeof(final) / sizeof(*final_u32)); ++y)
			final_u32[y] = final_u32[y] ^ temp_u32[y];
	}
	memcpy(derived_key, final, EXT4_MAX_KEY_SIZE);
}

static int disable_echo(struct termios *saved_settings)
{
	struct termios current_settings;
	int rc = 0;

	rc = tcgetattr(0, &current_settings);
	if (rc)
		return rc;
	*saved_settings = current_settings;
	current_settings.c_lflag &= ~ECHO;
	rc = tcsetattr(0, TCSANOW, &current_settings);

	return rc;
}

struct keyring_map {
	char name[4];
	size_t name_len;
	int code;
};

static const struct keyring_map keyrings[] = {
	{"@us", 3, KEY_SPEC_USER_SESSION_KEYRING},
	{"@u", 2, KEY_SPEC_USER_KEYRING},
	{"@s", 2, KEY_SPEC_SESSION_KEYRING},
	{"@g", 2, KEY_SPEC_GROUP_KEYRING},
	{"@p", 2, KEY_SPEC_PROCESS_KEYRING},
	{"@t", 2, KEY_SPEC_THREAD_KEYRING},
};

static int get_keyring_id(const char *keyring)
{
	int x;
	char *end;

	/*
	 * If no keyring is specified, by default use either the user
	 * session key ring or the session keyring.  Fetching the
	 * session keyring will return the user session keyring if no
	 * session keyring has been set.
	 *
	 * We need to do this instead of simply adding the key to
	 * KEY_SPEC_SESSION_KEYRING since trying to add a key to a
	 * session keyring that does not yet exist will cause the
	 * kernel to create a session keyring --- which wil then get
	 * garbage collected as soon as e4crypt exits.
	 *
	 * The fact that the keyctl system call and the add_key system
	 * call treats KEY_SPEC_SESSION_KEYRING differently when a
	 * session keyring does not exist is very unfortunate and
	 * confusing, but so it goes...
	 */
	if (keyring == NULL)
		return keyctl(KEYCTL_GET_KEYRING_ID,
			      KEY_SPEC_SESSION_KEYRING, 0);
	for (x = 0; x < (sizeof(keyrings) / sizeof(keyrings[0])); ++x) {
		if (strcmp(keyring, keyrings[x].name) == 0) {
			return keyrings[x].code;
		}
	}
	x = strtol(keyring, &end, 10);
	if (*end == '\0') {
		if (keyctl(KEYCTL_DESCRIBE, x, NULL, 0) < 0)
			return 0;
		return x;
	}
	return 0;
}

static void insert_key_into_keyring(
	const char *keyring, const char raw_key[EXT4_MAX_KEY_SIZE],
	const char key_ref_str[EXT4_KEY_REF_STR_BUF_SIZE])
{
	int keyring_id = get_keyring_id(keyring);
	struct ext4_encryption_key key;
	char key_ref_full[EXT2FS_KEY_DESC_PREFIX_SIZE +
			  EXT4_KEY_REF_STR_BUF_SIZE];
	int rc;

	if (keyring_id == 0) {
		printf("Invalid keyring [%s].\n", keyring);
		exit(1);
	}
	strcpy(key_ref_full, EXT2FS_KEY_DESC_PREFIX);
	strcpy(&key_ref_full[EXT2FS_KEY_DESC_PREFIX_SIZE], key_ref_str);
	rc = keyctl(KEYCTL_SEARCH, keyring_id, EXT2FS_KEY_TYPE_LOGON,
		    key_ref_full, 0);
	if (rc != -1) {
		printf("Key with descriptor [%s] already exists\n",
		       key_ref_str);
		exit(1);
	} else if ((rc == -1) && (errno != ENOKEY)) {
		printf("keyctl_search failed: %s\n", strerror(errno));
		if (errno == -EINVAL)
			printf("Keyring [%s] is not available.\n", keyring);
		exit(1);
	}
	key.mode = EXT4_ENCRYPTION_MODE_AES_256_XTS;
	memcpy(key.raw, raw_key, EXT4_MAX_KEY_SIZE);
	key.size = EXT4_MAX_KEY_SIZE;
	rc = syscall(__NR_add_key, EXT2FS_KEY_TYPE_LOGON, key_ref_full,
		      (void *)&key, sizeof(key), keyring_id);
	if (rc == -1) {
		if (errno == EDQUOT) {
			printf("Error adding key to keyring; quota exceeded\n");
		} else {
			printf("Error adding key with key descriptor [%s]: "
			       "%s\n", key_ref_str, strerror(errno));
		}
		exit(1);
	} else {
		printf("Key with descriptor [%s] successfully inserted into "
		       "keyring\n", key_ref_str);
	}
}

static void generate_key_ref_str_from_raw_key(
	const char raw_key[EXT4_MAX_KEY_SIZE],
	char key_ref_str[EXT4_KEY_REF_STR_BUF_SIZE])
{
	char key_ref1[SHA512_LENGTH];
	char key_ref2[SHA512_LENGTH];
	int x;

	ext2fs_sha512(raw_key, EXT4_MAX_KEY_SIZE, key_ref1);
	ext2fs_sha512(key_ref1, SHA512_LENGTH, key_ref2);
        for (x = 0; x < EXT4_KEY_DESCRIPTOR_SIZE; ++x) {
                sprintf(&key_ref_str[x * 2], "%.2x",
			(unsigned char)key_ref2[x]);
	}
	key_ref_str[EXT4_KEY_REF_STR_BUF_SIZE - 1] = '\0';
}

static void insert_passphrase_into_keyring(
	const char *keyring, const char *salt,
	char key_ref_str[EXT4_KEY_REF_STR_BUF_SIZE])
{
	char *p;
	char raw_key[EXT4_MAX_KEY_SIZE];
	char passphrase[EXT4_MAX_PASSPHRASE_SIZE];
        struct termios current_settings;

	if (!salt) {
		printf("Please provide a salt.\n");
		exit(1);
	}
	printf("Enter passphrase (echo disabled): ");
	disable_echo(&current_settings);
	p = fgets(passphrase, sizeof(passphrase), stdin);
	tcsetattr(0, TCSANOW, &current_settings);
	printf("\n");
	if (!p) {
		printf("Aborting.\n");
		exit(1);
	}
	p = strrchr(passphrase, '\n');
	if (p)
		*p = '\0';
	pbkdf2_sha512(passphrase, salt, EXT4_PBKDF2_ITERATIONS, raw_key);
	generate_key_ref_str_from_raw_key(raw_key, key_ref_str);
	insert_key_into_keyring(keyring, raw_key, key_ref_str);
	memset(passphrase, 0, sizeof(passphrase));
	memset(raw_key, 0, sizeof(raw_key));
}

static int is_keyring_valid(const char *keyring)
{
	return (get_keyring_id(keyring) != 0);
}

static void process_passphrase(const char *keyring, const char *salt,
			       int argc, char *argv[], int path_start_index)
{
	char key_ref_str[EXT4_KEY_REF_STR_BUF_SIZE];

	if (!is_keyring_valid(keyring)) {
		printf("Invalid keyring name [%s]. Consult keyctl "
		       "documentation for valid names.\n", keyring);
		exit(1);
	}
	insert_passphrase_into_keyring(keyring, salt, key_ref_str);
	if (path_start_index != argc)
		set_policy(key_ref_str, argc, argv, path_start_index);
}

int main(int argc, char *argv[])
{
	char *key_ref_str = NULL;
	char *keyring = NULL;
	char *salt = NULL;
	int add_passphrase = 0;
	int opt;

	if (argc == 1)
		goto fail;
	while ((opt = getopt(argc, argv, "ak:s:n:")) != -1) {
		switch (opt) {
		case 'k':
			/* Specify a keyring. */
			keyring = optarg;
			break;
		case 'a':
			/* Add passphrase-based key to keyring. */
			add_passphrase = 1;
			break;
		case 's':
			/* Set policy on a directory. */
			key_ref_str = optarg;
			break;
		case 'n':
			/* Salt value for passphrase. */
			salt = optarg;
			break;
		default:
			printf("Unrecognized option: %c\n", opt);
			goto fail;
		}
	}
	if (key_ref_str) {
		if (add_passphrase) {
			printf("-s option invalid with -a\n");
			goto fail;
		}
		if (keyring) {
			printf("-s option invalid with -k\n");
			goto fail;
		}
		if (salt) {
			printf("-s option invalid with -n\n");
			goto fail;
		}
		set_policy(key_ref_str, argc, argv, optind);
		exit(0);
	}
	if (add_passphrase) {
		if (!salt) {
			printf("-a option requires -n\n");
			goto fail;
		}
		if (key_ref_str) {
			printf("-a option invalid with -s\n");
			goto fail;
		}
		process_passphrase(keyring, salt, argc, argv, optind);
		exit(0);
	}
fail:
	printf(MSG_USAGE);
	return 1;
}
