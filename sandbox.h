#ifndef SANDBOX_H_
#define SANDBOX_H_

/*
 * A basic per-process sandbox: a private scratch directory on the /run tmpfs,
 * used as the launched process's $HOME. Create one before spawning a process
 * and destroy it after the process exits. (Kept deliberately small for now -
 * room to grow into more isolation later.)
 */

/* Create a fresh sandbox; returns the directory to use as HOME, or NULL. */
const char *sandbox_prepare(void);

/* Destroy the current sandbox. */
void sandbox_teardown(void);

#endif /* SANDBOX_H_ */
