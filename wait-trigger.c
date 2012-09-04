#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#ifndef O_NONBLOCK
#define O_NONBLOCK O_NDELAY
#endif

int main(int argc, char *argv[])
{
	struct stat sb;
	fd_set rfds;
	char d[8192];
	int fd, fdw;
	mode_t m;

	if (argc != 2) {
#define M "Usage: wait-trigger pipe\n"
		write(2, M, sizeof(M)-1);
		exit(1);
	}

	umask(m = umask(0));
	m = (0666 & ~m);

	for (;;) {
#ifdef HASMKFIFO
		(void) mkfifo(argv[1], m);
#else
		(void) mknod(argv[1], S_IFIFO | m, 0);
#endif
		if (stat(argv[1], &sb) == -1) {
#define E "Can't access/create pipe trigger\n"
			write(2, E, sizeof(E)-1);
			exit(1);
		}
		if (!S_ISFIFO(sb.st_mode)) {
			write(2, E, sizeof(E)-1);
			exit(1);
#undef E
		}

		fd = open(argv[1], O_RDONLY | O_NDELAY);
		(void) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		/* this works around a bug where select() will fail on
		 * a named pipe that has no writers...
		 *
		 * sadly, not all systems are affected, so this wastes a
		 * file descriptor most of the time...
		 */
		fdw = open(argv[1], O_WRONLY | O_NDELAY);

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		if (select(fd+1,&rfds,0,0,0) > 0) {
			while (read(fd, d, sizeof(d)) > 0);
			break;
		}
		(void)close(fdw);
		(void)close(fd);
	}
	/* trigger pulled */
	exit(0);
}
