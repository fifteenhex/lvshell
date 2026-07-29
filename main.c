/* needed for usleep() */
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
/*To fix SDL's "undefined reference to WinMain" issue*/
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/sdl/lv_sdl_window.h"
#include "lvgl/src/drivers/sdl/lv_sdl_mouse.h"
#include "lvgl/src/drivers/sdl/lv_sdl_keyboard.h"

#include "apps.h"
#include "util.h"

#define LVSHELL_HOR_RES 640
#define LVSHELL_VER_RES 480

struct context {
	const struct app_entry *apps;
	int                     num_apps;
	pid_t                   child_pid;
};

static struct context cntx;

static void hal_init_input(void)
{
	lv_group_t *g = lv_group_create();
	lv_group_set_default(g);

	/* Keyboard drives the focus group; mouse allows direct clicks. */
	lv_indev_t *kb_indev = lv_sdl_keyboard_create();
	lv_indev_set_group(kb_indev, g);

	lv_sdl_mouse_create();
}

static void hal_init(void)
{
	/* v9's SDL window driver owns the buffers and flush callback. */
	lv_display_t *disp = lv_sdl_window_create(LVSHELL_HOR_RES, LVSHELL_VER_RES);

	lv_theme_t *th = lv_theme_default_init(disp,
			lv_palette_main(LV_PALETTE_BLUE),
			lv_palette_main(LV_PALETTE_RED),
			true, LV_FONT_DEFAULT);
	lv_display_set_theme(disp, th);

	hal_init_input();
}

static void launch_handler(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	const struct app_entry *app = lv_event_get_user_data(e);

	if (code != LV_EVENT_PRESSED)
		return;

	if (cntx.child_pid) {
		printf("Current child is still running...\n");
		return;
	}

	cntx.child_pid = util_start_cmd(app->argv[0], (const char * const *)app->argv,
			app->dir[0] ? app->dir : NULL);
}

/* The main menu is built on its own screen so a splash can show first. */
static lv_obj_t *main_screen;

static void setup_battery(lv_obj_t *parent)
{
	lv_obj_t *batbar = lv_bar_create(parent);
	lv_obj_set_size(batbar, 35, 10);
	lv_obj_align(batbar, LV_ALIGN_TOP_RIGHT, -5, 5);
	lv_bar_set_value(batbar, 50, LV_ANIM_OFF);
}

static void setup_carousell(lv_obj_t *parent)
{
	lv_obj_t *panel = lv_obj_create(parent);
	lv_obj_set_size(panel, lv_pct(100), lv_pct(60));
	lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);

	if (cntx.num_apps == 0) {
		/* Nothing runnable was found - say so rather than show a blank strip. */
		lv_obj_t *label = lv_label_create(panel);
		lv_label_set_text(label,
				"No games found.\n"
				"Add WADs or ROMs under /data and restart.");
		lv_obj_center(label);
		return;
	}

	lv_obj_set_scroll_snap_x(panel, LV_SCROLL_SNAP_CENTER);
	lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);

	for (int i = 0; i < cntx.num_apps; i++) {
		lv_obj_t *btn = lv_button_create(panel);
		lv_obj_set_size(btn, lv_pct(50), lv_pct(100));
		lv_obj_add_event_cb(btn, launch_handler, LV_EVENT_ALL,
				(void *)&cntx.apps[i]);
		lv_obj_t *label = lv_label_create(btn);
		lv_label_set_text(label, cntx.apps[i].title);
		lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
		lv_obj_set_width(label, lv_pct(90));
		lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -10);
	}

	lv_obj_update_snap(panel, LV_ANIM_ON);
}

static void setup_screen_tag(lv_obj_t *parent)
{
	lv_obj_t *ltr_label = lv_label_create(parent);
	lv_label_set_text(ltr_label,
			"Miyoo Mini - Less shitty kernel edition.");
	lv_obj_set_style_text_font(ltr_label, &lv_font_montserrat_16, 0);
	lv_obj_set_width(ltr_label, LV_SIZE_CONTENT);
	lv_obj_align(ltr_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

/**********************************************************************************************************************/
/* Settings / About screens                                                                                           */
/**********************************************************************************************************************/

static lv_obj_t *settings_screen;
static lv_obj_t *about_screen;

static void nav_to(lv_obj_t *scr, lv_screen_load_anim_t anim)
{
	lv_screen_load_anim(scr, anim, 250, 0, false);
}

static void nav_main(lv_event_t *e)          { (void)e; nav_to(main_screen,     LV_SCR_LOAD_ANIM_MOVE_RIGHT); }
static void nav_settings(lv_event_t *e)      { (void)e; nav_to(settings_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT); }
static void nav_about(lv_event_t *e)         { (void)e; nav_to(about_screen,    LV_SCR_LOAD_ANIM_MOVE_LEFT); }
static void nav_back_settings(lv_event_t *e) { (void)e; nav_to(settings_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT); }

static void make_header(lv_obj_t *scr, const char *title, lv_event_cb_t back_cb)
{
	lv_obj_t *btn = lv_button_create(scr);
	lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 5, 5);
	lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *bl = lv_label_create(btn);
	lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");

	lv_obj_t *tl = lv_label_create(scr);
	lv_label_set_text(tl, title);
	lv_obj_set_style_text_font(tl, &lv_font_montserrat_16, 0);
	lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 15);
}

