#ifndef INPUT_H_
#define INPUT_H_

#include "lvgl/lvgl.h"

/*
 * Keypad input from the Miyoo Mini's gpio-keys (read straight off the input
 * devices), mapped to LVGL navigation keys so the D-pad cycles the focus
 * group: Up/Left = previous, Down/Right = next, A/Start = enter, B/Select =
 * escape.
 */
void input_init(void);

/* Route the D-pad to a screen's focus group. */
void input_set_group(lv_group_t *group);

#endif /* INPUT_H_ */
