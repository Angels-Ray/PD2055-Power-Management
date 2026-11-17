# PD2055 Power Management

[English](README.md)

适用于 vivo iQOO Neo5（PD2055）的 KernelSU 模块。模块按设定的 SOC 停止充电。快充时模块设置 USB 输入电流上限。低电量直充时模块提高电池电流请求。模块按温度规则接管 CPU 频率、CPU 在线状态和充电电流。模块附带一个配置 app。

## 功能

- 停充上限：`charge_limit_soc`，达到这个 SOC 后停止充电。
- 输入电流上限：`max_input_ma`，按 SOC 和温度降额。
- 直充提速：`boost_ma`、`boost_cable_mohm`。
- 温度规则：`thermal_rule`，每条规则给出 CPU 频率、CPU 在线数和充电电流。
- CPU 频率接管：`thermal_cpu`，通过 cpufreq notifier 生效。
- CPU 在线状态：`thermal_online`，按当前规则管理 CPU5、CPU6、CPU7。
- 按温度限制充电电流：`thermal_charge`。
- 配置 app：`app/PD2055PowerManagement.apk`，单 Activity，无常驻服务。
- 暂停和恢复：`pause`、`resume`，KernelSU 动作按钮。
- 停止并恢复节点：`shutdown`。

## 已知限制

- `ctm_current_max` 只能降低输入电流上限。实际输入电流由充电器和厂商充电链路决定。
- AP 不能设置 VBUS 电压。它也不能强制充电器进入快充档位。
- 提速幅度受线阻和电荷泵 CV 限制。

## 安装

在 KernelSU 里安装 zip，然后用 KernelSU 软重启。`service.sh` 在开机时启动 `bin/pdcharge daemon`。

`customize.sh` 对 `app/PD2055PowerManagement.apk` 执行 `pm install -r`。安装失败时，从 zip 里取出 APK，在电脑上安装：

```
adb install -r PD2055PowerManagement.apk
```

`customize.sh` 把 APK 复制到 `/data/local/tmp` 时，在手机上安装：

```
adb shell pm install -r /data/local/tmp/PD2055PowerManagement.apk
```

第一次运行时，app 可能提示无法执行 `su`。在 KernelSU 的超级用户列表里给 PD2055 Power Management 授权。

## 配置

配置文件是 `/data/adb/pd2055-power-management/config.conf`。

