#ifndef DEBUG_H_
#define DEBUG_H_

#include <stdio.h>

/*
 * Debug logging, enabled by the LVSHELL_DEBUG build option. It writes to a file
 * (opened by dbg_init() in main.c) because lvshell runs as a background daemon
 * with stdout/stderr redirected to /dev/null and DirectFB owns the console, so
 * ordinary stderr logging is invisible.
 */
#ifdef LVSHELL_DEBUG
extern FILE *g_dbg;
#define DBG(...) do { if (g_dbg) { fprintf(g_dbg, __VA_ARGS__); fflush(g_dbg); } } while (0)
#else
#define DBG(...) do { } while (0)
#endif

#endif /* DEBUG_H_ */
