/* pidns_init_sleep.c
 *
 * A simple demonstration of PID namespace
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


/* A simple error-handling function: print an error message based
   on the value `errno` and terminate the calling process
*/
#define bail(msg)				\
	do { perror(msg);			\
		exit(EXIT_FAILURE);		\
	} while (0)


/* Start function for cloned child */
static int childFunc(void *arg) {

	printf("childFunc(): PID  = %ld\n", (long) getpid());
	printf("childFunc(): PPID = %ld\n", (long) getppid());


	char *mount_point = arg;

	if (mount_point != NULL) {
		mkdir(mount_point, 0555);		// Create directory for mount point
		if (mount("proc", mount_point, "proc", 0, NULL) == -1)
			bail("mount");
		printf("Mounting procfs at %s\n", mount_point);
	}

	execlp("sleep", "sleep", "600", (char *)NULL);
	bail("execlp");		// only reached if execlp() fails
}

#define STACK_SIZE	(1024 * 1024)	// stack size for cloned child

static char child_stack[STACK_SIZE];

int main(int argc, char **argv) {
	pid_t	child_pid;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <proc dir>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	// create child taht has its own PID namespace;
	// child commences excution in childFunc()
	child_pid = clone(childFunc, child_stack + STACK_SIZE, CLONE_NEWPID| SIGCHLD, argv[1]);
	if (child_pid == -1)
		bail("clone");

	printf("PID of child created by clone() is %ld\n", (long) child_pid);

	// Parent falls through to here
	if (waitpid(child_pid, NULL, 0) == -1) // wait for child
		bail("waitpid");
	printf("child has terminated\n");

	exit(EXIT_SUCCESS);
}
