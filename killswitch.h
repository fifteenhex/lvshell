#ifndef KILLSWITCH_H_
#define KILLSWITCH_H_

#include <stdbool.h>

/*
 * Emergency "kill the running app" switch: watches the hardware Menu button on
 * the evdev input devices (which keep broadcasting even while a game owns the
 * display) and reports when it has been held down for >10 seconds, so the shell
 * can force-quit a stuck game. Build once with killswitch_init(); poll from the
 * main loop with killswitch_menu_held().
 */
void killswitch_init(void);

/* Drain input events and update the hold timer. Returns true exactly once each
 * time the Menu button crosses 10 s held (re-arms after the button is released). */
bool killswitch_menu_held(void);

#endif /* KILLSWITCH_H_ */
