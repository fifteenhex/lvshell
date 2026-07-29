/* needed for usleep() */
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
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

/*
 * Debug logging. Enabled with -DLVSHELL_DEBUG (the LVSHELL_DEBUG CMake option),
 * which also turns on LVGL's own logging in lv_conf.h. Writes to a file so it
 * survives lvshell being started as a background daemon with stdout/stderr
 * redirected to /dev/null, and DirectFB taking over the console.
 */
#ifdef LVSHELL_DEBUG
static FILE *g_dbg;
#define DBG(...) do { if (g_dbg) { fprintf(g_dbg, __VA_ARGS__); fflush(g_dbg); } } while (0)

static void dbg_log_cb(lv_log_level_t level, const char *buf)
{
	(void)level;
	DBG("LVGL: %s", buf);
}

static void dbg_init(void)
{
	g_dbg = fopen("/tmp/lvshell.dbg", "w");
}

static void dbg_init_lvgl(void)
{
	lv_log_register_print_cb(dbg_log_cb);
}
#else
#define DBG(...) do { } while (0)
static void dbg_init(void) { }
static void dbg_init_lvgl(void) { }
#endif

/*
 * Focus handling. The Miyoo has no touchscreen: the D-pad drives an LVGL focus
 * group. Each screen gets its own group so the D-pad only cycles the widgets on
 * the visible screen, and a bold highlight makes the current selection obvious.
 */
#define MAX_SCREEN_GROUPS 24

static lv_indev_t *kb_indev;
static lv_group_t *cur_group;
static lv_style_t  style_focus;

static struct {
	lv_obj_t   *scr;
	lv_group_t *grp;
} screen_groups[MAX_SCREEN_GROUPS];
static int num_screen_groups;

static void focus_style_init(void)
{
	lv_style_init(&style_focus);
	lv_style_set_outline_width(&style_focus, 4);
	lv_style_set_outline_pad(&style_focus, 2);
	lv_style_set_outline_color(&style_focus, lv_palette_main(LV_PALETTE_AMBER));
	lv_style_set_outline_opa(&style_focus, LV_OPA_COVER);
	lv_style_set_bg_color(&style_focus, lv_palette_lighten(LV_PALETTE_BLUE, 2));
}

/* Highlight 'obj' when it holds focus. */
static void make_focusable(lv_obj_t *obj)
{
	lv_obj_add_style(obj, &style_focus, LV_STATE_FOCUSED);
}

/* Start a fresh focus group for a screen; widgets created afterwards join it
 * (it becomes the default group). */
static lv_group_t *screen_group_begin(lv_obj_t *scr)
{
	lv_group_t *g = lv_group_create();

	lv_group_set_default(g);
	if (num_screen_groups < MAX_SCREEN_GROUPS) {
		screen_groups[num_screen_groups].scr = scr;
		screen_groups[num_screen_groups].grp = g;
		num_screen_groups++;
	}
	return g;
}

static lv_group_t *group_for_screen(lv_obj_t *scr)
{
	for (int i = 0; i < num_screen_groups; i++)
		if (screen_groups[i].scr == scr)
			return screen_groups[i].grp;
	return NULL;
}

