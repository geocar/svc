/* TTYwrap (C) Copyright 2002-2003 Geo Carncross (Internet Connection) */

/*
 * ttywrap is the necessary feature of expect. it creates a pseudo-tty,
 * and attaches it to the standard i/o that was passed to this process.
 *
 * it should be setuid-root safe; just define NEED_SUID if you're going to
 * install setuid. If not, don't set it.
 *
 */

#ifdef USE_FORKPTY
#include <pty.h>
#endif

#ifdef USE_STREAMS
#include <stropts.h>
#endif

#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/termios.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

#ifndef PIPE_BUF
#ifndef _PC_PIPEBUF
#define PIPE_BUF 16384
#else
#define PIPE_BUF _PC_PIPEBUF
#endif
#endif

#ifndef PATH_MAX
#ifndef _PC_PATHMAX
#define PATH_MAX 4096
#else
#define PATH_MAX _PC_PATHMAX
#endif
#endif


static void no_echo(int fd)
{
	struct termios tt;
	if (tcgetattr(fd, &tt) == -1) return;
	tt.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
		|INLCR|IGNCR|ICRNL|IXON);
	tt.c_oflag &= ~OPOST;
	tt.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	tt.c_cflag &= ~(CSIZE|PARENB);
	tt.c_cflag |= CS8;
	(void)tcsetattr(fd, TCSANOW, &tt);
}
static void drop_privs(void)
{
	/* this is only necessary if we're built SETUID */
#ifdef NEED_SUID
	if (geteuid() == 0) {
		if (getgid() != getegid()) (void)setgid(getgid());
		if (getuid() != geteuid()) (void)setuid(getuid());
	} else {
		exit(255);
	}
#else
	if (getuid() != geteuid() || getgid() != getegid()) exit(255);
#endif
}

static int sigchld[2];

static void sigchld_h(int n)
{
	fcntl(sigchld[1],F_SETFL,fcntl(sigchld[1],F_GETFL)|O_NONBLOCK);
	(void)write(sigchld[1], "\1", 1);
}

static void bwrite(int fd, char *buf, int len)
{
	int r, x;
	for (x = 0; x < len;) {
		do {
			r = write(fd, buf+x, len-x);
		} while (r == -1 && errno == EINTR);
		if (r < 1) exit(255);
		x += r;
	}
}
static int dopipe(int r, int w)
{
	static char buf[PIPE_BUF*2];
	int i;

	do {
		i = read(r, buf, sizeof(buf));
	} while (i == -1 && errno == EINTR);
#ifdef EAGAIN
	if (i == -1 && errno == EAGAIN) return 1;
#endif
#ifdef EWOULDBLOCK
	if (i == -1 && errno == EWOULDBLOCK) return 1;
#endif
	if (i < 1) return 0;
	bwrite(w, buf, i);
	return 1;
}
static void dostdio(int tty, int pid)
{
	fd_set r, w;
	int i, st;
	int m, cl;

	no_echo(tty);

	m = tty;
	if (sigchld[0] > m) m = sigchld[0];
	m++;

	fcntl(tty,F_SETFL,fcntl(tty,F_GETFL)|O_NONBLOCK);
	fcntl(0,F_SETFL,fcntl(0,F_GETFL)|O_NONBLOCK);
	fcntl(1,F_SETFL,fcntl(1,F_GETFL)|O_NONBLOCK);
	for (cl = 0;;) {
		do {
			FD_ZERO(&r);FD_ZERO(&w);
			if (!(cl & 1)) { FD_SET(tty,&r);FD_SET(1, &w); }
			if (!(cl & 2)) { FD_SET(tty,&w);FD_SET(0, &r); }
			FD_SET(sigchld[0],&r);

			i = select(m, &r, &w, 0, 0);
		} while (i == -1 && errno == EINTR);
		if (FD_ISSET(tty, &r) && FD_ISSET(1, &w)) {
			if (!dopipe(tty, 1)) cl|=1;
		}
		if (FD_ISSET(tty, &w) && FD_ISSET(0, &r)) {
			if (!dopipe(0, tty)) cl|=2;
		}
		if ((cl & 1) && FD_ISSET(sigchld[0],&r)) break;
	}
	(void)close(0);
	(void)close(1);
	(void)close(tty);

	/* deliver SIGUP */
	signal(SIGHUP,SIG_IGN);
	(void)kill(0, SIGHUP);
	signal(SIGHUP,SIG_DFL);

	while (wait(&st) != pid);
	if (WIFEXITED(st)) exit(WEXITSTATUS(st));
	exit(255);
}

