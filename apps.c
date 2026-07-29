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

/* Ask ScummVM which games live under 'dir' and add one entry each, launching
 * the game directly instead of the launcher. */
static void scummvm_detect_path(const char *exe, const char *dir)
{
	char cmd[256];
	FILE *fp;
	char line[1024];
	bool in_table = false;

	snprintf(cmd, sizeof(cmd),
		 "scummvm --recursive --path=%s --detect 2>/dev/null", dir);
	fp = popen(cmd, "r");
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

/*
 * ScummVM: detect games under the user's data dir and under the bundled-games
 * dir (where packages like the Sam & Max demo install), with no hardcoded game
 * list.
 */
static void discover_scummvm(void)
{
	static const char *exe = "/usr/bin/scummvm";
	static const char *dirs[] = {
		"/data/scummvm",
		"/usr/share/games/scummvm",   /* bundled demos */
		NULL,
	};

	if (!path_exists(exe))
		return;

	cur_group = "ScummVM";
	for (int i = 0; dirs[i]; i++)
		scummvm_detect_path(exe, dirs[i]);
}

/*
 * Emulated systems. Each console is its own menu group; the menu shows the
 * *system* (Megadrive, NES, ...), not the emulator. ROMs live in
 * /data/roms/<dir>/ (or flat in /data/roms or /data, matched by extension).
 * The emulator is picked per system - mednafen currently handles all of these,
 * but the table makes it easy to route a system to a different emulator later.
 */
#define MEDNAFEN_EXE "/usr/bin/mednafen"
#define MGBA_EXE     "/usr/bin/mgba"

static const char *const ext_nes[]  = { ".nes", ".fds", NULL };
static const char *const ext_gen[]  = { ".md", ".gen", ".smd", ".bin", NULL };
static const char *const ext_snes[] = { ".sfc", ".smc", NULL };
static const char *const ext_gb[]   = { ".gb", ".gbc", NULL };
static const char *const ext_gba[]  = { ".gba", NULL };
static const char *const ext_sms[]  = { ".sms", NULL };
static const char *const ext_gg[]   = { ".gg", NULL };
static const char *const ext_pce[]  = { ".pce", NULL };
static const char *const ext_lynx[] = { ".lnx", NULL };
static const char *const ext_ngp[]  = { ".ngp", ".ngc", NULL };
static const char *const ext_ws[]   = { ".ws", ".wsc", NULL };

static const struct emu_system {
	const char        *dir;    /* /data/roms/<dir> */
	const char        *name;   /* menu group name */
	const char        *emu;    /* emulator executable */
	const char *const *exts;
} systems[] = {
	{ "nes",        "NES",              MEDNAFEN_EXE, ext_nes  },
	{ "genesis",    "Megadrive",        MEDNAFEN_EXE, ext_gen  },
	{ "snes",       "SNES",             MEDNAFEN_EXE, ext_snes },
	{ "gb",         "Game Boy",         MGBA_EXE,     ext_gb   },
	{ "gba",        "Game Boy Advance", MGBA_EXE,     ext_gba  },
	{ "sms",        "Master System",    MEDNAFEN_EXE, ext_sms  },
	{ "gamegear",   "Game Gear",        MEDNAFEN_EXE, ext_gg   },
	{ "pcengine",   "PC Engine",        MEDNAFEN_EXE, ext_pce  },
	{ "lynx",       "Lynx",             MEDNAFEN_EXE, ext_lynx },
	{ "ngp",        "Neo Geo Pocket",   MEDNAFEN_EXE, ext_ngp  },
	{ "wonderswan", "WonderSwan",       MEDNAFEN_EXE, ext_ws   },
};
#define NUM_SYSTEMS ((int)(sizeof(systems) / sizeof(systems[0])))

static const struct emu_system *cur_sys;

static void rom_found(const char *dir, const char *name)
{
	char path[512];
	const char *argv[6];
	int a = 0;

	snprintf(path, sizeof(path), "%s/%s", dir, name);

	argv[a++] = cur_sys->emu;
	/* mednafen defaults to an oversized window (bigger than the 640x480
	 * panel); force fullscreen so the picture fits the screen. */
	if (!strcmp(cur_sys->emu, MEDNAFEN_EXE)) {
		argv[a++] = "-fs";
		argv[a++] = "1";
	}
	argv[a++] = path;
	argv[a] = NULL;
	apps_add(name, argv, NULL, NULL);
}

static void discover_systems(void)
{
	for (int i = 0; i < NUM_SYSTEMS; i++) {
		const struct emu_system *s = &systems[i];
		char subdir[256];
		const char *dirs[] = { subdir, "/data/roms", "/data", NULL };

		if (!path_exists(s->emu))
			continue;
		cur_sys = s;
		cur_group = s->name;
		snprintf(subdir, sizeof(subdir), "/data/roms/%s", s->dir);
		for_each_data_file(dirs, s->exts, rom_found);
	}
}

/*
 * fake-08: PICO-8 carts. A cart distributed as a .p8.png is an ordinary PNG
 * (the label art with the cart data steganographically tucked in), so the file
 * doubles as the menu icon. Plain-text .p8 carts have no art.
 */
#define FAKE08_EXE "/usr/games/FAKE08"

static void pico8_found(const char *dir, const char *name)
{
	char path[512], title[64];
	const char *argv[] = { FAKE08_EXE, path, NULL };
	const char *icon = NULL;
	size_t tl;

	snprintf(path, sizeof(path), "%s/%s", dir, name);

	/* Title from the filename, minus the .p8.png / .p8 extension. */
	snprintf(title, sizeof(title), "%s", name);
	tl = strlen(title);
	if (ends_with_ci(title, ".p8.png"))
		title[tl - 7] = 0;
	else if (ends_with_ci(title, ".p8"))
		title[tl - 3] = 0;

	/* A .p8.png renders directly as the card art. */
	if (ends_with_ci(name, ".p8.png"))
		icon = path;

	apps_add(title, argv, NULL, icon);
}

static void discover_pico8(void)
{
	static const char *dirs[] = { "/data/pico8", "/data/roms/pico8", "/usr/share/pico-8", NULL };
	static const char *exts[] = { ".p8", ".p8.png", NULL };

	if (!path_exists(FAKE08_EXE))
		return;

	cur_group = "PICO-8";
	for_each_data_file(dirs, exts, pico8_found);
}

/* hatari: Atari ST floppy images, booted with the bundled EmuTOS ROM. */
#define HATARI_EXE "/usr/bin/hatari"
#define HATARI_TOS "/usr/share/hatari/tos.img"

static void atarist_found(const char *dir, const char *name)
{
	char path[512];
	const char *argv[] = { HATARI_EXE, "--tos", HATARI_TOS, path, NULL };

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	apps_add(name, argv, NULL, NULL);
}

static void discover_atarist(void)
{
	static const char *dirs[] = { "/data/atarist", "/data/roms/atarist", NULL };
	static const char *exts[] = { ".st", ".stx", ".msa", ".dim", ".ipf", NULL };

	if (!path_exists(HATARI_EXE))
		return;

	cur_group = "Atari ST";
	for_each_data_file(dirs, exts, atarist_found);
}

/**********************************************************************************************************************/

typedef void (*discover_fn)(void);

static const discover_fn discoverers[] = {
	discover_doom,
	discover_scummvm,
	discover_systems,
	discover_pico8,
	discover_atarist,
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
