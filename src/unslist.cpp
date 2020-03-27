/* unslist.cpp: Functions to manage the list of unsolvable levels.
 *
 * Copyright (C) 2001-2006 by Brian Raiter, under the GNU General Public
 * License. No warranty. See COPYING for details.
 */

#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>

#include	"defs.h"
#include	"fileio.h"
#include	"unslist.h"
#include	"err.h"

/* The information comprising one entry in the list of unsolvable
 * levels.
 */
typedef	struct unslistentry {
	int			setid;		/* the ID of the level set's name */
	int			levelnum;	/* the level's number */
	int			size;		/* the levels data's compressed size */
	unsigned long	hashval;	/* the levels data's hash value */
	int			note;		/* the entry's annotation ID, if any */
} unslistentry;

/* Whether the module has been initialised
 */
static bool 	initialised = false;


/* The pool of strings. In here are stored the level set names and the
 * annotations. The string IDs are simple offsets from the strings
 * pointer.
 */
static int		stringsused = 0;
static int		stringsallocated = 0;
static char	       *strings = NULL;

/* The list of level set names for which unsolvable levels appear on
 * the list. This list allows the program to quickly find the level
 * set name's string ID.
 */
static int		namescount = 0;
static int		namesallocated = 0;
static int	       *names = NULL;

/* The list of unsolvable levels proper.
 */
static int		listcount = 0;
static int		listallocated = 0;
static unslistentry    *unslist = NULL;

/*
 * Managing the pool of strings.
 */

/* Turn a string ID back into a string pointer. (Note that a string ID
 * of zero will always return a null string, assuming that
 * storestring() has been called at least once.)
 */
#define	getstring(id)	(strings + (id))

/* Make a copy of a string and add it to the string pool. The new
 * string's ID is returned.
 */
static int storestring(char const *str)
{
	int	len;

	len = strlen(str) + 1;
	if (stringsused + len > stringsallocated) {
		stringsallocated = stringsallocated ? 2 * stringsallocated : 256;
		x_type_alloc(char, strings, stringsallocated);
		if (!stringsused) {
			*strings = '\0';
			++stringsused;
		}
	}
	memcpy(strings + stringsused, str, len);
	stringsused += len;
	return stringsused - len;
}

/*
 * Managing the list of set names.
 */

/* Return the string ID of the given set name. If the set name is not
 * already in the list, then if add is TRUE the set name is added to
 * the list; otherwise zero is returned.
 */
static int lookupsetname(char const *name, bool add)
{
	int	i;

	for (i = 0 ; i < namescount ; ++i)
		if (!strcmp(getstring(names[i]), name))
			return names[i];
	if (!add)
		return 0;

	if (namescount >= namesallocated) {
		namesallocated = namesallocated ? 2 * namesallocated : 8;
		x_type_alloc(int, names, namesallocated * sizeof *names);
	}
	names[namescount] = storestring(name);
	return names[namescount++];
}

/*
 * Managing the list of unsolvable levels.
 */

/* Add a new entry with the given data to the list.
 */
static bool addtounslist(int setid, int levelnum,
	int size, unsigned long hashval, int note)
{
	if (listcount == listallocated) {
		listallocated = listallocated ? listallocated * 2 : 16;
		x_type_alloc(unslistentry, unslist, listallocated * sizeof *unslist);
	}
	unslist[listcount].setid = setid;
	unslist[listcount].levelnum = levelnum;
	unslist[listcount].size = size;
	unslist[listcount].hashval = hashval;
	unslist[listcount].note = note;
	++listcount;
	return true;
}

/* Remove all entries for the given level from the list. FALSE is
 * returned if the level was not on the list to begin with.
 */
static bool removefromunslist(int setid, int levelnum)
{
	bool f = false;

	for (int i = 0 ; i < listcount ; ++i) {
		if (unslist[i].setid == setid && unslist[i].levelnum == levelnum) {
			--listcount;
			unslist[i] = unslist[listcount];
			f = true;
		}
	}
	return f;
}

/* Add the information in the given file to the list of unsolvable
 * levels. Errors in the file are flagged but do not prevent the
 * function from reading the rest of the file.
 */
static bool readunslist(fileinfo *file)
{
	char		buf[256], token[256];
	char const	       *p;
	unsigned long	hashval;
	int			setid;
	unsigned int	size;
	int			lineno, levelnum, n;

	setid = 0;
	for (lineno = 1 ; ; ++lineno) {
		n = sizeof buf - 1;
		if (!file->getline(buf, &n, NULL))
			break;
		for (p = buf ; isspace(*p) ; ++p) ;
		if (!*p || *p == '#')
			continue;
		if (sscanf(p, "[%999[^]]]", token) == 1) {
			setid = lookupsetname(token, true);
			continue;
		}
		n = sscanf(p, "%5d: %04X%08lX: %200[^\n\r]",
			&levelnum, &size, &hashval, token);
		if (n > 0 && levelnum > 0 && levelnum < 65536 && setid) {
			if (n == 1) {
				n = sscanf(p, "%*d: %2s", token);
				if (n > 0 && !strcmp(token, "ok")) {
					removefromunslist(setid, levelnum);
					continue;
				}
			} else if (n >= 3) {
				addtounslist(setid, levelnum, size, hashval,
					n == 4 ? storestring(token) : 0);
				continue;
			}
		}
		warn("%s:%d: syntax error", file->getName(), lineno);
	}
	return true;
}

/*
 * Exported functions.
 */

/* Look up the levels that constitute the given series and find which
 * levels appear in the list. Those that do will have the unsolvable
 * field in the gamesetup structure initialized.
 */
int markunsolvablelevels(gameseries *series)
{
	int		count = 0;
	int		setid, i, j;

	for (j = 0 ; j < series->count ; ++j)
		series->games[j].unsolvable = NULL;

	setid = lookupsetname(series->name, false);
	if (!setid)
		return 0;

	for (i = 0 ; i < listcount ; ++i) {
		if (unslist[i].setid != setid)
			continue;
		for (j = 0 ; j < series->count ; ++j) {
			if (series->games[j].number == unslist[i].levelnum
				&& series->games[j].levelsize == unslist[i].size
				&& series->games[j].levelhash == unslist[i].hashval) {
				series->games[j].unsolvable = getstring(unslist[i].note);
				++count;
				break;
			}
		}
	}
	return count;
}

/* Free all memory associated with the list of unsolvable levels.
 */
static void clearunslist(void)
{
	free(unslist);
	listcount = 0;
	listallocated = 0;
	unslist = NULL;

	free(names);
	namescount = 0;
	namesallocated = 0;
	names = NULL;

	free(strings);
	stringsused = 0;
	stringsallocated = 0;
	strings = NULL;
}

/* Read the list of unsolvable levels from the given filename. If the
 * filename does not contain a path, then the function looks for the
 * file in the resource directory and the user's save directory.
 */
void loadunslistfromfile(char const *filename)
{
	fileinfo	file;

	if(!initialised) {
		atexit(clearunslist);
		initialised = true;
	}

	if (!file.open(RESDIR, filename, "r", NULL)) {
		if (!file.open(SETTINGSDIR, filename, "r", NULL)) {
			warn("%s: Failed to load list of unsolvable levels", filename);
			return;
		}
	}

	readunslist(&file);
	file.close();
}