#if defined(USE_FORKPTY)
static void doit(char **argv)
{
	int master;
	int pid;

	pid = forkpty(&master, NULL, NULL, NULL);
	if (pid == -1) exit(99);
	if (pid == 0) {
		drop_privs();
		no_echo(0);
		no_echo(1);
		no_echo(2);
		execvp(argv[0], argv);
		exit(255);
	}
	drop_privs();
	dostdio(master, pid);
}
#elif defined(USE_STREAMS)
static void doit(char **argv)
{
	int fd, tty, ptm;
	char *pts;
	int pid;

	ptm = open("/dev/ptmx", O_RDWR|O_NOCTTY);
	if (ptm < 0) exit(99);
	if (grantpt(ptm) < 0) exit(98);
	if (unlockpt(ptm) < 0) exit(98);

	pts = (char *)ptsname(ptm);
	if (!pts) exit(97);

	tty = open(pts, O_RDWR|O_NOCTTY);
	if (ioctl(tty, I_PUSH, "ptem") < 0) exit(95);
	if (ioctl(tty, I_PUSH, "ldterm") < 0) exit(95);
	ioctl(tty, I_PUSH, "ttcompat");

	/* okay */

	pid = fork();
	if (pid == -1) exit(99);
	if (pid == 0) {
		/* disconnect tty */
		fd = open("/dev/tty", O_RDWR|O_NOCTTY);
		if (fd >= 0) {
			ioctl(fd, TIOCNOTTY, (char *)0);
			close(fd);
		}
	
		/* create process group */
		setsid();

#ifdef TIOCSCTTY
		if (ioctl(tty, TIOCSCTTY, (char *)0) == -1) exit(98);
#endif
		setpgrp();

		signal(SIGHUP, SIG_IGN);
		vhangup();
		signal(SIGHUP, SIG_DFL);

		/* now it's time */
		drop_privs();

		fd = open(pts, O_RDWR);
		if (fd < 0) exit(96);
		close(tty);
		/* waste the FD */
		
		/* close these fd's */
		close(0);
		close(1);
		close(2);
	
		fd = open("/dev/tty", O_RDWR);
		if (fd < 0) exit(99);
		no_echo(fd);
	
		if (fd != 0) dup2(fd, 0);
		if (fd != 1) dup2(fd, 1);
		if (fd != 2) dup2(fd, 2);
		if (fd > 2) close(fd);

		close(ptm);

		execvp(argv[0], argv);
		exit(1);
	}

	drop_privs();
	close(tty);
	dostdio(ptm, pid);
}
#endif

int main(int argc, char *argv[])
{
	/* this makes sure fd 0 and 1 are opened */
	if (fcntl(0, F_GETFL) == -1) exit(1);
	if (fcntl(1, F_GETFL) == -1) exit(1);

	if (argc < 2) {
		drop_privs();

		/* don't print to stderr */
		if (fcntl(2, F_GETFL) != -1) {
			/* write this out */
#define x_usage "Usage: ttywrap exe args...\n"
			bwrite(2, x_usage, sizeof(x_usage));
#undef x_usage
		}
		exit(1);
	}
	if (pipe(sigchld) == -1) exit(1);
	signal(SIGCHLD,sigchld_h);

	argv++;
	doit(argv);

	exit(1);
}

