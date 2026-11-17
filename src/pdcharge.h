#ifndef PDCHARGE_H
#define PDCHARGE_H

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#ifndef PD_VERSION
#define PD_VERSION "0.1.0"
#endif

#define PD_DATA_DIR "/data/adb/pd2055-charge"
#define PD_MODULE_DIR "/data/adb/modules/pd2055_charge"
#define PD_DISABLE_FILE PD_MODULE_DIR "/disable"
#define PD_REMOVE_FILE PD_MODULE_DIR "/remove"
#define PD_HARD_MAX_INPUT_MA 5000
#define PD_MIN_INPUT_MA 100
#define PD_VBUS_FAST_UV 8000000
#define PD_CTM_NODE "/sys/class/power_supply/usb/ctm_current_max"
#define PD_DCHG_NODE "/sys/class/stm32_debug/dchg_state"
#define PD_CABLE_MOHM_NODE "/sys/class/stm32_debug/cable_mohm"
#define PD_SET_MA_NODE "/sys/module/psset/parameters/set_ma"
#define PD_HEARTBEAT_NODE "/sys/module/psset/parameters/heartbeat"
#define PD_KMOD_PATH PD_MODULE_DIR "/kmod/psset.ko"
#define PD_KMOD_NAME "psset"
#define PD_CPU_KMOD_PATH PD_MODULE_DIR "/kmod/pd2055_cpufreq.ko"
#define PD_CPU_KMOD_NAME "pd2055_cpufreq"
#define PD_CPU_TAKEOVER_NODE "/sys/module/pd2055_cpufreq/parameters/takeover"
#define PD_CPU_PROBE_NODE "/sys/module/pd2055_cpufreq/parameters/probe"
#define PD_CPU_MAX0_NODE "/sys/module/pd2055_cpufreq/parameters/max0"
#define PD_CPU_MAX4_NODE "/sys/module/pd2055_cpufreq/parameters/max4"
#define PD_CPU_MAX7_NODE "/sys/module/pd2055_cpufreq/parameters/max7"
#define PD_CPU_APPLY_NODE "/sys/module/pd2055_cpufreq/parameters/apply"
#define PD_CPU_SAFETY_NODE "/sys/module/pd2055_cpufreq/parameters/safety_temp_mc"
#define PD_CPU_TEMP_NODE "/sys/module/pd2055_cpufreq/parameters/temp_mc"
#define PD_CPU_ZONE_NODE "/sys/module/pd2055_cpufreq/parameters/zone_name"
#define PD_CPU_ZONE_USED_NODE "/sys/module/pd2055_cpufreq/parameters/zone_used"
#define PD_THERMAL_ZONE_DIR "/sys/class/thermal"
#define PD_BATTERY_TEMP_NODE "/sys/class/power_supply/battery/temp"
#define PD_KEEP_SOC_NODE "/sys/class/cms_class/keep_soc"
#define PD_KEEP_SOC_EFFECTIVE_NODE "/sys/class/power_supply/battery/keep_chg_soc"
#define PD_KEEP_SOC_VOTER 3
#define PD_THERMAL_RULE_MAX 10
#define PD_THERMAL_HYST_DC 20
#define PD_POLICY0_MAX_NODE "/sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq"
#define PD_POLICY4_MAX_NODE "/sys/devices/system/cpu/cpufreq/policy4/scaling_max_freq"
#define PD_POLICY7_MAX_NODE "/sys/devices/system/cpu/cpufreq/policy7/scaling_max_freq"
#define PD_POLICY0_FREQ_NODE "/sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies"
#define PD_POLICY4_FREQ_NODE "/sys/devices/system/cpu/cpufreq/policy4/scaling_available_frequencies"
#define PD_POLICY7_FREQ_NODE "/sys/devices/system/cpu/cpufreq/policy7/scaling_available_frequencies"
#define PD_CPUS_OFFLINED_NODE "/sys/module/msm_thermal/core_control/cpus_offlined"
#define PD_INSMOD_BIN "/system/bin/insmod"
#define PD_RMMOD_BIN "/system/bin/rmmod"
#define PD_HARD_MAX_FCC_MA 6600
#define PD_BOOST_MIN_MA 1000
#define PD_BOOST_POLL_MAX_MS 10000
#define PD_BOOST_RELEASE_WAIT_S 15
#define PD_KMOD_RESTORE_MA 6600
#define PD_KMOD_INTERVAL_MS 500
#define PD_KMOD_TIMEOUT_MS 30000
#define PD_LOG_MAX_BYTES (128 * 1024)
#define PD_PATH_MAX 4096

struct thermal_rule {
	int temp_dc;
	int little_mhz;
	int big_mhz;
	int prime_mhz;
	int charge_ma;
	int big_online;
	int prime_online;
};

struct thermal_state {
	int rule_index;
	int temp_dc;
	int charge_ma;
	int cpu_applied;
	int cpu_error;
	int online_applied;
	int online_error;
	int little_khz;
	int big_khz;
	int prime_khz;
	int big_online;
	int prime_online;
	char zone_path[PD_PATH_MAX];
};

