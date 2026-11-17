#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/ktime.h>

static char *psy_name = "mcu";
module_param(psy_name, charp, 0444);

static int prop = 0x141;
module_param(prop, int, 0444);

/* Target current in mA. 0 stops overriding and writes restore_ma once. */
static int set_ma;
module_param(set_ma, int, 0644);

/* Current in mA written when overriding stops. */
static int restore_ma = 6600;
module_param(restore_ma, int, 0644);

/* Re-apply period in ms. */
static int interval_ms = 500;
module_param(interval_ms, int, 0644);

/* Restore after this long without a heartbeat write. 0 disables the check. */
static int timeout_ms = 30000;
module_param(timeout_ms, int, 0644);

/* Monotonic time excludes suspend, so a sleeping system does not trip it. */
static u64 last_hb_ns;

static int hb_set(const char *val, const struct kernel_param *kp)
{
	last_hb_ns = ktime_get_ns();
	return 0;
}

static const struct kernel_param_ops hb_ops = {
	.set = hb_set,
};

module_param_cb(heartbeat, &hb_ops, NULL, 0644);

static struct power_supply *psy;
static struct task_struct *worker;
static bool overridden;
static bool apply_failed;

static int psset_apply(int ma)
{
	union power_supply_propval val;

	val.intval = ma;
	return power_supply_set_property(psy, prop, &val);
}

static int psset_worker(void *unused)
{
	int last = 0;
	bool tripped = false;

	while (!kthread_should_stop()) {
		int target = READ_ONCE(set_ma);
		int restore = READ_ONCE(restore_ma);
		int timeout = READ_ONCE(timeout_ms);
		int interval;

		if (target > 0) {
			u64 hb = READ_ONCE(last_hb_ns);

			if (timeout > 0 && hb &&
			    ktime_get_ns() - hb > (u64)timeout * 1000000ULL) {
				if (!tripped) {
					int rc = psset_apply(restore);

					pr_info("psset: heartbeat timeout, restore %d mA rc=%d\n",
						restore, rc);
					tripped = true;
					overridden = false;
					apply_failed = false;
				}
			} else {
				int rc;

				tripped = false;
				rc = psset_apply(target);
				if (rc) {
					if (!apply_failed)
						pr_info("psset: set %d mA failed: %d\n",
							target, rc);
					apply_failed = true;
				} else {
					apply_failed = false;
					overridden = true;
				}
			}
		} else {
			if (last > 0 && restore > 0) {
				int rc = psset_apply(restore);

				pr_info("psset: override off, restore %d mA rc=%d\n",
					restore, rc);
				overridden = false;
			}
			apply_failed = false;
			tripped = false;
		}
		last = target;
		interval = READ_ONCE(interval_ms);
		msleep(interval > 0 ? interval : 500);
	}
	return 0;
}

static int __init psset_init(void)
{
	union power_supply_propval val;
	int rc;

	psy = power_supply_get_by_name(psy_name);
	if (!psy) {
		pr_info("psset: no power supply %s\n", psy_name);
		return -ENODEV;
	}

	val.intval = -1;
	rc = power_supply_get_property(psy, prop, &val);
	pr_info("psset: psy=%s prop=0x%x read_rc=%d value=%d\n",
		psy_name, prop, rc, val.intval);

	worker = kthread_run(psset_worker, NULL, "psset");
	if (IS_ERR(worker)) {
		rc = PTR_ERR(worker);
		worker = NULL;
		power_supply_put(psy);
		psy = NULL;
		return rc;
	}
	return 0;
}

static void __exit psset_exit(void)
{
	int rc;

	if (worker)
		kthread_stop(worker);
	if (psy && overridden && READ_ONCE(restore_ma) > 0) {
		rc = psset_apply(READ_ONCE(restore_ma));
		pr_info("psset: unload, restore %d mA rc=%d\n",
			READ_ONCE(restore_ma), rc);
	}
	if (psy)
		power_supply_put(psy);
	pr_info("psset: unload\n");
}

module_init(psset_init);
module_exit(psset_exit);
MODULE_LICENSE("GPL v2");
