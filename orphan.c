/* orphan.c
 *
 * Demonstrate that a child becomes orphaned (and is adopted by init(1),
 * whose pid is 1) when its parent exits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	pid_t	pid;

	pid = fork();
	if (pid == -1)	{
		perror("fork");
		exit(EXIT_FAILURE);
	}

	// parent
	if (pid != 0) {
		printf("Parent (PID=%ld) created child with PID %ld\n", (long) getpid(), (long) pid);
		printf("Parent (PID=%ld, PPID=%ld) terminating\n", (long) getpid(), (long) getppid());
		exit(EXIT_FAILURE);
	}

	// child falls through here
	do {
		usleep(100000);
	} while (getppid() != 1);	// Am I an orphan yet?

	printf("\nChild (PID=%ld) now an orphan (parent PID=%ld)\n",
			(long)getpid(), (long)getppid());

	sleep(1);

	printf("Child (PID=%ld) terminating\n", (long) getpid());
	_exit(EXIT_SUCCESS);
}
