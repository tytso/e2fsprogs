/*
 * problemP.h --- Private header file for fix_problem()
 *
 * Copyright 1997 by Theodore Ts'o
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

struct e2fsck_problem {
	problem_t	e2p_code;
	const char *	e2p_description;
	char		prompt;
	short		flags;
	problem_t	second_code;
};

struct latch_descr {
	int		latch_code;
	problem_t	question;
	problem_t	end_message;
	int		flags;
};

#define PR_PREEN_OK	0x0001	/* Don't need to do preenhalt */
#define PR_NO_OK	0x0002	/* If user answers no, don't make fs invalid */
#define PR_NO_DEFAULT	0x0004	/* Default to no */
#define PR_MSG_ONLY	0x0008	/* Print message only */
#define PR_FATAL	0x0080	/* Fatal error */
#define PR_AFTER_CODE	0x0100	/* After asking the first question, */
				/* ask another */
#define PR_PREEN_NOMSG	0x0200	/* Don't print a message if we're preening */
#define PR_NOCOLLATE	0x0400	/* Don't collate answers for this latch */
#define PR_NO_NOMSG	0x0800	/* Don't print a message if e2fsck -n */
#define PR_PREEN_NO	0x1000	/* Use No as an answer if preening */