| 键 | 默认值 | 范围 | 单位 | 说明 |
| --- | --- | --- | --- | --- |
| `enabled` | 1 | 0 或 1 | | 0 时撤销投票、提速和 CPU 在线控制 |
| `boost_enabled` | 1 | 0 或 1 | | 0 时关闭直充提速 |
| `boost_soc_max` | 40 | 0 到 100 | % | SOC 低于这个值时提速 |
| `boost_ma` | 6600 | 1000 到 6600 | mA | 提速时的电池电流请求 |
| `boost_cable_mohm` | 100 | 0 到 1000 | mOhm | 提速时 MCU 使用的线阻；0 时不写这个节点 |
| `max_input_ma` | 5000 | 100 到 5000 | mA | 输入电流上限 |
| `min_input_ma` | 500 | 100 到 `max_input_ma` | mA | 电池或主板到达停止温度时使用的输入电流 |
| `charge_limit_soc` | 100 | 50 到 100 | % | 达到这个 SOC 时停止充电；100 表示不限制 |
| `soc_mid` | 90 | 0 到 `charge_limit_soc` | % | 高于这个 SOC 且降额比例小于 100 时，输入限流乘降额比例 |
| `soc_mid_scale` | 100 | 10 到 100 | % | `soc_mid` 以上的降额比例；100 表示不降额 |
| `temp_derate_dc` | 420 | 200 到 600 | 0.1 °C | 达到这个电池温度后使用 `temp_derate_scale` |
| `temp_stop_dc` | 450 | `temp_derate_dc` + 10 到 700 | 0.1 °C | 使用 `min_input_ma` 的电池温度 |
| `board_temp_derate_dc` | 550 | 300 到 800 | 0.1 °C | 达到这个主板温度后使用 `temp_derate_scale` |
| `board_temp_stop_dc` | 600 | `board_temp_derate_dc` + 10 到 900 | 0.1 °C | 输入电流使用 `min_input_ma`、提速停止的主板温度 |
| `temp_derate_scale` | 60 | 10 到 100 | % | 电池或主板到达降额温度后，输入和提速目标乘这个比例 |
| `poll_active_ms` | 10000 | 1000 到 60000 | ms | 控制中的轮询间隔 |
| `poll_idle_ms` | 60000 | 1000 到 60000 | ms | 空闲时的轮询间隔 |
| `thermal_cpu` | 0 | 0 或 1 | | 1 时按当前规则接管 CPU 频率上限 |
| `thermal_charge` | 0 | 0 或 1 | | 1 时按当前规则的充电电流压低充电目标；0 时只按电池、主板温度阈值保护 |
| `thermal_online` | 0 | 0 或 1 | | 1 时按当前规则管理 CPU5、CPU6、CPU7 的在线状态 |
| `thermal_interval_ms` | 5000 | 1000 到 60000 | ms | 温控开启时的温度检测间隔 |
| `thermal_idle_interval_ms` | 15000 | `thermal_interval_ms` 到 60000 | ms | 电池未充电、温度比最低规则低超过 2.0 °C 时的温度检测间隔 |
| `thermal_rule` | 4 条 | 见下文 | | `温度,小核,大核,超大核,充电电流,大核在线数,超大核在线` |

`charge_limit_soc` 也接受旧键名 `soc_full`。

`thermal_rule` 字段：温度 [0.1 °C]，小核 [MHz]，大核 [MHz]，超大核 [MHz]，充电电流 [mA]，大核在线数 [1 到 3]，超大核在线 [0 或 1]。一条规则可以省略最后两个字段，此时 `big_online=3`、`prime_online=1`。有效范围是 300 到 750、300 到 2000、300 到 2600、300 到 3300、100 到 5000、1 到 3、0 到 1。最多 10 条规则。温度必须严格递增。超出范围或顺序错误的规则被忽略。配置文件里只要有一条 `thermal_rule`，文件里的规则就替换四条默认规则：

```
thermal_rule=400,1804,2419,2265,3000,3,1
thermal_rule=440,1612,2150,2073,2500,2,1
thermal_rule=480,1401,1862,1862,1800,2,0
thermal_rule=520,1171,1574,1632,1000,1,0
```

`big_online` 计的是 CPU4、CPU5、CPU6。守护进程只写 `cpu5/online` 和 `cpu6/online`，不动 CPU4。`prime_online` 写 `cpu7/online`。`thermal_online=0` 时，如果守护进程应用过在线状态，它向 CPU5、CPU6、CPU7 写 1。

配置文件变化后，守护进程在下一轮轮询时重读。`pdcharge reload` 让守护进程立即重读。

## 命令

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