static void hal_init_input(void)
{
	/* Keyboard/D-pad drives the focus group; mouse allows direct clicks. */
	kb_indev = lv_sdl_keyboard_create();
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

/* ------------------------------------------------------------------------- */
/* Cracktro-style background: subtle squares spinning and flying across.      */
/* ------------------------------------------------------------------------- */

#define BG_SQUARES 10

static void bg_anim_x(void *var, int32_t v)
{
	lv_obj_set_x((lv_obj_t *)var, v);
}

static void bg_anim_rot(void *var, int32_t v)
{
	lv_obj_set_style_transform_rotation((lv_obj_t *)var, v, 0);
}

static void setup_background(lv_obj_t *parent)
{
	for (int i = 0; i < BG_SQUARES; i++) {
		int sz = 18 + (i * 11) % 34;
		lv_obj_t *sq = lv_obj_create(parent);
		lv_anim_t ax, ar;

		lv_obj_remove_flag(sq, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(sq, sz, sz);
		lv_obj_set_style_radius(sq, 3, 0);
		lv_obj_set_style_border_width(sq, 0, 0);
		lv_obj_set_style_bg_color(sq, lv_palette_main(LV_PALETTE_BLUE), 0);
		lv_obj_set_style_bg_opa(sq, LV_OPA_20, 0);   /* subtle */
		lv_obj_set_style_transform_pivot_x(sq, sz / 2, 0);
		lv_obj_set_style_transform_pivot_y(sq, sz / 2, 0);
		lv_obj_set_y(sq, (i * 61) % 440);
		lv_obj_move_background(sq);

		/* Fly left -> right, looping, staggered. */
		lv_anim_init(&ax);
		lv_anim_set_var(&ax, sq);
		lv_anim_set_exec_cb(&ax, bg_anim_x);
		lv_anim_set_values(&ax, -sz, LVSHELL_HOR_RES);
		lv_anim_set_duration(&ax, 6000 + (i * 500));
		lv_anim_set_delay(&ax, i * 350);
		lv_anim_set_repeat_count(&ax, LV_ANIM_REPEAT_INFINITE);
		lv_anim_start(&ax);

		/* Spin (angle is in 0.1 degree units). */
		lv_anim_init(&ar);
		lv_anim_set_var(&ar, sq);
		lv_anim_set_exec_cb(&ar, bg_anim_rot);
		lv_anim_set_values(&ar, 0, 3600);
		lv_anim_set_duration(&ar, 4000 + (i * 300));
		lv_anim_set_repeat_count(&ar, LV_ANIM_REPEAT_INFINITE);
		lv_anim_start(&ar);
	}
}

static void setup_battery(lv_obj_t *parent)
{
	lv_obj_t *batbar = lv_bar_create(parent);
	lv_obj_set_size(batbar, 35, 10);
	lv_obj_align(batbar, LV_ALIGN_TOP_RIGHT, -5, 5);
	lv_bar_set_value(batbar, 50, LV_ANIM_OFF);
}

/*
 * Discovered entries are grouped by their "app" (ScummVM, Doom, ...). The main
 * screen shows one card per group; opening a group shows its games.
 */
#define MAX_GROUPS 12

struct app_group {
	const char             *name;
	const struct app_entry *members[APP_MAX_ENTRIES];
	int                     count;
	lv_obj_t               *screen;
};

static struct app_group groups[MAX_GROUPS];
static int              num_groups;

static void build_groups(void)
{
	num_groups = 0;
	for (int i = 0; i < cntx.num_apps; i++) {
		const char *g = cntx.apps[i].group;
		int gi = -1;

		for (int j = 0; j < num_groups; j++) {
			if (!strcmp(groups[j].name, g)) {
				gi = j;
				break;
			}
		}
		if (gi < 0) {
			if (num_groups >= MAX_GROUPS)
				continue;
			gi = num_groups++;
			groups[gi].name = g;
			groups[gi].count = 0;
			groups[gi].screen = NULL;
		}
		if (groups[gi].count < APP_MAX_ENTRIES)
			groups[gi].members[groups[gi].count++] = &cntx.apps[i];
	}
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
	lv_group_t *g = group_for_screen(scr);

	if (g) {
		cur_group = g;
		lv_indev_set_group(kb_indev, g);
		/* Make sure something on the new screen is highlighted. */
		if (!lv_group_get_focused(g))
			lv_group_focus_next(g);
	}
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
	make_focusable(btn);
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

/**********************************************************************************************************************/
/* Carousel cards (games and app groups)                                                                              */
/**********************************************************************************************************************/

/* A horizontal, centre-snapping strip filling the middle of a screen. */
static lv_obj_t *carousel_panel(lv_obj_t *parent)
{
	lv_obj_t *panel = lv_obj_create(parent);

	lv_obj_set_size(panel, lv_pct(100), lv_pct(60));
	lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_scroll_snap_x(panel, LV_SCROLL_SNAP_CENTER);
	lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
	return panel;
}

static void empty_message(lv_obj_t *panel, const char *msg)
{
	lv_obj_t *label = lv_label_create(panel);

	lv_label_set_text(label, msg);
	lv_obj_center(label);
}

static void card_add_icon(lv_obj_t *btn, const char *icon)
{
	char src[200];
	lv_obj_t *img;

	if (!icon || !icon[0])
		return;

	img = lv_image_create(btn);
	snprintf(src, sizeof(src), "L:%s", icon);
	lv_image_set_src(img, src);
	lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 10);
}

static void card_add_label(lv_obj_t *btn, const char *text)
{
	lv_obj_t *label = lv_label_create(btn);

	lv_label_set_text(label, text);
	lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(label, lv_pct(90));
	lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -10);
}

/* One launchable game. */
static void add_game_card(lv_obj_t *panel, const struct app_entry *app)
{
	lv_obj_t *btn = lv_button_create(panel);

	lv_obj_set_size(btn, lv_pct(50), lv_pct(100));
	make_focusable(btn);
	lv_obj_add_event_cb(btn, launch_handler, LV_EVENT_ALL, (void *)app);
	card_add_icon(btn, app->icon);
	card_add_label(btn, app->title);
}

static void group_open_cb(lv_event_t *e)
{
	nav_to(lv_event_get_user_data(e), LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

/* One app group; clicking it opens that group's screen. */
static void add_group_card(lv_obj_t *panel, struct app_group *grp)
{
	lv_obj_t *btn = lv_button_create(panel);
	const char *icon = NULL;
	char sub[64];

	lv_obj_set_size(btn, lv_pct(50), lv_pct(100));
	make_focusable(btn);
	lv_obj_add_event_cb(btn, group_open_cb, LV_EVENT_CLICKED, grp->screen);

	/* Represent the group with its first member's cover, if any. */
	for (int i = 0; i < grp->count; i++) {
		if (grp->members[i]->icon[0]) {
			icon = grp->members[i]->icon;
			break;
		}
	}
	card_add_icon(btn, icon);

	snprintf(sub, sizeof(sub), "%s\n%d game%s", grp->name,
		 grp->count, grp->count == 1 ? "" : "s");
	card_add_label(btn, sub);
}

/* Main screen: one card per app group. */
static void setup_group_carousel(lv_obj_t *parent)
{
	lv_obj_t *panel = carousel_panel(parent);

	if (num_groups == 0) {
		empty_message(panel,
				"No games found.\n"
				"Add WADs or ROMs under /data and restart.");
		return;
	}

	for (int i = 0; i < num_groups; i++)
		add_group_card(panel, &groups[i]);

	lv_obj_update_snap(panel, LV_ANIM_ON);
}

/* Build a per-group screen listing that group's games. */
static void build_group_screens(void)
{
	for (int i = 0; i < num_groups; i++) {
		lv_obj_t *scr = lv_obj_create(NULL);
		lv_obj_t *panel;

		lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
		screen_group_begin(scr);
		setup_background(scr);
		make_header(scr, groups[i].name, nav_main);

		panel = carousel_panel(scr);
		for (int j = 0; j < groups[i].count; j++)
			add_game_card(panel, groups[i].members[j]);
		lv_obj_update_snap(panel, LV_ANIM_ON);

		groups[i].screen = scr;
	}
}

static void build_settings_screen(void)
{
	settings_screen = lv_obj_create(NULL);
	screen_group_begin(settings_screen);
	make_header(settings_screen, "Settings", nav_main);

	lv_obj_t *list = lv_list_create(settings_screen);
	lv_obj_set_size(list, lv_pct(96), lv_pct(78));
	lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -6);

	make_focusable(lv_list_add_button(list, LV_SYMBOL_IMAGE, "Display")); /* placeholder */
	make_focusable(lv_list_add_button(list, LV_SYMBOL_AUDIO, "Audio"));   /* placeholder */
	lv_obj_t *about = lv_list_add_button(list, LV_SYMBOL_LIST, "About");
	make_focusable(about);
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
	screen_group_begin(about_screen);
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
	/* The background squares fly off-screen; don't let that add a scrollbar. */
	lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

	screen_group_begin(parent);
	setup_background(parent);
	setup_battery(parent);
	setup_screen_tag(parent);
	setup_group_carousel(parent);

	/* Settings button, top-left. */
	lv_obj_t *sbtn = lv_button_create(parent);
	lv_obj_align(sbtn, LV_ALIGN_TOP_LEFT, 5, 5);
	make_focusable(sbtn);
	lv_obj_add_event_cb(sbtn, nav_settings, LV_EVENT_CLICKED, NULL);
	lv_obj_t *sl = lv_label_create(sbtn);
	lv_label_set_text(sl, LV_SYMBOL_SETTINGS);
}

/**********************************************************************************************************************/
/* First-boot: offer to create the user data partition                                                                */
/**********************************************************************************************************************/

static lv_obj_t *dp_modal;
static lv_obj_t *dp_label;
static lv_obj_t *dp_buttons;
static pid_t     dp_pid;
static bool      dp_should_offer;

static void dp_close(lv_event_t *e)
{
	(void)e;
	if (dp_modal) {
		lv_obj_delete(dp_modal);
		dp_modal = NULL;
	}
}

static void dp_create_cb(lv_event_t *e)
{
	(void)e;
	if (dp_pid)
		return;

	dp_pid = fork();
	if (dp_pid == 0) {
		execl("/usr/bin/datapart", "datapart", "create", (char *)NULL);
		_exit(127);
	}

	lv_label_set_text(dp_label, "Creating data partition, please wait...");
	if (dp_buttons)
		lv_obj_add_flag(dp_buttons, LV_OBJ_FLAG_HIDDEN);
}

static void show_datapart_dialog(void)
{
	lv_obj_t *ok, *skip, *l;

	if (dp_modal)
		return;

	dp_modal = lv_obj_create(lv_layer_top());
	lv_obj_set_size(dp_modal, lv_pct(80), LV_SIZE_CONTENT);
	lv_obj_center(dp_modal);
	lv_obj_set_flex_flow(dp_modal, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(dp_modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	dp_label = lv_label_create(dp_modal);
	lv_label_set_long_mode(dp_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(dp_label, lv_pct(100));
	lv_label_set_text(dp_label,
			"There is free space on your SD card.\n"
			"Create a data partition (exFAT) for your games and saves?");

	dp_buttons = lv_obj_create(dp_modal);
	lv_obj_set_size(dp_buttons, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(dp_buttons, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(dp_buttons, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	skip = lv_button_create(dp_buttons);
	lv_obj_add_event_cb(skip, dp_close, LV_EVENT_CLICKED, NULL);
	l = lv_label_create(skip); lv_label_set_text(l, "Skip");

	ok = lv_button_create(dp_buttons);
	lv_obj_add_event_cb(ok, dp_create_cb, LV_EVENT_CLICKED, NULL);
	l = lv_label_create(ok); lv_label_set_text(l, "Create");
}

/* Poll the datapart child; when it finishes, report and offer to dismiss. */
static void dp_poll(void)
{
	int status;
	lv_obj_t *ok, *l;

	if (dp_pid <= 0)
		return;
	if (waitpid(dp_pid, &status, WNOHANG) != dp_pid)
		return;

	dp_pid = 0;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		lv_label_set_text(dp_label, "Data partition created and mounted at /data.");
	else
		lv_label_set_text(dp_label, "Could not create the data partition.");

	if (dp_buttons) {
		lv_obj_clean(dp_buttons);
		lv_obj_remove_flag(dp_buttons, LV_OBJ_FLAG_HIDDEN);
		ok = lv_button_create(dp_buttons);
		lv_obj_add_event_cb(ok, dp_close, LV_EVENT_CLICKED, NULL);
		l = lv_label_create(ok); lv_label_set_text(l, "OK");
	}
}

/* Placeholder splash: shown for a moment, then fades to the main menu. */
#define SPLASH_MS 2000

static void splash_done_cb(lv_timer_t *t)
{
	lv_group_t *g = group_for_screen(main_screen);

	if (g) {
		cur_group = g;
		lv_indev_set_group(kb_indev, g);
		if (!lv_group_get_focused(g))
			lv_group_focus_next(g);
	}
	lv_screen_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, false);
	lv_timer_delete(t);

	if (dp_should_offer)
		show_datapart_dialog();
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
	/* mkfifo's mode is masked by umask; force it writable so a non-root shell
	 * can drive the UI (lvshell runs as root from the init script). */
	chmod(CTL_PATH, 0666);
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
	else if (!strcmp(cmd, "datapart")) {
		char *arg = strtok_r(NULL, " \t\r\n", &save);
		if (arg && !strcmp(arg, "create"))
			dp_create_cb(NULL);
		else
			show_datapart_dialog();
	}
	else if (!strcmp(cmd, "group")) {
		char *arg = strtok_r(NULL, " \t\r\n", &save);
		int g = arg ? atoi(arg) : 0;
		if (g >= 0 && g < num_groups && groups[g].screen)
			nav_to(groups[g].screen, LV_SCR_LOAD_ANIM_MOVE_LEFT);
	}
	else if (!strcmp(cmd, "focus")) {
		/* Advance the highlight, as the D-pad would. */
		char *arg = strtok_r(NULL, " \t\r\n", &save);
		if (cur_group) {
			if (arg && !strcmp(arg, "prev"))
				lv_group_focus_prev(cur_group);
			else
				lv_group_focus_next(cur_group);
		}
	}
	else if (!strcmp(cmd, "launch")) {
		char *arg = strtok_r(NULL, " \t\r\n", &save);
		ctl_launch(arg ? atoi(arg) : 0);
	}
}

int main(int argc, char **argv)
{
	dbg_init();

	/*
	 * Discover apps before we grab the display: the ScummVM discoverer runs
	 * "scummvm --detect", which itself opens the (single) DirectFB device.
	 */
	cntx.num_apps = apps_discover(&cntx.apps);

	lv_init();
	dbg_init_lvgl();
	hal_init();
	focus_style_init();

	/* Offer to create the data partition on first boot if there's room. */
	{
		int r = system("/usr/bin/datapart check");
		dp_should_offer = (r != -1 && WIFEXITED(r) && WEXITSTATUS(r) == 0);
	}

	main_screen = lv_obj_create(NULL);
	build_groups();
	build_group_screens();
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
		dp_poll();
		lv_timer_handler();
		usleep(5 * 1000);
	}

	return 0;
}