static void about_row(lv_obj_t *parent, const char *key, const char *val)
{
	lv_obj_t *l = lv_label_create(parent);
	lv_label_set_text_fmt(l, "%s:  %s", key, val);
}

static void build_settings_screen(void)
{
	settings_screen = lv_obj_create(NULL);
	make_header(settings_screen, "Settings", nav_main);

	lv_obj_t *list = lv_list_create(settings_screen);
	lv_obj_set_size(list, lv_pct(96), lv_pct(78));
	lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -6);

	lv_list_add_button(list, LV_SYMBOL_IMAGE, "Display");   /* placeholder */
	lv_list_add_button(list, LV_SYMBOL_AUDIO, "Audio");     /* placeholder */
	lv_obj_t *about = lv_list_add_button(list, LV_SYMBOL_LIST, "About");
	lv_obj_add_event_cb(about, nav_about, LV_EVENT_CLICKED, NULL);
}

static void build_about_screen(void)
{
	struct utsname u;
	char kern[96], shell[64];

	uname(&u);
	snprintf(kern, sizeof(kern), "%s %s", u.release, u.machine);
	snprintf(shell, sizeof(shell), "lvshell (LVGL %d.%d.%d)",
			LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

	about_screen = lv_obj_create(NULL);
	make_header(about_screen, "About", nav_back_settings);

	lv_obj_t *cont = lv_obj_create(about_screen);
	lv_obj_set_size(cont, lv_pct(96), lv_pct(78));
	lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -6);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

	about_row(cont, "Shell",    shell);
	about_row(cont, "System",   u.sysname);
	about_row(cont, "Kernel",   kern);
	about_row(cont, "Hostname", u.nodename);
}

static void setup_ui(lv_obj_t *parent)
{
	setup_battery(parent);
	setup_screen_tag(parent);
	setup_carousell(parent);

	/* Settings button, top-left. */
	lv_obj_t *sbtn = lv_button_create(parent);
	lv_obj_align(sbtn, LV_ALIGN_TOP_LEFT, 5, 5);
	lv_obj_add_event_cb(sbtn, nav_settings, LV_EVENT_CLICKED, NULL);
	lv_obj_t *sl = lv_label_create(sbtn);
	lv_label_set_text(sl, LV_SYMBOL_SETTINGS);
}

/* Placeholder splash: shown for a moment, then fades to the main menu. */
#define SPLASH_MS 2000

static void splash_done_cb(lv_timer_t *t)
{
	lv_screen_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, false);
	lv_timer_delete(t);
}

static void show_splash(void)
{
	lv_obj_t *scr = lv_obj_create(NULL);

	lv_obj_t *title = lv_label_create(scr);
	lv_label_set_text(title, "lvshell");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
	lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

	lv_obj_t *sub = lv_label_create(scr);
	lv_label_set_text(sub, "Miyoo Mini");
	lv_obj_align(sub, LV_ALIGN_CENTER, 0, -5);

	lv_obj_t *spinner = lv_spinner_create(scr);
	lv_obj_set_size(spinner, 44, 44);
	lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 55);

	lv_screen_load(scr);
	lv_timer_create(splash_done_cb, SPLASH_MS, NULL);
}

/*
 * Control FIFO: lets the UI be driven remotely for testing/scripting, e.g.
 *   echo settings > /tmp/lvshell.ctl
 *   echo launch 0 > /tmp/lvshell.ctl
 * without faking input events.
 */
#define CTL_PATH "/tmp/lvshell.ctl"

static int ctl_fd = -1;

static void ctl_init(void)
{
	unlink(CTL_PATH);
	if (mkfifo(CTL_PATH, 0666) != 0)
		return;
	/* O_RDWR keeps a writer around so reads return EAGAIN (not EOF) when idle. */
	ctl_fd = open(CTL_PATH, O_RDWR | O_NONBLOCK);
}

static void ctl_launch(int i)
{
	if (i < 0 || i >= cntx.num_apps || cntx.child_pid)
		return;
	cntx.child_pid = util_start_cmd(cntx.apps[i].argv[0],
			(const char * const *)cntx.apps[i].argv,
			cntx.apps[i].dir[0] ? cntx.apps[i].dir : NULL);
}

static void ctl_poll(void)
{
	char buf[128], *cmd, *save;
	ssize_t n;

	if (ctl_fd < 0)
		return;

	n = read(ctl_fd, buf, sizeof(buf) - 1);
	if (n <= 0)
		return;
	buf[n] = 0;

	cmd = strtok_r(buf, " \t\r\n", &save);
	if (!cmd)
		return;

	if (!strcmp(cmd, "main"))
		nav_to(main_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
	else if (!strcmp(cmd, "settings"))
		nav_to(settings_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT);
	else if (!strcmp(cmd, "about"))
		nav_to(about_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT);
	else if (!strcmp(cmd, "launch")) {
		char *arg = strtok_r(NULL, " \t\r\n", &save);
		ctl_launch(arg ? atoi(arg) : 0);
	}
}

int main(int argc, char **argv)
{
	lv_init();
	hal_init();

	cntx.num_apps = apps_discover(&cntx.apps);

	main_screen = lv_obj_create(NULL);
	setup_ui(main_screen);
	build_settings_screen();
	build_about_screen();

	ctl_init();
	show_splash();

	while (1) {
		/*
		 * Periodically call the lv_task handler.
		 * It could be done in a timer interrupt or an OS task too.
		 */
		ctl_poll();
		lv_timer_handler();
		usleep(5 * 1000);
	}

	return 0;
}
