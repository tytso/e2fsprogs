#!/bin/bash

if [ -z "$1" -o -z "$2" ]; then
	echo "Usage: $0 kernel-file e2fsprogs-file"
	exit 0
fi

# Transform a few things to fit the compatibility things defined in jfs_user.h.
exec sed -e 's/struct kmem_cache/lkmem_cache_t/g' \
	 < "$1" > "$2"
