#include "pdcharge.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#define SYSFS_MAGIC 0x62656572

int read_text_file(const char *path, char *buf, size_t size)
{
	int fd;
	ssize_t n;
	size_t len;

	if (!path || !buf || size == 0)
		return -1;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;

	n = read(fd, buf, size - 1);
	close(fd);
	if (n < 0)
		return -1;

	buf[n] = '\0';
	len = (size_t)n;
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
			   buf[len - 1] == ' ' || buf[len - 1] == '\t'))
		buf[--len] = '\0';

	return (int)len;
}

int read_int_file(const char *path, int *out)
{
	char buf[64];
	char *end;
	long value;

	if (read_text_file(path, buf, sizeof(buf)) < 0)
		return -1;

	errno = 0;
	value = strtol(buf, &end, 10);
	if (errno || end == buf || value < -2147483647L || value > 2147483647L)
		return -1;

	*out = (int)value;
	return 0;
}

int is_write_allowed(const char *path)
{
	static const char *const node = PD_CTM_NODE;
	const char *logical;

	if (!path)
		return 0;

	if (!strcmp(path, node))
		return 1;

	if (strncmp(path, "/tmp/", 5) && strncmp(path, "/data/local/tmp/", 16))
		return 0;

	logical = strstr(path, "/sys/");
	return logical && !strcmp(logical, node);
}

int write_int_file(const char *path, int value)
{
	struct statfs sfs;
	struct stat st;
	char buf[32];
	int live;
	int fd;
	int len;

	if (!is_write_allowed(path))
		return -1;

	live = !strncmp(path, "/sys/", 5);
	len = snprintf(buf, sizeof(buf), "%d\n", value);
	fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -1;

	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return -1;
	}
	if (live && (fstatfs(fd, &sfs) < 0 || sfs.f_type != SYSFS_MAGIC)) {
		close(fd);
		return -1;
	}

	if (write(fd, buf, (size_t)len) != len) {
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

int file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

int ensure_dir(const char *path)
{
	char tmp[PD_PATH_MAX];
	size_t len;
	size_t i;

	if (!path || strlen(path) >= sizeof(tmp))
		return -1;

	strcpy(tmp, path);
	len = strlen(tmp);
	while (len > 1 && tmp[len - 1] == '/')
		tmp[--len] = '\0';

	for (i = 1; i < len; i++) {
		if (tmp[i] != '/')
			continue;
		tmp[i] = '\0';
		if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
			return -1;
		tmp[i] = '/';
	}

	if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
		return -1;

	return 0;
}

void path_join(char *dst, size_t size, const char *root, const char *path)
{
	if (strcmp(root, "/") == 0)
		snprintf(dst, size, "%s", path);
	else
		snprintf(dst, size, "%s%s", root, path);
}

void sleep_ms(int ms, const volatile sig_atomic_t *stop,
	      const volatile sig_atomic_t *wake)
{
	struct timespec req;
	struct timespec rem;

	req.tv_sec = ms / 1000;
	req.tv_nsec = (long)(ms % 1000) * 1000 * 1000;

	while (!*stop && !(wake && *wake)) {
		if (nanosleep(&req, &rem) == 0)
			return;
		if (errno != EINTR)
			return;
		req = rem;
	}
}
