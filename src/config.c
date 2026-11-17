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

static int parse_rule(const char *s, struct thermal_rule *rule)
{
	char buf[128];
	char *parts[7];
	char *p;
	int values[7];
	int count = 0;
	int i;

	if (strlen(s) >= sizeof(buf))
		return -1;
	strcpy(buf, s);

	p = buf;
	while (count < 7) {
		parts[count++] = p;
		p = strchr(p, ',');
		if (!p)
			break;
		*p++ = '\0';
	}
	if ((count != 5 && count != 7) || p)
		return -1;

	for (i = 0; i < count; i++) {
		parts[i] = trim(parts[i]);
		if (parse_int(parts[i], &values[i]))
			return -1;
	}

	if (values[0] < 300 || values[0] > 750)
		return -1;
	if (values[1] < 300 || values[1] > 2000)
		return -1;
	if (values[2] < 300 || values[2] > 2600)
		return -1;
	if (values[3] < 300 || values[3] > 3300)
		return -1;
	if (values[4] < PD_MIN_INPUT_MA || values[4] > PD_HARD_MAX_INPUT_MA)
		return -1;

	rule->temp_dc = values[0];
	rule->little_mhz = values[1];
	rule->big_mhz = values[2];
	rule->prime_mhz = values[3];
	rule->charge_ma = values[4];
	if (count == 7) {
		if (values[5] < 1 || values[5] > 3)
			return -1;
		if (values[6] < 0 || values[6] > 1)
			return -1;
		rule->big_online = values[5];
		rule->prime_online = values[6];
	} else {
		rule->big_online = 3;
		rule->prime_online = 1;
	}
	return 0;
}

static void config_stamp(struct context *ctx, const struct stat *st)
{
	ctx->config_mtime = st->st_mtime;
	ctx->config_mtime_ns = st->st_mtim.tv_nsec;
	ctx->config_size = st->st_size;
	ctx->config_ino = st->st_ino;
	ctx->config_stamp_valid = 1;
}

static const struct thermal_rule default_rules[] = {
	{ 400, 1804, 2419, 2265, 3000, 3, 1 },
	{ 440, 1612, 2150, 2073, 2500, 2, 1 },
	{ 480, 1401, 1862, 1862, 1800, 2, 0 },
	{ 520, 1171, 1574, 1632, 1000, 1, 0 },
};

void config_defaults(struct config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->enabled = 1;
	cfg->max_input_ma = 5000;
	cfg->min_input_ma = 500;
	cfg->charge_limit_soc = 100;
	cfg->soc_mid = 90;
	cfg->soc_mid_scale = 100;
	cfg->temp_derate_dc = 420;
	cfg->temp_stop_dc = 450;
	cfg->board_temp_derate_dc = 550;
	cfg->board_temp_stop_dc = 600;
	cfg->temp_derate_scale = 60;
	cfg->poll_active_ms = 10000;
	cfg->poll_idle_ms = 60000;
	cfg->boost_enabled = 1;
	cfg->boost_soc_max = 40;
	cfg->boost_ma = 6600;
	cfg->boost_cable_mohm = 100;
	cfg->thermal_online = 0;
	cfg->thermal_interval_ms = 5000;
	cfg->thermal_idle_interval_ms = 15000;
	cfg->thermal_rule_count = (int)(sizeof(default_rules) /
					sizeof(default_rules[0]));
	memcpy(cfg->thermal_rules, default_rules, sizeof(default_rules));
}

