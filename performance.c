/*
 * Performance screen: live CPU-load graph, CPU-frequency graph and a memory
 * gauge. All figures come straight from /proc and /sys - no extra deps - and
 * are refreshed about once a second while the screen is visible. See
 * performance.h.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "lvgl/lvgl.h"
#include "ui.h"
#include "performance.h"

/* One point per refresh; at ~1 Hz this is a minute of scrolling history. */
#define PERF_POINTS      60
#define PERF_INTERVAL_MS 1000

static lv_obj_t        *perf_cpu_chart;
static lv_chart_series_t *perf_cpu_ser;
static lv_obj_t        *perf_cpu_label;

static lv_obj_t        *perf_freq_chart;
static lv_chart_series_t *perf_freq_ser;
static lv_obj_t        *perf_freq_label;

static lv_obj_t        *perf_mem_bar;
static lv_obj_t        *perf_mem_label;

/* Previous /proc/stat totals, for the load delta. */
static unsigned long long perf_prev_busy, perf_prev_total;
static bool               perf_have_prev;

/* CPU frequency range (kHz), read once from cpufreq. */
static long perf_freq_min_khz, perf_freq_max_khz;

static uint32_t perf_last_ms;

/**********************************************************************************************************************/
/* /proc + /sys readers                                                                                               */
/**********************************************************************************************************************/

/* Aggregate CPU busy/total jiffies from the first line of /proc/stat. */
static bool read_cpu(unsigned long long *busy, unsigned long long *total)
{
	unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0,
			   irq = 0, softirq = 0, steal = 0;
	FILE *f = fopen("/proc/stat", "r");
	int n;

	if (!f)
		return false;
	n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
		   &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal);
	fclose(f);
	if (n < 4)
		return false;

	*busy  = user + nice + sys + irq + softirq + steal;
	*total = *busy + idle + iowait;
	return true;
}

/* A single unsigned value from a sysfs/proc file (e.g. cpufreq). */
static long read_long_file(const char *path)
{
	FILE *f = fopen(path, "r");
	long v = -1;

	if (!f)
		return -1;
	if (fscanf(f, "%ld", &v) != 1)
		v = -1;
	fclose(f);
	return v;
}

/* Total and available memory in kB from /proc/meminfo. */
static bool read_mem(long *total_kb, long *avail_kb)
{
	FILE *f = fopen("/proc/meminfo", "r");
	char key[64];
	long val;
	char unit[16];
	int got = 0;

	*total_kb = *avail_kb = 0;
	if (!f)
		return false;
	while (fscanf(f, "%63[^:]: %ld %15s\n", key, &val, unit) == 3) {
		if (!strcmp(key, "MemTotal")) {
			*total_kb = val;
			got++;
		} else if (!strcmp(key, "MemAvailable")) {
			*avail_kb = val;
			got++;
		}
		if (got == 2)
			break;
	}
	fclose(f);
	return *total_kb > 0;
}

/**********************************************************************************************************************/
/* Screen                                                                                                             */
/**********************************************************************************************************************/

