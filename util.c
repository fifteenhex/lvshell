#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

const pid_t util_start_cmd(const char *executable, char * const *args,
		const char* dir)
{
	pid_t pid;
	int status;
	char **argp;
	extern char **environ;
	char **envp;
	//char *env[16] = NULL;
	//int envc = 0;

	if (!executable)
		return -1;

	printf("Starting: %s\n", executable);

	printf("args: \n");
	for(argp = args; *argp != NULL; argp++)
		printf("%s ", *argp);
	printf("\n");

	printf("evn: \n");
	for(envp = environ; *envp != NULL; envp++){
		printf("%s ", *envp);
	}
	printf("\n");

	pid = fork();
	/* failed to fork */
	if (pid == -1)
		return pid;
	/* I'm the child */
	else if (pid == 0) {
		if(dir) {
			printf("Changing dir to %s\n", dir);
			chdir(dir);
		}
		execve(executable, args, environ);
		/* It's only possible to get here if execve failed */
		printf("Failed to execve()\n");
		_Exit(127);
	}
	/* I'm the parent */
	else {
		printf("Forked as: %d\n", pid);
		return pid;
	}

	/* shouldn't get here? */
	return 0;
}