int config_write_default(const char *path)
{
	FILE *fp = fopen(path, "w");

	if (!fp)
		return -1;

	fputs("# PD2055 Power Management\n"
	      "# 电流单位 mA，温度单位 0.1 C，间隔单位 ms。\n"
	      "\n"
	      "# 启用控制\n"
	      "enabled=1\n"
	      "# 启用直充提速\n"
	      "boost_enabled=1\n"
	      "# 达到或超过这个 SOC 时停止提速 [%]\n"
	      "boost_soc_max=40\n"
	      "# 提速时的电池电流请求 [mA]\n"
	      "boost_ma=6600\n"
	      "# 提速时 MCU 使用的线阻 [mOhm]\n"
	      "boost_cable_mohm=100\n"
	      "# 输入电流上限 [mA]\n"
	      "max_input_ma=5000\n"
	      "# 电池或主板达到停止温度时使用的限流 [mA]\n"
	      "min_input_ma=500\n"
	      "# 达到这个 SOC 时停止充电；100 表示不限制 [%]\n"
	      "charge_limit_soc=100\n"
	      "# 高于这个 SOC 时缩放限流值 [%]\n"
	      "soc_mid=90\n"
	      "# 高于 soc_mid 时的缩放比例 [%]\n"
	      "soc_mid_scale=100\n"
	      "# 电池降额温度 [0.1 C]\n"
	      "temp_derate_dc=420\n"
	      "# 电池停止温度 [0.1 C]\n"
	      "temp_stop_dc=450\n"
	      "# 主板降额温度 [0.1 C]\n"
	      "board_temp_derate_dc=550\n"
	      "# 主板停止温度 [0.1 C]\n"
	      "board_temp_stop_dc=600\n"
	      "# 到降额温度后输入和提速目标乘这个比例 [%]\n"
	      "temp_derate_scale=60\n"
	      "# 控制中的轮询间隔 [ms]\n"
	      "poll_active_ms=10000\n"
	      "# 空闲时的轮询间隔 [ms]\n"
	      "poll_idle_ms=60000\n"
	      "\n"
	      "# 接管 CPU 频率\n"
	      "thermal_cpu=0\n"
	      "# 按温控规则限制充电电流\n"
	      "thermal_charge=0\n"
	      "# 按温控规则管理 CPU5、CPU6、CPU7 的在线状态\n"
	      "thermal_online=0\n"
	      "# 温控检测间隔 [ms]\n"
	      "thermal_interval_ms=5000\n"
	      "# 电池未充电且温度比最低规则低超过 2.0 C 时的温控检测间隔 [ms]\n"
	      "thermal_idle_interval_ms=15000\n"
	      "# 温控规则：温度 [0.1 C]，小核 [MHz]，大核 [MHz]，"
	      "超大核 [MHz]，充电电流 [mA]，大核在线数，超大核在线数\n"
	      "thermal_rule=400,1804,2419,2265,3000,3,1\n"
	      "thermal_rule=440,1612,2150,2073,2500,2,1\n"
	      "thermal_rule=480,1401,1862,1862,1800,2,0\n"
	      "thermal_rule=520,1171,1574,1632,1000,1,0\n", fp);
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
	int rules_from_file = 0;

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
		} else if (!strcmp(key, "boost_enabled")) {
			if (parse_bool(val, &ctx->cfg.boost_enabled))
				log_msg("config boost_enabled invalid");
		} else if (!strcmp(key, "thermal_cpu")) {
			if (parse_bool(val, &ctx->cfg.thermal_cpu))
				log_msg("config thermal_cpu invalid");
		} else if (!strcmp(key, "thermal_charge")) {
			if (parse_bool(val, &ctx->cfg.thermal_charge))
				log_msg("config thermal_charge invalid");
		} else if (!strcmp(key, "thermal_online")) {
			if (parse_bool(val, &ctx->cfg.thermal_online))
				log_msg("config thermal_online invalid");
		} else if (!strcmp(key, "thermal_rule")) {
			struct thermal_rule rule;
			int count;

			if (!rules_from_file) {
				rules_from_file = 1;
				ctx->cfg.thermal_rule_count = 0;
			}
			count = ctx->cfg.thermal_rule_count;

			if (count >= PD_THERMAL_RULE_MAX) {
				log_msg("config line %d thermal_rule full", line_no);
			} else if (parse_rule(val, &rule)) {
				log_msg("config line %d thermal_rule invalid", line_no);
			} else if (count > 0 &&
				   rule.temp_dc <= ctx->cfg.thermal_rules[count - 1].temp_dc) {
				log_msg("config line %d thermal_rule temp order", line_no);
			} else {
				ctx->cfg.thermal_rules[count] = rule;
				ctx->cfg.thermal_rule_count = count + 1;
			}
		} else if (!parse_int(val, &value)) {
			if (!strcmp(key, "max_input_ma"))
				ctx->cfg.max_input_ma = value;
			else if (!strcmp(key, "min_input_ma"))
				ctx->cfg.min_input_ma = value;
			else if (!strcmp(key, "charge_limit_soc") ||
				 !strcmp(key, "soc_full"))
				ctx->cfg.charge_limit_soc = value;
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
			else if (!strcmp(key, "temp_derate_scale"))
				ctx->cfg.temp_derate_scale = value;
			else if (!strcmp(key, "poll_active_ms"))
				ctx->cfg.poll_active_ms = value;
			else if (!strcmp(key, "poll_idle_ms"))
				ctx->cfg.poll_idle_ms = value;
			else if (!strcmp(key, "boost_soc_max"))
				ctx->cfg.boost_soc_max = value;
			else if (!strcmp(key, "boost_ma"))
				ctx->cfg.boost_ma = value;
			else if (!strcmp(key, "boost_cable_mohm"))
				ctx->cfg.boost_cable_mohm = value;
			else if (!strcmp(key, "thermal_interval_ms"))
				ctx->cfg.thermal_interval_ms = value;
			else if (!strcmp(key, "thermal_idle_interval_ms"))
				ctx->cfg.thermal_idle_interval_ms = value;
		}
	}
	fclose(fp);

	ctx->cfg.max_input_ma = clamp_int(ctx->cfg.max_input_ma,
					  PD_MIN_INPUT_MA, PD_HARD_MAX_INPUT_MA);
	ctx->cfg.min_input_ma = clamp_int(ctx->cfg.min_input_ma,
					  PD_MIN_INPUT_MA, ctx->cfg.max_input_ma);
	ctx->cfg.charge_limit_soc = clamp_int(ctx->cfg.charge_limit_soc, 50, 100);
	ctx->cfg.soc_mid = clamp_int(ctx->cfg.soc_mid, 0,
				     ctx->cfg.charge_limit_soc);
	ctx->cfg.soc_mid_scale = clamp_int(ctx->cfg.soc_mid_scale, 10, 100);
	ctx->cfg.temp_derate_dc = clamp_int(ctx->cfg.temp_derate_dc, 200, 600);
	ctx->cfg.temp_stop_dc = clamp_int(ctx->cfg.temp_stop_dc,
					  ctx->cfg.temp_derate_dc + 10, 700);
	ctx->cfg.board_temp_derate_dc = clamp_int(ctx->cfg.board_temp_derate_dc, 300, 800);
	ctx->cfg.board_temp_stop_dc = clamp_int(ctx->cfg.board_temp_stop_dc,
						ctx->cfg.board_temp_derate_dc + 10, 900);
	ctx->cfg.temp_derate_scale = clamp_int(ctx->cfg.temp_derate_scale, 10, 100);
	ctx->cfg.poll_active_ms = clamp_int(ctx->cfg.poll_active_ms, 1000, 60000);
	ctx->cfg.poll_idle_ms = clamp_int(ctx->cfg.poll_idle_ms, 1000, 60000);
	ctx->cfg.boost_soc_max = clamp_int(ctx->cfg.boost_soc_max, 0,
					   ctx->cfg.charge_limit_soc);
	ctx->cfg.boost_ma = clamp_int(ctx->cfg.boost_ma,
				      PD_BOOST_MIN_MA, PD_HARD_MAX_FCC_MA);
	ctx->cfg.boost_cable_mohm = clamp_int(ctx->cfg.boost_cable_mohm, 0, 1000);
	ctx->cfg.thermal_interval_ms = clamp_int(ctx->cfg.thermal_interval_ms,
						 1000, 60000);
	ctx->cfg.thermal_idle_interval_ms =
		clamp_int(ctx->cfg.thermal_idle_interval_ms,
			  ctx->cfg.thermal_interval_ms, 60000);

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

