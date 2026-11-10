# PD2055 快速耗电

以手机能维持的最快速度消耗电量。`drain.sh` 需要 root；`drain-cpu.sh` 以 shell 用户运行。

[English](README.md)

## 使用

```
adb push drain.sh /data/local/tmp/
adb shell
su
sh /data/local/tmp/drain.sh [秒数]
```

不填秒数时一直运行，按 Ctrl-C 停止。

手机拔掉数据线时使用无线 adb。USB 连接会同时给电池充电。

设置 `TORCH=0` 可以关闭手电筒：

```
TORCH=0 sh /data/local/tmp/drain.sh
```

## 只占用 CPU

`drain-cpu.sh` 在每个在线 CPU 上运行一个 `yes` 进程。不需要 root，不修改设置，不写文件。CPU 频率仍受系统调速器和温度限制。

```
adb push drain-cpu.sh /data/local/tmp/
adb shell
sh /data/local/tmp/drain-cpu.sh [秒数]
```

## 行为

- `drain.sh` 把所有在线 CPU 设为 performance 调速器，每个在线 CPU 运行一个 `yes` 进程。
- 屏幕亮度设为最大，休眠时间设为最大。
- 开启 Wi-Fi 和蓝牙，每秒向 1.1.1.1 发送 5 个 ping。
- 以最大电流打开 `led:torch_0`。
- 持有名为 `pd2055-drain` 的内核 wakelock。
- 每 10s 输出一次电池状态。

工具不播放声音，不使用振动。

按 Ctrl-C 恢复 CPU 调速器、屏幕设置、无线开关、手电筒和 wakelock。

## 写入

`drain.sh` 只写下面这些设置和 sysfs 文件：

- `settings`：`screen_brightness`、`screen_brightness_mode`、`screen_off_timeout`。
- `/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
- `/sys/class/leds/led:torch_0/brightness`
- `/sys/class/leds/led:switch_0/brightness`
- `/sys/power/wake_lock`、`/sys/power/wake_unlock`

`drain.sh` 通过 `svc` 改变 Wi-Fi 和蓝牙状态，不创建文件。

手机发热后会降频，耗电速度在温度限制处下降。
