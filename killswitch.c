/*
 * Menu-button kill switch. See killswitch.h.
 *
 * The evdev devices broadcast to every reader, so we can watch the Menu button
 * from here without disturbing the game's own input. Holding Menu for more than
 * ten seconds is deliberately awkward, so it only ever triggers on purpose - to
 * force-quit a game that has otherwise become unresponsive.
 */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>

#include "lvgl/lvgl.h"
#include "killswitch.h"

#define KS_MAX_FDS  16
#define KS_HOLD_MS  10000       /* > 10 seconds */
#define KS_MENU_KEY KEY_MENU    /* 139, per the miyoo-mini dts */

static int      ks_fds[KS_MAX_FDS];
static int      ks_nfds;
static bool     ks_down;        /* Menu currently pressed */
static uint32_t ks_down_ms;     /* tick when it went down */

void killswitch_init(void)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;

	ks_nfds = 0;
	if (!d)
		return;
	while ((e = readdir(d)) && ks_nfds < KS_MAX_FDS) {
		char path[300];
		int fd;

		if (strncmp(e->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd >= 0)
			ks_fds[ks_nfds++] = fd;
	}
	closedir(d);
}

bool killswitch_menu_held(void)
{
	struct input_event ev;
	int i;

	for (i = 0; i < ks_nfds; i++) {
		while (read(ks_fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			if (ev.type != EV_KEY || ev.code != KS_MENU_KEY)
				continue;
			if (ev.value == 1) {            /* press */
				ks_down = true;
				ks_down_ms = lv_tick_get();
			} else if (ev.value == 0) {     /* release */
				ks_down = false;
			}
			/* value 2 is autorepeat: keep the original press time. */
		}
	}

	if (ks_down && lv_tick_elaps(ks_down_ms) >= KS_HOLD_MS) {
		ks_down = false;   /* require a release before it can fire again */
		return true;
	}
	return false;
}
