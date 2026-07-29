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

/* Copy the open input-device fds into 'out' (up to 'max'); returns the count.
 * Used to poll for input so the main loop can wake immediately on a button. */
int input_get_fds(int *out, int max);

#endif /* INPUT_H_ */
