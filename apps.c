/*
 * App/game discovery.
 *
 * A "discoverer" inspects the system - which programs are installed and which
 * data files (WADs, ROMs, ...) exist - and reports the launchable entries it
 * finds through apps_add(). To support a new kind of app, add a discover_*()
 * function and list it in the discoverers[] registry at the bottom.
 */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "apps.h"

#define APP_MAX_ENTRIES 48

static struct app_entry entries[APP_MAX_ENTRIES];
static int              num_entries;

/* The sink discoverers report through. */
static void apps_add(const char *title, const char *const argv[], const char *dir)
{
	struct app_entry *e;
	int i;

	if (num_entries >= APP_MAX_ENTRIES)
		return;

	e = &entries[num_entries++];
	memset(e, 0, sizeof(*e));
	strncpy(e->title, title, sizeof(e->title) - 1);
	for (i = 0; argv[i] && i < APP_MAX_ARGS - 1; i++)
		e->argv[i] = strdup(argv[i]);
	if (dir)
		strncpy(e->dir, dir, sizeof(e->dir) - 1);
}

/**********************************************************************************************************************/
/* Helpers                                                                                                            */
/**********************************************************************************************************************/

static bool path_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static bool ends_with_ci(const char *name, const char *ext)
{
	size_t nl = strlen(name), el = strlen(ext);

	return nl >= el && strcasecmp(name + nl - el, ext) == 0;
}

/*
 * For each file in one of 'dirs' whose name ends with one of 'exts', call cb().
 * Non-existent directories are skipped.
 */
typedef void (*file_cb)(const char *dir, const char *name);

static void for_each_data_file(const char *const *dirs, const char *const *exts, file_cb cb)
{
	int d, x;

	for (d = 0; dirs[d]; d++) {
		DIR *dp = opendir(dirs[d]);
		struct dirent *e;

		if (!dp)
			continue;

		while ((e = readdir(dp))) {
			for (x = 0; exts[x]; x++) {
				if (ends_with_ci(e->d_name, exts[x])) {
					cb(dirs[d], e->d_name);
					break;
				}
			}
		}

		closedir(dp);
	}
}

/**********************************************************************************************************************/
/* Discoverers                                                                                                        */
/**********************************************************************************************************************/

/* chocolate-doom: one entry per IWAD found. */
#define DOOM_EXE "/usr/bin/chocolate-doom"

static void doom_title(const char *file, char *out, size_t outlen)
{
	static const struct { const char *file; const char *name; } known[] = {
		{ "doom2.wad",     "Doom II"              },
		{ "doom.wad",      "The Ultimate Doom"    },
		{ "doom1.wad",     "Doom (shareware)"     },
		{ "plutonia.wad",  "Final Doom: Plutonia" },
		{ "tnt.wad",       "Final Doom: TNT"      },
		{ "freedoom2.wad", "Freedoom: Phase 2"    },
		{ "freedoom1.wad", "Freedoom: Phase 1"    },
		{ NULL, NULL },
	};
	const char *dot;
	int i;

	for (i = 0; known[i].file; i++) {
		if (strcasecmp(file, known[i].file) == 0) {
			snprintf(out, outlen, "%s", known[i].name);
			return;
		}
	}

	dot = strrchr(file, '.');
	snprintf(out, outlen, "Doom (%.*s)", dot ? (int)(dot - file) : (int)strlen(file), file);
}

static void doom_found(const char *dir, const char *name)
{
	char path[512], title[64];
	const char *argv[] = { DOOM_EXE, "-iwad", path, NULL };

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	doom_title(name, title, sizeof(title));
	apps_add(title, argv, NULL);
}

static void discover_doom(void)
{
	static const char *dirs[] = { "/data/doom", "/data", "/usr/share/games/doom", NULL };
	static const char *exts[] = { ".wad", NULL };

	if (!path_exists(DOOM_EXE))
		return;

	for_each_data_file(dirs, exts, doom_found);
}

/* ScummVM manages its own games through its launcher. */
static void discover_scummvm(void)
{
	static const char *exe = "/usr/bin/scummvm";
	const char *argv[] = { exe, NULL };

	if (path_exists(exe))
		apps_add("ScummVM", argv, NULL);
}

/* Mednafen: one entry per ROM found. */
#define MEDNAFEN_EXE "/usr/bin/mednafen"

static void mednafen_found(const char *dir, const char *name)
{
	char path[512];
	const char *argv[] = { MEDNAFEN_EXE, path, NULL };

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	apps_add(name, argv, NULL);
}

static void discover_mednafen(void)
{
	static const char *dirs[] = { "/data/roms", "/data", NULL };
	static const char *exts[] = {
		".nes", ".fds", ".gb", ".gbc", ".gba", ".sfc", ".smc", ".md", ".gen",
		".sms", ".gg", ".pce", ".lnx", ".ngp", ".ngc", ".ws", ".wsc", NULL
	};

	if (!path_exists(MEDNAFEN_EXE))
		return;

	for_each_data_file(dirs, exts, mednafen_found);
}

/**********************************************************************************************************************/

typedef void (*discover_fn)(void);

static const discover_fn discoverers[] = {
	discover_doom,
	discover_scummvm,
	discover_mednafen,
};

int apps_discover(const struct app_entry **out)
{
	unsigned i;

	num_entries = 0;
	for (i = 0; i < sizeof(discoverers) / sizeof(discoverers[0]); i++)
		discoverers[i]();

	*out = entries;
	return num_entries;
}
