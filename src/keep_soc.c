#include "pdcharge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int parse_int_text(const char *s, int *out)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(s, &end, 10);
	while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
		end++;
	if (errno || end == s || *end)
		return -1;
	if (value < -2147483647L || value > 2147483647L)
		return -1;
	*out = (int)value;
	return 0;
}

static int keep_soc_node(const struct context *ctx, char *path, size_t size)
{
	path_join(path, size, ctx->root, PD_KEEP_SOC_NODE);
	return file_exists(path) ? 0 : -1;
}

static int keep_soc_voter(const struct context *ctx, int *value)
{
	char path[PD_PATH_MAX];
	char buf[256];
	char *token;
	char *save;
	int found = 1;

	path_join(path, sizeof(path), ctx->root, PD_KEEP_SOC_NODE);
	if (read_text_file(path, buf, sizeof(buf)) < 0)
		return -1;

	for (token = strtok_r(buf, ",", &save); token;
	     token = strtok_r(NULL, ",", &save)) {
		char *colon = strchr(token, ':');
		int id;
		int v;

		if (!colon)
			continue;
		*colon = '\0';
		if (parse_int_text(token, &id) || id != PD_KEEP_SOC_VOTER)
			continue;
		if (parse_int_text(colon + 1, &v))
			continue;
		*value = v;
		found = 0;
		break;
	}

	return found;
}

static int keep_soc_write_voter(const struct context *ctx, int value)
{
	char path[PD_PATH_MAX];
	char text[32];
	FILE *fp;

	path_join(path, sizeof(path), ctx->root, PD_KEEP_SOC_NODE);
	fp = fopen(path, "w");
	if (!fp)
		return -1;

	snprintf(text, sizeof(text), "%d %d", PD_KEEP_SOC_VOTER, value);
	if (fputs(text, fp) == EOF) {
		fclose(fp);
		return -1;
	}
	return fclose(fp) == 0 ? 0 : -1;
}

static int keep_soc_save_orig(struct context *ctx)
{
	char buf[32];
	int value = -1;
	int found;
	FILE *fp;

	if (ctx->keep_soc_orig_valid)
		return 0;

	if (read_text_file(ctx->keep_soc_orig_path, buf, sizeof(buf)) == 0 &&
	    parse_int_text(buf, &value) == 0) {
		ctx->keep_soc_orig_soc = value;
		ctx->keep_soc_orig_valid = 1;
		return 0;
	}

	found = keep_soc_voter(ctx, &value);
	if (found < 0)
		return -1;
	if (found > 0)
		value = -1;

	fp = fopen(ctx->keep_soc_orig_path, "w");
	if (!fp)
		return -1;
	fprintf(fp, "%d\n", value);
	if (fclose(fp) != 0)
		return -1;

	ctx->keep_soc_orig_soc = value;
	ctx->keep_soc_orig_valid = 1;
	return 0;
}

int keep_soc_restore_state(struct context *ctx)
{
	char buf[32];
	int orig;
	int ret;

	ctx->keep_soc_applied = -1;
	ctx->keep_soc_orig_valid = 0;
	if (!file_exists(ctx->keep_soc_orig_path))
		return 0;

	if (read_text_file(ctx->keep_soc_orig_path, buf, sizeof(buf)) < 0 ||
	    parse_int_text(buf, &orig)) {
		log_msg("keep_soc state invalid");
		unlink(ctx->keep_soc_orig_path);
		return -1;
	}

	ret = keep_soc_write_voter(ctx, orig >= 0 ? orig : 0);
	if (ret < 0) {
		log_msg("keep_soc recover failed orig=%d", orig);
		return -1;
	}

	log_msg("keep_soc recover orig=%d", orig);
	unlink(ctx->keep_soc_orig_path);
	return 0;
}

int keep_soc_apply(struct context *ctx, int limit)
{
	char path[PD_PATH_MAX];
	int current = -1;
	int found;

	if (limit >= 100)
		return keep_soc_release(ctx, "limit-off");
	if (keep_soc_node(ctx, path, sizeof(path)) < 0) {
		if (!ctx->keep_soc_error) {
			log_msg("keep_soc missing %s", path);
			ctx->keep_soc_error = 1;
		}
		return -1;
	}
	if (keep_soc_save_orig(ctx) < 0) {
		if (!ctx->keep_soc_error) {
			log_msg("keep_soc orig read failed");
			ctx->keep_soc_error = 1;
		}
		return -1;
	}

	found = keep_soc_voter(ctx, &current);
	if (found == 0 && current == limit) {
		ctx->keep_soc_applied = limit;
		ctx->keep_soc_error = 0;
		return 0;
	}

	if (keep_soc_write_voter(ctx, limit) < 0) {
		if (!ctx->keep_soc_error) {
			log_msg("keep_soc write failed limit=%d", limit);
			ctx->keep_soc_error = 1;
		}
		return -1;
	}

	ctx->keep_soc_applied = limit;
	ctx->keep_soc_error = 0;
	log_msg("keep_soc apply limit=%d orig=%d", limit,
		ctx->keep_soc_orig_soc);
	return 0;
}

int keep_soc_release(struct context *ctx, const char *reason)
{
	char buf[32];
	int orig = -1;
	int current = -1;
	int found;
	int ret;

	if (ctx->keep_soc_applied < 0 &&
	    !file_exists(ctx->keep_soc_orig_path)) {
		ctx->keep_soc_orig_valid = 0;
		return 0;
	}

	if (read_text_file(ctx->keep_soc_orig_path, buf, sizeof(buf)) == 0 &&
	    parse_int_text(buf, &orig) == 0) {
		/* use the saved value */
	} else if (ctx->keep_soc_orig_valid) {
		orig = ctx->keep_soc_orig_soc;
	}

	if (ctx->keep_soc_applied >= 0) {
		found = keep_soc_voter(ctx, &current);
		if (found == 0 && current != ctx->keep_soc_applied) {
			log_msg("keep_soc release skipped applied=%d read=%d",
				ctx->keep_soc_applied, current);
			goto clear;
		}
	}

	ret = keep_soc_write_voter(ctx, orig >= 0 ? orig : 0);
	if (ret < 0) {
		log_msg("keep_soc release failed orig=%d reason=%s", orig, reason);
		ctx->keep_soc_applied = -1;
		return -1;
	}

	log_msg("keep_soc release limit=%d orig=%d reason=%s",
		ctx->keep_soc_applied, orig, reason);

clear:
	unlink(ctx->keep_soc_orig_path);
	ctx->keep_soc_orig_valid = 0;
	ctx->keep_soc_orig_soc = -1;
	ctx->keep_soc_applied = -1;
	ctx->keep_soc_error = 0;
	return 0;
}

int keep_soc_sync(struct context *ctx)
{
	if (ctx->cfg.enabled && ctx->cfg.charge_limit_soc < 100)
		return keep_soc_apply(ctx, ctx->cfg.charge_limit_soc);
	return keep_soc_release(ctx, "off");
}

int keep_soc_effective(struct context *ctx)
{
	char path[PD_PATH_MAX];
	char buf[256];
	int value;

	path_join(path, sizeof(path), ctx->root, PD_KEEP_SOC_EFFECTIVE_NODE);
	if (read_int_file(path, &value) == 0 && value > 0 && value <= 100)
		return value;

	path_join(path, sizeof(path), ctx->root, PD_KEEP_SOC_NODE);
	if (read_text_file(path, buf, sizeof(buf)) < 0)
		return -1;
	if (sscanf(buf, "vote:%d", &value) == 1 && value > 0 && value <= 100)
		return value;
	return -1;
}
