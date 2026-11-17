# PD2055 Power Management

[中文](README.zh-CN.md)

KernelSU module for the vivo iQOO Neo5 (PD2055). It stops charging at a set SOC. It caps the USB input current during fast charging, raises the battery current request during low-SOC direct charge, and controls CPU frequency, CPU online state and charging by temperature. The module includes a configuration app.

## Features

- Charge stop: `charge_limit_soc`, stops charging at this SOC.
- Input current ceiling: `max_input_ma`, scaled by SOC and temperature.
- Direct-charge boost: `boost_ma`, `boost_cable_mohm`.
- Temperature rules: `thermal_rule`, with CPU frequency, CPU online counts and charge current per rule.
- CPU frequency takeover: `thermal_cpu`, enforced through a cpufreq notifier.
- CPU online state: `thermal_online`, manages CPU5, CPU6 and CPU7 by the active rule.
- Charge current cap by temperature: `thermal_charge`.
- Configuration app: `app/PD2055PowerManagement.apk`, one Activity, no background service.
- Pause and resume: `pause`, `resume`, and the KernelSU action button.
- Stop and restore nodes: `shutdown`.

## Limitations

- `ctm_current_max` can only lower the input current ceiling. The charger and the vendor stack set the actual input current.
- The AP cannot set the VBUS voltage. It also cannot force the charger into a fast-charge mode.
- The boost gain is bounded by the cable resistance and the charge pump CV.

## Install

Install the zip in KernelSU and soft reboot from KernelSU. `service.sh` starts `bin/pdcharge daemon` at boot.

`customize.sh` tries `pm install -r` on `app/PD2055PowerManagement.apk`. If the install fails, extract the APK from the zip and install it from the host:

```
adb install -r PD2055PowerManagement.apk
```

When `customize.sh` copied the APK to `/data/local/tmp`, install it on the device:

```
adb shell pm install -r /data/local/tmp/PD2055PowerManagement.apk
```

On the first run the app may report that it cannot execute `su`. Grant root to PD2055 Power Management in the KernelSU superuser list.

## Configuration

The configuration file is `/data/adb/pd2055-power-management/config.conf`.

| Key | Default | Range | Unit | Description |
| --- | --- | --- | --- | --- |
| `enabled` | 1 | 0 or 1 | | 0 releases the vote, the boost and the CPU online control |
| `boost_enabled` | 1 | 0 or 1 | | 0 disables the direct-charge boost |
| `boost_soc_max` | 40 | 0 to 100 | % | Boost runs below this SOC |
| `boost_ma` | 6600 | 1000 to 6600 | mA | Battery current request during boost |
| `boost_cable_mohm` | 100 | 0 to 1000 | mOhm | Cable resistance used by the MCU during boost; 0 leaves the node unchanged |
| `max_input_ma` | 5000 | 100 to 5000 | mA | Input current ceiling |
| `min_input_ma` | 500 | 100 to `max_input_ma` | mA | Input current at a battery or board temperature stop threshold |
| `charge_limit_soc` | 100 | 50 to 100 | % | Stop charging at this SOC; 100 disables the limit |
| `soc_mid` | 90 | 0 to `charge_limit_soc` | % | Above this SOC, with a scale below 100, the input ceiling is multiplied by the scale |
| `soc_mid_scale` | 100 | 10 to 100 | % | Scale above `soc_mid`; 100 disables the scaling |
| `temp_derate_dc` | 420 | 200 to 600 | 0.1 °C | Battery temperature that applies `temp_derate_scale` |
| `temp_stop_dc` | 450 | `temp_derate_dc` + 10 to 700 | 0.1 °C | Battery temperature for `min_input_ma` |
| `board_temp_derate_dc` | 550 | 300 to 800 | 0.1 °C | Board temperature that applies `temp_derate_scale` |
| `board_temp_stop_dc` | 600 | `board_temp_derate_dc` + 10 to 900 | 0.1 °C | Board temperature for `min_input_ma`; the boost stops |
| `temp_derate_scale` | 60 | 10 to 100 | % | Shared scale for the input and boost targets when the battery or board reaches its derate temperature |
| `poll_active_ms` | 10000 | 1000 to 60000 | ms | Poll interval while controlling |
| `poll_idle_ms` | 60000 | 1000 to 60000 | ms | Poll interval while idle |
| `thermal_cpu` | 0 | 0 or 1 | | 1 takes over the CPU frequency ceilings by the active rule |
| `thermal_charge` | 0 | 0 or 1 | | 1 caps the charge targets by the active rule's charge current; 0 leaves only the battery and board temperature thresholds |
| `thermal_online` | 0 | 0 or 1 | | 1 manages CPU5, CPU6 and CPU7 online state by the active rule |
| `thermal_interval_ms` | 5000 | 1000 to 60000 | ms | Temperature check interval while thermal control is on |
| `thermal_idle_interval_ms` | 15000 | `thermal_interval_ms` to 60000 | ms | Temperature check interval when the battery is not charging and the temperature is more than 2.0 °C below the lowest rule |
| `thermal_rule` | 4 rules | see below | | `temp,little,big,prime,charge,big_online,prime_online` |

