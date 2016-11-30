#ifndef BASE_FS_H
# define BASE_FS_H

# include "fsmap.h"
# include "block_range.h"

struct basefs_entry {
	char *path;
	struct block_range *head;
	struct block_range *tail;
};

extern struct fsmap_format base_fs_format;

#endif /* !BASE_FS_H */
