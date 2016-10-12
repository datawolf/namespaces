/* ns_child_exec.c
 *
 * Create a child process that executes a shell command in new namespaces
 **/

#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <signal.h>

/* A simple error-handling function: print an error message based
   on the value `errno` and terminate the calling process
*/
#define bail(msg)				\
	do { perror(msg);			\
		exit(EXIT_FAILURE);		\
	} while (0)

static void usage(char *name) {
	fprintf(stderr, "Usage: %s [options] cmd [arg...]\n", name);
	fprintf(stderr, "Options can be:\n");
	fprintf(stderr, "	-i new IPC namespace\n");
	fprintf(stderr, "	-m new mount namespace\n");
	fprintf(stderr, "	-n new network namespace\n");
	fprintf(stderr, "	-p new PID namespace\n");
	fprintf(stderr, "	-u new UTS namespace\n");
	fprintf(stderr, "	-U new user namespace\n");
	fprintf(stderr, "	-v Display verbose message\n");
	exit(EXIT_FAILURE);
}

// Start function for cloned child
static int childFunc(void *arg) {
	char **argv = arg;
	execvp(argv[0], &argv[0]);
	bail("execvp");
}

#define STACK_SIZE	(1024 * 1024)
static char child_stack[STACK_SIZE];		// space for child's stack

int main(int argc, char **argv) {
	int flags, opt, verbose;
	pid_t	child_pid;

	flags = 0;
	verbose = 0;

	/* Parse command-line options
	 the initial `+` character in the final getopt() argument
	 prevents GNU-style permutation of command-line options.
	 That's usefull, since sometimes the `command` to be execute by this
	 programe itself has command-line options.
	 We do not want getopt() to treat those as options to this program.
	*/
	while ((opt = getopt(argc, argv, "+imnpuUv")) != -1) {
		switch(opt) {
		case 'i': flags |= CLONE_NEWIPC;	break;
		case 'm': flags |= CLONE_NEWNS;		break;
		case 'n': flags |= CLONE_NEWNET;	break;
		case 'p': flags |= CLONE_NEWPID;	break;
		case 'u': flags |= CLONE_NEWUTS;	break;
		case 'U': flags |= CLONE_NEWUSER;	break;
		case 'v': verbose = 1;
		default: usage(argv[0]);
		}
	}

	if (optind >= argc)
		usage(argv[0]);

	child_pid = clone(childFunc, child_stack + STACK_SIZE, flags | SIGCHLD, &argv[optind]);
	if (child_pid == -1)
		bail("clone");

	if (verbose)
		printf("%s: PId of child created by clone() is %ld\n", argv[0], (long) child_pid);

	// Parent falls through to here
	if (waitpid(child_pid, NULL, 0) == -1)
		bail("waitpid");

	if (verbose)
		printf("%s: terminating\n", argv[0]);

	exit(EXIT_FAILURE);
}
