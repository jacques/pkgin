/* $Id$ */

/*
 * Copyright (c) 2009, 2010, 2011 The NetBSD Foundation, Inc.
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
#include <regex.h>

void
free_global_pkglists()
{
	free_pkglist(r_plisthead, LIST);
	free_pkglist(l_plisthead, LIST);
}

/**
 * \fn malloc_pkglist
 *
 * \brief Pkglist allocation for all types of lists
 */
Pkglist *
malloc_pkglist(uint8_t type)
{
	Pkglist *pkglist;

	XMALLOC(pkglist, sizeof(Pkglist));

	/*!< Init all the things ! (http://knowyourmeme.com/memes/x-all-the-y) */
	pkglist->type = type;
	pkglist->full = NULL;
	pkglist->name = NULL;
	pkglist->version = NULL;
	pkglist->depend = NULL;
	pkglist->size_pkg = 0;
	pkglist->file_size = 0;
	pkglist->level = 0;

	switch (type) {
	case LIST:
		pkglist->comment = NULL;
		break;
	case DEPTREE:
		pkglist->computed = 0;
		pkglist->keep = 0;
		break;
	case IMPACT:
		pkglist->action = DONOTHING;
		pkglist->old = NULL;
		break;
	}

	return pkglist;
}

/**
 * \fn free_pkglist_entry
 *
 * \brief free a Pkglist single entry
 */
void
free_pkglist_entry(Pkglist *plist, uint8_t type)
{
	XFREE(plist->full);
	XFREE(plist->name);
	XFREE(plist->version);
	XFREE(plist->depend);
	switch (type) {
	case LIST:
		XFREE(plist->comment);
		break;
	case IMPACT:
		XFREE(plist->old);
	}
	XFREE(plist);
}

/**
 * \fn free_pkglist
 *
 * \brief Free all types of package list
 */
void
free_pkglist(Plisthead *plisthead, uint8_t type)
{
	Pkglist *plist;

	if (plisthead == NULL)
		return;

	while (!SLIST_EMPTY(plisthead)) {
		plist = SLIST_FIRST(plisthead);
		SLIST_REMOVE_HEAD(plisthead, next);

		free_pkglist_entry(plist, type);
	}
	XFREE(plisthead);

	plisthead = NULL;
}

/**
 * \fn init_head
 *
 * \brief Init a Plisthead
 */
Plisthead *
init_head(void)
{
	Plisthead *plisthead;

	XMALLOC(plisthead, sizeof(Plisthead));
	SLIST_INIT(plisthead);

	return plisthead;
}

/* compare pkg version */
static int
pkg_is_installed(Plisthead *plisthead, Pkglist *pkg)
{
	Pkglist *pkglist;

	SLIST_FOREACH(pkglist, plisthead, next) {
		/* make sure packages match */
		if (strcmp(pkglist->name, pkg->name) != 0)
			continue;

		/* exact same version */
		if (strcmp(pkglist->version, pkg->version) == 0)
			return 0;

		return version_check(pkglist->full, pkg->full);
	}

	return -1;
}

void
list_pkgs(const char *pkgquery, int lstype)
{
	Pkglist	   	*plist;
	Plisthead 	*plisthead;
	int			rc;
	char		pkgstatus, outpkg[BUFSIZ];

	/* list installed packages + status */
	if (lstype == PKG_LLIST_CMD && lslimit != '\0') {

		if (l_plisthead == NULL)
			return;

		if (r_plisthead != NULL) {

			SLIST_FOREACH(plist, r_plisthead, next) {
				rc = pkg_is_installed(l_plisthead, plist);

				pkgstatus = '\0';

				if (lslimit == PKG_EQUAL && rc == 0)
					pkgstatus = PKG_EQUAL;
				if (lslimit == PKG_GREATER && rc == 1)
					pkgstatus = PKG_GREATER;
				if (lslimit == PKG_LESSER && rc == 2)
					pkgstatus = PKG_LESSER;

				if (pkgstatus != '\0') {
					snprintf(outpkg, BUFSIZ, "%s %c",
						plist->full, pkgstatus);
					printf("%-20s %s\n", outpkg, plist->comment);
				}

			}
		}
		return;
	} /* lstype == LLIST && status */

	if ((plisthead = rec_pkglist(pkgquery)) != NULL) {
		SLIST_FOREACH(plist, plisthead, next)
			printf("%-20s %s\n", plist->full, plist->comment);

		free_pkglist(plisthead, LIST);
	}
}

void
search_pkg(const char *pattern)
{
	Pkglist		 	*plist;
	regex_t		re;
	int			rc;
	char		eb[64], is_inst, outpkg[BUFSIZ];
	int		matched_pkgs;

	matched_pkgs = 0;

	if (r_plisthead != NULL) {
		if ((rc = regcomp(&re, pattern, REG_EXTENDED|REG_NOSUB|REG_ICASE))
			!= 0) {
			regerror(rc, &re, eb, sizeof(eb));
			errx(1, "regcomp: %s: %s", pattern, eb);
		}

		SLIST_FOREACH(plist, r_plisthead, next) {
			is_inst = '\0';

			if (regexec(&re, plist->name, 0, NULL, 0) == 0 ||
				regexec(&re, plist->comment, 0, NULL, 0) == 0) {

				matched_pkgs = 1;

				if (l_plisthead != NULL) {
					rc = pkg_is_installed(l_plisthead, plist);

					if (rc == 0)
						is_inst = PKG_EQUAL;
					if (rc == 1)
						is_inst = PKG_GREATER;
					if (rc == 2)
						is_inst = PKG_LESSER;

				}

				snprintf(outpkg, BUFSIZ, "%s %c", plist->full, is_inst);

				printf("%-20s %s\n", outpkg, plist->comment);
			}
		}

		regfree(&re);

		if (matched_pkgs == 1)
			printf(MSG_IS_INSTALLED_CODE);
		else
			printf(MSG_NO_SEARCH_RESULTS, pattern);
	}
}

/**
 * \fn unique_pkg
 *
 * Returns greatest version package matching in full package name form
 */
char *
unique_pkg(const char *pkgname)
{
	char	*u_pkg = NULL, query[BUFSIZ];

	XMALLOC(u_pkg, sizeof(char) * BUFSIZ);

	/* record if it's a versionned pkgname */
	if (exact_pkgfmt(pkgname))
		snprintf(query, BUFSIZ, UNIQUE_EXACT_PKG, pkgname);
	else
		snprintf(query, BUFSIZ, UNIQUE_PKG, pkgname);

	if (pkgindb_doquery(query, pdb_get_value, u_pkg) != PDB_OK) {
		XFREE(u_pkg);
		return NULL;
	}

	return u_pkg;
}

/* return a pkgname corresponding to a dependency */
Pkglist *
map_pkg_to_dep(Plisthead *plisthead, char *depname)
{
	Pkglist	*plist;

	SLIST_FOREACH(plist, plisthead, next)
		if (pkg_match(depname, plist->full))
			return plist;

	return NULL;
}
