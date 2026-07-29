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

static struct app_entry entries[APP_MAX_ENTRIES];
static int              num_entries;

/* The group (app/category) the running discoverer reports its entries under. */
static const char *cur_group = "";

/* The sink discoverers report through. 'dir' and 'icon' may be NULL. */
static void apps_add(const char *title, const char *const argv[],
		     const char *dir, const char *icon)
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
	if (icon)
		strncpy(e->icon, icon, sizeof(e->icon) - 1);
	strncpy(e->group, cur_group, sizeof(e->group) - 1);
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
	apps_add(title, argv, NULL, NULL);
}

static void discover_doom(void)
{
	static const char *dirs[] = { "/data/doom", "/data", "/usr/share/games/doom", NULL };
	static const char *exts[] = { ".wad", NULL };

	if (!path_exists(DOOM_EXE))
		return;

	cur_group = "Doom";
	for_each_data_file(dirs, exts, doom_found);
}

/* Split 'line' in place on runs of >= 2 spaces (the column gaps in scummvm's
 * --detect table) into up to 'max' fields. */
static int split_columns(char *line, char **fields, int max)
{
	int n = 0;
	char *p = line;

	while (*p && n < max) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p || *p == '\n')
			break;
		fields[n++] = p;
		while (*p && *p != '\n' && !(p[0] == ' ' && p[1] == ' '))
			p++;
		if (*p)
			*p++ = 0;
	}

	for (int i = 0; i < n; i++) {
		char *e = fields[i] + strlen(fields[i]);
		while (e > fields[i] && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n'))
			*--e = 0;
	}
	return n;
}

/*
 * Map a scummvm game id ("engine:game") to its icon, installed by the
 * scummvm-icons package as <engine>-<game>.png. Falls back to a generic
 * per-engine <engine>.png. Leaves 'out' empty if no icon is present.
 */
#define SCUMMVM_ICON_DIR "/usr/share/scummvm/icons"

static void scummvm_icon(const char *gameid, char *out, size_t outlen)
{
	char id[128];
	const char *colon;
	size_t i;

	out[0] = 0;

	/* "engine:game" -> "engine-game" */
	for (i = 0; gameid[i] && i < sizeof(id) - 1; i++)
		id[i] = gameid[i] == ':' ? '-' : gameid[i];
	id[i] = 0;

	snprintf(out, outlen, "%s/%s.png", SCUMMVM_ICON_DIR, id);
	if (path_exists(out))
		return;

	/* Generic per-engine icon: keep the part before the '-'. */
	colon = strchr(id, '-');
	if (colon) {
		snprintf(out, outlen, "%s/%.*s.png", SCUMMVM_ICON_DIR,
			 (int)(colon - id), id);
		if (path_exists(out))
			return;
	}

	out[0] = 0;
}

/*
 * ScummVM: ask it which games live under the data dir (no hardcoded game list)
 * and add one entry each, launching the game directly instead of the launcher.
 */
static void discover_scummvm(void)
{
	static const char *exe = "/usr/bin/scummvm";
	FILE *fp;
	char line[1024];
	bool in_table = false;

	if (!path_exists(exe))
		return;

	cur_group = "ScummVM";
	fp = popen("scummvm --recursive --path=/data/scummvm --detect 2>/dev/null", "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		char *f[3];
		char pathopt[600];
		char icon[192];
		const char *argv[4];

		/* Skip everything before the "GameID  Description  Full Path" header. */
		if (!in_table) {
			if (strncmp(line, "GameID", 6) == 0)
				in_table = true;
			continue;
		}
		if (line[0] == '-' || line[0] == '\n')
			continue;
		if (split_columns(line, f, 3) < 3)
			continue;

		/* f[0]=game id, f[1]=description, f[2]=full path */
		snprintf(pathopt, sizeof(pathopt), "--path=%s", f[2]);
		argv[0] = exe;
		argv[1] = pathopt;
		argv[2] = "--auto-detect";
		argv[3] = NULL;
		scummvm_icon(f[0], icon, sizeof(icon));
		apps_add(f[1], argv, NULL, icon[0] ? icon : NULL);
	}

	pclose(fp);
}

/* Mednafen: one entry per ROM found. */
#define MEDNAFEN_EXE "/usr/bin/mednafen"

static void mednafen_found(const char *dir, const char *name)
{
	char path[512];
	const char *argv[] = { MEDNAFEN_EXE, path, NULL };

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	apps_add(name, argv, NULL, NULL);
}

static const char *const mednafen_exts[] = {
	".nes", ".fds", ".gb", ".gbc", ".gba", ".sfc", ".smc", ".md", ".gen",
	".sms", ".gg", ".pce", ".lnx", ".ngp", ".ngc", ".ws", ".wsc", NULL
};

static void discover_mednafen(void)
{
	static const char *dirs[] = { "/data/roms", "/data", NULL };
	const char *roms = "/data/roms";
	DIR *dp;
	struct dirent *e;

	if (!path_exists(MEDNAFEN_EXE))
		return;

	cur_group = "Mednafen";
	for_each_data_file(dirs, mednafen_exts, mednafen_found);

	/* ROMs are organised in a per-system sub-directory of /data/roms; scan
	 * each of those too. */
	dp = opendir(roms);
	if (!dp)
		return;
	while ((e = readdir(dp))) {
		char sub[256];
		const char *subdir[2] = { sub, NULL };

		if (e->d_name[0] == '.')
			continue;
		snprintf(sub, sizeof(sub), "%s/%s", roms, e->d_name);
		for_each_data_file(subdir, mednafen_exts, mednafen_found);
	}
	closedir(dp);
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
