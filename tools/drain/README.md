# PD2055 battery drain

Drains the battery at the highest rate the phone can sustain. `drain.sh` requires root; `drain-cpu.sh` runs as the shell user.

[中文](README.zh-CN.md)

## Usage

```
adb push drain.sh /data/local/tmp/
adb shell
su
sh /data/local/tmp/drain.sh [seconds]
```

`seconds` defaults to 0 and runs until Ctrl-C.

Use wireless adb while the phone is unplugged. A USB connection charges the battery.

Set `TORCH=0` to leave the torch off:

```
TORCH=0 sh /data/local/tmp/drain.sh
```

## CPU only

`drain-cpu.sh` runs one `yes` process per online CPU. It needs no root, changes no settings, and writes no files. The system governor and thermal limits still control the clock.

```
adb push drain-cpu.sh /data/local/tmp/
adb shell
sh /data/local/tmp/drain-cpu.sh [seconds]
```

## Operation

- `drain.sh` sets each online CPU to the performance governor and runs one `yes` process per online CPU.
- Sets the display to maximum brightness and the screen timeout to the maximum.
- Enables Wi-Fi and Bluetooth and sends 5 pings per second to 1.1.1.1.
- Turns on `led:torch_0` at its maximum current.
- Holds the `pd2055-drain` kernel wakelock.
- Prints battery status every 10s.

The tool plays no audio and does not use the vibrator.

Ctrl-C restores the CPU governors, display settings, radios, torch, and wakelock.

## Writes

`drain.sh` writes only these settings and sysfs files:

- `settings`: `screen_brightness`, `screen_brightness_mode`, `screen_off_timeout`.
- `/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
- `/sys/class/leds/led:torch_0/brightness`
- `/sys/class/leds/led:switch_0/brightness`
- `/sys/power/wake_lock`, `/sys/power/wake_unlock`

`drain.sh` changes the Wi-Fi and Bluetooth state through `svc` and creates no files.

The phone throttles as it heats, so the drain rate drops at the thermal limit.
