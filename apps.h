#ifndef APPS_H_
#define APPS_H_

#define APP_MAX_ARGS 8

/* A discovered, launchable app/game. */
struct app_entry {
	char  title[64];
	char *argv[APP_MAX_ARGS];   /* NULL-terminated; argv[0] is the executable */
	char  dir[128];             /* working directory, "" for none */
};

/*
 * Discover the apps/games present on the system by running every registered
 * discoverer. Returns the number found and points *entries at an internal
 * array (valid until the next call).
 */
int apps_discover(const struct app_entry **entries);

#endif
