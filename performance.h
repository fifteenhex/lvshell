#ifndef PERFORMANCE_H_
#define PERFORMANCE_H_

/*
 * Performance screen: live graphs of CPU load and CPU frequency plus a memory
 * gauge, read from /proc and /sys. Build it once (creates performance_screen)
 * and poll it from the main loop; polling is a no-op unless the screen is
 * showing, so it costs nothing on the other screens.
 */
void performance_build(void);
void performance_poll(void);

#endif /* PERFORMANCE_H_ */
