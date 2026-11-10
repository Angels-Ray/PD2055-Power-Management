#include "pdcharge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *trim(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t')
		s++;
	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
			   end[-1] == '\r' || end[-1] == '\n'))
		*--end = '\0';
	return s;
}

static int parse_int(const char *s, int *out)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(s, &end, 10);
	if (errno || end == s || *end != '\0')
		return -1;
	if (value < -2147483647L || value > 2147483647L)
		return -1;
	*out = (int)value;
	return 0;
}

static int parse_bool(const char *s, int *out)
{
	if (!strcmp(s, "1") || !strcmp(s, "true") || !strcmp(s, "on") ||
	    !strcmp(s, "yes")) {
		*out = 1;
		return 0;
	}
	if (!strcmp(s, "0") || !strcmp(s, "false") || !strcmp(s, "off") ||
	    !strcmp(s, "no")) {
		*out = 0;
		return 0;
	}
	return -1;
}

static int clamp_int(int value, int low, int high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static void config_stamp(struct context *ctx, const struct stat *st)
{
	ctx->config_mtime = st->st_mtime;
	ctx->config_mtime_ns = st->st_mtim.tv_nsec;
	ctx->config_size = st->st_size;
	ctx->config_ino = st->st_ino;
	ctx->config_stamp_valid = 1;
}

void config_defaults(struct config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->enabled = 1;
	cfg->max_input_ma = 3300;
	cfg->min_input_ma = 500;
	cfg->soc_full = 85;
	cfg->soc_mid = 75;
	cfg->soc_mid_scale = 70;
	cfg->temp_derate_dc = 360;
	cfg->temp_stop_dc = 420;
	cfg->board_temp_derate_dc = 520;
	cfg->board_temp_stop_dc = 600;
	cfg->poll_active_ms = 10000;
	cfg->poll_idle_ms = 60000;
}

int config_write_default(const char *path)
{
	FILE *fp = fopen(path, "w");

	if (!fp)
		return -1;

	fputs("# PD2055 Charge\n"
	      "# 电流单位 mA，温度单位 0.1 C，间隔单位 ms。\n"
	      "\n"
	      "# 启用限流\n"
	      "enabled=1\n"
	      "# 输入电流上限 [mA]\n"
	      "max_input_ma=3300\n"
	      "# 温度到达停止阈值时使用的限流值 [mA]\n"
	      "min_input_ma=500\n"
	      "# SOC 达到或超过这个值时释放 [%]\n"
	      "soc_full=85\n"
	      "# 高于这个 SOC 时缩放限流值 [%]\n"
	      "soc_mid=75\n"
	      "# 高于 soc_mid 时的缩放比例 [%]\n"
	      "soc_mid_scale=70\n"
	      "# 目标值第一次乘 60% 的电池温度 [0.1 C]\n"
	      "temp_derate_dc=360\n"
	      "# 使用 min_input_ma 的电池温度 [0.1 C]\n"
	      "temp_stop_dc=420\n"
	      "# 目标值第一次乘 60% 的主板温度 [0.1 C]\n"
	      "board_temp_derate_dc=520\n"
	      "# 使用 min_input_ma 的主板温度 [0.1 C]\n"
	      "board_temp_stop_dc=600\n"
	      "# 控制中的轮询间隔 [ms]\n"
	      "poll_active_ms=10000\n"
	      "# 空闲时的轮询间隔 [ms]\n"
	      "poll_idle_ms=60000\n", fp);
	fclose(fp);
	return 0;
}

int config_load(struct context *ctx)
{
	struct config previous = ctx->cfg;
	char line[512];
	struct stat before;
	struct stat after;
	FILE *fp;
	int value;
	int line_no = 0;
	int have_before;

	config_defaults(&ctx->cfg);
	ctx->config_stamp_valid = 0;
	have_before = stat(ctx->config_path, &before) == 0;

	fp = fopen(ctx->config_path, "r");
	if (!fp) {
		if (errno == ENOENT) {
			if (config_write_default(ctx->config_path) == 0) {
				log_msg("config created %s", ctx->config_path);
				if (stat(ctx->config_path, &after) == 0)
					config_stamp(ctx, &after);
			} else {
				log_msg("config create failed %s", ctx->config_path);
				return -1;
			}
			return 0;
		}
		log_msg("config open failed %s", ctx->config_path);
		return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		char *key;
		char *val;
		char *eq;
		char *comment;

		line_no++;
		comment = strchr(line, '#');
		if (comment)
			*comment = '\0';
		key = trim(line);
		if (!*key)
			continue;
		eq = strchr(key, '=');
		if (!eq) {
			log_msg("config line %d ignored", line_no);
			continue;
		}
		*eq = '\0';
		val = trim(eq + 1);
		key = trim(key);

		if (!strcmp(key, "enabled")) {
			if (parse_bool(val, &ctx->cfg.enabled))
				log_msg("config enabled invalid");
		} else if (!parse_int(val, &value)) {
			if (!strcmp(key, "max_input_ma"))
				ctx->cfg.max_input_ma = value;
			else if (!strcmp(key, "min_input_ma"))
				ctx->cfg.min_input_ma = value;
			else if (!strcmp(key, "soc_full"))
				ctx->cfg.soc_full = value;
			else if (!strcmp(key, "soc_mid"))
				ctx->cfg.soc_mid = value;
			else if (!strcmp(key, "soc_mid_scale"))
				ctx->cfg.soc_mid_scale = value;
			else if (!strcmp(key, "temp_derate_dc"))
				ctx->cfg.temp_derate_dc = value;
			else if (!strcmp(key, "temp_stop_dc"))
				ctx->cfg.temp_stop_dc = value;
			else if (!strcmp(key, "board_temp_derate_dc"))
				ctx->cfg.board_temp_derate_dc = value;
			else if (!strcmp(key, "board_temp_stop_dc"))
				ctx->cfg.board_temp_stop_dc = value;
			else if (!strcmp(key, "poll_active_ms"))
				ctx->cfg.poll_active_ms = value;
			else if (!strcmp(key, "poll_idle_ms"))
				ctx->cfg.poll_idle_ms = value;
		}
	}
	fclose(fp);

	ctx->cfg.max_input_ma = clamp_int(ctx->cfg.max_input_ma,
					  PD_MIN_INPUT_MA, PD_HARD_MAX_INPUT_MA);
	ctx->cfg.min_input_ma = clamp_int(ctx->cfg.min_input_ma,
					  PD_MIN_INPUT_MA, ctx->cfg.max_input_ma);
	ctx->cfg.soc_full = clamp_int(ctx->cfg.soc_full, 50, 100);
	ctx->cfg.soc_mid = clamp_int(ctx->cfg.soc_mid, 0, ctx->cfg.soc_full);
	ctx->cfg.soc_mid_scale = clamp_int(ctx->cfg.soc_mid_scale, 10, 100);
	ctx->cfg.temp_derate_dc = clamp_int(ctx->cfg.temp_derate_dc, 200, 600);
	ctx->cfg.temp_stop_dc = clamp_int(ctx->cfg.temp_stop_dc,
					  ctx->cfg.temp_derate_dc + 10, 700);
	ctx->cfg.board_temp_derate_dc = clamp_int(ctx->cfg.board_temp_derate_dc, 300, 800);
	ctx->cfg.board_temp_stop_dc = clamp_int(ctx->cfg.board_temp_stop_dc,
						ctx->cfg.board_temp_derate_dc + 10, 900);
	ctx->cfg.poll_active_ms = clamp_int(ctx->cfg.poll_active_ms, 1000, 60000);
	ctx->cfg.poll_idle_ms = clamp_int(ctx->cfg.poll_idle_ms, 1000, 60000);

	if (stat(ctx->config_path, &after) == 0 && have_before &&
	    before.st_mtime == after.st_mtime &&
	    before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
	    before.st_size == after.st_size &&
	    before.st_ino == after.st_ino) {
		config_stamp(ctx, &after);
		return 0;
	}

	if (previous.poll_idle_ms > 0)
		ctx->cfg = previous;
	return 1;
}

int config_changed(const struct context *ctx)
{
	struct stat st;

	if (stat(ctx->config_path, &st) < 0)
		return 0;
	if (!ctx->config_stamp_valid)
		return 1;
	return st.st_mtime != ctx->config_mtime ||
	       st.st_mtim.tv_nsec != ctx->config_mtime_ns ||
	       st.st_size != ctx->config_size ||
	       st.st_ino != ctx->config_ino;
}
