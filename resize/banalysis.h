/*
 * banalysis.h --- Block analysis header file
 */

struct ext2_inode_context {
	ino_t			ino;
	struct ext2_inode *	inode;
	errcode_t		error;
	ext2_brel		brel;
	void *			ctx;
};

struct ext2_block_analyzer_funcs {
	int (*pre_analyze)(ext2_filsys fs,
			   struct ext2_inode_context *icontext,
			   void *private);
	blk_t (*block_analyze)(ext2_filsys fs, blk_t blk,
			       blk_t ref_block, int ref_offset, 
			       struct ext2_inode_context *icontext,
			       void *private);
	void (*post_analyze)(ext2_filsys fs,
			     struct ext2_inode_context *icontext,
			     void *private);
};

errcode_t ext2_block_analyze(ext2_filsys fs,
			     struct ext2_block_analyzer_funcs *funcs,
			     ext2_brel block_relocation_table,
			     void *private);

