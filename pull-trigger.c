#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef O_NONBLOCK
#define O_NONBLOCK O_NDELAY
#endif

int main(int argc, char *argv[])
{
	int fd;

	fd = open(argv[1], O_WRONLY|O_NDELAY);
	if (fd > -1) {
		(void) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		(void) write(fd, "", 1);
		(void) close(fd);
	}
	exit(0);
}