`charge_limit_soc` also accepts the legacy key `soc_full`.

`thermal_rule` fields: temperature [0.1 °C], little core [MHz], big core [MHz], prime core [MHz], charge current [mA], big cores online (1 to 3), prime core online (0 or 1). A rule may omit the last two fields; then `big_online=3` and `prime_online=1`. Valid ranges are 300 to 750, 300 to 2000, 300 to 2600, 300 to 3300, 100 to 5000, 1 to 3 and 0 to 1. Up to 10 rules. Temperatures must be strictly ascending. A rule out of range or out of order is ignored. When the file contains at least one `thermal_rule` line, the file's rules replace the four defaults:

```
thermal_rule=400,1804,2419,2265,3000,3,1
thermal_rule=440,1612,2150,2073,2500,2,1
thermal_rule=480,1401,1862,1862,1800,2,0
thermal_rule=520,1171,1574,1632,1000,1,0
```

`big_online` counts CPU4, CPU5 and CPU6. The daemon writes only `cpu5/online` and `cpu6/online`; CPU4 is left alone. `prime_online` writes `cpu7/online`. With `thermal_online=0`, the daemon writes 1 to CPU5, CPU6 and CPU7 if it had applied an online state.

The daemon re-reads the configuration on the next poll when the file changes. `pdcharge reload` forces a re-read.

## Commands

```
MOD=/data/adb/modules/pd2055_power_management
$MOD/bin/pdcharge status
$MOD/bin/pdcharge once
$MOD/bin/pdcharge pause
$MOD/bin/pdcharge resume
$MOD/bin/pdcharge reload
$MOD/bin/pdcharge shutdown
$MOD/bin/pdcharge config-get
$MOD/bin/pdcharge config-set < config.conf
$MOD/bin/pdcharge cpu-probe
$MOD/bin/pdcharge cpu-on 1804 2419 2265
$MOD/bin/pdcharge cpu-off
$MOD/bin/pdcharge cpu-status
$MOD/bin/pdcharge cpu-unload
```

- `status` prints telemetry, the current `ctm_current_max` value, the policy target after the thermal caps and the module state. `imax` is the largest of `usb/current_max`, `pc_port/current_max` and `usb/pd_current_max`, printed in A. It is not the battery current. Read `ibat` for the battery current during direct charge. `dchg=1` means `dchg_state` reads `dual chg normal.`. `boost=on` means `set_ma` is greater than 0. `module=off` means a `disable` or `remove` marker is present in the module directory. `daemon=running` means the daemon is running. `limit` is `charge_limit_soc` from the configuration. `keep` is the effective stop SOC read from `battery/keep_chg_soc`; 100 means no limit. `reason` gives the target reason, such as `full`, `soc`, `thermal` or `protocol`. With `thermal_cpu`, `thermal_charge` or `thermal_online` set to 1, it also prints the zone temperature, the active rule and the actual online counts.
- `once` applies one decision and exits. It also applies the charge stop. The value stays until `shutdown`, the next daemon start or reboot. It refuses to run while the daemon is running or the module is disabled or removed. It does not change CPU online state.
- `pause` releases the vote, the boost, the charge stop and any CPU online state at once. The daemon then polls at `poll_idle_ms`.
- `resume` removes the pause marker. The daemon applies the target at once.
- `reload` makes the daemon re-read the configuration at once. It releases the current CPU frequency and online state before applying the new rule.
- `shutdown` stops the daemon, restores the saved node value, writes 0 to `cable_mohm`, unloads `psset.ko`, releases the CPU takeover and the charge stop vote, and restores CPU5, CPU6 and CPU7 online if it applied an online state.
- `config-get` prints the effective configuration as `key=value` lines, including the seven fields of every rule. It creates the default file when the file is missing.
- `config-set` reads a complete configuration from stdin. It writes `<config>.tmp` in the same directory, syncs it, renames it to `config.conf` with mode 0600, and sends `SIGHUP` when the daemon is running. It prints `ok daemon=running` or `ok daemon=stopped`.
- `cpu-probe` loads `pd2055_cpufreq.ko` with `probe=1` and `takeover=0`. The kernel log then shows every `CPUFREQ_ADJUST` writer.
- `cpu-on LITTLE_MHZ BIG_MHZ PRIME_MHZ` loads the module if needed, maps each value to the nearest `scaling_available_frequencies` entry, sets `max0`, `max4` and `max7`, sets `takeover=1` and applies. With `thermal_cpu=1`, the daemon overwrites these targets on the next rule change.
- `cpu-off` sets `takeover=0` and applies. PEM controls the CPU again; the module stays loaded.
- `cpu-status` prints the module parameters, the cached temperature, the zone, the three `scaling_max_freq` values, `cpus_offlined` and the actual little, big and prime online counts.
- `cpu-unload` sets `takeover=0`, applies and unloads `pd2055_cpufreq.ko`.

