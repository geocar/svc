#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#ifndef PID_DIR
#define PID_DIR		"/var/run"
#endif

#ifndef PID_EXT
#define PID_EXT		".pid"
#endif

#ifdef NO_LOCKING
#warning "Remember to rm -f PID_DIR/*.PID_EXT before running sleep-svc scripts in startup"
#endif

static int child_pid = -1;
static int scheduled_restart = 0;
static int child_died = 0;
static int child_status = 0;
static char *pidfile = 0;

void dolock(int fd);
int trylock(int fd);

static void handle_signal(int signum)
{
	if (signum == SIGHUP) {
		if (child_pid != -1) {
			kill(child_pid, SIGTERM);
			sleep(1);
			waitpid(child_pid, 0, WNOHANG);
			child_pid = -1;
		}
		scheduled_restart = 1;
		return;
	}

	unlink(pidfile);
	if (child_pid != -1)
		kill(child_pid, SIGTERM);
	sleep(1);
	waitpid(child_pid, 0, WNOHANG);
	exit(0);
}

int main(int argc, char **argv)
{
	FILE *fp;
	int wret;
	sigset_t sigs;
	time_t last10, now;
	int child_restarts;
	int ignore_running;
	int i, fg_hack, pi[2];
	char fgbuf[1];

	ignore_running = 0;
	fg_hack = 0;

	while (argc > 1) {
		if (strcmp(argv[1], "-f") == 0) {
			argv++;
			argc--;
			fg_hack = 1;
		} else if (strcmp(argv[1], "-i") == 0) {
			argv++;
			argc--;
			ignore_running = 1;
		} else {
			/* no more args I recognize */
			break;
		}
	}

	if (argc < 2) {
		fprintf(stderr, "Usage: %s [-i] [-f] nym child...\n", argv[0]);
		exit(1);
	}

	pidfile = (char *)malloc(strlen(argv[1]) +
			strlen(PID_DIR PID_EXT) + 2);
	if (!pidfile) {
		perror("malloc");
		exit(1);
	}

	/* we do it like this:
	 * if we _can_ lock it, then no one else is attached to it.
	 * if we cannot lock it, then we're the only attacher.
	 * we keep the lock (and the fd- and the inode) to avoid deadlocks
	 * and race conditions.
	 */
	sprintf(pidfile, PID_DIR "/%s" PID_EXT, argv[1]);
	fp = fopen(pidfile, "r+");
	if (fp) {
		int my_pid;
		if (trylock(fileno(fp)) == -1
		&& fscanf(fp, "%u", &my_pid) == 1 && !ignore_running) {
			if (kill(my_pid, 0) != -1) {
				fclose(fp);
				fprintf(stderr, "already running\n");
				exit(1);
			}
		}
		/* trylock returns it locked */
		rewind(fp);
	} else {
		fp = fopen(pidfile, "w");
		if (!fp) {
			perror("fopen");
			exit(1);
		}
		dolock(fileno(fp));
	}
	fprintf(fp, "%u\n", (unsigned int)getpid());
	fflush(fp);

	time(&last10);

	child_restarts = 0;

restart_handler_l:
	if (fg_hack) {
		if (pipe(pi) == -1) {
			perror("pipe");
			exit(1);
		}
	}

	child_died = 0;
	child_pid = fork();
	if (child_pid == -1) {
		perror("fork");
		exit(1);
	}

	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_DFL);
	if (child_pid == 0) {
		/* child */
		fclose(fp);
		if (fg_hack) close(pi[0]);
		for (i = 0; i < 30; i++) dup(pi[1]);

		sleep(1); /* avoid race condition */

		execvp(argv[2], argv+2);
		exit(1);
	}
	if (fg_hack) close(pi[1]);

	signal(SIGHUP, handle_signal);
	signal(SIGTERM, handle_signal);
	signal(SIGABRT, handle_signal);
	signal(SIGQUIT, handle_signal);
	signal(SIGINT, handle_signal);

	if (fg_hack) {
		/* read keeps us blocked until the child process dies.
		 * UNLESS the child process went out of it's way to close
		 * _all_ file descriptors, and the administrator has no
		 * way of keeping that program foreground...
		 *
		 * but at that point... we're kind of screwed anyway.
		 */
		do {
			i = read(pi[0], fgbuf, 1);
		} while ((i == -1 && errno == EINTR) || i == 1);
	}

	if ((wret = waitpid(child_pid, &child_status, 0)) == -1) {
		if (errno == EINTR) {
			sigemptyset(&sigs);
			sigpending(&sigs);
			if (sigismember(&sigs, SIGHUP) == 1)
				handle_signal(SIGHUP);
			if (sigismember(&sigs, SIGTERM) == 1)
				handle_signal(SIGTERM);
			if (sigismember(&sigs, SIGINT) == 1)
				handle_signal(SIGINT);
		} else
			child_died = 1;
	} else
	if (wret != 0) {
		child_died = 1;
	}

	if (scheduled_restart && !child_died)
		goto restart_handler_l;

	time(&now);

	fflush(stderr);
	if (now - last10 < (5*60*60)) {
		child_restarts++;
		if (child_restarts > 5) {
			fprintf(stderr, "%s restarted 10 times in 5 minutes, sleeping for 1 minute\n", argv[1]);
			fflush(stderr);
			sleep(60);

			/* reset timer */
			child_restarts = 0;
			last10 = now;
		}
	} else {
		child_restarts = 0;
		last10 = now;
	}
	goto restart_handler_l;
}
