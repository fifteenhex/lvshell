/*
 * Per-process sandbox: a private scratch directory on the /run tmpfs used as
 * the launched process's $HOME, created before the process starts and removed
 * after it exits (so nothing it writes persists, and it can't scribble on the
 * read-only rootfs or the data partition). See sandbox.h.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <sys/stat.h>

#include "debug.h"
#include "sandbox.h"

#define SANDBOX_ROOT "/run/lvshell"

static char box[64];

const char *sandbox_prepare(void)
{
	char tmpl[64];

	mkdir(SANDBOX_ROOT, 0755);
	snprintf(tmpl, sizeof(tmpl), "%s/box.XXXXXX", SANDBOX_ROOT);
	if (!mkdtemp(tmpl)) {
		box[0] = 0;
		return NULL;
	}
	strncpy(box, tmpl, sizeof(box) - 1);
	DBG("sandbox: created %s\n", box);
	return box;
}

static int rm_one(const char *path, const struct stat *sb, int type,
		struct FTW *ftw)
{
	(void)sb;
	(void)type;
	(void)ftw;
	remove(path);   /* files then, thanks to FTW_DEPTH, their directories */
	return 0;
}

void sandbox_teardown(void)
{
	if (!box[0])
		return;
	nftw(box, rm_one, 8, FTW_DEPTH | FTW_PHYS);
	DBG("sandbox: destroyed %s\n", box);
	box[0] = 0;
}
