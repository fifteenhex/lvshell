/*
 * Button test screen: shows the hardware buttons and lights up the matching
 * box when pressed. Reads the input (evdev) devices directly - evdev
 * broadcasts to every reader, so this doesn't disturb the keypad/SDL input.
 * See buttontest.h.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>

#include "lvgl/lvgl.h"
#include "ui.h"
#include "buttontest.h"

#define BT_MAX_FDS 16

static int       bt_fds[BT_MAX_FDS];
static int       bt_nfds;
static lv_obj_t *bt_last_label;

/* Miyoo Mini gpio-keys -> label map (from the ssd202d-miyoo-mini dts). The
 * "Last" readout below also shows the raw keycode for anything not listed. */
static const struct bt_btn {
	int         code;
	const char *label;
} bt_buttons[] = {
	{ KEY_UP,    "Up"     },
	{ KEY_DOWN,  "Down"   },
	{ KEY_LEFT,  "Left"   },
	{ KEY_RIGHT, "Right"  },
	{ KEY_A,     "A"      },
	{ KEY_B,     "B"      },
	{ KEY_X,     "X"      },
	{ KEY_Y,     "Y"      },
	{ KEY_Q,     "L1"     },
	{ KEY_W,     "L2"     },
	{ KEY_E,     "R1"     },
	{ KEY_R,     "R2"     },
	{ KEY_ENTER, "Start"  },
	{ KEY_ESC,   "Select" },
	{ KEY_MENU,  "Menu"   },
	{ KEY_POWER, "Power"  },
};
#define BT_NBTN ((int)(sizeof(bt_buttons) / sizeof(bt_buttons[0])))

static lv_obj_t *bt_indicator[BT_NBTN];

static void bt_open(void)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;

	bt_nfds = 0;
	if (!d)
		return;
	while ((e = readdir(d)) && bt_nfds < BT_MAX_FDS) {
		char path[64];
		int fd;

		if (strncmp(e->d_name, "event", 5) != 0)
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd >= 0)
			bt_fds[bt_nfds++] = fd;
	}
	closedir(d);
}

static void bt_event(int code, int down)
{
	const char *name = NULL;

	for (int i = 0; i < BT_NBTN; i++) {
		if (bt_buttons[i].code != code)
			continue;
		name = bt_buttons[i].label;
		if (bt_indicator[i])
			lv_obj_set_style_bg_color(bt_indicator[i],
				down ? lv_palette_main(LV_PALETTE_GREEN)
				     : lv_palette_darken(LV_PALETTE_GREY, 3), 0);
	}

	if (bt_last_label) {
		if (name)
			lv_label_set_text_fmt(bt_last_label, "Last: %s (KEY %d) %s",
				name, code, down ? "down" : "up");
		else
			lv_label_set_text_fmt(bt_last_label, "Last: KEY %d %s",
				code, down ? "down" : "up");
	}
}

void buttontest_poll(void)
{
	struct input_event ev;

	for (int i = 0; i < bt_nfds; i++)
		while (read(bt_fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
			if (ev.type == EV_KEY && ev.value != 2)   /* skip autorepeat */
				bt_event(ev.code, ev.value);
}

void buttontest_build(void)
{
	lv_obj_t *grid, *hint;

	buttontest_screen = lv_obj_create(NULL);
	screen_group_begin(buttontest_screen);
	make_header(buttontest_screen, "Button Test", nav_back_settings);

	grid = lv_obj_create(buttontest_screen);
	lv_obj_set_size(grid, lv_pct(96), lv_pct(56));
	lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 45);
	lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY,
			LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	for (int i = 0; i < BT_NBTN; i++) {
		lv_obj_t *cell = lv_obj_create(grid);
		lv_obj_t *l;

		lv_obj_set_size(cell, 84, 46);
		lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_bg_color(cell, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
		l = lv_label_create(cell);
		lv_label_set_text(l, bt_buttons[i].label);
		lv_obj_center(l);
		bt_indicator[i] = cell;
	}

	bt_last_label = lv_label_create(buttontest_screen);
	lv_label_set_text(bt_last_label, "Last: -");
	lv_obj_align(bt_last_label, LV_ALIGN_BOTTOM_MID, 0, -30);

	hint = lv_label_create(buttontest_screen);
	lv_label_set_text(hint, "Press a button; its box lights up.");
	lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

	bt_open();
}
