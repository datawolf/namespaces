/* multi_pidns.c
 *
 * Create a series of child process in nested PID namespaces
 */
#define _GNU_SOURCE
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sched.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>


/* A simple error-handling function: print an error message based
   on the value `errno` and terminate the calling process
*/
#define bail(msg)				\
	do { perror(msg);			\
		exit(EXIT_FAILURE);		\
	} while (0)


#define STACK_SIZE	(1024 * 1024)	// stack size for cloned child

static char child_stack[STACK_SIZE];

/* Recursively create a series of child process in nested PID namespaces.
  'arg' is an integer that counts down to 0 during the recursion.
  When the counter reaches 0, recursion stops and the tail child executes
  the sleep(1) program.
  */

static int childFunc(void *arg) {
	static int first_call = 1;
	long level = (long) arg;

	if (!first_call) {
		char mount_point[PATH_MAX];
		snprintf(mount_point, PATH_MAX, "/proc%c", (char) ('0' + level));
		mkdir(mount_point, 0555);
		if (mount("proc", mount_point, "proc", 0, NULL) == -1)
			bail("mount");

		printf("Mounting procfs at %s\n", mount_point);
	}

	first_call = 0;

	if (level > 0) {
		level--;
		pid_t	child_pid;

		// create child taht has its own PID namespace;
		// child commences excution in childFunc()
		child_pid = clone(childFunc, child_stack + STACK_SIZE, CLONE_NEWPID| SIGCHLD, (void*) level);
		if (child_pid == -1)
			bail("clone");

		if (waitpid(child_pid, NULL, 0) == -1) // wait for child
			bail("waitpid");
	} else {
		printf("Final child sleeping\n");
		execlp("sleep", "sleep", "1000", (char *)NULL);
		bail("execlp");
	}

	return 0;
}

int main(int argc, char **argv) {
	long	levels;

	levels = (argc > 1) ? atoi(argv[1]) : 5;
	childFunc((void *)levels);

	exit(EXIT_SUCCESS);
}