- `status` 输出遥测、`ctm_current_max` 当前值、叠加温控上限后的策略目标和模块状态。`imax` 取 `usb/current_max`、`pc_port/current_max`、`usb/pd_current_max` 中的最大值，按 A 显示。这个值不是电池电流。直充时电池电流看 `ibat`。`dchg=1` 表示 `dchg_state` 读到 `dual chg normal.`。`boost=on` 表示 `set_ma` 大于 0。`module=off` 表示模块目录里有 `disable` 或 `remove` 标记。`daemon=running` 表示守护进程在运行。`limit` 是配置里的 `charge_limit_soc`。`keep` 是 `battery/keep_chg_soc` 读到的实际停充 SOC，100 表示不限制。`reason` 给出目标的原因，例如 `full`、`soc`、`thermal`、`protocol`。`thermal_cpu`、`thermal_charge` 或 `thermal_online` 为 1 时，它还输出温度区温度、当前规则和实际在线核数。
- `once` 应用一次决策后退出。它同时应用停充上限。值保留到 `shutdown`、下一次守护进程启动或重启。守护进程运行或模块禁用、卸载时，`once` 拒绝执行。它不改变 CPU 在线状态。
- `pause` 立即撤销投票、提速、停充上限和 CPU 在线状态，按 `poll_idle_ms` 轮询。
- `resume` 删除暂停标记，守护进程立即按目标控制。
- `reload` 让守护进程立即重读配置。重读前，它释放当前的 CPU 频率和在线状态。
- `shutdown` 停止守护进程，恢复保存的节点值，向 `cable_mohm` 写 0，卸载 `psset.ko`，释放 CPU 接管和停充投票。如果应用过在线状态，它把 CPU5、CPU6、CPU7 恢复为在线。
- `config-get` 按 `key=value` 行输出生效配置，每条规则输出七个字段。文件不存在时，它创建默认文件。
- `config-set` 从 stdin 读取完整配置。它在同一目录写 `<config>.tmp`，同步后改名为 `config.conf`，权限 0600。守护进程运行时，它发送 `SIGHUP`。它输出 `ok daemon=running` 或 `ok daemon=stopped`。
- `cpu-probe` 以 `probe=1`、`takeover=0` 加载 `pd2055_cpufreq.ko`。内核日志随后记录每个 `CPUFREQ_ADJUST` 写入者。
- `cpu-on LITTLE_MHZ BIG_MHZ PRIME_MHZ` 按需加载模块，把每个值取到 `scaling_available_frequencies` 中最近的一档，写入 `max0`、`max4`、`max7`，再写 `takeover=1` 并应用。`thermal_cpu=1` 时，守护进程在下一次规则变化时覆盖这些目标。
- `cpu-off` 写 `takeover=0` 并应用。PEM 重新控制 CPU，模块保持加载。
- `cpu-status` 输出模块参数、缓存温度、温度区、三个 `scaling_max_freq` 值、`cpus_offlined` 和实际的小核、大核、超大核在线数。
- `cpu-unload` 写 `takeover=0`，应用，然后卸载 `pd2055_cpufreq.ko`。

KernelSU 的动作按钮在 `pause` 和 `resume` 之间切换。

## App

模块 zip 包含 `app/PD2055PowerManagement.apk`，包名 `com.pd2055.charge`，版本 1.1.1。这个 APK 是普通 Android app，不是 LSPosed 模块。它只有一个 Activity，没有 service、receiver、定时任务、wakelock 和网络权限。

界面顶部是状态卡片。第一行显示 SOC、电池状态和是否快充。下面显示电池温度、主板温度、输入电压和输入电流上限、电池电流、当前目标和原因。再下面显示模块开关、守护进程、直充和提速状态。温控打开时，最后一行显示温度区温度和当前规则。

预设按钮 `全部关闭`、`日常`、`保养`、`快充` 只填写表单，点应用后才写入。`保养` 设置 `charge_limit_soc=80`、`max_input_ma=3000`，关闭直充提速。`快充` 把提速最高 SOC 设为 50。`日常` 使用不限制停充、5000 mA 上限和默认提速。

表单分 `充电` 和 `温控接管` 两部分。两个标题使用更大字号，`温控接管` 前有一条分隔线。每个开关和输入项各占一行圆角区块。开关项使用系统 `Switch` 控件，点击整行切换。`充电` 下分四个区块：开关与停充、输入限流、温度保护、直充提速。输入限流包含 `max_input_ma`、`soc_mid`、`soc_mid_scale`。温度保护包含电池和主板的降额、停止温度，以及 `temp_derate_scale`、`min_input_ma`。温度保护旁注明四个温度共用一个降额比例和一个停止限流。`充电` 下方注明输入电流上限和提速请求不是同一个量，温度保护同时调整两者，实际电池电流还受输入功率和充电器转换限制。直充提速旁注明只在直充且 SOC 低于提速最高 SOC 时生效。`温控接管` 注明规则温度取 `skin-therm` 外壳温度，不是电池温度，也不是主板温度。温度输入显示一位小数的 °C，写入时换算成 0.1 °C。温控规则也按 °C 编辑，每行一档。高级参数默认收起，包含轮询间隔、提速线阻和内核状态。

