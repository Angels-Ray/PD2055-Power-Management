#include "pdcharge.h"

#include <stdio.h>
#include <string.h>
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
