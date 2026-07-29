#ifndef TICKER_H_
#define TICKER_H_

#include "lvgl/lvgl.h"

/*
 * Now-playing ticker: the current module name slides in from the right, pauses
 * in the centre, then slides off to the left, with the letters jiggling while
 * it moves. Create it on the main screen, then poll it from the main loop.
 */
void ticker_setup(lv_obj_t *parent);
void ticker_poll(void);

#endif /* TICKER_H_ */
