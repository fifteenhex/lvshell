#ifndef UI_H_
#define UI_H_

#include "lvgl/lvgl.h"

/* Panel resolution (the Miyoo Mini's screen). */
#define LVSHELL_HOR_RES 640
#define LVSHELL_VER_RES 480

/* ------------------------------------------------------------------------- */
/* Focus handling (D-pad). Each screen gets its own group so the D-pad only    */
/* cycles the widgets on the visible screen, with a bold highlight.            */
/* ------------------------------------------------------------------------- */
extern lv_group_t *cur_group;

void focus_style_init(void);
void make_focusable(lv_obj_t *obj);
lv_group_t *screen_group_begin(lv_obj_t *scr);   /* new group, becomes default */
lv_group_t *group_for_screen(lv_obj_t *scr);

/* ------------------------------------------------------------------------- */
/* Navigation. Screens are shared globals so any module can navigate to them.  */
/* ------------------------------------------------------------------------- */
extern lv_obj_t *main_screen;
extern lv_obj_t *settings_screen;
extern lv_obj_t *about_screen;
extern lv_obj_t *buttontest_screen;
extern lv_obj_t *performance_screen;

void nav_to(lv_obj_t *scr, lv_screen_load_anim_t anim);
void make_header(lv_obj_t *scr, const char *title, lv_event_cb_t back_cb);

void nav_main(lv_event_t *e);
void nav_settings(lv_event_t *e);
void nav_about(lv_event_t *e);
void nav_back_settings(lv_event_t *e);
void nav_buttontest(lv_event_t *e);
void nav_performance(lv_event_t *e);

#endif /* UI_H_ */
