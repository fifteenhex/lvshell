#ifndef MUSIC_H_
#define MUSIC_H_

/*
 * Background chiptune music. Plays module files from a directory with xmp
 * (libxmp) while the menu is up, looping the playlist. Playback is suspended
 * while a game runs so the game owns the audio device.
 */

/* Scan the music directory for module files. */
void music_init(void);

/* Start, or resume, playback (no-op if already playing / nothing to play). */
void music_start(void);

/* Stop playback, e.g. to hand the audio device to a launching game. */
void music_stop(void);

/* Call from the main loop: reaps the player and loops the playlist. */
void music_poll(void);

#endif /* MUSIC_H_ */
