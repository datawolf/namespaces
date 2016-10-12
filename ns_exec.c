/* ns_exec.c
 *
 * Join a namespace and execute a command in the namespace
 **/

#define _GNU_SOURCE
#include <fcntl.h>
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

int main(int argc, char **argv) {
	int fd;

	if (argc < 3) {
		fprintf(stderr, "%s /proc/PID/ns/FILE cmd args...\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	fd = open(argv[1], O_RDONLY);		// get file descriptor for namespace
	if (fd == -1)
		bail("open");

	if (setns(fd, 0) == -1)			// join that namespace
		bail("setns");

	execvp(argv[2], &argv[2]);		// execute a command in namespace
	bail("execvp");

}