app 通过 `pdcharge config-get` 读取配置，通过 KernelSU 的 `su` 执行 `pdcharge config-set` 写入完整配置。app 写入所有键，所以配置文件里的注释不会保留。五字段规则写回时变成 `big_online=3`、`prime_online=1`。

app 在前台时每 10 s 执行一次 `pdcharge status` 刷新状态，退到后台后停止。它不保留进程，不开机启动。守护进程未运行时，配置照常写入，启动后生效。

守护进程每轮检查模块目录下的 `disable` 和 `remove` 标记。禁用或卸载后，守护进程在一个轮询间隔内撤销投票并释放提速。撤销标记后，守护进程恢复控制。进程不会退出。要停止进程，执行 `shutdown` 或软重启。

## 文件

- `/data/adb/pd2055-power-management/config.conf`：配置。
- `/data/adb/pd2055-power-management/logs/pdcharge.log`：日志。超过 128 KiB 时，守护进程启动时把它改名为 `pdcharge.log.1`。
- `/data/adb/pd2055-power-management/state/`：pid 文件、暂停标记、保存的节点值、`keep_soc_orig` 和 `online_applied` 标记。
- `app/PD2055PowerManagement.apk`：配置 app。
- `kmod/psset.ko`：直充提速使用的内核模块。
- `kmod/pd2055_cpufreq.ko`：CPU 频率接管使用的内核模块。
- 模块目录下的 `config.conf`：默认配置。`customize.sh` 在 `/data/adb/pd2055-power-management/config.conf` 不存在时复制过去。

## 工作原理

### 输入电流上限

快充指 `vbus` 至少 8 V，或 `charge_type` 为 `Fast`、`Turbo` 且 `current_max` 至少 1 A。直充的 `charge_type` 是 `N/A`，`current_max` 很低，所以快充由 `vbus` 条件识别。

输入电流上限只写 `/sys/class/power_supply/usb/ctm_current_max`，单位是 uA。内核以 `CTM_VOTER` 的名字把它加到 `USB_ICL` 投票表。`USB_ICL` 取所有投票中的最小值。

第一次写入前，模块保存节点原值。释放时，只有节点仍等于模块写入的值，才恢复原值。其他写入者改过节点时，模块保留当前值。

守护进程在以下情况撤销投票：

- `enabled=0`
- 暂停
- 模块禁用或卸载
- 充电器离线
- SOC 读不到
- SOC 达到或超过 `charge_limit_soc`
- 不是快充

目标值是 `max_input_ma`。SOC 高于 `soc_mid` 且 `soc_mid_scale` 小于 100 时，目标值按 `soc_mid_scale` 降额。电池温度达到 `temp_derate_dc` 或主板温度达到 `board_temp_derate_dc` 时，目标值乘 `temp_derate_scale`。结果限制在 `min_input_ma` 到 `max_input_ma`。温度达到停止阈值时，目标值为 `min_input_ma`。

### 停充上限

`/sys/class/cms_class/keep_soc` 是各写入者的投票表。写入格式是 `<投票号> <SOC>`。读到的内容包含 `vote:<SOC>` 和 `<投票号>:<SOC>` 项。模块使用 3 号投票。`/sys/class/power_supply/battery/keep_chg_soc` 显示投票生效后的值，100 表示不限制。vivo 的 `com.iqoo.powersaving` 优化充电也使用 3 号投票。