The KernelSU action button toggles `pause` and `resume`.

## App

The module zip includes `app/PD2055PowerManagement.apk`, package `com.pd2055.charge`, version 1.1.1. The APK is a plain Android app, not an LSPosed module. It has one Activity and no service, receiver, scheduled job, wakelock or network permission.

The top of the screen is a status card. The first line shows SOC, battery status and whether charging is fast. Below it are the battery and board temperatures, the input voltage and input current ceiling, the battery current, and the current target and reason. The next line shows the module switch, daemon, direct charge and boost state. With thermal control on, the last line shows the zone temperature and active rule.

The preset buttons `全部关闭`, `日常`, `保养` and `快充` only fill the form; the values are written after Apply. `保养` sets `charge_limit_soc=80` and `max_input_ma=3000`, and disables the boost. `快充` sets the boost SOC limit to 50. `日常` uses no charge stop, a 5000 mA ceiling and the default boost.

The form has two top-level sections, `充电` and `温控接管`. Both headings use a larger font, and a divider separates `温控接管` from the charging part. Every switch and input is its own rounded block row. Switch rows use the native `Switch` widget and the whole row toggles it. `充电` contains four sub-blocks: switches and charge stop, input current limit, temperature protection, and direct-charge boost. The input current limit sub-block holds `max_input_ma`, `soc_mid` and `soc_mid_scale`. Temperature protection holds the battery and board derate and stop temperatures, `temp_derate_scale` and `min_input_ma`, with a note that the four temperatures share one scale and one stop current. A note under `充电` says the input current ceiling and the boost request are different quantities, temperature protection adjusts both, and the actual battery current is also limited by input power and charger conversion. The boost sub-block notes that it only runs in direct charge below `boost_soc_max`. `温控接管` has a note that the rule temperature is the `skin-therm` shell temperature, not the battery or board temperature. Temperature fields show °C with one decimal and are converted to 0.1 °C on write. Thermal rules are edited in °C, one rule per line. The advanced section is collapsed and contains the poll intervals, boost cable resistance and kernel status.

The app reads the configuration with `pdcharge config-get` and applies the full configuration with `pdcharge config-set` through the KernelSU `su` binary. The app writes all keys, so comments in the configuration file are not preserved. A five-field rule is written back with `big_online=3` and `prime_online=1`.

While in the foreground the app runs `pdcharge status` every 10 s and stops the refresh when it goes to the background. It does not keep a process or start at boot. When the daemon is stopped the configuration is still written and takes effect after the next start.

The daemon checks the `disable` and `remove` markers in the module directory on each loop. Disabling or uninstalling releases the vote and the boost within one poll interval. Removing the marker resumes control. The process keeps running. Run `shutdown` or soft reboot to stop it.

## Files

- `/data/adb/pd2055-power-management/config.conf`: configuration.
- `/data/adb/pd2055-power-management/logs/pdcharge.log`: log. At daemon start a log larger than 128 KiB is renamed to `pdcharge.log.1`.
- `/data/adb/pd2055-power-management/state/`: pid file, pause marker, saved node values, `keep_soc_orig` and the `online_applied` marker.
- `app/PD2055PowerManagement.apk`: configuration app.
- `kmod/psset.ko`: kernel module used by the direct-charge boost.
- `kmod/pd2055_cpufreq.ko`: kernel module used by the CPU frequency takeover.
- `config.conf` in the module directory: default configuration. `customize.sh` copies it when `/data/adb/pd2055-power-management/config.conf` does not exist.