struct config {
	int enabled;
	int max_input_ma;
	int min_input_ma;
	int charge_limit_soc;
	int soc_mid;
	int soc_mid_scale;
	int temp_derate_dc;
	int temp_stop_dc;
	int board_temp_derate_dc;
	int board_temp_stop_dc;
	int temp_derate_scale;
	int poll_active_ms;
	int poll_idle_ms;
	int boost_enabled;
	int boost_soc_max;
	int boost_ma;
	int boost_cable_mohm;
	int thermal_cpu;
	int thermal_charge;
	int thermal_online;
	int thermal_interval_ms;
	int thermal_idle_interval_ms;
	struct thermal_rule thermal_rules[PD_THERMAL_RULE_MAX];
	int thermal_rule_count;
};

struct telemetry {
	int online;
	int fast;
	int dchg;
	int soc;
	int batt_temp_dc;
	int board_temp_dc;
	int vbus_uv;
	int current_max_ua;
	int ibat_ua;
	char status[32];
	char charge_type[32];
};

struct context {
	char root[PD_PATH_MAX];
	char data_dir[PD_PATH_MAX];
	char config_path[PD_PATH_MAX];
	char state_dir[PD_PATH_MAX];
	char log_path[PD_PATH_MAX];
	char pid_path[PD_PATH_MAX];
	char paused_path[PD_PATH_MAX];
	char active_path[PD_PATH_MAX];
	char orig_path[PD_PATH_MAX];
	char applied_path[PD_PATH_MAX];
	char keep_soc_orig_path[PD_PATH_MAX];
	struct config cfg;
	struct thermal_state thermal;
	int controlling;
	int applied_ua;
	int boost_active;
	int boost_ma_applied;
	int boost_cable_set;
	int kmod_loaded;
	int keep_soc_orig_valid;
	int keep_soc_orig_soc;
	int keep_soc_applied;
	int keep_soc_error;
	time_t config_mtime;
	long config_mtime_ns;
	off_t config_size;
	ino_t config_ino;
	int config_stamp_valid;
};

int read_text_file(const char *path, char *buf, size_t size);
int read_int_file(const char *path, int *out);
int write_int_file(const char *path, int value);
int file_exists(const char *path);
int ensure_dir(const char *path);
void path_join(char *dst, size_t size, const char *root, const char *path);
int is_write_allowed(const char *path);
void sleep_ms(int ms, const volatile sig_atomic_t *stop,
	      const volatile sig_atomic_t *wake);

void log_init(const char *path);
void log_close(void);
void log_msg(const char *fmt, ...);

void config_defaults(struct config *cfg);
int config_load(struct context *ctx);
int config_changed(const struct context *ctx);
int config_write_default(const char *path);
void config_print(const struct config *cfg);

void charger_state(const struct context *ctx, int *online, int *charging);
int dchg_active(const struct context *ctx);
int telemetry_read(const struct context *ctx, struct telemetry *t);
int policy_target(const struct config *cfg, const struct telemetry *t, const char **reason);
int boost_target_ma(const struct config *cfg, const struct telemetry *t,
		    const char **reason);

int control_apply(struct context *ctx, int target_ma);
int control_release(struct context *ctx);
int control_restore_state(struct context *ctx);
int keep_soc_sync(struct context *ctx);
int keep_soc_apply(struct context *ctx, int limit);
int keep_soc_release(struct context *ctx, const char *reason);
int keep_soc_restore_state(struct context *ctx);
int keep_soc_effective(struct context *ctx);
int boost_enter(struct context *ctx, int target_ma, int cable_mohm);
int boost_refresh(struct context *ctx, int target_ma, int cable_mohm);
int boost_release(struct context *ctx, const char *reason);
int boost_restore_state(struct context *ctx);

int cpu_probe(struct context *ctx);
int cpu_on(struct context *ctx, int little_mhz, int big_mhz, int prime_mhz);
int cpu_off(struct context *ctx);
int cpu_status(struct context *ctx);
int cpu_unload(struct context *ctx);
int cpu_restore_state(struct context *ctx);
int cpu_nearest_khz(struct context *ctx, int policy, int khz);
int cpu_apply_targets(struct context *ctx, int little_khz, int big_khz,
		      int prime_khz);
int cpu_release_targets(struct context *ctx);
int cpu_online_apply(struct context *ctx, int big_online, int prime_online);
int cpu_online_restore(struct context *ctx);
int cpu_online_recover(struct context *ctx);
void cpu_online_counts(struct context *ctx, int *little, int *big, int *prime);

int thermal_tick(struct context *ctx);
void thermal_release(struct context *ctx, const char *reason);
int thermal_cap_ma(struct context *ctx);
void thermal_status_line(struct context *ctx);

int program_run(const char *path, char *const argv[]);

int daemon_run(struct context *ctx);
int cli_status(struct context *ctx);
int cli_once(struct context *ctx);
int cli_pause(struct context *ctx);
int cli_resume(struct context *ctx);
int cli_shutdown(struct context *ctx);

#endif
