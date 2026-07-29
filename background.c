/*
 * Cracktro-style animated background: a set of slow translucent squares
 * spinning and flying across, plus the occasional fast randomly-coloured one.
 * See background.h.
 */
#define _DEFAULT_SOURCE
#include <stdlib.h>

#include "lvgl/lvgl.h"
#include "ui.h"
#include "background.h"

#define BG_SQUARES 10

static void bg_anim_x(void *var, int32_t v)
{
	lv_obj_set_x((lv_obj_t *)var, v);
}

static void bg_anim_rot(void *var, int32_t v)
{
	lv_obj_set_style_transform_rotation((lv_obj_t *)var, v, 0);
}

static void cracktro_squares(lv_obj_t *parent)
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

/* Delete a square once it has flown off the screen. */
static void color_square_done(lv_anim_t *a)
{
	lv_obj_delete((lv_obj_t *)a->var);
}

/* Fling a single fast, randomly-coloured square across the background, then
 * reschedule for a random moment at least a second away (one at a time). */
static void spawn_color_squares(lv_timer_t *t)
{
	lv_obj_t *parent = lv_timer_get_user_data(t);
	int sz = 12 + rand() % 26;
	lv_obj_t *sq = lv_obj_create(parent);
	lv_color_t col = lv_color_make(rand() & 0xff, rand() & 0xff, rand() & 0xff);
	lv_anim_t ax, ar;

	lv_obj_remove_flag(sq, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(sq, sz, sz);
	lv_obj_set_style_radius(sq, 2, 0);
	lv_obj_set_style_border_width(sq, 0, 0);
	lv_obj_set_style_bg_color(sq, col, 0);
	lv_obj_set_style_bg_opa(sq, LV_OPA_70, 0);
	lv_obj_set_style_transform_pivot_x(sq, sz / 2, 0);
	lv_obj_set_style_transform_pivot_y(sq, sz / 2, 0);
	lv_obj_set_y(sq, rand() % LVSHELL_VER_RES);
	lv_obj_move_background(sq);

	lv_anim_init(&ax);
	lv_anim_set_var(&ax, sq);
	lv_anim_set_exec_cb(&ax, bg_anim_x);
	lv_anim_set_values(&ax, -sz, LVSHELL_HOR_RES + sz);
	lv_anim_set_duration(&ax, 800 + rand() % 500);   /* fast */
	lv_anim_set_completed_cb(&ax, color_square_done);
	lv_anim_start(&ax);

	lv_anim_init(&ar);
	lv_anim_set_var(&ar, sq);
	lv_anim_set_exec_cb(&ar, bg_anim_rot);
	lv_anim_set_values(&ar, 0, 3600);
	lv_anim_set_duration(&ar, 600 + rand() % 600);
	lv_anim_set_repeat_count(&ar, LV_ANIM_REPEAT_INFINITE);
	lv_anim_start(&ar);

	lv_timer_set_period(t, 1000 + rand() % 2500);
}

void background_setup(lv_obj_t *parent)
{
	cracktro_squares(parent);
	lv_timer_create(spawn_color_squares, 3000, parent);
}
