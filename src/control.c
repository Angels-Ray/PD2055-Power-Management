#include "pdcharge.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void mark_active(struct context *ctx, int active)
{
	FILE *fp;

	if (!active) {
		unlink(ctx->active_path);
		return;
	}

	fp = fopen(ctx->active_path, "w");
	if (fp)
		fclose(fp);
}

static int save_int(const char *path, int value)
{
	FILE *fp = fopen(path, "w");

	if (!fp)
		return -1;
	fprintf(fp, "%d\n", value);
	if (fclose(fp) != 0)
		return -1;
	return 0;
}

int control_apply(struct context *ctx, int target_ma)
{
	char path[PD_PATH_MAX];
	int current_ua = -1;
	int readback_ua;
	int target_ua;

	target_ma = (target_ma / 50) * 50;
	if (target_ma < PD_MIN_INPUT_MA)
		target_ma = PD_MIN_INPUT_MA;
	if (target_ma > PD_HARD_MAX_INPUT_MA)
		target_ma = PD_HARD_MAX_INPUT_MA;
	target_ua = target_ma * 1000;

	if (ctx->controlling && ctx->applied_ua == target_ua)
		return 0;

	path_join(path, sizeof(path), ctx->root, PD_CTM_NODE);

	if (!ctx->controlling) {
		if (read_int_file(path, &current_ua) < 0)
			current_ua = -1;
		if (save_int(ctx->orig_path, current_ua) < 0)
			log_msg("orig save failed value=%d", current_ua);
	}

	if (write_int_file(path, target_ua) < 0) {
		log_msg("ctm write failed target=%d", target_ma);
		return -1;
	}

	if (read_int_file(path, &readback_ua) < 0 || readback_ua < 0) {
		readback_ua = target_ua;
	} else if (readback_ua != target_ua) {
		log_msg("ctm readback target=%d read=%d", target_ua, readback_ua);
	}

	if (save_int(ctx->applied_path, readback_ua) < 0)
		log_msg("applied save failed value=%d", readback_ua);

	if (!ctx->controlling)
		log_msg("apply target=%d orig=%d", target_ma, current_ua);

	ctx->controlling = 1;
	ctx->applied_ua = readback_ua;
	mark_active(ctx, 1);
	return 0;
}

int control_release(struct context *ctx)
{
	char path[PD_PATH_MAX];
	int current_ua;
	int applied_ua;
	int orig_ua;

	if (!ctx->controlling)
		return 0;

	path_join(path, sizeof(path), ctx->root, PD_CTM_NODE);

	if (read_int_file(ctx->applied_path, &applied_ua) < 0)
		applied_ua = ctx->applied_ua;
	if (read_int_file(ctx->orig_path, &orig_ua) < 0)
		orig_ua = -1;

	if (read_int_file(path, &current_ua) == 0 && current_ua == applied_ua) {
		if (write_int_file(path, orig_ua) < 0)
			log_msg("ctm release failed applied=%d orig=%d",
				applied_ua, orig_ua);
		else
			log_msg("release applied=%d orig=%d", applied_ua, orig_ua);
	} else {
		log_msg("release skipped applied=%d", applied_ua);
	}

	unlink(ctx->orig_path);
	unlink(ctx->applied_path);
	mark_active(ctx, 0);
	ctx->controlling = 0;
	ctx->applied_ua = 0;
	return 0;
}

