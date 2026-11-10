# PD2055 Charge

[English](README.md)

适用于 vivo iQOO Neo5（PD2055）的 KernelSU 模块。模块在快充时限制 USB 输入电流。

## 控制点

PD2055 通过厂商充电链路给电池充电：STM32L011 MCU、BQ25790 和 vivo cms。`force_main_fcc` 这类电池侧电流节点在 AP 上不可设置。写入返回成功，硬件不变。

模块只写一个控制节点，`/sys/class/power_supply/usb/ctm_current_max`，单位是 uA。内核通过 `USB_ICL` 投票表上的 `CTM_VOTER` 应用这个值。写入 -1 撤销投票。vivo 充电链路还会把这个值同步到 BQ25790 的输入电流限制寄存器，所以 20V 快充路径也受这个限制。

第一次写入前，模块保存节点原值。释放时，只有节点仍等于模块写入的值，才恢复原值。其他写入者改过节点时，模块保留当前值。

模块不写 `/data/adb` 和 `/data/local/tmp` 之外的普通文件。这两个目录之外，模块只写 `/sys/class/power_supply/usb/ctm_current_max`。

## 文件

- `/data/adb/pd2055-charge/config.conf`：配置。
- `/data/adb/pd2055-charge/logs/pdcharge.log`：日志。守护进程启动时，超过 128 KiB 的日志改名为 `pdcharge.log.1`。
- `/data/adb/pd2055-charge/state/`：pid 文件、暂停标记、保存的节点值。
- 模块目录下的 `config.conf`：默认配置。`/data/adb/pd2055-charge/config.conf` 不存在时，`customize.sh` 把它复制过去。

## 安装

在 KernelSU 里安装 zip，然后重启。`service.sh` 在开机时启动守护进程。模块包含 `bin/pdcharge`、`module.prop`、`service.sh`、`action.sh`、`customize.sh`、`uninstall.sh` 和 `config.conf`。

## 配置

| 键 | 默认值 | 范围 | 说明 |
| --- | --- | --- | --- |
| `enabled` | 1 | 0 或 1 | 启用限流 |
| `max_input_ma` | 3300 | 100 到 3300 | 输入电流上限 [mA] |
| `min_input_ma` | 500 | 100 到 `max_input_ma` | 温度到达停止阈值时使用的限流值 [mA] |
| `soc_full` | 85 | 50 到 100 | SOC 达到或超过这个值时释放 [%] |
| `soc_mid` | 75 | 0 到 `soc_full` | 高于这个 SOC 时缩放限流值 [%] |
| `soc_mid_scale` | 70 | 10 到 100 | 高于 `soc_mid` 时的缩放比例 [%] |
| `temp_derate_dc` | 360 | 200 到 600 | 目标值第一次乘 60% 的电池温度 [0.1 C] |
| `temp_stop_dc` | 420 | `temp_derate_dc` + 10 到 700 | 使用 `min_input_ma` 的电池温度 [0.1 C] |
| `board_temp_derate_dc` | 520 | 300 到 800 | 目标值第一次乘 60% 的主板温度 [0.1 C] |
| `board_temp_stop_dc` | 600 | `board_temp_derate_dc` + 10 到 900 | 使用 `min_input_ma` 的主板温度 [0.1 C] |
| `poll_active_ms` | 10000 | 1000 到 60000 | 控制中的轮询间隔 [ms] |
| `poll_idle_ms` | 60000 | 1000 到 60000 | 空闲时的轮询间隔 [ms] |

当 `enabled=0`、SOC 读不到、SOC 达到或超过 `soc_full`、或充电不是快充时，守护进程撤销投票。`charge_type` 为 `Fast` 或 `Turbo` 时，`current_max` 至少 1A 或 `vbus` 至少 8V 即视为快充。其他充电类型需要同时满足 `current_max` 至少 1A 和 `vbus` 至少 8V。

目标值是 `max_input_ma`。SOC 高于 `soc_mid` 时按 `soc_mid_scale` 缩放。每达到一个降额阈值再乘 60%。结果限制在 `min_input_ma` 到 3300mA。到达 `temp_stop_dc` 或 `board_temp_stop_dc` 时，目标值为 `min_input_ma`。

配置文件变化后，守护进程自动重读。检查是每轮一次 `stat`，不增加定时器。`stat` 没有变化时，文件不打开，也不解析。新值在一个轮询间隔内生效。`pdcharge reload` 让配置立即生效。

## 输入电流与功率

原装 66W 充电器 V6650L0A0-CN 提供 5V/2A、9V/2A、11V/3A、11V/5A 和 20V/3.3A。PD2055 在 20V/3.3A 达到 66W。BQ25790 的输入电流限制最大 3.3A，所以大于 3300mA 的值没有额外效果。

20V 输入下，配置的限流值对应功率：

| `max_input_ma` | 20V 下的功率 |
| --- | --- |
| 1000 | 约 20W |
| 2000 | 约 40W |
| 2500 | 约 50W |
| 3000 | 约 60W |
| 3300 | 约 66W |

## 命令

```
MOD=/data/adb/modules/pd2055_charge
$MOD/bin/pdcharge status
$MOD/bin/pdcharge once
$MOD/bin/pdcharge pause
$MOD/bin/pdcharge resume
$MOD/bin/pdcharge reload
$MOD/bin/pdcharge shutdown
```

- `status` 输出遥测、当前 `ctm_current_max` 值、策略目标和模块开关状态。
- `once` 应用一次决策后退出。值保留到 `shutdown`、下一次守护进程启动或重启。守护进程在运行或模块处于关闭状态时，`once` 拒绝执行。
- `pause` 立即撤销投票，并按 `poll_idle_ms` 轮询。
- `resume` 删除暂停标记，守护进程立即按目标投票。
- `reload` 让守护进程立即重读配置。
- `shutdown` 停止守护进程，并恢复保存的节点值。

KernelSU 的动作按钮在 `pause` 和 `resume` 之间切换。

KernelSU 没有模块开关的运行时回调。守护进程每轮检查 `/data/adb/modules/pd2055_charge` 下的 `disable` 和 `remove` 标记。禁用或卸载模块后，守护进程在一个轮询间隔内撤销投票。撤销禁用或卸载后，守护进程恢复投票。进程继续运行。要停止进程，执行 `shutdown` 或重启。软重启不结束原生进程。

## 耗电

充电时，守护进程每个 `poll_active_ms` 读取约 8 个小 sysfs 文件。空闲时，每个 `poll_idle_ms` 读取一次 `battery/status`。只有目标变化时，才写节点。

每轮还会用 `stat` 和 `access` 检查配置文件和 KernelSU 标记。这些检查搭在已有的唤醒上，不增加定时器，也不增加唤醒。

`nanosleep` 不持有 wakelock。设备挂起时，定时器不会唤醒 SoC。唤醒后，轮询继续。

## 构建

```
make                    # build/pdcharge，静态 aarch64
make build/pdcharge-host
make package            # build/PD2055-Charge-<version>.zip
```

构建默认使用 `$HOME/opt/zig/zig` 下的 Zig 0.17.1。设置 `ZIG=/path/to/zig` 可以使用其他路径。
