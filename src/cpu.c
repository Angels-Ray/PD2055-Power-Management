#include "pdcharge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CPU_APPLY_VALUE 1

static void append_path(char *dst, size_t size, const char *base,
			const char *suffix)
{
	size_t blen = strlen(base);
	size_t slen = strlen(suffix);

	if (blen >= size)
		blen = size - 1;
	if (blen + slen >= size)
		slen = size - blen - 1;
	memcpy(dst, base, blen);
	memcpy(dst + blen, suffix, slen);
	dst[blen + slen] = '\0';
}

static int kmod_present(const struct context *ctx)
{
	char path[PD_PATH_MAX];

	path_join(path, sizeof(path), ctx->root, "/sys/module/" PD_CPU_KMOD_NAME);
	return file_exists(path);
}

static int node_write(struct context *ctx, const char *node, int value)
{
	char path[PD_PATH_MAX];

	path_join(path, sizeof(path), ctx->root, node);
	return write_int_file(path, value);
}

static int node_read(struct context *ctx, const char *node, int *value)
{
	char path[PD_PATH_MAX];

	path_join(path, sizeof(path), ctx->root, node);
	return read_int_file(path, value);
}

static int node_text(struct context *ctx, const char *node, char *buf,
		     size_t size)
{
	char path[PD_PATH_MAX];

	path_join(path, sizeof(path), ctx->root, node);
	return read_text_file(path, buf, size);
}

static int node_read_bool(struct context *ctx, const char *node, int *value)
{
	char text[16];

	if (node_text(ctx, node, text, sizeof(text)) < 0)
		return -1;
	*value = text[0] == 'Y' || text[0] == 'y' || text[0] == '1';
	return 0;
}

static void policy_node(struct context *ctx, int policy, const char *name,
			char *dst, size_t size)
{
	char logical[160];

	snprintf(logical, sizeof(logical),
		 "/sys/devices/system/cpu/cpufreq/policy%d/%s", policy, name);
	path_join(dst, size, ctx->root, logical);
}

static int policy_read_int(struct context *ctx, int policy, const char *name,
			   int *value)
{
	char path[PD_PATH_MAX];

	policy_node(ctx, policy, name, path, sizeof(path));
	return read_int_file(path, value);
}

static void online_node(struct context *ctx, int cpu, char *dst, size_t size)
{
	char logical[128];

	snprintf(logical, sizeof(logical),
		 "/sys/devices/system/cpu/cpu%d/online", cpu);
	path_join(dst, size, ctx->root, logical);
}

static int online_read(struct context *ctx, int cpu, int *value)
{
	char path[PD_PATH_MAX];

	online_node(ctx, cpu, path, sizeof(path));
	return read_int_file(path, value);
}

static int online_write(struct context *ctx, int cpu, int value)
{
	char path[PD_PATH_MAX];

	online_node(ctx, cpu, path, sizeof(path));
	return write_int_file(path, value);
}

static void online_marker_path(struct context *ctx, char *dst, size_t size)
{
	append_path(dst, size, ctx->state_dir, "/online_applied");
}

static int online_marker_exists(struct context *ctx)
{
	char path[PD_PATH_MAX];

	online_marker_path(ctx, path, sizeof(path));
	return file_exists(path);
}

static int online_marker_set(struct context *ctx)
{
	char path[PD_PATH_MAX];
	FILE *fp;

	online_marker_path(ctx, path, sizeof(path));
	fp = fopen(path, "w");
	if (!fp)
		return -1;
	fputs("1\n", fp);
	fclose(fp);
	return 0;
}

static void online_marker_clear(struct context *ctx)
{
	char path[PD_PATH_MAX];

	online_marker_path(ctx, path, sizeof(path));
	unlink(path);
}

static int parse_freq_list(const char *buf, int *values, int max)
{
	const char *p = buf;
	char *end;
	long value;
	int count = 0;

	while (*p && count < max) {
		errno = 0;
		value = strtol(p, &end, 10);
		if (end == p)
			break;
		if (!errno && value > 0 && value <= 2147483647L)
			values[count++] = (int)value;
		p = end;
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
	}

	return count;
}

