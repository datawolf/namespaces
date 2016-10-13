/* demo_userns.c
 *
 * Demonstrate the use of the clone() CLONE_NEWUSER flag.
 * 
 * Link with `-lcap` and make sure that `libcap-devel` (or similar)
 * package is installed on the system.
 **/

#define _GNU_SOURCE
#include <sys/wait.h>
#include <sys/capability.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#define bail(msg)						\
	do {							\
		perror(msg);					\
		exit(EXIT_FAILURE);				\
	} while (0)


// Startup funciton for cloned child
static int childFunc(void *arg) {
	cap_t	caps;

	for(;;) {
		printf("eUID = %ld; eGID = %ld ", (long)geteuid(), (long)getegid());

		caps = cap_get_proc();
		printf("capabilities: %s\n", cap_to_text(caps, NULL));

		if (arg == NULL)
			break;

		sleep(5);
	}

	return 0;
}


#define		STACK_SIZE	(1024 *1024)
static char child_stack[STACK_SIZE];	//space for child's stack

int main(int argc, char **argv) {
	pid_t	pid;

	// create child; child commences execution in childFunc()
	pid = clone(childFunc, child_stack + STACK_SIZE, CLONE_NEWUSER | SIGCHLD, argv[1]);
	if (pid == -1)
		bail("clone");

	// Parent falls through to here, Wait for child
	if (waitpid(pid, NULL, 0) == -1)
		bail("waitpid");

	exit(EXIT_SUCCESS);
}