int control_restore_state(struct context *ctx)
{
	char path[PD_PATH_MAX];
	int current_ua;
	int applied_ua;
	int orig_ua;

	if (!file_exists(ctx->active_path))
		return 0;

	path_join(path, sizeof(path), ctx->root, PD_CTM_NODE);

	if (read_int_file(ctx->applied_path, &applied_ua) < 0)
		applied_ua = 0;
	if (read_int_file(ctx->orig_path, &orig_ua) < 0)
		orig_ua = -1;

	if (read_int_file(path, &current_ua) == 0 && current_ua == applied_ua) {
		if (write_int_file(path, orig_ua) < 0)
			log_msg("recover release failed applied=%d orig=%d",
				applied_ua, orig_ua);
		else
			log_msg("recover release applied=%d orig=%d",
				applied_ua, orig_ua);
	} else {
		log_msg("recover skip applied=%d", applied_ua);
	}

	unlink(ctx->orig_path);
	unlink(ctx->applied_path);
	mark_active(ctx, 0);
	ctx->controlling = 0;
	ctx->applied_ua = 0;
	return 0;
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

int program_run(const char *path, char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execv(path, argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

static void sleep_seconds(int seconds)
{
	struct timespec req;

	req.tv_sec = seconds;
	req.tv_nsec = 0;
	while (nanosleep(&req, &req) < 0 && errno == EINTR)
		;
}

static int kmod_present(const struct context *ctx)
{
	char path[PD_PATH_MAX];

	path_join(path, sizeof(path), ctx->root, "/sys/module/" PD_KMOD_NAME);
	return file_exists(path);
}

static int kmod_load(struct context *ctx)
{
	char ko[PD_PATH_MAX];
	char setma[PD_PATH_MAX];
	char restore[32];
	char interval[32];
	char timeout[32];
	char *argv[8];
	int i;

	path_join(ko, sizeof(ko), ctx->root, PD_KMOD_PATH);
	if (!file_exists(ko)) {
		log_msg("boost kmod missing %s", ko);
		return -1;
	}

	snprintf(restore, sizeof(restore), "restore_ma=%d", PD_KMOD_RESTORE_MA);
	snprintf(interval, sizeof(interval), "interval_ms=%d", PD_KMOD_INTERVAL_MS);
	snprintf(timeout, sizeof(timeout), "timeout_ms=%d", PD_KMOD_TIMEOUT_MS);

	argv[0] = (char *)PD_INSMOD_BIN;
	argv[1] = ko;
	argv[2] = "psy_name=mcu";
	argv[3] = "prop=0x141";
	argv[4] = restore;
	argv[5] = interval;
	argv[6] = timeout;
	argv[7] = NULL;

	i = program_run(PD_INSMOD_BIN, argv);
	if (i != 0) {
		log_msg("boost kmod load failed rc=%d", i);
		return -1;
	}

	path_join(setma, sizeof(setma), ctx->root, PD_SET_MA_NODE);
	for (i = 0; i < 20; i++) {
		if (file_exists(setma))
			return 0;
		usleep(100 * 1000);
	}

	log_msg("boost kmod parameter missing %s", setma);
	return -1;
}

static int kmod_unload(void)
{
	char *argv[3];

	argv[0] = (char *)PD_RMMOD_BIN;
	argv[1] = (char *)PD_KMOD_NAME;
	argv[2] = NULL;

	return program_run(PD_RMMOD_BIN, argv);
}

int boost_enter(struct context *ctx, int target_ma, int cable_mohm)
{
	if (!kmod_present(ctx) && kmod_load(ctx) < 0)
		return -1;

	ctx->kmod_loaded = 1;

	node_write(ctx, PD_HEARTBEAT_NODE, 1);

	if (node_write(ctx, PD_SET_MA_NODE, target_ma) < 0) {
		log_msg("boost set_ma failed target=%d", target_ma);
		return -1;
	}

	ctx->boost_active = 1;
	ctx->boost_ma_applied = target_ma;
	ctx->boost_cable_set = 0;

	if (cable_mohm > 0) {
		if (node_write(ctx, PD_CABLE_MOHM_NODE, cable_mohm) == 0)
			ctx->boost_cable_set = 1;
		else
			log_msg("boost cable write failed value=%d", cable_mohm);
	}

	log_msg("boost apply ma=%d cable=%d", target_ma, cable_mohm);
	return 0;
}

int boost_refresh(struct context *ctx, int target_ma, int cable_mohm)
{
	int current;

	if (!ctx->boost_active)
		return boost_enter(ctx, target_ma, cable_mohm);

	node_write(ctx, PD_HEARTBEAT_NODE, 1);

	if (target_ma != ctx->boost_ma_applied) {
		if (node_write(ctx, PD_SET_MA_NODE, target_ma) == 0) {
			log_msg("boost set_ma %d -> %d",
				ctx->boost_ma_applied, target_ma);
			ctx->boost_ma_applied = target_ma;
		} else {
			log_msg("boost set_ma failed target=%d", target_ma);
		}
	}

	if (cable_mohm > 0) {
		if (node_read(ctx, PD_CABLE_MOHM_NODE, &current) < 0 ||
		    current != cable_mohm) {
			if (node_write(ctx, PD_CABLE_MOHM_NODE, cable_mohm) == 0)
				ctx->boost_cable_set = 1;
			else
				log_msg("boost cable write failed value=%d",
					cable_mohm);
		}
	}

	return 0;
}

int boost_release(struct context *ctx, const char *reason)
{
	int cable_was_set;
	int ret = 0;

	if (!ctx->boost_active && !ctx->kmod_loaded && !kmod_present(ctx))
		return 0;

	cable_was_set = ctx->boost_cable_set;

	if (ctx->boost_cable_set) {
		if (node_write(ctx, PD_CABLE_MOHM_NODE, 0) == 0)
			log_msg("boost cable restore");
		else {
			log_msg("boost cable restore failed");
			ret = -1;
		}
		ctx->boost_cable_set = 0;
	}

	if (ctx->boost_active) {
		log_msg("boost release reason=%s cable=%d", reason, cable_was_set);
		if (cable_was_set)
			sleep_seconds(PD_BOOST_RELEASE_WAIT_S);
		ctx->boost_active = 0;
	}

	if (ctx->kmod_loaded || kmod_present(ctx)) {
		if (node_write(ctx, PD_SET_MA_NODE, 0) < 0) {
			log_msg("boost set_ma release failed");
			ret = -1;
		}
		sleep_seconds(1);
		if (kmod_unload() == 0)
			ctx->kmod_loaded = 0;
		else {
			log_msg("boost kmod unload failed");
			ret = -1;
		}
	}

	ctx->boost_ma_applied = 0;
	log_msg("boost released reason=%s rc=%d", reason, ret);
	return ret;
}

int boost_restore_state(struct context *ctx)
{
	int ret = 0;

	node_write(ctx, PD_CABLE_MOHM_NODE, 0);

	if (kmod_present(ctx)) {
		node_write(ctx, PD_SET_MA_NODE, 0);
		sleep_seconds(1);
		if (kmod_unload() != 0) {
			log_msg("boost restore unload failed");
			ret = -1;
		}
	}

	ctx->boost_active = 0;
	ctx->boost_ma_applied = 0;
	ctx->boost_cable_set = 0;
	ctx->kmod_loaded = 0;
	log_msg("boost restore rc=%d", ret);
	return ret;
}
