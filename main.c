/* needed for usleep() */
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <unistd.h>
/*To fix SDL's "undefined reference to WinMain" issue*/
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "lvgl/lvgl.h"
#include "lvgl/examples/lv_examples.h"
#include "lvgl/demos/lv_demos.h"
#include "lv_drivers/sdl/sdl.h"

struct toplevel {
	const char *title;
};

struct context {
	struct toplevel *toplevel;
	unsigned num_toplevel;
};

static struct context cntx;

static lv_indev_drv_t indev_keyboard = { };

static void hal_init_input(void)
{
	lv_indev_t *kb_indev;
	lv_group_t *g = lv_group_create();

	lv_group_set_default(g);
	lv_indev_drv_init(&indev_keyboard);
	kb_indev = lv_indev_drv_register(&indev_keyboard);
	/* Encoder seems wrong but this gives the best fit */
	indev_keyboard.type = LV_INDEV_TYPE_ENCODER;
	indev_keyboard.read_cb = sdl_keyboard_read;
	lv_indev_set_group(kb_indev, g);
}

static void hal_init(void)
{
	sdl_init();

	/*Create a display buffer*/
	static lv_disp_draw_buf_t disp_buf1;
	static lv_color_t buf1_1[SDL_HOR_RES * 100];
	lv_disp_draw_buf_init(&disp_buf1, buf1_1, NULL, SDL_HOR_RES * 100);

	/*Create a display*/
	static lv_disp_drv_t disp_drv;
	/*Basic initialization*/
	lv_disp_drv_init(&disp_drv);
	disp_drv.draw_buf = &disp_buf1;
	disp_drv.flush_cb = sdl_display_flush;
	disp_drv.hor_res = SDL_HOR_RES;
	disp_drv.ver_res = SDL_VER_RES;

	lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
	lv_theme_t *th = lv_theme_default_init(disp,
			lv_palette_main(LV_PALETTE_BLUE),
			lv_palette_main(LV_PALETTE_RED),
			LV_THEME_DEFAULT_DARK, LV_FONT_DEFAULT);
	lv_disp_set_theme(disp, th);

	hal_init_input();
}

static void quake_handler(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {
	}
}

static void doom_handler(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_CLICKED) {
		LV_LOG_USER("Clicked");
	}
}

static void setup_carousell()
{
	lv_obj_t *panel = lv_obj_create(lv_scr_act());
	lv_obj_set_size(panel, lv_pct(100), lv_pct(60));
	lv_obj_set_scroll_snap_x(panel, LV_SCROLL_SNAP_CENTER);
	lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
	lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);

	for (int i = 0; i < cntx.num_toplevel; i++) {
		lv_obj_t *btn = lv_btn_create(panel);
		lv_obj_set_size(btn, lv_pct(50), lv_pct(100));
		lv_obj_t *label = lv_label_create(btn);
		lv_label_set_text(label, cntx.toplevel[i].title);
		lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -10);
	}

	lv_obj_update_snap(panel, LV_ANIM_ON);

}

static void setup_screen_tag()
{
	lv_obj_t *ltr_label = lv_label_create(lv_scr_act());
	lv_label_set_text(ltr_label,
			"Miyoo Mini - Less shitty kernel edition.");
	lv_obj_set_style_text_font(ltr_label, &lv_font_montserrat_16, 0);
	lv_obj_set_width(ltr_label, LV_SIZE_CONTENT);
	lv_obj_align(ltr_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

static void setup_ui()
{
	setup_screen_tag();
	setup_carousell();

#if 0
	{
		lv_obj_t *btn1 = lv_btn_create(lv_scr_act());
		lv_obj_add_event_cb(btn1, quake_handler, LV_EVENT_ALL, NULL);
		lv_obj_align(btn1, LV_ALIGN_CENTER, 0, -40);

		lv_obj_t *label = lv_label_create(btn1);
		lv_label_set_text(label, "Play Quake");
		lv_obj_center(label);
	}
#endif
}

int main(int argc, char **argv)
{
	lv_init();
	hal_init();

	struct toplevel tops[] = {
			{ .title = "Chocolate Doom - Doom2" },
			{ .title = "cppquake - Quake" },
			{ .title = "Settings" },
	};

	cntx.toplevel = tops;
	cntx.num_toplevel = sizeof(tops) / sizeof(tops[0]);

	setup_ui();

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
