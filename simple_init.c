/* simple_init.c
 *
 * A simple init(1)-style program to be used as the init program in
 * PID namespaces. The programe reaps the status of its children and
 * provides a simple shell facility for executing commands
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <wordexp.h>
#include <errno.h>


/* A simple error-handling function: print an error message based
   on the value `errno` and terminate the calling process
*/
#define bail(msg)				\
	do { perror(msg);			\
		exit(EXIT_FAILURE);		\
	} while (0)


static int verbose = 0;

// Display wait status(from waitipd() or similar) given in `status`
// SIGCHLD handler: reap child processes as they change state
static void child_handler(int sig)
{
	pid_t	pid;
	int status;

	// WUNTRACED and WCONTINUED allow waitpid() to catch stopped and 
	// continued children (in addition to terminated children)
	while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED)) != 0) {
		if (pid == -1) {
			if (errno == ECHILD)  // no more children
				break;
			else
				perror("waitpid");
		}

		if (verbose)
			printf("\tinit: SIGCHLD handler: PID %ld terminated\n", (long)pid);
	}
}


// Perform word expansion on string in `cmd`, allocating and 
// returning a vector of words on success or NULL on failure
static char **expand_words(char *cmd) {
	char **arg_vec;
	int s;
	wordexp_t  pwordexp;

	s = wordexp(cmd, &pwordexp, 0);
	if (s != 0) {
		fprintf(stderr, "Word expansion failed\n");
		return NULL;
	}

	arg_vec = calloc(pwordexp.we_wordc + 1, sizeof(char *));
	if (arg_vec == NULL)
		bail("calloc");

	for (s = 0; s < pwordexp.we_wordc; s++)
		arg_vec[s] = pwordexp.we_wordv[s];

	arg_vec[pwordexp.we_wordc] = NULL;

	return arg_vec;
}


static void usage(char *name) {
	fprintf(stderr, "Usage: %s [-q]\n", name);
	fprintf(stderr, "\t-v\tProvide verbose logging\n");

	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	struct sigaction	sa;
#define	CMD_SIZE	10000
	char cmd[CMD_SIZE];
	pid_t	pid;
	int opt;

	while ((opt = getopt(argc, argv, "v")) != -1) {
		switch(opt) {
		case 'v':	verbose = 1;	break;
		default:	usage(argv[0]);
		}
	}

	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = child_handler;
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		bail("sigaction");

	if (verbose)
		printf("\tinit: my PID is %ld\n", (long) getpid());

	// Performing terminal operations while not being the foregroud process
	// group for the terminal generates a SIGTTOU that stops the process
	// However our init `shell` needs to be able to perform such operations
	// (just like a normal shell), so we ignore that signal, which allows the
	// operations to proceed successfully

	signal(SIGTTOU, SIG_IGN);

	// Become leader of a new process group and make that process group the 
	// group the foreground process group for the terminal
	if (setpgid(0,0) == -1)
		bail("setpgid");
	if (tcsetpgrp(STDIN_FILENO, getpgrp()) == -1)
		bail("tcsetpgrp-child");

	while (1) {
		// Read a shell command; exit on end of file
		printf("init$ ");
		if (fgets(cmd, CMD_SIZE, stdin) == NULL) {
			if (verbose)
				printf("\tinit: exiting");
			printf("\n");
			exit(EXIT_FAILURE);
		}

		if (cmd[strlen(cmd)-1] == '\n')
			cmd[strlen(cmd)-1] = '\0';	// Strip trailing `\n`

		if (strlen(cmd) == 0)
			continue;	// ignore empty commands

		pid = fork();		// create child process
		if (pid == -1)
			bail("fork");

		// child
		if (pid == 0) {
			char **arg_vec;

			arg_vec = expand_words(cmd);
			if (arg_vec == NULL)		// Word expansion failed
				continue;

			// make child the leader of a new process group and 
			// make that process group the foreground process group for the terminal

			if (setpgid(0,0) == -1)
				bail("setpgid");

			if (tcsetpgrp(STDIN_FILENO, getpgrp()) == -1)
				bail("tcsetpgrp-child");

			// child executes shell command and terminates
			execvp(arg_vec[0], arg_vec);
			bail("execvp");
		}

		// Parent falls through to here
		if (verbose)
			printf("\tinit: created child %ld\n", (long)pid);

		pause();		// Will be interrupted by signal handler

		// After child changes state, ensure that the `init` program is 
		// the foreground process group for the terminal

		if (tcsetpgrp(STDIN_FILENO, getpgrp()) == -1)
			bail("tcsetpgrp-parent");
	}
}