`enabled=1` 且 `charge_limit_soc` 小于 100 时，守护进程在启动、重读配置和每次充电轮询时写入 `3 <charge_limit_soc>`。第一次写入前，它把 3 号投票的当前值保存到 `state/keep_soc_orig`；没有这个投票时保存 -1。

释放时写回保存的值，-1 写成 0 删除投票。释放条件是 `enabled=0`、暂停、模块禁用或卸载、充电器离线、`charge_limit_soc=100` 和 `shutdown`。如果 3 号投票已被其他写入者改成别的值，释放跳过写入，日志记录 `keep_soc release skipped`。

守护进程启动时如果发现 `state/keep_soc_orig`，先按保存的值恢复投票。这用于进程被强制结束后的恢复。

停充只让充电器停止向电池充电，电量不会下降。SOC 已高于上限时，`status` 的 `reason` 是 `full` 或 `soc`，目标是 0。

### 温度规则

守护进程逐个读取 `/sys/class/thermal/thermal_zone*/type`，按 `skin-therm`、`case_therm`、`board_therm`、`quiet_therm`、`ap_ntc`、`quiet-therm-adc`、`mtktsAP` 的顺序取第一个匹配的温度区。所有温度区都读不到时，使用 `/sys/class/power_supply/battery/temp`，单位 0.1 °C。

本机取到的是 `thermal_zone85`，类型 `skin-therm`。它测的是机身后盖附近的外壳温度，不是电池温度，也不是主板充电温度。电池和主板阈值分别读 `/sys/class/power_supply/battery/temp` 和 `battery/primary_board_temp`。

`thermal_cpu=1`、`thermal_charge=1` 或 `thermal_online=1` 时，守护进程按温控间隔计算规则。电池正在充电、当前有规则生效，或温度距最低规则不超过 2.0 °C 时，使用 `thermal_interval_ms`。电池未充电、没有规则生效，且温度比最低规则低超过 2.0 °C 时，使用 `thermal_idle_interval_ms`。当前规则是温度不高于温度区读数的最后一条。温度降到当前规则温度以下 2.0 °C 后，才切换到更低的规则。低于最低规则时，同样要再降 2.0 °C 才释放。

`thermal_cpu=1` 时，当前规则的小核、大核、超大核频率映射到 `scaling_available_frequencies` 的条目。守护进程取绝对差最小的一档，差值相同时取较低的一档。下限取 `scaling_min_freq` 和 `cpuinfo_min_freq` 中较高者，上限是 `cpuinfo_max_freq`。低于最低规则或 `thermal_cpu=0` 时，守护进程写 `takeover=0` 和 `apply`。模块保持加载。

`thermal_charge=1` 时，当前规则的充电电流限制输入电流目标和直充提速目标。两者取自身目标和规则值的较小值，规则值大于自身目标时不提高目标。低于最低规则时不施加规则上限。`thermal_charge=0` 时，温控规则仍可管理 CPU 频率和在线状态，但规则里的充电电流不参与限流；电池和主板温度降额、停止阈值照常生效。日志在有效上限变化时输出 `thermal cap <mA>`，没有上限时输出 `thermal cap -1`。`status` 的 `reason` 在上限生效且低于自身目标时为 `thermal`。

`thermal_online=1` 时，当前规则的 `big_online` 和 `prime_online` 决定 CPU5、CPU6、CPU7 的在线状态。低于最低规则或 `thermal_online=0` 时，如果守护进程应用过在线状态，它把三个 CPU 恢复为在线。

`status` 输出一行 `thermal`，给出温度区温度、按该温度选出的七字段规则、规则里的 CPU 和充电取值，以及实际的小核、大核、超大核在线数。

### CPU 频率接管