## How it works

### Input current ceiling

Fast charging means `vbus` is at least 8 V, or `charge_type` is `Fast` or `Turbo` with `current_max` at least 1 A. Direct charge reports `charge_type` `N/A` and a low `current_max`, so the `vbus` condition detects it.

The ceiling is written only to `/sys/class/power_supply/usb/ctm_current_max`, in uA. The kernel adds it to the `USB_ICL` votable as `CTM_VOTER`. `USB_ICL` uses the minimum of all votes.

Before the first write the module saves the node value. On release it restores that value only if the node still holds the value written by the module. When another writer changes the node, the module leaves the current value in place.

The daemon releases the vote when:

- `enabled=0`
- the daemon is paused
- the module is disabled or removed
- the charger is offline
- SOC is unreadable
- SOC is at or above `charge_limit_soc`
- charging is not fast

The target is `max_input_ma`. When SOC is above `soc_mid` and `soc_mid_scale` is below 100, the target is scaled by `soc_mid_scale`. When the battery temperature reaches `temp_derate_dc` or the board temperature reaches `board_temp_derate_dc`, the target is multiplied by `temp_derate_scale`. The result is clamped to `min_input_ma` to `max_input_ma`. At a temperature stop threshold the target is `min_input_ma`.

### Charge stop

`/sys/class/cms_class/keep_soc` is a voter table. A write uses the form `<voter> <SOC>`. The read value contains `vote:<SOC>` and `<voter>:<SOC>` entries. The module uses voter 3. `/sys/class/power_supply/battery/keep_chg_soc` shows the effective value; 100 means no limit. The vivo `com.iqoo.powersaving` optimized charging also uses voter 3.

With `enabled=1` and `charge_limit_soc` below 100, the daemon writes `3 <charge_limit_soc>` at start, after a configuration reload and on every charge poll. Before the first write it saves the current value of voter 3 to `state/keep_soc_orig`; when the voter is absent it saves -1.

On release the saved value is written back; -1 is written as 0 to remove the vote. Release happens on `enabled=0`, pause, module disable or removal, charger offline, `charge_limit_soc=100` and `shutdown`. When another writer has changed voter 3 to a different value, release skips the write and logs `keep_soc release skipped`.

At daemon start, if `state/keep_soc_orig` exists, the daemon first restores the saved vote. This recovers from a forced process exit.

The stop only makes the charger stop charging the battery; the charge level does not fall. When SOC is already above the limit, `reason` in `status` is `full` or `soc` and the target is 0.

### Temperature rules

The daemon reads `/sys/class/thermal/thermal_zone*/type` and uses the first zone matching `skin-therm`, `case_therm`, `board_therm`, `quiet_therm`, `ap_ntc`, `quiet-therm-adc` or `mtktsAP`. When no zone can be read, it uses `/sys/class/power_supply/battery/temp`, in 0.1 °C.

On this device the first match is `thermal_zone85`, type `skin-therm`. It measures the shell temperature near the back cover. It is not the battery temperature and not the board charge temperature. The battery and board thresholds read `/sys/class/power_supply/battery/temp` and `battery/primary_board_temp`.

With `thermal_cpu=1`, `thermal_charge=1` or `thermal_online=1`, the daemon evaluates the rules at the thermal interval. It uses `thermal_interval_ms` when the battery is charging, when a rule is active, or when the temperature is within 2.0 °C of the lowest rule. It uses `thermal_idle_interval_ms` when the battery is not charging, no rule is active, and the temperature is more than 2.0 °C below the lowest rule. The active rule is the last rule whose temperature is at or below the zone temperature. A lower rule replaces the active rule only after the temperature falls 2.0 °C below the active rule's temperature. Below the lowest rule, the same hold applies before release.

With `thermal_cpu=1`, the active rule's three frequencies are mapped to `scaling_available_frequencies` entries. The daemon picks the nearest entry by absolute difference; on a tie it picks the lower one. The floor is the higher of `scaling_min_freq` and `cpuinfo_min_freq`. The ceiling is `cpuinfo_max_freq`. Below the lowest rule, or with `thermal_cpu=0`, the daemon writes `takeover=0` and `apply`. The module stays loaded.

