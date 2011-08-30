/* $Id$ */

/*
 * Copyright (c) 2009, 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emile "iMil" Heitor <imil@NetBSD.org> .
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "pkgin.h"
#include "cmd.h"
#include "lib.h"

static void	usage(void);
static int	find_cmd(const char *);
static void	missing_param(int, int, const char *);
static void ginto(void);

uint8_t		yesflag = 0, noflag = 0, force_update = 0, force_reinstall = 0;
uint8_t		verbosity = 0, package_version = 0;
char		lslimit = '\0';
char		pkgtools_flags[5];
FILE  		*tracefp = NULL;

int
main(int argc, char *argv[])
{
	uint8_t		do_inst = DO_INST; /* by default, do install packages */
	int 		ch;
	struct stat	sb;
	const char	*chrootpath = NULL;

	setprogname(argv[0]);

	if (argc < 2)
		usage();

	while ((ch = getopt(argc, argv, "dhyfFPvVl:nc:t:")) != -1) {
		switch (ch) {
		case 'f':
			force_update = 1;
			break;
		case 'F':
			force_reinstall = 1;
			break;
		case 'y':
			yesflag = 1;
			noflag = 0;
			break;
		case 'n':
			yesflag = 0;
			noflag = 1;
			break;
		case 'v':
			printf("%s %s (using %s)\n", 
				getprogname(), PKGIN_VERSION, pdb_version());
			exit(EXIT_SUCCESS);
			/* NOTREACHED */
		case 'h':
			usage();
			/* NOTREACHED */
		case 'l':
			lslimit = optarg[0];
			break;
		case 'd':
			do_inst = DONT_INST; /* download only */
			break;
		case 'c':
			chrootpath = optarg;
			break;
		case 'V':
			verbosity = 1;
			break;
		case 'P':
			package_version = 1;
			break;
		case 't':
			if ((tracefp = fopen(optarg, "w")) == NULL)
				err(EXIT_FAILURE, MSG_CANT_OPEN_TRACEFILE, optarg);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		fprintf(stderr, MSG_MISSING_CMD);
		usage();
		/* NOTREACHED */
	}

	/* initializations */

	/* enter chroot if -c specified */
	if (chrootpath != NULL) {
		if (chroot(chrootpath) == -1)
			errx(-1, MSG_CHROOT_FAILED);

		if (chdir("/") == -1)
			errx(-1, MSG_CHDIR_FAILED);
	}

	/* for pkg_install */
	unsetenv("PKG_PATH");
	/* create base directories */
	if (stat(pkgin_cache, &sb) < 0)
		create_dirs();

	pkgindb_init();

	/* update local db if pkgdb mtime has changed */
	update_db(LOCAL_SUMMARY, NULL);

	/* split PKG_REPOS env variable and record them */
	split_repos();

	/* find command index */
	ch = find_cmd(argv[0]);

	/* we need packages lists for almost everything */
	if (ch != PKG_UPDT_CMD) /* already loaded by update_db() */
		init_global_pkglists();

	/* fill pkgtools flags */
	if (verbosity)
		strncpy(pkgtools_flags, "-fv", 3);
	else
		strncpy(pkgtools_flags, "-f", 2);

	switch (ch) {
	case PKG_UPDT_CMD: /* update packages db */
		update_db(REMOTE_SUMMARY, NULL);
		break;
	case PKG_SHDDP_CMD: /* show direct depends */
		missing_param(argc, 2, MSG_MISSING_PKGNAME);
		show_direct_depends(argv[1]);
		break;
	case PKG_SHFDP_CMD: /* show full dependency tree */
		missing_param(argc, 2, MSG_MISSING_PKGNAME);
		show_full_dep_tree(argv[1],
			DIRECT_DEPS, MSG_FULLDEPTREE);
		break;
	case PKG_SHRDP_CMD: /* show full reverse dependency tree */
		missing_param(argc, 2, MSG_MISSING_PKGNAME);
		show_full_dep_tree(argv[1],
			LOCAL_REVERSE_DEPS,
			MSG_REVDEPTREE);
		break;
	case PKG_LLIST_CMD:
		list_pkgs(LOCAL_PKGS_QUERY, PKG_LLIST_CMD); /* list local packages */
		break;
	case PKG_RLIST_CMD: /* list available packages */
		list_pkgs(REMOTE_PKGS_QUERY, PKG_RLIST_CMD);
		break;
	case PKG_INST_CMD: /* install a package and its dependencies */
		missing_param(argc, 2, MSG_PKG_ARGS_INST);
		pkgin_install(&argv[1], do_inst);
		break;
	case PKG_UPGRD_CMD: /* upgrade keep-packages */
		pkgin_upgrade(UPGRADE_KEEP);
		break;
	case PKG_FUPGRD_CMD: /* upgrade everything installed */
		pkgin_upgrade(UPGRADE_ALL);
		break;
	case PKG_REMV_CMD: /* remove packages and reverse dependencies */
		missing_param(argc, 2, MSG_PKG_ARGS_RM);
		pkgin_remove(&argv[1]);
		break;
	case PKG_AUTORM_CMD: /* autoremove orphan packages */
		pkgin_autoremove();
		break;
	case PKG_KEEP_CMD: /* mark a package as "keep" (not automatic) */
		missing_param(argc, 2, MSG_PKG_ARGS_KEEP);
		pkg_keep(KEEP, &argv[1]);
		break;
	case PKG_UNKEEP_CMD: /* mark a package as "unkeep" (automatic) */
		missing_param(argc, 2, MSG_PKG_ARGS_UNKEEP);
		pkg_keep(UNKEEP, &argv[1]);
		break;
	case PKG_SHKP_CMD: /* show keep packages */
		show_pkg_keep();
		break;
	case PKG_SRCH_CMD: /* search for package */
		missing_param(argc, 2, MSG_MISSING_SRCH);
		search_pkg(argv[1]);
		break;
	case PKG_CLEAN_CMD: /* clean pkgin's packages cache */
		clean_cache();
		break;
	case PKG_EXPORT_CMD: /* export PKGPATH for keep packages */
		export_keep();
		break;
	case PKG_IMPORT_CMD: /* import for keep packages and install them */
		missing_param(argc, 2, MSG_MISSING_FILENAME);
		import_keep(do_inst, argv[1]);
		break;
	case PKG_GINTO_CMD: /* Miod's request */
		ginto();
		break;
	default:
		usage();
		/* NOTREACHED */
	}

	free_global_pkglists();

	pkgindb_close();

	if (tracefp != NULL)
		fclose(tracefp);

	XFREE(env_repos);
	XFREE(pkg_repos);

	return EXIT_SUCCESS;
}

static void
missing_param(int argc, int nargs, const char *msg)
{
	if (argc < nargs)
		errx(EXIT_FAILURE, msg);
}

/* find command index */
static int
find_cmd(const char *arg)
{
	int i;

	for (i = 0; cmd[i].name != NULL; i++) {
		if (strncmp(arg, cmd[i].name, strlen(cmd[i].name)) == 0 ||
			strncmp(arg, cmd[i].shortcut, strlen(cmd[i].name)) == 0)
			return cmd[i].cmdtype;
	}

	return -1;
}

static void
usage()
{
	int i;

	(void)fprintf(stderr, MSG_USAGE, getprogname());

	printf(MSG_CMDS_SHORTCUTS);

	for (i = 0; cmd[i].name != NULL; i++)
		if (cmd[i].cmdtype != PKG_GINTO_CMD)
			printf("%s (%s) -  %s\n",
				cmd[i].name, cmd[i].shortcut, cmd[i].descr);

	exit(EXIT_FAILURE);
}

void
cleanup(int i)
{
}

static void
ginto()
{
	printf("* 2 oz gin\n* 5 oz tonic water\n* 1 lime wedge\n");
}
