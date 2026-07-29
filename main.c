/* needed for usleep() */
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <unistd.h>
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

static void setup_ui(lv_obj_t *parent)
{
	setup_battery(parent);
	setup_screen_tag(parent);
	setup_carousell(parent);
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

int main(int argc, char **argv)
{
	lv_init();
	hal_init();

	cntx.num_apps = apps_discover(&cntx.apps);

	main_screen = lv_obj_create(NULL);
	setup_ui(main_screen);

	show_splash();

	while (1) {
		/*
		 * Periodically call the lv_task handler.
		 * It could be done in a timer interrupt or an OS task too.
		 */
		lv_timer_handler();
		usleep(5 * 1000);
	}

	return 0;
}