With `thermal_charge=1`, the active rule's charge current caps the input current target and the boost target. Each target takes the lower of its own value and the rule value, so the rule never raises a target. Below the lowest rule no rule cap applies. With `thermal_charge=0`, the rules can still manage CPU frequency and online state, but the rule charge current does not cap charging; the battery and board temperature derate and stop thresholds still apply. The log prints `thermal cap <mA>` when the effective cap changes and `thermal cap -1` when no cap applies. The `reason` field in `status` is `thermal` while a cap is active and below the target.

With `thermal_online=1`, the active rule's `big_online` and `prime_online` values set the CPU5, CPU6 and CPU7 online state. Below the lowest rule, or with `thermal_online=0`, the daemon restores the three CPUs online if it applied an online state.

`status` prints a `thermal` line with the zone temperature, the rule selected from that temperature in seven-field form, the rule values for CPU and charge, and the actual little, big and prime online counts.

### CPU frequency takeover

`kmod/pd2055_cpufreq.ko` registers a cpufreq policy notifier. With `takeover=1`, each `CPUFREQ_ADJUST` event sets the policy ceiling to `max0`, `max4` or `max7` for policy0, policy4 and policy7. This covers PEM writes to `scaling_max_freq` and `scaling_min_freq`, and the `CPUFREQ_ADJUST` changes from `msm_performance` and `cpu-boost`. No PEM process is hooked.

The kernel calls the `CPUFREQ_ADJUST` chain before `CPUFREQ_INCOMPATIBLE`. The kernel thermal cooling callback runs in the second chain, so `skin-therm` and the other thermal zones still lower the frequency after the module sets its ceiling.

While takeover is active, the kernel module reads a thermal zone every 5 s into `temp_mc`. With `takeover=0` it does not read the zone and runs every 30 s. With `takeover=1`, a write to `apply` samples the zone first. It tries `skin-therm` first, then `case_therm`, `board_therm`, `quiet_therm`, `ap_ntc` and `quiet-therm-adc`. At or above `safety_temp_mc` the module only lowers the ceiling. Below it, the ceiling is set to the target, and `min` is lowered to the target when needed. A target of 0 leaves that cluster alone.

`cpu-on` maps each requested MHz to the nearest entry in `scaling_available_frequencies`, so 1612 MHz becomes 1612800 kHz. On a tie it takes the lower entry. The values written to `max0`, `max4` and `max7` are kHz. The cpufreq driver rounds other values down to an available frequency.

Writing `apply` calls `cpufreq_update_policy(0)`, `cpufreq_update_policy(4)` and `cpufreq_update_policy(7)`. A new target takes effect without waiting for the next PEM write.

| Node | Default | Unit | Description |
| --- | --- | --- | --- |
| `takeover` | 0 | | 1 enforces the three ceilings |
| `probe` | 0 | | 1 logs each `CPUFREQ_ADJUST` writer |
| `max0` | 0 | kHz | policy0 ceiling; 0 leaves the cluster alone |
| `max4` | 0 | kHz | policy4 ceiling; 0 leaves the cluster alone |
| `max7` | 0 | kHz | policy7 ceiling; 0 leaves the cluster alone |
| `safety_temp_mc` | 90000 | mC | At or above this temperature the module only lowers a ceiling |
| `temp_mc` | | mC | Cached zone temperature; read-only |
| `zone_name` | empty | | Zone to read; empty uses the built-in list |
| `zone_used` | | | Zone selected at load; read-only |
| `apply` | | | Write to re-evaluate the three policies |

The parameters are under `/sys/module/pd2055_cpufreq/parameters/`. `probe=1` logs lines like:

```text
pd2055_cpufreq: adjust cpu=0 comm=writer pid=1234 uid=1000 min=... max=... user_max=... take=0 temp=...
```

The log is rate limited. Read it with `dmesg`.

`cpu-off` and `cpu-unload` write `takeover=0` and `apply` before unloading, so PEM's last `scaling_max_freq` value is restored. Unloading the module without this step leaves the last enforced ceiling in place until the next cpufreq update.

The notifier does not cover writes to `/sys/module/msm_thermal/core_control/cpus_offlined` or `/proc/pem/thml_*`. GPU and DDR policies are untouched. Kernel thermal zones remain the final clamp.

