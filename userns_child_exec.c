/* userns_child_exec.c
 *
 * Create a child process that executes a shell command in new namespaces
 * allow UID and  GID mappings to be specified when creating
 * a user namespace
 **/

#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>


/* A simple error-handling function: print an error message based
   on the value `errno` and terminate the calling process
*/
#define bail(msg)				\
	do { perror(msg);			\
		exit(EXIT_FAILURE);		\
	} while (0)


struct child_args {
	char **argv;		// command to be execute by child, with arguments
	int	pipe_fd[2];	// pipe used to synchronize parent and child
};

static int verbose;

static void usage(char *name) {
	fprintf(stderr, "Usage: %s [options] cmd [arg...]\n", name);
	fprintf(stderr, "Create a child process that executes a shell command"
			"in a new user namespace"
			"and possibly also other new namespaces\n\n");
	fprintf(stderr, "Options can be:\n");
	fprintf(stderr, "	-i		 new IPC namespace\n");
	fprintf(stderr, "	-m		 new mount namespace\n");
	fprintf(stderr, "	-n		 new network namespace\n");
	fprintf(stderr, "	-p		 new PID namespace\n");
	fprintf(stderr, "	-u		 new UTS namespace\n");
	fprintf(stderr, "	-U		 new user namespace\n");
	fprintf(stderr, "	-M uid_map	 Speciffy UID map for user user namespace\n");
	fprintf(stderr, "	-G gid_map	 Speciffy GID map for user user namespace\n");
	fprintf(stderr, "			 If -M or -G is specified, -U is required\n");
	fprintf(stderr, "	-z		 Map user's UID and GID to 0 in user namespace\n");
	fprintf(stderr, "			(equivalent to: -M '0 <uid> 1' -G '0 <gid> 1')\n");
	fprintf(stderr, "	-v		 Display verbose message\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "	If -z, -M, or -G is specified, -U is required.\n");
	fprintf(stderr, "	It is not permitted to specify both -z and either -M or -G.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "	Map string for -M and -G consist of records of the form:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "	ID-inside-ns	ID-outside-ns	len\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "A map string can contain multiple records, separated by commas;\n");
	fprintf(stderr, "the commas are replaced by newlines before we writing to map files\n");

	exit(EXIT_FAILURE);
}


/* Update the mapping file `map_file`, with the value provided in
  `mapping`, a string that defines a UID and GID mapping. A UID or GID
   consists of one or more newline-delimited records of the form:

     ID-inside-ns	Id-outside-ns length

   Requireing the user to supply a string that contains newlines is
   of course inconvenient for command-line use. Thuswe permit the
   use of commas to delimit records in this string, and replace them
   with newlines befores writing the string to the file
**/

static void update_map(char *mapping, char *map_file) {
	int fd, j;
	size_t	map_len;		// length of `mapping`

	// Replace commas in mapping string with newlines
	map_len = strlen(mapping);
	for (j = 0; j < map_len; j++)
		if (mapping[j] == ',')
			mapping[j] = '\n';

	fd = open(map_file, O_RDWR);
	if (fd == -1) {
		fprintf(stderr, "ERROR: open %s: %s\n", map_file, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (write(fd, mapping, map_len) != map_len) {
		fprintf(stderr, "ERROR: write %s: %s\n", map_file, strerror(errno));
		exit(EXIT_FAILURE);
	}

	close(fd);
}

/* Linux 3.19 made a change in the handling of setgroups(2) and the 'gid_map'
 * file to address a security issue.
 * 
 * the issue allowd *unprivileged* users to employ user namespaces in order to drop 
 * The upshort of the 3.19 changes is that in order to update the 
 * 'gid_map' file. use of the setgroups() system call in this usernamespace
 * must first be disable by writing `deny` to one of the /proc/PID/setgroups files for
 * this namespaces.
 * That is the purpose of the following function.
 **/
static void proc_setgroups_write(pid_t child_pid, char *str){
	char setgroups_path[PATH_MAX];
	int fd;

	snprintf(setgroups_path, PATH_MAX, "/proc/%ld/setgroups", (long)child_pid);

	fd = open(setgroups_path, O_RDWR);
	if (fd == -1) {
		/**
		  * We maybe on a system that does not support /proc/PID/setgroups. 
		  * In that case, the file won't exist and the system won't impose the
		  * restrictions tht Linux 3.19 added. That's fine: we do not need to do
		  * anything in order to permit `gid_map` to be updated.
		  *
		  * However, if the error from open() was something other than ENOENT
		  * error that is expected for that case, let the user know.
		  */
		if (errno != ENOENT)
			fprintf(stderr, "ERROR: open %s: %s\n", setgroups_path,
				 strerror(errno));
		return;
	}

	if (write(fd, str, strlen(str)) == -1) 
		fprintf(stderr, "ERROR: open %s: %s\n", setgroups_path,
			strerror(errno));

	close(fd);
}

// Start function for cloned child
static int childFunc(void *arg) {
	struct child_args *args = (struct child_args*)arg;
	char ch;

	// Wait until the parent has updated the UID and GID mappings.
	// see comment in main(). We wait for end of file on a pipe that will
	// be closed by the parent process once it has updated the mappings

	// close our descriptor for the write end of the pipe so that we see EOF
	// when parent closed its descriptor
	close(args->pipe_fd[1]);

	if (read(args->pipe_fd[0], &ch, 1) != 0) {
		fprintf(stderr, "Failure in child: read from pipe returned !=0\n");
		exit(EXIT_FAILURE);
	}

	execvp(args->argv[0], args->argv);
	bail("execvp");
}

#define STACK_SIZE	(1024 * 1024)
static char child_stack[STACK_SIZE];		// space for child's stack

int main(int argc, char **argv) {
	int flags, opt, map_zero;
	pid_t	child_pid;
	struct child_args	args;
	char *uid_map, *gid_map;
	char map_path[PATH_MAX];
	const int MAP_BUF_SIZE = 100;
	char map_buf[MAP_BUF_SIZE];

	flags = 0;
	verbose = 0;
	map_zero = 0;
	gid_map = NULL;
	uid_map = NULL;

	/* Parse command-line options
	 the initial `+` character in the final getopt() argument
	 prevents GNU-style permutation of command-line options.
	 That's usefull, since sometimes the `command` to be execute by this
	 programe itself has command-line options.
	 We do not want getopt() to treat those as options to this program.
	*/
	while ((opt = getopt(argc, argv, "+imnpuUvM:G:z")) != -1) {
		switch(opt) {
		case 'i': flags |= CLONE_NEWIPC;	break;
		case 'm': flags |= CLONE_NEWNS;		break;
		case 'n': flags |= CLONE_NEWNET;	break;
		case 'p': flags |= CLONE_NEWPID;	break;
		case 'u': flags |= CLONE_NEWUTS;	break;
		case 'v': verbose = 1;			break;
		case 'z': map_zero = 1;			break;
		case 'M': uid_map = optarg;		break;
		case 'G': gid_map = optarg;		break;
		case 'U': flags |= CLONE_NEWUSER;	break;
		default: usage(argv[0]);
		}
	}

	// -M or -g without -U is nosensical
	if (((uid_map != NULL || gid_map != NULL || map_zero) && !(flags & CLONE_NEWUSER)) || 
		(map_zero && (uid_map != NULL || gid_map != NULL)))
		usage(argv[0]);

	if (optind >= argc)
		usage(argv[0]);

	args.argv = &argv[optind];

	// We use a pipe to synchronize the parent and child. in order to
	// ensure that the parent sets the UID  and GID maps before the child call
	// execve().
	// This ensures that the child maintains its capabilities during the execve()
	// in common case where we want to map the child's effective user ID to 0
	// in the new user namespaces.
	// Without this synchronization, the child would lose its capabilities
	// if it performed an execve() with nonzero user IDs
	// (see the capabilities(7) man page for details of the
	// transformation of a process's capabilities during execve())
	if (pipe(args.pipe_fd) == -1)
		bail("pipe");

	// create the child in new namespaces
	child_pid = clone(childFunc, child_stack + STACK_SIZE, flags | SIGCHLD, &args);
	if (child_pid == -1)
		bail("clone");

	// Parent falls through to here
	if (verbose)
		printf("%s: PID of child created by clone() is %ld\n", argv[0], (long) child_pid);

	// Update the uid and gid maps in the child
	if (uid_map != NULL || map_zero) {
		snprintf(map_path, PATH_MAX, "/proc/%ld/uid_map", (long) child_pid);
		if (map_zero ){
			snprintf(map_buf, MAP_BUF_SIZE, "0 %ld 1", (long) getuid());
			uid_map = map_buf;
		}
		update_map(uid_map, map_path);
	}
	if (gid_map != NULL || map_zero) {
		proc_setgroups_write(child_pid, "deny");
		snprintf(map_path, PATH_MAX, "/proc/%ld/gid_map", (long) child_pid);
		if (map_zero ){
			snprintf(map_buf, MAP_BUF_SIZE, "0 %ld 1", (long) getgid());
			gid_map = map_buf;
		}
		update_map(gid_map, map_path);
	}

	// close the write end of the pipe, to signal to the child that we have
	// update the UID and GID maps
	close(args.pipe_fd[1]);

	if (waitpid(child_pid, NULL, 0) == -1)
		bail("waitpid");

	if (verbose)
		printf("%s: terminating\n", argv[0]);

	exit(EXIT_FAILURE);
}