`kmod/pd2055_cpufreq.ko` 注册 cpufreq policy notifier。`takeover=1` 时，每个 `CPUFREQ_ADJUST` 事件把 policy0、policy4、policy7 的上限设为 `max0`、`max4`、`max7`。PEM 对 `scaling_max_freq`、`scaling_min_freq` 的写入，以及 `msm_performance`、`cpu-boost` 在 `CPUFREQ_ADJUST` 里的调整，都经过这条链路。不需要 hook PEM 进程。

内核先执行 `CPUFREQ_ADJUST` 链，再执行 `CPUFREQ_INCOMPATIBLE` 链。内核温控回调在第二条链里，所以模块设置上限后，`skin-therm` 等温控区仍会继续降低频率。

接管期间，内核模块每 5 s 读一次温度区，写入 `temp_mc`。`takeover=0` 时不读温度区，每 30 s 运行一次。`takeover=1` 时，写 `apply` 先读一次温度区。模块先找 `skin-therm`，再依次找 `case_therm`、`board_therm`、`quiet_therm`、`ap_ntc`、`quiet-therm-adc`。温度达到或超过 `safety_temp_mc` 时，模块只降低上限。低于该温度时，上限设为目标值；需要时把 `min` 降到目标值。目标值为 0 时不改这个 cluster。

`cpu-on` 把请求的 MHz 取到 `scaling_available_frequencies` 中最近的一档，1612 MHz 会变成 1612800 kHz。差值相同时取较低的一档。写入 `max0`、`max4`、`max7` 的值单位是 kHz。其他值由 cpufreq 驱动向下取到可用频率。

写 `apply` 会调用 `cpufreq_update_policy(0)`、`cpufreq_update_policy(4)`、`cpufreq_update_policy(7)`。新目标立即生效，不必等 PEM 下一次写入。

| 节点 | 默认值 | 单位 | 说明 |
| --- | --- | --- | --- |
| `takeover` | 0 | | 1 时执行三个上限 |
| `probe` | 0 | | 1 时记录每个 `CPUFREQ_ADJUST` 写入者 |
| `max0` | 0 | kHz | policy0 上限；0 时不改这个 cluster |
| `max4` | 0 | kHz | policy4 上限；0 时不改这个 cluster |
| `max7` | 0 | kHz | policy7 上限；0 时不改这个 cluster |
| `safety_temp_mc` | 90000 | mC | 达到或超过这个温度时只降低上限 |
| `temp_mc` | | mC | 缓存的温度区读数；只读 |
| `zone_name` | 空 | | 指定温度区；空时使用内置列表 |
| `zone_used` | | | 加载时选中的温度区；只读 |
| `apply` | | | 写入后重新计算三个 policy |

参数位于 `/sys/module/pd2055_cpufreq/parameters/`。`probe=1` 记录如下格式的行：

```text
pd2055_cpufreq: adjust cpu=0 comm=writer pid=1234 uid=1000 min=... max=... user_max=... take=0 temp=...
```

日志有速率限制。用 `dmesg` 读取。

`cpu-off` 和 `cpu-unload` 在卸载前写 `takeover=0` 和 `apply`，恢复 PEM 最后一次写入的 `scaling_max_freq`。直接卸载模块会保留最后一次上限，直到下一次 cpufreq 更新。

notifier 不处理 `/sys/module/msm_thermal/core_control/cpus_offlined` 和 `/proc/pem/thml_*` 的写入。GPU 和 DDR 策略不受影响。内核温控区仍是最后的限制。

### CPU 在线状态

`thermal_online=1` 时，守护进程直接写 `/sys/devices/system/cpu/cpu5/online`、`cpu6/online` 和 `cpu7/online`。`big_online=1` 时 CPU5 和 CPU6 保持离线。`big_online=2` 时 CPU5 上线。`big_online=3` 时 CPU5 和 CPU6 上线。`prime_online` 控制 CPU7。CPU4 是第一个大核，模块不写它。CPU0 是启动 CPU，不能离线。

守护进程每次温控 tick 读这三个节点，只写与规则不同的节点。它不使用 `core_ctl`。在这台设备上，修改 `core_ctl/min_cpus` 和 `core_ctl/max_cpus` 没有改变在线状态。