int cpu_nearest_khz(struct context *ctx, int policy, int khz)
{
	char path[PD_PATH_MAX];
	char buf[1024];
	int available[64];
	int count;
	int cpuinfo_min = 0;
	int cpuinfo_max = 0;
	int scaling_min = 0;
	int floor = 0;
	int target = khz;
	int best = 0;
	int best_delta = 0;
	int above_floor = 0;
	int delta;
	int i;

	policy_node(ctx, policy, "scaling_available_frequencies", path,
		    sizeof(path));
	if (read_text_file(path, buf, sizeof(buf)) < 0)
		return khz;
	count = parse_freq_list(buf, available, 64);
	if (count <= 0)
		return khz;

	if (policy_read_int(ctx, policy, "cpuinfo_min_freq", &cpuinfo_min) == 0 &&
	    cpuinfo_min > floor)
		floor = cpuinfo_min;
	if (policy_read_int(ctx, policy, "scaling_min_freq", &scaling_min) == 0 &&
	    scaling_min > floor)
		floor = scaling_min;

	if (floor > 0 && target < floor)
		target = floor;
	if (policy_read_int(ctx, policy, "cpuinfo_max_freq", &cpuinfo_max) == 0 &&
	    cpuinfo_max > 0 && target > cpuinfo_max)
		target = cpuinfo_max;

	for (i = 0; i < count; i++) {
		if (floor <= 0 || available[i] >= floor) {
			if (above_floor == 0 || available[i] < above_floor)
				above_floor = available[i];
		}
		delta = available[i] > target ? available[i] - target :
						target - available[i];
		if (best == 0 || delta < best_delta ||
		    (delta == best_delta && available[i] < best)) {
			best = available[i];
			best_delta = delta;
		}
	}

	if (best > 0 && floor > 0 && best < floor && above_floor > 0)
		best = above_floor;

	return best > 0 ? best : target;
}

static int kmod_load(struct context *ctx)
{
	char ko[PD_PATH_MAX];
	char param[PD_PATH_MAX];
	char *argv[3];
	int rc;
	int i;

	if (kmod_present(ctx))
		return 0;

	path_join(ko, sizeof(ko), ctx->root, PD_CPU_KMOD_PATH);
	if (!file_exists(ko)) {
		log_msg("cpu kmod missing %s", ko);
		return -1;
	}

	argv[0] = (char *)PD_INSMOD_BIN;
	argv[1] = ko;
	argv[2] = NULL;

	rc = program_run(PD_INSMOD_BIN, argv);
	if (rc != 0) {
		log_msg("cpu kmod load failed rc=%d", rc);
		return -1;
	}

	path_join(param, sizeof(param), ctx->root, PD_CPU_TAKEOVER_NODE);
	for (i = 0; i < 20; i++) {
		if (file_exists(param))
			return 0;
		usleep(100 * 1000);
	}

	log_msg("cpu kmod parameter missing %s", param);
	return -1;
}

static int kmod_unload(void)
{
	char *argv[3];

	argv[0] = (char *)PD_RMMOD_BIN;
	argv[1] = (char *)PD_CPU_KMOD_NAME;
	argv[2] = NULL;

	return program_run(PD_RMMOD_BIN, argv);
}

int cpu_apply_targets(struct context *ctx, int little_khz, int big_khz,
		      int prime_khz)
{
	int takeover = 0;

	if (ctx->thermal.cpu_applied && kmod_present(ctx) &&
	    node_read_bool(ctx, PD_CPU_TAKEOVER_NODE, &takeover) == 0 &&
	    takeover &&
	    ctx->thermal.little_khz == little_khz &&
	    ctx->thermal.big_khz == big_khz &&
	    ctx->thermal.prime_khz == prime_khz)
		return 0;

	if (kmod_load(ctx) < 0)
		return -1;

	if (node_write(ctx, PD_CPU_PROBE_NODE, 0) < 0 ||
	    node_write(ctx, PD_CPU_MAX0_NODE, little_khz) < 0 ||
	    node_write(ctx, PD_CPU_MAX4_NODE, big_khz) < 0 ||
	    node_write(ctx, PD_CPU_MAX7_NODE, prime_khz) < 0 ||
	    node_write(ctx, PD_CPU_TAKEOVER_NODE, 1) < 0 ||
	    node_write(ctx, PD_CPU_APPLY_NODE, CPU_APPLY_VALUE) < 0) {
		node_write(ctx, PD_CPU_TAKEOVER_NODE, 0);
		node_write(ctx, PD_CPU_APPLY_NODE, CPU_APPLY_VALUE);
		ctx->thermal.cpu_applied = 0;
		return -1;
	}

	ctx->thermal.cpu_applied = 1;
	ctx->thermal.little_khz = little_khz;
	ctx->thermal.big_khz = big_khz;
	ctx->thermal.prime_khz = prime_khz;
	return 0;
}

