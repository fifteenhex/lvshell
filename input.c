/*
 * Keypad input for the Miyoo Mini. The gpio-keys buttons (see the
 * ssd202d-miyoo-mini dts) are read straight off the input devices and mapped
 * to LVGL navigation keys, so the D-pad drives the focus group. See input.h.
 */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>

#include "lvgl/lvgl.h"
#include "debug.h"
#include "input.h"

#define IN_MAX_FDS 16
#define IN_QUEUE   64

static int         in_fds[IN_MAX_FDS];
static int         in_nfds;
static lv_indev_t *in_indev;

struct kev {
	uint32_t         key;
	lv_indev_state_t state;
};
static struct kev in_q[IN_QUEUE];
static int         in_head, in_tail;
static uint32_t    in_last_key;

/* Miyoo Mini gpio-keys -> LVGL keys. Up/Left step back through the focus
 * group, Down/Right step forward, A/Start activate, B/Select go back. */
static uint32_t map_key(int code)
{
	switch (code) {
	case KEY_UP:
	case KEY_LEFT:
		return LV_KEY_PREV;
	case KEY_DOWN:
	case KEY_RIGHT:
		return LV_KEY_NEXT;
	case KEY_A:      /* A */
	case KEY_ENTER:  /* Start */
		return LV_KEY_ENTER;
	case KEY_B:      /* B */
	case KEY_ESC:    /* Select */
		return LV_KEY_ESC;
	default:
		return 0;
	}
}

static void in_push(uint32_t key, lv_indev_state_t state)
{
	int next = (in_tail + 1) % IN_QUEUE;

	if (next == in_head)
		return;   /* full - drop */
	in_q[in_tail].key = key;
	in_q[in_tail].state = state;
	in_tail = next;
}

static void in_drain(void)
{
	struct input_event ev;

	for (int i = 0; i < in_nfds; i++) {
		while (read(in_fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			uint32_t key;

			if (ev.type != EV_KEY || ev.value == 2)   /* skip autorepeat */
				continue;
			key = map_key(ev.code);
			if (!key)
				continue;
			in_push(key, ev.value ? LV_INDEV_STATE_PRESSED
					       : LV_INDEV_STATE_RELEASED);
		}
	}
}

static void input_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	(void)indev;

	in_drain();
	if (in_head != in_tail) {
		data->key = in_q[in_head].key;
		data->state = in_q[in_head].state;
		in_last_key = in_q[in_head].key;
		in_head = (in_head + 1) % IN_QUEUE;
		data->continue_reading = (in_head != in_tail);
	} else {
		data->key = in_last_key;
		data->state = LV_INDEV_STATE_RELEASED;
	}
}

void input_init(void)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;

	if (d) {
		while ((e = readdir(d)) && in_nfds < IN_MAX_FDS) {
			char path[64];
			int fd;

			if (strncmp(e->d_name, "event", 5) != 0)
				continue;
			snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
			fd = open(path, O_RDONLY | O_NONBLOCK);
			if (fd >= 0)
				in_fds[in_nfds++] = fd;
		}
		closedir(d);
	}
	DBG("input: opened %d input device(s)\n", in_nfds);

	in_indev = lv_indev_create();
	lv_indev_set_type(in_indev, LV_INDEV_TYPE_KEYPAD);
	lv_indev_set_read_cb(in_indev, input_read_cb);
}

void input_set_group(lv_group_t *group)
{
	if (in_indev)
		lv_indev_set_group(in_indev, group);
}
