#ifndef PERFORMANCE_H_
#define PERFORMANCE_H_

/*
 * Performance screen: live graphs of CPU load and CPU frequency plus a memory
 * gauge, read from /proc and /sys, over a ~5 minute window. Build it once
 * (creates performance_screen) and poll it from the main loop. Sampling keeps
 * running even while a game owns the display (pass game_running = true), and
 * those samples are marked on the graph, so returning to the screen shows what
 * happened during the game.
 */
void performance_build(void);
void performance_poll(bool game_running);

#endif /* PERFORMANCE_H_ */
