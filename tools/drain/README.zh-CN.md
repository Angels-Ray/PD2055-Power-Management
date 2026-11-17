# PD2055 快速耗电

以手机能维持的最快速度消耗电量。以 shell 用户运行时，运行 CPU 和 GPU 负载。以 root 运行时，还会设置 performance 调速器、屏幕、无线开关、手电筒和 wakelock。

[English](README.md)

## 使用

```
make drain
adb push build/drain /data/local/tmp/
adb shell
/data/local/tmp/drain [秒数]
```

不填秒数时一直运行，按 Ctrl-C 停止。

通过 `su` 运行以启用 root 功能：

```
su -c '/data/local/tmp/drain'
```

没有 root 时只运行 CPU 和 GPU 负载。

手机拔掉数据线时使用无线 adb。USB 连接会同时给电池充电。

设置 `TORCH=0` 可以关闭手电筒。设置 `GPU=0` 只运行 CPU：

```
GPU=0 TORCH=0 /data/local/tmp/drain
```

设置 `BATTERY_TEMP_STOP` 改变电池温度上限。单位是 0.1 °C，默认 460，即 46.0 °C。下面的 450 表示 45.0 °C：

```
BATTERY_TEMP_STOP=450 /data/local/tmp/drain
```

## 行为

以 shell 用户运行时：

- 在每个在线 CPU 上运行一个持续计算的进程。
- 通过 EGL 和 GLES 在离屏的 1920x1080 表面上运行片段着色器。

以 root 运行时：

- 把 CPU 调速器设为 performance。
- 屏幕亮度设为最大，休眠时间设为最大。
- 开启 Wi-Fi 和蓝牙。
- 每秒向 1.1.1.1 发送 5 个 ping。
- 把 `led:torch_0` 的亮度设为最大。
- 持有名为 `pd2055-drain` 的内核 wakelock。
- 每 10 s 输出一次电池状态。

程序不播放声音，不使用振动。

按 Ctrl-C 停止负载，并恢复 root 模式修改的设置。

电池温度达到 `BATTERY_TEMP_STOP` 时，程序停止负载并恢复 root 模式修改的设置。电池温度从 `/sys/class/power_supply/battery/temp` 读取。PD2055 上这个节点只有 root 可读，所以以 shell 用户运行时温度上限不生效。

## 写入

程序不写普通文件。

root 模式只写下面这些设置和 sysfs 文件：

- `settings`：`screen_brightness`、`screen_brightness_mode`、`screen_off_timeout`。
- `/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
- `/sys/class/leds/led:torch_0/brightness`
- `/sys/class/leds/led:switch_0/brightness`
- `/sys/power/wake_lock`、`/sys/power/wake_unlock`

程序通过 `svc` 改变 Wi-Fi 和蓝牙状态。

手机发热后会降频，耗电速度在温度限制处下降。

## 构建

```
make drain
```

目标用 `adb pull` 从设备取 `libEGL.so` 和 `libGLESv3.so` 作为链接输入。设置 `ADB` 可以使用其他 adb 命令。
