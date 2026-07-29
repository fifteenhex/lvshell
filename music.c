/*
 * Background chiptune music: play module files from a directory with xmp
 * (libxmp) while the menu is up. See music.h.
 */
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/wait.h>

#include "lvgl/lvgl.h"
#include "util.h"
#include "debug.h"
#include "music.h"

#define XMP_EXE "/usr/bin/xmp"
#define MUSIC_MAX 64

/* User modules first, then the built-in default(s) shipped in the rootfs. */
static const char *music_dirs[] = {
	"/data/music",
	"/usr/share/lvshell/music",
	NULL
};

static pid_t    music_pid;
static char    *music_files[MUSIC_MAX];
static int      music_count;
static int      music_index;      /* currently playing track */
static bool     music_disabled;   /* no player / no audio device */
static uint32_t music_start_tick;

static void music_scan_dir(const char *dir)
{
	static const char *exts[] = {
		".mod", ".xm", ".it", ".s3m", ".mtm", ".med", ".669", ".far",
		".stm", ".ult", ".amf", ".okt", ".ptm", ".gdm", NULL
	};
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d)) && music_count < MUSIC_MAX) {
		size_t nl = strlen(e->d_name);

		for (int i = 0; exts[i]; i++) {
			size_t el = strlen(exts[i]);
			char path[512];

			if (nl < el || strcasecmp(e->d_name + nl - el, exts[i]))
				continue;
			snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
			music_files[music_count++] = strdup(path);
			break;
		}
	}
	closedir(d);
}

void music_init(void)
{
	for (int i = 0; music_dirs[i]; i++)
		music_scan_dir(music_dirs[i]);
}

void music_start(void)
{
	const char *argv[3];

	if (music_disabled || music_pid || music_count == 0)
		return;
	if (access(XMP_EXE, X_OK) != 0) {
		music_disabled = true;
		return;
	}

	/* Play one track at a time so we know which one is playing (for the UI). */
	argv[0] = XMP_EXE;
	argv[1] = music_files[music_index];
	argv[2] = NULL;

	music_start_tick = lv_tick_get();
	music_pid = util_start_cmd(argv[0], (const char * const *)argv, NULL);
	DBG("music: playing [%d/%d] %s pid %d\n", music_index + 1, music_count,
		music_files[music_index], (int)music_pid);
}

void music_stop(void)
{
	if (music_pid > 0) {
		kill(music_pid, SIGTERM);
		waitpid(music_pid, NULL, 0);
		music_pid = 0;
	}
}

void music_poll(void)
{
	if (music_pid <= 0)
		return;
	if (waitpid(music_pid, NULL, WNOHANG) != music_pid)
		return;

	music_pid = 0;
	/* If xmp died almost immediately there is no working audio device; stop
	 * retrying rather than spin. Real modules play for far longer than this. */
	if (lv_tick_get() - music_start_tick < 1000) {
		music_disabled = true;
		DBG("music: xmp exited immediately, disabling background music\n");
		return;
	}
	/* Track finished: advance to the next one (looping the playlist). */
	music_index = (music_index + 1) % music_count;
	music_start();
}

/* Display name (file base name, no extension) of the current track, or NULL. */
const char *music_current(void)
{
	static char name[128];
	const char *base, *dot;
	size_t len;

	if (music_pid <= 0 || music_count == 0)
		return NULL;

	base = strrchr(music_files[music_index], '/');
	base = base ? base + 1 : music_files[music_index];
	dot = strrchr(base, '.');
	len = dot ? (size_t)(dot - base) : strlen(base);
	if (len >= sizeof(name))
		len = sizeof(name) - 1;
	memcpy(name, base, len);
	name[len] = 0;
	return name;
}
