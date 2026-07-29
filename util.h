#ifndef UTIL_H_
#define UTIL_H_

/*
 * Fork+exec 'executable' with argv 'args' (NULL-terminated), optionally from
 * working directory 'dir'. If 'logpath' is non-NULL the child's stdout/stderr
 * are redirected there (truncated). Returns the child pid, or -1.
 */
pid_t util_start_cmd(const char *executable, const char * const *args,
		const char *dir, const char *logpath);

#endif
