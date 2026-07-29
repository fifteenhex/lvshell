/*
 * Now-playing ticker. While music plays it alternates two messages -
 * "<note> Now Playing... <note>" and "<note> <module name> <note>" - each of
 * which slides in from the right decelerating, pauses in the centre, then
 * slides off to the left accelerating. It is built from one label per glyph so
 * each can jiggle on the Y axis while it moves. See ticker.h.
 */
#include <stdio.h>

#include "lvgl/lvgl.h"
#include "ui.h"
#include "music.h"
#include "ticker.h"

#define TICKER_SLIDE_MS 900
#define TICKER_PAUSE_MS 1800
#define TICKER_MAXCHARS 48
#define JIGGLE_AMP      5

static lv_obj_t *ticker_cont;                     /* one label per glyph */
static lv_obj_t *ticker_chars[TICKER_MAXCHARS];
static int       ticker_nchars;
static bool      ticker_active;
static bool      jiggle_on;                       /* wobble the letters? */
static int32_t   jiggle_amp;                      /* current amplitude, 8.8 fixed */

static void ticker_set_x(void *var, int32_t v) { lv_obj_set_x((lv_obj_t *)var, v); }

/*
 * Wobble the letters on the Y axis, phase-shifted. The amplitude eases toward
 * its target (full while sliding, zero while paused) so starting and stopping
 * the jiggle isn't jarring. Runs off a periodic timer.
 */
static void jiggle_timer_cb(lv_timer_t *t)
{
	int32_t target = jiggle_on ? (JIGGLE_AMP << 8) : 0;
	uint32_t tick;

	(void)t;
	jiggle_amp += (target - jiggle_amp) >> 3;   /* exponential ease (~1/8) */
	if (ticker_nchars == 0)
		return;

	tick = lv_tick_get();
	for (int i = 0; i < ticker_nchars; i++) {
		int32_t ang = (int32_t)((tick / 2 + i * 50) % 360);
		int32_t off = ((jiggle_amp >> 8) * lv_trigo_sin(ang)) / 32767;

		lv_obj_set_y(ticker_chars[i], JIGGLE_AMP + off);
	}
}

/* Resume jiggling when the slide-out starts (one-shot). */
static void jiggle_resume_cb(lv_timer_t *t)
{
	jiggle_on = true;
	lv_timer_delete(t);
}

/* Bytes in the UTF-8 sequence starting with 'b' (so multi-byte glyphs like the
 * audio symbol become a single ticker cell). */
static int utf8_len(unsigned char b)
{
	if (b < 0x80)          return 1;
	if ((b & 0xE0) == 0xC0) return 2;
	if ((b & 0xF0) == 0xE0) return 3;
	if ((b & 0xF8) == 0xF0) return 4;
	return 1;
}

/* Build one label per glyph (UTF-8 aware); return the total width. */
static int32_t ticker_build(const char *text)
{
	int32_t x = 0;
	const char *p = text;

	lv_obj_clean(ticker_cont);
	ticker_nchars = 0;
	while (*p && ticker_nchars < TICKER_MAXCHARS) {
		lv_obj_t *c = lv_label_create(ticker_cont);
		char ch[5] = { 0 };
		int n = utf8_len((unsigned char)*p), k;

		for (k = 0; k < n && p[k]; k++)
			ch[k] = p[k];

		lv_obj_set_style_text_font(c, &lv_font_montserrat_16, 0);
		lv_label_set_text(c, ch);
		lv_obj_update_layout(c);
		lv_obj_set_pos(c, x, JIGGLE_AMP);
		x += lv_obj_get_width(c) + (n == 1 && *p == ' ' ? 3 : 0);
		ticker_chars[ticker_nchars++] = c;
		p += n;
	}
	return x;
}

static void ticker_out_done(lv_anim_t *a)
{
	(void)a;
	jiggle_on = false;
	ticker_active = false;   /* ticker_poll() may start the next pass */
}

/* Reached the centre: pause (calm), then slide off to the left, jiggling. */
static void ticker_in_done(lv_anim_t *a)
{
	int32_t w = lv_obj_get_width(ticker_cont);
	lv_anim_t out;

	(void)a;
	jiggle_on = false;   /* calm during the centre pause (eases out) */
	lv_anim_init(&out);
	lv_anim_set_var(&out, ticker_cont);
	lv_anim_set_exec_cb(&out, ticker_set_x);
	lv_anim_set_values(&out, (LVSHELL_HOR_RES - w) / 2, -w);
	lv_anim_set_duration(&out, TICKER_SLIDE_MS);
	lv_anim_set_delay(&out, TICKER_PAUSE_MS);
	lv_anim_set_path_cb(&out, lv_anim_path_ease_in);
	lv_anim_set_completed_cb(&out, ticker_out_done);
	lv_anim_start(&out);
	/* Jiggle again (eases in) once it starts moving out. */
	lv_timer_create(jiggle_resume_cb, TICKER_PAUSE_MS, NULL);
}

/* Slide the name in from the right (decelerating) to the centre, jiggling. */
static void ticker_start(const char *text)
{
	int32_t w = ticker_build(text);
	lv_anim_t in;

	lv_obj_set_width(ticker_cont, w);
	lv_anim_init(&in);
	lv_anim_set_var(&in, ticker_cont);
	lv_anim_set_exec_cb(&in, ticker_set_x);
	lv_anim_set_values(&in, LVSHELL_HOR_RES, (LVSHELL_HOR_RES - w) / 2);
	lv_anim_set_duration(&in, TICKER_SLIDE_MS);
	lv_anim_set_path_cb(&in, lv_anim_path_ease_out);
	lv_anim_set_completed_cb(&in, ticker_in_done);
	lv_anim_start(&in);
	jiggle_on = true;   /* eases in from flat */
}

void ticker_poll(void)
{
	static int pass;                          /* alternates the two messages */
	const char *name;
	char text[TICKER_MAXCHARS + 32];

	if (ticker_active || !ticker_cont)
		return;
	name = music_current();
	if (!name)
		return;

	/* Alternate "<note> Now Playing... <note>" and "<note> <mod name> <note>".
	 * LVGL has no music-note glyph, so the audio symbol stands in for it. */
	if (pass++ & 1)
		snprintf(text, sizeof(text), LV_SYMBOL_AUDIO " %s " LV_SYMBOL_AUDIO, name);
	else
		snprintf(text, sizeof(text), LV_SYMBOL_AUDIO " Now Playing... " LV_SYMBOL_AUDIO);

	ticker_active = true;
	ticker_start(text);
}

void ticker_setup(lv_obj_t *parent)
{
	/* A transparent strip below the menu, above the corner tag, that holds
	 * one label per letter. Starts off-screen to the right. */
	ticker_cont = lv_obj_create(parent);
	lv_obj_remove_style_all(ticker_cont);
	lv_obj_remove_flag(ticker_cont, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(ticker_cont, 10, 16 + 2 * JIGGLE_AMP);
	lv_obj_set_y(ticker_cont, LVSHELL_VER_RES - 74);
	lv_obj_set_x(ticker_cont, LVSHELL_HOR_RES);
	lv_timer_create(jiggle_timer_cb, 16, NULL);
}
