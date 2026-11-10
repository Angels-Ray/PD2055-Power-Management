#include "pdcharge.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static FILE *log_fp;
static int log_enabled;

void log_init(const char *path)
{
	char rotated[PD_PATH_MAX];
	struct stat st;

	log_close();

	if (strcmp(path, "-") == 0) {
		log_fp = stderr;
		log_enabled = 1;
		return;
	}

	if (stat(path, &st) == 0 && st.st_size > PD_LOG_MAX_BYTES) {
		snprintf(rotated, sizeof(rotated), "%s.1", path);
		unlink(rotated);
		rename(path, rotated);
	}

	log_fp = fopen(path, "a");
	log_enabled = log_fp != NULL;
}

void log_close(void)
{
	if (log_fp && log_fp != stderr)
		fclose(log_fp);
	log_fp = NULL;
	log_enabled = 0;
}

void log_msg(const char *fmt, ...)
{
	char stamp[32];
	struct tm tm;
	time_t now;
	va_list ap;

	if (!log_enabled || !log_fp)
		return;

	now = time(NULL);
	localtime_r(&now, &tm);
	strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);

	fprintf(log_fp, "%s ", stamp);
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	va_end(ap);
	fputc('\n', log_fp);
	fflush(log_fp);
}