以下情况，守护进程向 CPU5、CPU6、CPU7 写 1：`thermal_online=0`、温度低于最低规则、`pause`、模块禁用、`reload` 和 `shutdown`。只有守护进程应用过在线状态时才写。`reload` 在加载新配置前释放状态，所以规则变化时三个 CPU 会短暂全部在线。

守护进程应用在线状态时创建 `/data/adb/pd2055-power-management/state/online_applied`。下一次守护进程启动时，这个标记让它向 CPU5、CPU6、CPU7 写 1 并删除标记。这样可以从守护进程崩溃后留下核心离线的状态恢复。

### 直充提速

直充时 STM32L011 控制充电环路。写入 `ctm_current_max` 不能提高这条路径的电流。

直充时 AP 有两条路径提高电池电流。

`/sys/class/stm32_debug/cable_mohm` 设置 MCU 计算输入电流请求时使用的线阻。设置值低于实际线阻时，MCU 的请求升高。MCU 自己也会测量线阻。测试用的线实测线阻为 430 到 490 mΩ。

`mcu` 电源的属性 0x141 是电池电流请求，单位 mA，MCU 把它限制在 6600 mA。MCU 以 `USER_FCC` 投票使用这个值。内核没有暴露这个属性的节点。提速期间，`kmod/psset.ko` 每 500 ms 对 `mcu` 电源调用一次 `power_supply_set_property`。

提速在以下条件全部满足时运行：

- `enabled=1`
- `boost_enabled=1`
- 充电器在线
- `dchg_state` 为 `dual chg normal.`
- SOC 低于 `boost_soc_max`
- 电池温度和主板温度未达到停止阈值

请求值是 `boost_ma`。它写的是电池电流请求，不是 `max_input_ma` 控制的 USB 输入电流上限。电池温度达到 `temp_derate_dc` 或主板温度达到 `board_temp_derate_dc` 时，请求乘 `temp_derate_scale`。请求限制在 1000 到 6600 mA。实际电池电流还受输入功率、充电器转换和电池电压限制。`boost_cable_mohm` 为 0 时，模块只写 `set_ma`，不写 `cable_mohm`。

以下任一情况出现时，提速停止：

- 充电器离线
- 离开直充
- SOC 达到 `boost_soc_max`
- 温度达到停止阈值
- `enabled=0`
- `boost_enabled=0`
- 模块暂停、禁用或卸载

### 释放

释放时，如果提速改过 `cable_mohm`，模块先向它写 0。模块等待 15 s。然后模块向 `set_ma` 写 0。最后模块卸载 `psset.ko`。等待 15 s 是为了让 cms 在 FCC 覆盖结束前用实际线阻重新投票。没有改过线阻时，模块跳过这 15 s。

### 心跳

守护进程在每次提速轮询时写 `heartbeat`。提速期间轮询间隔最长 10 s。系统唤醒状态下，守护进程 30 s 没有写 `heartbeat` 时，内核模块把 `restore_ma` 的值写给 `mcu` 电源，6600 mA。设备挂起的时间不计入这 30 s。再次写 `heartbeat` 后，内核模块恢复覆盖。卸载 `psset.ko` 时，如果有生效的覆盖，它也会写入 6600 mA。

### 写入范围

普通文件只写 `/data/adb` 和 `/data/local/tmp` 下的路径。这两个目录以外，模块只写以下节点：

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

## 实测

在 PD2055 上使用原装 66W 充电器和非原装线，SOC 13% 到 27% 时实测：

| 设置 | 电池电流 | 输入电流请求 |
| --- | --- | --- |
| 无提速 | 2.8 A | 1500 mA |
| `boost_cable_mohm=100` | 3.4 到 3.7 A | 1900 到 2100 mA |
| `boost_cable_mohm=100`，`boost_ma` 为 4000 到 6000 | 约 3.4 A | 1800 mA |
| `boost_ma=1500` | 1.2 到 1.3 A | 750 mA |

