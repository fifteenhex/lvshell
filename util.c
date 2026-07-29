#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "debug.h"
#include "util.h"

extern char **environ;

/*
 * Fork and exec 'executable' with 'args' (argv, NULL-terminated), optionally
 * from working directory 'dir'. Returns the child pid, or -1 on failure to
 * fork. The child _Exit()s (never returns into shared state) if anything goes
 * wrong before/at exec.
 */
pid_t util_start_cmd(const char *executable, const char * const *args,
		const char *dir, const char *logpath)
{
	pid_t pid;

	if (!executable || !args || !args[0])
		return -1;

	pid = fork();
	if (pid < 0) {
		DBG("util: fork failed: %s\n", strerror(errno));
		return -1;
	}

	if (pid == 0) {
		/* Child. */
		if (dir && chdir(dir) != 0)
			_Exit(126);   /* couldn't enter the working directory */

		/* Capture the child's output so failures are diagnosable. */
		if (logpath) {
			int lf = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

			if (lf >= 0) {
				dup2(lf, 1);
				dup2(lf, 2);
				if (lf > 2)
					close(lf);
			}
		}

		/*
		 * Don't leak lvshell's file descriptors (DirectFB/SDL, the input
		 * devices, the control FIFO, the debug log, ...) into the child;
		 * the target opens its own. Keep only stdin/stdout/stderr.
		 */
		for (int fd = 3; fd < 256; fd++)
			close(fd);

		execve(executable, (char *const *)args, environ);
		_Exit(127);   /* execve only returns on failure */
	}

	DBG("util: started %s as pid %d\n", executable, (int)pid);
	return pid;
}
