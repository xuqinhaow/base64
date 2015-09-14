#include <stdbool.h>

#define RANDOMDEV  "/dev/urandom"

static bool
get_random_data (struct buffers *b, char **errmsg)
{
	int fd;
	ssize_t nread;
	size_t total_read = 0;

	/* Open random device for semi-random data: */
	if ((fd = open(RANDOMDEV, O_RDONLY)) < 0) {
		*errmsg = "Cannot open " RANDOMDEV "\n";
		return false;
	}
	printf("Filling buffer with random data...\n");

	while (total_read < b->regsz) {
		if ((nread = read(fd, b->reg + total_read, b->regsz - total_read)) < 0) {
			*errmsg = "Read error\n";
			close(fd);
			return false;
		}
		total_read += nread;
	}
	close(fd);
	return true;
}