void config_print(const struct config *cfg)
{
	int i;

	printf("enabled=%d\n", cfg->enabled);
	printf("boost_enabled=%d\n", cfg->boost_enabled);
	printf("boost_soc_max=%d\n", cfg->boost_soc_max);
	printf("boost_ma=%d\n", cfg->boost_ma);
	printf("boost_cable_mohm=%d\n", cfg->boost_cable_mohm);
	printf("max_input_ma=%d\n", cfg->max_input_ma);
	printf("min_input_ma=%d\n", cfg->min_input_ma);
	printf("charge_limit_soc=%d\n", cfg->charge_limit_soc);
	printf("soc_mid=%d\n", cfg->soc_mid);
	printf("soc_mid_scale=%d\n", cfg->soc_mid_scale);
	printf("temp_derate_dc=%d\n", cfg->temp_derate_dc);
	printf("temp_stop_dc=%d\n", cfg->temp_stop_dc);
	printf("board_temp_derate_dc=%d\n", cfg->board_temp_derate_dc);
	printf("board_temp_stop_dc=%d\n", cfg->board_temp_stop_dc);
	printf("temp_derate_scale=%d\n", cfg->temp_derate_scale);
	printf("poll_active_ms=%d\n", cfg->poll_active_ms);
	printf("poll_idle_ms=%d\n", cfg->poll_idle_ms);
	printf("thermal_cpu=%d\n", cfg->thermal_cpu);
	printf("thermal_charge=%d\n", cfg->thermal_charge);
	printf("thermal_online=%d\n", cfg->thermal_online);
	printf("thermal_interval_ms=%d\n", cfg->thermal_interval_ms);
	printf("thermal_idle_interval_ms=%d\n", cfg->thermal_idle_interval_ms);
	for (i = 0; i < cfg->thermal_rule_count; i++) {
		const struct thermal_rule *rule = &cfg->thermal_rules[i];

		printf("thermal_rule=%d,%d,%d,%d,%d,%d,%d\n",
		       rule->temp_dc, rule->little_mhz, rule->big_mhz,
		       rule->prime_mhz, rule->charge_ma, rule->big_online,
		       rule->prime_online);
	}
}