int cpu_release_targets(struct context *ctx)
{
	if (!ctx->thermal.cpu_applied)
		return 0;

	if (kmod_present(ctx)) {
		if (node_write(ctx, PD_CPU_TAKEOVER_NODE, 0) < 0 ||
		    node_write(ctx, PD_CPU_APPLY_NODE, CPU_APPLY_VALUE) < 0)
			return -1;
	}

	ctx->thermal.cpu_applied = 0;
	ctx->thermal.little_khz = 0;
	ctx->thermal.big_khz = 0;
	ctx->thermal.prime_khz = 0;
	return 0;
}

int cpu_online_apply(struct context *ctx, int big_online, int prime_online)
{
	static const int cpus[3] = { 5, 6, 7 };
	int desired[3];
	int changed = 0;
	int error = 0;
	int current;
	int i;

	if (big_online < 1)
		big_online = 1;
	if (big_online > 3)
		big_online = 3;
	if (prime_online < 0)
		prime_online = 0;
	if (prime_online > 1)
		prime_online = 1;

	desired[0] = big_online >= 2;
	desired[1] = big_online >= 3;
	desired[2] = prime_online;

	for (i = 0; i < 3; i++) {
		if (online_read(ctx, cpus[i], &current) < 0) {
			error = 1;
			continue;
		}
		if (current == desired[i])
			continue;
		if (online_write(ctx, cpus[i], desired[i]) < 0) {
			error = 1;
			continue;
		}
		changed = 1;
	}

	if (changed || !ctx->thermal.online_applied ||
	    ctx->thermal.big_online != big_online ||
	    ctx->thermal.prime_online != prime_online) {
		online_marker_set(ctx);
		ctx->thermal.online_applied = 1;
		ctx->thermal.big_online = big_online;
		ctx->thermal.prime_online = prime_online;
	}

	return error ? -1 : (changed ? 1 : 0);
}

int cpu_online_restore(struct context *ctx)
{
	static const int cpus[3] = { 5, 6, 7 };
	int error = 0;
	int i;

	if (!ctx->thermal.online_applied)
		return 0;

	for (i = 0; i < 3; i++) {
		if (online_write(ctx, cpus[i], 1) < 0)
			error = 1;
	}

	if (error)
		return -1;

	online_marker_clear(ctx);
	ctx->thermal.online_applied = 0;
	ctx->thermal.big_online = 0;
	ctx->thermal.prime_online = 0;
	return 0;
}

int cpu_online_recover(struct context *ctx)
{
	static const int cpus[3] = { 5, 6, 7 };
	int error = 0;
	int i;

	if (!online_marker_exists(ctx))
		return 0;

	for (i = 0; i < 3; i++) {
		if (online_write(ctx, cpus[i], 1) < 0)
			error = 1;
	}
	if (error) {
		log_msg("cpu online recover failed");
		return -1;
	}

	online_marker_clear(ctx);
	ctx->thermal.online_applied = 0;
	ctx->thermal.big_online = 0;
	ctx->thermal.prime_online = 0;
	log_msg("cpu online recover");
	return 1;
}

void cpu_online_counts(struct context *ctx, int *little, int *big, int *prime)
{
	int value;
	int i;

	*little = 0;
	*big = 0;
	*prime = 0;

	for (i = 0; i < 4; i++) {
		if (online_read(ctx, i, &value) == 0 && value)
			(*little)++;
	}
	for (i = 4; i < 7; i++) {
		if (online_read(ctx, i, &value) == 0 && value)
			(*big)++;
	}
	if (online_read(ctx, 7, &value) == 0 && value)
		(*prime)++;
}

int cpu_probe(struct context *ctx)
{
	if (kmod_load(ctx) < 0)
		return 1;

	if (node_write(ctx, PD_CPU_TAKEOVER_NODE, 0) < 0 ||
	    node_write(ctx, PD_CPU_APPLY_NODE, CPU_APPLY_VALUE) < 0 ||
	    node_write(ctx, PD_CPU_PROBE_NODE, 1) < 0) {
		log_msg("cpu probe write failed");
		fprintf(stderr, "cpu probe write failed\n");
		return 1;
	}

	log_msg("cpu probe on");
	printf("probe=1 takeover=0\n");
	return 0;
}

int cpu_on(struct context *ctx, int little_mhz, int big_mhz, int prime_mhz)
{
	int max0 = cpu_nearest_khz(ctx, 0, little_mhz * 1000);
	int max4 = cpu_nearest_khz(ctx, 4, big_mhz * 1000);
	int max7 = cpu_nearest_khz(ctx, 7, prime_mhz * 1000);

	if (cpu_apply_targets(ctx, max0, max4, max7) < 0) {
		log_msg("cpu on failed target=%d/%d/%d kHz", max0, max4, max7);
		fprintf(stderr, "cpu on failed\n");
		return 1;
	}

	log_msg("cpu on %d/%d/%d kHz", max0, max4, max7);
	printf("takeover=1 max=%d/%d/%d kHz\n", max0, max4, max7);
	return 0;
}