### CPU online state

With `thermal_online=1`, the daemon writes `/sys/devices/system/cpu/cpu5/online`, `cpu6/online` and `cpu7/online` directly. `big_online=1` keeps CPU5 and CPU6 offline. `big_online=2` brings CPU5 online. `big_online=3` brings CPU5 and CPU6 online. `prime_online` controls CPU7. CPU4 is the first big core and is not written. CPU0 is the boot CPU and cannot be offlined.

The daemon reads the three nodes on every thermal tick and writes only a node that differs from the rule. It does not use `core_ctl`; changing `core_ctl/min_cpus` and `core_ctl/max_cpus` did not change the online state on this device.

The daemon writes 1 to CPU5, CPU6 and CPU7 when `thermal_online=0`, when the temperature falls below the lowest rule, on `pause`, on module disable, on reload and on shutdown. The write happens only if the daemon applied an online state. A reload releases the state before loading the new configuration, so a rule change briefly brings all three CPUs online.

When the daemon applies an online state, it creates `/data/adb/pd2055-power-management/state/online_applied`. At the next daemon start, the marker makes the daemon write 1 to CPU5, CPU6 and CPU7 and remove the marker. This recovers from a daemon crash that left a core offline.

### Direct-charge boost

In direct charge the STM32L011 runs the charge loop. Writing `ctm_current_max` cannot raise the current in this path.

Two paths let the AP raise the battery current in direct charge.

`/sys/class/stm32_debug/cable_mohm` sets the cable resistance the MCU uses for its input current request. A value below the real resistance raises the request. The MCU also measures the cable itself. The test cable measured 430 to 490 mΩ.

The `mcu` power supply handles property 0x141 as a battery current request in mA, clamped to 6600 mA. The MCU applies it as the `USER_FCC` vote. The kernel exposes no node for this property. While the boost is active, `kmod/psset.ko` calls `power_supply_set_property` on the `mcu` supply every 500 ms.

The boost runs when all of these conditions hold:

- `enabled=1`
- `boost_enabled=1`
- the charger is online
- `dchg_state` is `dual chg normal.`
- SOC is below `boost_soc_max`
- both temperatures are below their stop thresholds

The request is `boost_ma`. It sets the battery current request, not the USB input current ceiling controlled by `max_input_ma`. When the battery temperature reaches `temp_derate_dc` or the board temperature reaches `board_temp_derate_dc`, the request is multiplied by `temp_derate_scale`. The request is clamped to 1000 to 6600 mA. The actual battery current is also limited by the input power, charger conversion and battery voltage. With `boost_cable_mohm` at 0 the module writes only `set_ma` and leaves `cable_mohm` unchanged.

The boost stops when any of these conditions holds:

- the charger goes offline
- charging leaves direct charge
- SOC reaches `boost_soc_max`
- a temperature reaches its stop threshold
- `enabled=0`
- `boost_enabled=0`
- the daemon is paused
- the module is disabled or removed

### Release

When the boost changed `cable_mohm`, release first writes 0 to it. After 15 s, release writes 0 to `set_ma`. It then unloads `psset.ko`. The 15 s wait lets cms vote again with the real cable resistance before the FCC override ends. When the boost did not change the cable value, release skips the wait.

### Heartbeat

The daemon writes `heartbeat` on every boost poll. The poll interval is capped at 10 s while the boost is active. If the daemon stops writing for 30 s while the system is awake, the kernel module writes the `restore_ma` value, 6600 mA, to the `mcu` supply once. Suspend time is not counted. Writing `heartbeat` again resumes the override. Unloading `psset.ko` with an active override also writes 6600 mA.

### Write scope

Regular files are written only under `/data/adb` and `/data/local/tmp`. Outside these directories the module writes only these nodes:

- `/sys/class/power_supply/usb/ctm_current_max`
- `/sys/class/cms_class/keep_soc`
- `/sys/class/stm32_debug/cable_mohm`
- `/sys/module/psset/parameters/set_ma`
- `/sys/module/psset/parameters/heartbeat`
- `/sys/module/pd2055_cpufreq/parameters/takeover`
- `/sys/module/pd2055_cpufreq/parameters/probe`
- `/sys/module/pd2055_cpufreq/parameters/max0`
- `/sys/module/pd2055_cpufreq/parameters/max4`
- `/sys/module/pd2055_cpufreq/parameters/max7`
- `/sys/module/pd2055_cpufreq/parameters/safety_temp_mc`
- `/sys/module/pd2055_cpufreq/parameters/apply`
- `/sys/devices/system/cpu/cpu5/online`
- `/sys/devices/system/cpu/cpu6/online`
- `/sys/devices/system/cpu/cpu7/online`

