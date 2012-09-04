#include <fcntl.h>
#include <unistd.h>

extern void dolock(int fd);

int main(int argc, char *argv[])
{
	int fd;

	if (argc < 3) {
#define M "Usage: lockf file exe...\n"
		(void)write(2, M, sizeof(M)-1);
		exit(0);
	}

	fd = open(argv[1], O_RDWR);
	if (fd > -1) {
		if (fcntl(fd, F_SETFD, 0) == 0) dolock(fd);
	}

	execvp(argv[2], argv+2);
	exit(127);
}
