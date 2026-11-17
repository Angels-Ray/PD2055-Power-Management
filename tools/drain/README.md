# PD2055 battery drain

Drains the battery at the highest rate the phone can sustain. Runs as the shell user with CPU and GPU load. Started as root, it also sets the performance governor, display, radios, torch, and wakelock.

[中文](README.zh-CN.md)

## Usage

```
make drain
adb push build/drain /data/local/tmp/
adb shell
/data/local/tmp/drain [seconds]
```

`seconds` defaults to 0 and runs until Ctrl-C.

Run through `su` to enable the root features:

```
su -c '/data/local/tmp/drain'
```

Without root the program runs the CPU and GPU loads only.

Use wireless adb while the phone is unplugged. A USB connection charges the battery.

Set `TORCH=0` to leave the torch off. Set `GPU=0` to run the CPU only:

```
GPU=0 TORCH=0 /data/local/tmp/drain
```

Set `BATTERY_TEMP_STOP` to change the battery temperature limit. The unit is 0.1 °C, so the default 460 is 46.0 °C. The value 450 below is 45.0 °C:

```
BATTERY_TEMP_STOP=450 /data/local/tmp/drain
```

## Operation

As shell:

- Runs one busy loop per online CPU.
- Runs a fragment shader on an offscreen 1920x1080 surface through EGL and GLES.

As root:

- Sets the CPU governor to performance.
- Sets the display to maximum brightness and the screen timeout to the maximum.
- Enables Wi-Fi and Bluetooth.
- Sends 5 pings per second to 1.1.1.1.
- Sets `led:torch_0` to its maximum brightness.
- Holds the `pd2055-drain` kernel wakelock.
- Prints battery status every 10 s.

The program plays no audio and does not use the vibrator.

Ctrl-C stops the loads and restores the settings changed in root mode.

The program also stops the loads and restores the settings changed in root mode when the battery temperature reaches `BATTERY_TEMP_STOP`. The battery temperature is read from `/sys/class/power_supply/battery/temp`. On PD2055 only root can read this node, so the limit does not apply when the program runs as the shell user.

## Writes

The program writes no regular files.

Root mode writes only these settings and sysfs files:

- `settings`: `screen_brightness`, `screen_brightness_mode`, `screen_off_timeout`.
- `/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
- `/sys/class/leds/led:torch_0/brightness`
- `/sys/class/leds/led:switch_0/brightness`
- `/sys/power/wake_lock`, `/sys/power/wake_unlock`

The program changes the Wi-Fi and Bluetooth state through `svc`.

The phone throttles as it heats, so the drain rate drops at the thermal limit.

## Build

```
make drain
```

The target pulls `libEGL.so` and `libGLESv3.so` from the device with `adb pull` and links against them. Set `ADB` to use another adb command.