## Measurements

Measured on a PD2055 with the stock 66W charger and a non-original cable, at SoC 13% to 27%:

| Setting | Battery current | Input current request |
| --- | --- | --- |
| no boost | 2.8 A | 1500 mA |
| `boost_cable_mohm=100` | 3.4 to 3.7 A | 1900 to 2100 mA |
| `boost_cable_mohm=100`, `boost_ma` 4000 to 6000 | about 3.4 A | 1800 mA |
| `boost_ma=1500` | 1.2 to 1.3 A | 750 mA |

With the cable value at 100 mΩ the battery voltage rose to 8.95 to 9.03 V. The charge pump CV at about 9.0 V capped the current. Requests above 4000 mA did not add current at this SOC. The highest battery temperature was 36.0 °C.

At 42% SOC the vendor stack requested 1400 mA input and the battery current fell to about 2.6 A. The default `boost_soc_max=40` stops the boost before this point.

The gain depends on the real cable. A cable with lower resistance raises the current in both the MCU request and the `cable_mohm` path.

## Power use

While controlling, the daemon reads the battery, temperature, voltage and current nodes once per `poll_active_ms`. While idle, it checks the charger state once per `poll_idle_ms`. When the charger goes from offline to online, the same loop iteration checks the charger state immediately instead of waiting for `poll_idle_ms`. It writes `ctm_current_max` only when the target changes. With the boost active, the poll is capped at 10 s and `psset.ko` applies the request every 500 ms. `psset.ko` is loaded when the boost starts and unloaded on release.

With `thermal_cpu`, `thermal_charge` or `thermal_online` set to 1, the loop wakes at the thermal interval and reads one thermal zone. The interval is `thermal_idle_interval_ms` when the battery is not charging, no rule is active, and the temperature is more than 2.0 °C below the lowest rule; otherwise it is `thermal_interval_ms`. The charge poll keeps its own interval. With all three set to 0, the thermal timer is off. With `thermal_online=1`, each thermal tick reads the three online nodes and writes only a node that differs from the rule. When the active rule is unchanged and the last apply succeeded, the thermal tick reuses the cached kHz and does not read `scaling_available_frequencies`, `cpuinfo_min_freq`, `scaling_min_freq` or `cpuinfo_max_freq`.

While takeover is active, `pd2055_cpufreq.ko` reads a thermal zone every 5 s. With `takeover=0` it runs every 30 s and does not read the zone. With `probe=1`, every `CPUFREQ_ADJUST` writes a kernel log line; `printk_ratelimit` limits the rate, so enable it only for diagnostics.

Each loop also checks the configuration file and the module markers with `stat` and `access`. These checks ride on wakeups that already happen, so they add no timer.

While in the foreground the app runs one `status` command every 10 s and stops at background. A read or apply runs one root command and exits. The app adds no timer.

`nanosleep` holds no wakelock. While the device is suspended, the timer does not wake the SoC. Polling continues after wake.

## Build

```
make
make build/pdcharge-host
make kmod
make app
make package
```

`make` builds the static aarch64 binary at `build/pdcharge`. `make build/pdcharge-host` builds a host binary for local tests. `make kmod` builds `build/psset.ko` and `build/pd2055_cpufreq.ko`. `make app` builds `build/PD2055PowerManagement.apk`. `make package` builds the binary, both kernel modules and the app. It writes `build/PD2055-Power-Management-<version>.zip`. The kernel modules are built against these trees in the PD2055 repository: `work/kernel-devsrc`, `work/kernel-4.19.125-build` and `work/kheaders/tree`. Set `KSRC`, `KOBJ` or `KHDR` to use other trees.

The app build uses JDK 17 `jdk17/jdk-17.0.21+9` under `$HOME/opt`, Android SDK build-tools 34.0.0 and the debug keystore at `$HOME/.android/debug.keystore`. Set `JAVA_HOME`, `ANDROID_HOME` or `KEYSTORE` to use other paths.

The build uses Zig 0.17.1 at `$HOME/opt/zig/zig` by default. Set `ZIG=/path/to/zig` to use another path.
