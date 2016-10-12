/* unshare.c
 *
 * A simple implementation of the unshare(1) command: unshare
 * namespaces and execute a command.
 **/

#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

/* A simple error-handling function: print an error message based
   on the value `errno` and terminate the calling process
*/
#define bail(msg)				\
	do { perror(msg);			\
		exit(EXIT_FAILURE);		\
	} while (0)

static void usage(char *name) {
	fprintf(stderr, "Usage: %s [options] program [arg...]\n", name);
	fprintf(stderr, "Options can be:\n");
	fprintf(stderr, "	-i unshare IPC namespace\n");
	fprintf(stderr, "	-m unshare mount namespace\n");
	fprintf(stderr, "	-n unshare network namespace\n");
	fprintf(stderr, "	-p unshare PID namespace\n");
	fprintf(stderr, "	-u unshare UTS namespace\n");
	fprintf(stderr, "	-U unshare user namespace\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	int flags, opt;

	flags = 0;

	while ((opt = getopt(argc, argv, "imnpuU")) != -1) {
		switch(opt) {
		case 'i': flags |= CLONE_NEWIPC;	break;
		case 'm': flags |= CLONE_NEWNS;		break;
		case 'n': flags |= CLONE_NEWNET;	break;
		case 'p': flags |= CLONE_NEWPID;	break;
		case 'u': flags |= CLONE_NEWUTS;	break;
		case 'U': flags |= CLONE_NEWUSER;	break;
		default: usage(argv[0]);
		}
	}

	if (optind >= argc)
		usage(argv[0]);

	if (unshare(flags) == -1)
		bail("unshare");

	execvp(argv[optind], &argv[optind]);
	bail("execvp");
}
