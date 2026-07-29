#ifndef BUTTONTEST_H_
#define BUTTONTEST_H_

/*
 * Button test screen: shows the hardware buttons and lights up the matching
 * box on press, reading the input (evdev) devices directly. Build it once
 * (creates buttontest_screen) and poll it from the main loop.
 */
void buttontest_build(void);
void buttontest_poll(void);

#endif /* BUTTONTEST_H_ */