int cpu_off(struct context *ctx)
{
	if (!kmod_present(ctx)) {
		printf("kmod=off\n");
		return 0;
	}

	if (node_write(ctx, PD_CPU_TAKEOVER_NODE, 0) < 0 ||
	    node_write(ctx, PD_CPU_APPLY_NODE, CPU_APPLY_VALUE) < 0) {
		log_msg("cpu off failed");
		fprintf(stderr, "cpu off failed\n");
		return 1;
	}

	ctx->thermal.cpu_applied = 0;
	log_msg("cpu off");
	printf("takeover=0\n");
	return 0;
}

int cpu_unload(struct context *ctx)
{
	int ret = 0;

	if (!kmod_present(ctx)) {
		printf("kmod=off\n");
		return 0;
	}

	node_write(ctx, PD_CPU_TAKEOVER_NODE, 0);
	node_write(ctx, PD_CPU_APPLY_NODE, CPU_APPLY_VALUE);
	usleep(100 * 1000);

	if (kmod_unload() != 0) {
		log_msg("cpu kmod unload failed");
		fprintf(stderr, "cpu kmod unload failed\n");
		ret = 1;
	} else {
		ctx->thermal.cpu_applied = 0;
		log_msg("cpu kmod unloaded");
		printf("kmod=off\n");
	}

	return ret;
}

int cpu_restore_state(struct context *ctx)
{
	int ret = 0;

	if (!kmod_present(ctx))
		return 0;

	node_write(ctx, PD_CPU_TAKEOVER_NODE, 0);
	node_write(ctx, PD_CPU_APPLY_NODE, CPU_APPLY_VALUE);
	usleep(100 * 1000);

	if (kmod_unload() != 0) {
		log_msg("cpu restore unload failed");
		ret = -1;
	} else {
		ctx->thermal.cpu_applied = 0;
		ctx->thermal.little_khz = 0;
		ctx->thermal.big_khz = 0;
		ctx->thermal.prime_khz = 0;
	}

	log_msg("cpu restore rc=%d", ret);
	return ret;
}

int cpu_status(struct context *ctx)
{
	char zone[64] = "-";
	int present = kmod_present(ctx);
	int takeover = -1;
	int probe = -1;
	int max0 = 0;
	int max4 = 0;
	int max7 = 0;
	int safety = 0;
	int temp = -1000;
	int p0 = -1;
	int p4 = -1;
	int p7 = -1;
	int offlined = -1;
	int little_online = 0;
	int big_online = 0;
	int prime_online = 0;

	if (present) {
		node_read_bool(ctx, PD_CPU_TAKEOVER_NODE, &takeover);
		node_read_bool(ctx, PD_CPU_PROBE_NODE, &probe);
		node_read(ctx, PD_CPU_MAX0_NODE, &max0);
		node_read(ctx, PD_CPU_MAX4_NODE, &max4);
		node_read(ctx, PD_CPU_MAX7_NODE, &max7);
		node_read(ctx, PD_CPU_SAFETY_NODE, &safety);
		if (node_read(ctx, PD_CPU_TEMP_NODE, &temp) < 0)
			temp = -1000;
		if (node_text(ctx, PD_CPU_ZONE_USED_NODE, zone, sizeof(zone)) < 0)
			snprintf(zone, sizeof(zone), "none");
	}

	node_read(ctx, PD_POLICY0_MAX_NODE, &p0);
	node_read(ctx, PD_POLICY4_MAX_NODE, &p4);
	node_read(ctx, PD_POLICY7_MAX_NODE, &p7);
	node_read(ctx, PD_CPUS_OFFLINED_NODE, &offlined);

	if (present) {
		printf("kmod=on takeover=%d probe=%d\n", takeover, probe);
		printf("max=%d/%d/%d kHz safety=%.1fC", max0, max4, max7,
		       safety / 1000.0);
		if (temp > -1000)
			printf(" temp=%.1fC zone=%s\n", temp / 1000.0, zone);
		else
			printf(" temp=n/a zone=%s\n", zone);
	} else {
		printf("kmod=off\n");
	}

	cpu_online_counts(ctx, &little_online, &big_online, &prime_online);
	printf("policy0=%d policy4=%d policy7=%d kHz offlined=%d\n",
	       p0, p4, p7, offlined);
	printf("online=%d/%d/%d\n", little_online, big_online, prime_online);
	return 0;
}
