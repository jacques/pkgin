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

#include <sqlite3.h>
#include "pkgin.h"

static sqlite3	*pdb;
static char		*pdberr = NULL;
static int		pdbres = 0;
static FILE		*sql_log_fp;

static const char *pragmaopts[] = {
	"cache_size = 1000000",
	"locking_mode = EXCLUSIVE",
	"empty_result_callbacks = 1",
	"synchronous = OFF",
	"journal_mode = MEMORY",
	NULL
};

const char *
pdb_version(void)
{
	return "SQLite "SQLITE_VERSION;
}

static void
pdb_err(const char *errmsg)
{
	warn("%s: %s", errmsg, sqlite3_errmsg(pdb));
	sqlite3_close(pdb);
	exit(EXIT_FAILURE);
}

/*
 * unused: optional parameter given as 4th argument of sqlite3_exec
 * argc  : row number
 * argv  : row
 * col   : column
 *
 *    col[0]    col[1]          col[argc]
 *  ______________________________________
 * | argv[0] | argv[1] | ... | argv[argc] |
 *
 * WARNING: callback is called on every line
 */
static int
pkgindb_simple_callback(void *param, int argc, char **argv, char **colname)
{
	pdbres = argc;

	if (argv == NULL)
		return PDB_ERR;

	return PDB_OK;
}

/* sqlite callback, record a single value */
int
pdb_get_value(void *param, int argc, char **argv, char **colname)
{
	char *value = (char *)param;

	if (argv != NULL) {
		XSTRCPY(value, argv[0]);

		return PDB_OK;
	}

	return PDB_ERR;
}

int
pkgindb_doquery(const char *query,
	int (*pkgindb_callback)(void *, int, char **, char **), void *param)
{
	if (sqlite3_exec(pdb, query, pkgindb_callback, param, &pdberr)
		!= SQLITE_OK) {
		if (sql_log_fp != NULL) {
			if (pdberr != NULL)
				fprintf(sql_log_fp, "SQL error: %s\n", pdberr);
			fprintf(sql_log_fp, "SQL query: %s\n", query);
		}
		sqlite3_free(pdberr);

		return PDB_ERR;
	}

	return PDB_OK;
}

void
pkgindb_close()
{
	sqlite3_close(pdb);

	if (sql_log_fp != NULL)
		fclose(sql_log_fp);
}

uint8_t
upgrade_database()
{
	if (pkgindb_doquery(COMPAT_CHECK,
			pkgindb_simple_callback, NULL) == PDB_ERR) {
		/* COMPAT_CHECK query leads to an error for an incompatible database */
		printf(MSG_DATABASE_NOT_COMPAT);
		if (!check_yesno(DEFAULT_YES))
			exit(EXIT_FAILURE);

		pkgindb_reset();

		return 1;
	}

	return 0;
}

void
pkgindb_init()
{
	int i;
	char buf[BUFSIZ];

	/*
	 * Do not exit if PKGIN_SQL_LOG is not writable.
	 * Permit users to do list-operations
	 */
	sql_log_fp = fopen(PKGIN_SQL_LOG, "w");

	if (sqlite3_open(PDB, &pdb) != SQLITE_OK)
		pdb_err("Can't open database " PDB);

	/* generic query in order to check tables existence */
	if (pkgindb_doquery("select * from sqlite_master;",
			pkgindb_simple_callback, NULL) != PDB_OK)
		pdb_err("Can't access database: %s");

	/* apply PRAGMA properties */
	for (i = 0; pragmaopts[i] != NULL; i++) {
		snprintf(buf, BUFSIZ, "PRAGMA %s;", pragmaopts[i]);
		pkgindb_doquery(buf, NULL, NULL);
	}

	pkgindb_doquery(CREATE_DRYDB, NULL, NULL);
}

/**
 * \brief destroy the database and re-create it (upgrade)
 */
void
pkgindb_reset()
{
	pkgindb_close();

	if (unlink(PDB) < 0)
		err(EXIT_FAILURE, "could not delete database file %s\n", PDB);

	pkgindb_init();
}

#define PKGDB_PATH PKG_DBDIR"/pkgdb.byfile.db"

int
pkg_db_mtime()
{
	uint8_t		pkgdb_present = 1;
	struct stat	st;
	time_t	   	db_mtime = 0;
	char		str_mtime[20], query[BUFSIZ];

	/* no pkgdb file */
	if (stat(PKGDB_PATH, &st) < 0)
		pkgdb_present = 0;

	str_mtime[0] = '\0';

	pkgindb_doquery("SELECT PKGDB_MTIME FROM PKGDB;",
		pdb_get_value, str_mtime);

	if (str_mtime[0] != '\0')
		db_mtime = (time_t)strtol(str_mtime, (char **)NULL, 10);

	/* mtime is up to date */
	if (!pkgdb_present || db_mtime == st.st_mtime)
		return 0;

	snprintf(query, BUFSIZ, UPDATE_PKGDB_MTIME, (long long)st.st_mtime);
	/* update mtime */
	pkgindb_doquery(query, NULL, NULL);

	return 1;
}

void
repo_record(char **repos)
{
	int		i;
	char	query[BUFSIZ], value[20];

	for (i = 0; repos[i] != NULL; i++) {
		snprintf(query, BUFSIZ, EXISTS_REPO, repos[i]);
		pkgindb_doquery(query, pdb_get_value, &value[0]);

		if (value[0] == '0') {
			/* repository does not exists */
			snprintf(query, BUFSIZ, INSERT_REPO, repos[i]);
			pkgindb_doquery(query, NULL, NULL);
		}
	}
}

time_t
pkg_sum_mtime(char *repo)
{
	time_t	db_mtime = 0;
	char	str_mtime[20], query[BUFSIZ];

	str_mtime[0] = '\0';

	snprintf(query, BUFSIZ,
		"SELECT REPO_MTIME FROM REPOS WHERE REPO_URL GLOB \'%s*\';", repo);
	pkgindb_doquery(query, pdb_get_value, str_mtime);

	if (str_mtime[0] != '\0')
		db_mtime = (time_t)strtol(str_mtime, (char **)NULL, 10);

	return db_mtime;
}
