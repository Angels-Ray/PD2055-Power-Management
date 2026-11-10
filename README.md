# PD2055 Charge

[中文](README.zh-CN.md)

KernelSU module for the vivo iQOO Neo5 (PD2055). It caps the USB input current during fast charging.

## Control point

PD2055 charges the battery through the vendor stack: STM32L011 MCU, BQ25790 and vivo cms. Battery-side current nodes such as `force_main_fcc` are not settable from the AP on this device; writes return success but leave the hardware unchanged.

The module writes one node, `/sys/class/power_supply/usb/ctm_current_max`, in uA. The kernel applies it through `CTM_VOTER` on the `USB_ICL` votable. A value of -1 removes the vote. The vivo charging stack also mirrors the value to the BQ25790 input current limit register, so the limit covers the 20V fast-charge path.

Before the first write the module saves the current node value. On release it restores that value only if the node still holds the value written by the module. When another writer changed the node, the module leaves the current value in place.

No regular file outside `/data/adb` and `/data/local/tmp` is written. The only node written outside these directories is `/sys/class/power_supply/usb/ctm_current_max`.

## Files

- `/data/adb/pd2055-charge/config.conf`: configuration.
- `/data/adb/pd2055-charge/logs/pdcharge.log`: log. At daemon start a log larger than 128 KiB is renamed to `pdcharge.log.1`.
- `/data/adb/pd2055-charge/state/`: pid file, pause marker, saved node values.
- `config.conf` in the module directory: default configuration. `customize.sh` copies it to `/data/adb/pd2055-charge/config.conf` when that file does not exist.

## Install

Install the zip in KernelSU and reboot. `service.sh` starts the daemon at boot. The module contains `bin/pdcharge`, `module.prop`, `service.sh`, `action.sh`, `customize.sh`, `uninstall.sh` and `config.conf`.

## Configuration

| Key | Default | Range | Description |
| --- | --- | --- | --- |
| `enabled` | 1 | 0 or 1 | Enable current limiting |
| `max_input_ma` | 3300 | 100 to 3300 | Upper input current limit [mA] |
| `min_input_ma` | 500 | 100 to `max_input_ma` | Limit used at temperature stop [mA] |
| `soc_full` | 85 | 50 to 100 | Release at or above this SOC [%] |
| `soc_mid` | 75 | 0 to `soc_full` | SOC above which the limit is scaled [%] |
| `soc_mid_scale` | 70 | 10 to 100 | Scale above `soc_mid` [%] |
| `temp_derate_dc` | 360 | 200 to 600 | Battery temperature for the first 60% step [0.1 C] |
| `temp_stop_dc` | 420 | `temp_derate_dc` + 10 to 700 | Battery temperature for `min_input_ma` [0.1 C] |
| `board_temp_derate_dc` | 520 | 300 to 800 | Board temperature for the first 60% step [0.1 C] |
| `board_temp_stop_dc` | 600 | `board_temp_derate_dc` + 10 to 900 | Board temperature for `min_input_ma` [0.1 C] |
| `poll_active_ms` | 10000 | 1000 to 60000 | Poll interval while controlling [ms] |
| `poll_idle_ms` | 60000 | 1000 to 60000 | Poll interval while idle [ms] |

The daemon releases the vote when `enabled=0`, when SOC is unreadable, when SOC is at or above `soc_full`, or when charging is not fast. Fast charging means `charge_type` is `Fast` or `Turbo` with `current_max` at least 1 A or `vbus` at least 8 V; other charge types need both conditions.

The target is `max_input_ma`, scaled by `soc_mid_scale` above `soc_mid` and multiplied by 60% for each derate threshold reached. The result is clamped to `min_input_ma` to 3300 mA. At `temp_stop_dc` or `board_temp_stop_dc` the target is `min_input_ma`.

The daemon reloads the configuration when the file changes. The check is one `stat` per loop and adds no timer. The file is not opened or parsed unless the stat changes. The new values take effect within one poll interval; `pdcharge reload` forces it at once.

## Input current and power

The stock 66W charger, model V6650L0A0-CN, provides 5V/2A, 9V/2A, 11V/3A, 11V/5A and 20V/3.3A. PD2055 reaches 66W at 20V/3.3A. The BQ25790 input current limit tops out at 3.3 A, so values above 3300 mA have no additional effect.

At 20V input, the configured limit maps to power as follows:

| `max_input_ma` | Power at 20V |
| --- | --- |
| 1000 | about 20W |
| 2000 | about 40W |
| 2500 | about 50W |
| 3000 | about 60W |
| 3300 | about 66W |

## Commands

```
MOD=/data/adb/modules/pd2055_charge
$MOD/bin/pdcharge status
$MOD/bin/pdcharge once
$MOD/bin/pdcharge pause
$MOD/bin/pdcharge resume
$MOD/bin/pdcharge reload
$MOD/bin/pdcharge shutdown
```

- `status` prints telemetry, the current `ctm_current_max` value, the policy target and the module switch state.
- `once` applies one decision and exits. The value stays until `shutdown`, the next daemon start or reboot. It refuses to run while the daemon is running or the module is disabled.
- `pause` releases the vote at once and polls at `poll_idle_ms`.
- `resume` removes the pause marker; the daemon applies the target at once.
- `reload` makes the daemon re-read the configuration at once.
- `shutdown` stops the daemon and restores the saved node value.

The KernelSU action button toggles `pause` and `resume`.

KernelSU has no runtime callback for the module switch. The daemon checks the `disable` and `remove` markers under `/data/adb/modules/pd2055_charge` on each loop. Disabling or uninstalling the module releases the vote within one poll interval; undoing either lets the daemon apply again. The process keeps running. Run `shutdown` or reboot to stop it. A soft reboot does not stop native processes.

## Power use

While charging, the daemon reads about eight small sysfs files per `poll_active_ms`. While idle, it reads `battery/status` once per `poll_idle_ms`. It writes the node only when the target changes.

Each loop also checks the configuration file and the KernelSU markers with `stat` and `access`. These checks ride on wakeups that already happen, so they add no timer and no extra wakeup.

`nanosleep` holds no wakelock. While the device is suspended, the timer does not wake the SoC; polling continues after wake.

## Build

```
make                    # build/pdcharge, static aarch64
make build/pdcharge-host
make package            # build/PD2055-Charge-<version>.zip
```

The build uses Zig 0.17.1 at `$HOME/opt/zig/zig` by default. Set `ZIG=/path/to/zig` to use another path.