线阻设为 100 mΩ 时，电池电压升到 8.95 到 9.03 V。电荷泵 CV 在约 9.0 V 限制电流。这个 SOC 下，高于 4000 mA 的请求不再增加电流。电池最高温度 36.0 °C。

SOC 42% 时，厂商链路请求 1400 mA 输入电流，电池电流降到约 2.6 A。默认 `boost_soc_max=40` 在这一点之前停止提速。

提速幅度取决于实际线阻。线阻更低的线会同时提高 MCU 请求和 `cable_mohm` 路径的电流。

## 耗电

控制中，守护进程每 `poll_active_ms` 读取电池、温度、电压和电流节点。空闲时，每 `poll_idle_ms` 检查一次充电状态。充电器从离线变为在线时，当轮循环立即检查一次充电状态，不等 `poll_idle_ms`。只有目标变化时，才写 `ctm_current_max`。提速期间，轮询间隔最长 10 s，`psset.ko` 每 500 ms 应用一次请求。`psset.ko` 在提速开始时加载，释放时卸载。

`thermal_cpu`、`thermal_charge` 或 `thermal_online` 为 1 时，循环按温控间隔唤醒一次，读取一个温度区。电池未充电、没有规则生效，且温度比最低规则低超过 2.0 °C 时，间隔是 `thermal_idle_interval_ms`；其他情况是 `thermal_interval_ms`。充电轮询仍按自己的间隔运行。三者都为 0 时，温控定时器不启用。`thermal_online=1` 时，每次温控 tick 读三个在线节点，只写与规则不同的节点。当前规则不变且上次应用成功时，温控 tick 复用上次映射的 kHz，不再读 `scaling_available_frequencies`、`cpuinfo_min_freq`、`scaling_min_freq` 和 `cpuinfo_max_freq`。

`pd2055_cpufreq.ko` 在接管期间每 5 s 读一次温度区。`takeover=0` 时每 30 s 运行一次，不读温度区。`probe=1` 时每次 `CPUFREQ_ADJUST` 写一条内核日志，日志受 `printk_ratelimit` 限制，只在诊断时打开。

每轮还用 `stat` 和 `access` 检查配置文件和模块标记。这些检查搭在已有的唤醒上，不增加定时器。

app 只在前台时每 10 s 执行一条 `status` 命令刷新状态，退到后台后停止。读取和写入各执行一条 root 命令后退出。app 不增加定时器。

`nanosleep` 不持有 wakelock。设备挂起时，定时器不唤醒 SoC。唤醒后，轮询继续。

## 构建

```
make
make build/pdcharge-host
make kmod
make app
make package
```

`make` 构建静态 aarch64 的 `build/pdcharge`。`make build/pdcharge-host` 构建本机测试用的可执行文件。`make kmod` 构建 `build/psset.ko` 和 `build/pd2055_cpufreq.ko`。`make app` 构建 `build/PD2055PowerManagement.apk`。`make package` 构建二进制、两个内核模块和 app。它生成 `build/PD2055-Power-Management-<version>.zip`。内核模块使用 PD2055 仓库里的 `work/kernel-devsrc`、`work/kernel-4.19.125-build` 和 `work/kheaders/tree` 构建。设置 `KSRC`、`KOBJ` 或 `KHDR` 可以使用其他目录。

app 构建使用 `$HOME/opt` 下的 JDK 17 `jdk17/jdk-17.0.21+9`、Android SDK build-tools 34.0.0，签名用 `$HOME/.android/debug.keystore`。设置 `JAVA_HOME`、`ANDROID_HOME` 或 `KEYSTORE` 可以使用其他路径。

构建默认使用 Zig 0.17.1，路径是 `$HOME/opt/zig/zig`。设置 `ZIG=/path/to/zig` 可以使用其他路径。
