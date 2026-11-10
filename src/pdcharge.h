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
#define PD_HARD_MAX_INPUT_MA 3300
#define PD_MIN_INPUT_MA 100
#define PD_VBUS_FAST_UV 8000000
#define PD_CTM_NODE "/sys/class/power_supply/usb/ctm_current_max"
#define PD_LOG_MAX_BYTES (128 * 1024)
#define PD_PATH_MAX 4096

struct config {
	int enabled;
	int max_input_ma;
	int min_input_ma;
	int soc_full;
	int soc_mid;
	int soc_mid_scale;
	int temp_derate_dc;
	int temp_stop_dc;
	int board_temp_derate_dc;
	int board_temp_stop_dc;
	int poll_active_ms;
	int poll_idle_ms;
};

struct telemetry {
	int online;
	int fast;
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
	struct config cfg;
	int controlling;
	int applied_ua;
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

int charger_online(const struct context *ctx);
int telemetry_read(const struct context *ctx, struct telemetry *t);
int policy_target(const struct config *cfg, const struct telemetry *t, const char **reason);

int control_apply(struct context *ctx, int target_ma);
int control_release(struct context *ctx);
int control_restore_state(struct context *ctx);

int daemon_run(struct context *ctx);
int cli_status(struct context *ctx);
int cli_once(struct context *ctx);
int cli_pause(struct context *ctx);
int cli_resume(struct context *ctx);
int cli_shutdown(struct context *ctx);

#endif