/* A titled block: bold caption label + a body widget, laid out in a column. */
static lv_obj_t *perf_block(lv_obj_t *parent, const char *caption, lv_obj_t **out_label)
{
	lv_obj_t *box = lv_obj_create(parent);
	lv_obj_set_width(box, lv_pct(100));
	lv_obj_set_height(box, LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(box, 6, 0);
	lv_obj_set_style_pad_row(box, 4, 0);
	lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *lbl = lv_label_create(box);
	lv_label_set_text(lbl, caption);
	if (out_label)
		*out_label = lbl;
	return box;
}

static lv_obj_t *perf_make_chart(lv_obj_t *parent, lv_chart_series_t **ser,
				 lv_palette_t palette, int y_min, int y_max)
{
	lv_obj_t *chart = lv_chart_create(parent);
	lv_obj_set_width(chart, lv_pct(100));
	lv_obj_set_height(chart, 96);
	lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
	lv_chart_set_point_count(chart, PERF_POINTS);
	lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
	lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
	lv_chart_set_div_line_count(chart, 4, 0);
	/* A clean trend line - no per-point markers. */
	lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
	*ser = lv_chart_add_series(chart, lv_palette_main(palette), LV_CHART_AXIS_PRIMARY_Y);
	return chart;
}

void performance_build(void)
{
	performance_screen = lv_obj_create(NULL);
	screen_group_begin(performance_screen);
	make_header(performance_screen, "Performance", nav_settings);

	lv_obj_t *cont = lv_obj_create(performance_screen);
	lv_obj_set_size(cont, lv_pct(96), lv_pct(80));
	lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -6);
	lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(cont, 6, 0);

	/* CPU load (%) */
	lv_obj_t *b1 = perf_block(cont, "CPU load", &perf_cpu_label);
	perf_cpu_chart = perf_make_chart(b1, &perf_cpu_ser, LV_PALETTE_GREEN, 0, 100);

	/* CPU frequency (MHz) - range filled in from cpufreq if available. */
	perf_freq_min_khz = read_long_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq");
	perf_freq_max_khz = read_long_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq");
	lv_obj_t *b2 = perf_block(cont, "CPU frequency", &perf_freq_label);
	perf_freq_chart = perf_make_chart(b2, &perf_freq_ser, LV_PALETTE_AMBER,
					  perf_freq_min_khz > 0 ? perf_freq_min_khz / 1000 : 0,
					  perf_freq_max_khz > 0 ? perf_freq_max_khz / 1000 : 1200);

	/* Memory (bar + label) */
	lv_obj_t *b3 = perf_block(cont, "Memory", &perf_mem_label);
	perf_mem_bar = lv_bar_create(b3);
	lv_obj_set_width(perf_mem_bar, lv_pct(100));
	lv_obj_set_height(perf_mem_bar, 16);
}

/**********************************************************************************************************************/
/* Refresh                                                                                                            */
/**********************************************************************************************************************/

void performance_poll(void)
{
	unsigned long long busy, total;
	long freq_khz, mem_total, mem_avail;
	uint32_t now;

	/* Only work while the screen is actually visible. */
	if (!performance_screen || lv_screen_active() != performance_screen)
		return;

	now = lv_tick_get();
	if (perf_have_prev && (now - perf_last_ms) < PERF_INTERVAL_MS)
		return;
	perf_last_ms = now;

	/* CPU load: percentage of busy jiffies since the last sample. */
	if (read_cpu(&busy, &total)) {
		if (perf_have_prev && total > perf_prev_total) {
			unsigned long long db = busy - perf_prev_busy;
			unsigned long long dt = total - perf_prev_total;
			int pct = (int)((db * 100 + dt / 2) / dt);

			if (pct > 100)
				pct = 100;
			lv_chart_set_next_value(perf_cpu_chart, perf_cpu_ser, pct);
			lv_label_set_text_fmt(perf_cpu_label, "CPU load: %d%%", pct);
		}
		perf_prev_busy = busy;
		perf_prev_total = total;
		perf_have_prev = true;
	}

	/* CPU frequency (MHz). */
	freq_khz = read_long_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
	if (freq_khz > 0) {
		lv_chart_set_next_value(perf_freq_chart, perf_freq_ser, freq_khz / 1000);
		lv_label_set_text_fmt(perf_freq_label, "CPU frequency: %ld MHz", freq_khz / 1000);
	} else {
		lv_label_set_text(perf_freq_label, "CPU frequency: n/a");
	}

	/* Memory used / total. */
	if (read_mem(&mem_total, &mem_avail)) {
		long used_mb = (mem_total - mem_avail) / 1024;
		long total_mb = mem_total / 1024;

		lv_bar_set_range(perf_mem_bar, 0, (int32_t)total_mb);
		lv_bar_set_value(perf_mem_bar, (int32_t)used_mb, LV_ANIM_OFF);
		lv_label_set_text_fmt(perf_mem_label, "Memory: %ld / %ld MB", used_mb, total_mb);
	}
}
