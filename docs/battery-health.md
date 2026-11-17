# 电池最大容量

设置 → 电池 → 电池健康与充电 → 电池最大容量 的数值由 `com.iqoo.powersaving` 的 `BatteryHealthSettingsActivity.s()` 在页面刷新时按 cycle 查表计算。

## 输入

| 名称 | 来源 | 缺失或读取失败 |
| --- | --- | --- |
| cycle | `k.b.a()` 读取 `/sys/class/fuelsummary/cycle` | 返回 -2 |
| soh | `k.b.b()` 读取 `/sys/class/fuelsummary/soh` | 返回 -1 |
| support | `y.g.h()` 读取 `persist.vivo.battery_health_support` | 返回 0 |
| curve | `y.g.a()` 读取 `persist.vivo.battery_health_curve` | 返回 0 |
| older_switch | Secure 设置 `battery_health_value_older_switch` | `k.a.a()` 使用 0，`k.a.h()` 使用 -1 |
| older | Secure 设置 `battery_health_value_older` | 0 |
| older_save | Secure 设置 `battery_health_value_older_save` | -1 |

`k.b.a()` 和 `k.b.b()` 用 `Integer.parseInt` 解析节点内容。文件不存在、内容为空、内容不是整数都进入失败分支。

`cycle` 由 fuelsummary 提供，值来自 BQ28Z610 电量计。`/sys/class/power_supply/bq_bms/cycle_count` 和 `/sys/class/power_supply/battery/cycle_count` 是同一个计数。

## 计算流程

`k.a.b()` 读取三个输入，然后调用 `k.a.c()`。

```
k.a.b():
    cycle = k.b.a()
    soh = k.b.b()
    support = y.g.h()
    return k.a.c(cycle, soh, support)
```

`k.a.c()` 检查 support。

```
k.a.c(cycle, soh, support):
    if support == 0:
        return -1
    if support == 1 or support == 2:
        return k.a.a(cycle, soh, support)
    return -1
```

support 为 0 或其他值时返回 -1。support 为 1 或 2 时进入 `k.a.a()`。

`k.a.a()` 处理校准状态、cycle 为 0 的情况和 curve 选择。

```
k.a.a(cycle, soh, support):
    if cycle == -1 and support == 2:
        return -2
    if cycle == 0:
        return 100

    curve = y.g.a()
    older_switch = Settings.Secure.getInt(..., "battery_health_value_older_switch", 0)

    if curve == 300:
        return k.a.d(table_d, cycle, soh)
    if curve == 301:
        return k.a.d(table_c, cycle, soh)
    if curve == 302:
        if older_switch != 1:
            return k.a.e(cycle, 1, soh)
        return k.a.d(table_d, cycle, soh)
    if curve == 303:
        if older_switch != 1:
            return k.a.e(cycle, 2, soh)
        return k.a.d(table_c, cycle, soh)
    return k.a.d(table_b, cycle, soh)
```

cycle 为 -1 且 support 为 2 时返回 -2，界面显示校准中。cycle 为 0 时返回 100。

support 为 1 或 2 时，cycle 读取失败会得到 -2，不是 -1。这个值不满足校准条件，会继续查表，最后返回 soh。

## 查表

```
k.a.d(table, cycle, soh):
    i = 0
    while i < len(table):
        if cycle > table[i]:
            return i
        i += 1
    return soh
```

table 有 101 项，按降序排列，最后一项是 0。返回值是 0 到 100 的整数，作为百分比使用。

cycle 大于 0 时，返回值等于表中大于等于 cycle 的阈值个数。循环在最后一项之前返回，soh 不参与结果。cycle 小于等于 0 时，循环走到末尾，返回 soh。cycle 为 0 时 `k.a.a()` 已经返回 100，不会调用 `k.a.d()`。

## 曲线表

三张表来自 `Lk/a;-><clinit>`。表项是 cycle 阈值，单位是次，不是百分比。

表 b 是默认表。curve 为空、0 或其他未列出的值时使用表 b。

```
CURVE_B = [
    2551, 2535, 2519, 2503, 2487, 2471, 2455, 2439, 2423, 2407,
    2391, 2375, 2359, 2343, 2327, 2311, 2295, 2279, 2263, 2247,
    2231, 2215, 2199, 2183, 2167, 2151, 2135, 2119, 2103, 2087,
    2071, 2055, 2039, 2023, 2007, 1991, 1975, 1959, 1943, 1927,
    1911, 1895, 1879, 1863, 1847, 1831, 1815, 1799, 1783, 1767,
    1751, 1735, 1719, 1703, 1687, 1671, 1655, 1618, 1599, 1577,
    1537, 1504, 1464, 1430, 1394, 1358, 1325, 1290, 1255, 1222,
    1187, 1153, 1119, 1086, 1057, 1024, 992, 958, 927, 896,
    866, 836, 805, 773, 740, 706, 671, 635, 598, 561,
    522, 485, 448, 410, 370, 328, 282, 231, 171, 101,
    0,
]
```

curve 为 301 时使用表 c。

```
CURVE_C = [
    2553, 2552, 2550, 2549, 2546, 2544, 2541, 2538, 2535, 2532,
    2528, 2524, 2520, 2516, 2511, 2506, 2501, 2495, 2489, 2483,
    2477, 2470, 2463, 2456, 2448, 2440, 2432, 2424, 2415, 2406,
    2396, 2386, 2376, 2365, 2354, 2343, 2331, 2319, 2307, 2294,
    2281, 2267, 2253, 2239, 2224, 2208, 2192, 2176, 2159, 2142,
    2124, 2105, 2086, 2067, 2047, 2026, 2005, 1983, 1961, 1937,
    1914, 1889, 1864, 1838, 1811, 1784, 1755, 1726, 1696, 1665,
    1633, 1600, 1567, 1532, 1496, 1459, 1421, 1382, 1341, 1300,
    1256, 1211, 1165, 1118, 1069, 1019, 968, 915, 861, 805,
    747, 688, 627, 564, 498, 429, 357, 281, 201, 115,
    0,
]
```

curve 为 300 时使用表 d。

```
CURVE_D = [
    1685, 1684, 1684, 1684, 1684, 1683, 1683, 1682, 1681, 1681,
    1680, 1679, 1677, 1676, 1675, 1673, 1672, 1670, 1668, 1666,
    1664, 1661, 1659, 1656, 1653, 1650, 1647, 1644, 1640, 1637,
    1633, 1629, 1625, 1620, 1616, 1611, 1606, 1600, 1595, 1589,
    1583, 1577, 1570, 1563, 1556, 1548, 1541, 1533, 1524, 1515,
    1506, 1497, 1487, 1476, 1466, 1454, 1443, 1431, 1418, 1405,
    1391, 1377, 1362, 1347, 1331, 1314, 1297, 1279, 1261, 1241,
    1221, 1200, 1178, 1156, 1132, 1108, 1082, 1056, 1028, 1000,
    970, 939, 907, 874, 839, 803, 766, 727, 687, 645,
    601, 556, 509, 460, 409, 355, 299, 240, 177, 104,
    0,
]
```

curve 为 302 和 303 时先比较表 b 与新表，见下一节。

`table_b`、`table_c`、`table_d` 分别对应 `Lk/a;->b`、`Lk/a;->c`、`Lk/a;->d`。

## 旧值比较

`k.a.e()` 只在 curve 为 302 或 303 且 older_switch 不等于 1 时调用。

```
k.a.e(cycle, type, soh):
    old = k.a.d(table_b, cycle, soh)
    if type == 2:
        new = k.a.d(table_c, cycle, soh)
    else:
        new = k.a.d(table_d, cycle, soh)

    if old == new:
        Settings.Secure.putInt(..., "battery_health_value_older_switch", 1)
        return old
    if old > new:
        return old
    if k.a.h(context):
        k.a.i(context)
        return -2

    older = Settings.Secure.getInt(..., "battery_health_value_older", 0)
    if older == 0:
        k.a.i(context)
        return -2
    if older < new:
        return older

    Settings.Secure.putInt(..., "battery_health_value_older_switch", 1)
    return new
```

type 为 1 时比较表 b 和表 d，对应 curve 302。type 为 2 时比较表 b 和表 c，对应 curve 303。

`k.a.h()` 在 `battery_health_value_older_switch` 和 `battery_health_value_older_save` 都等于 -1 时返回 true。读取这两个键的默认值是 -1。

`k.a.i()` 把 Secure 设置 `value_update_battery_node` 加 1。

`battery_health_value_older_save` 只参与 `k.a.h()` 的判断，`k.a.e()` 不读它。

older 的默认值是 0。读到 0 时 `k.a.e()` 返回 -2。

## 显示

页面读取 `y.h.g()` 的返回值，然后按下面的规则显示。

```
value = y.h.g(this)

if value < 0 or value > 100:
    if value == -2:
        summary = "校准中"
        summaryEx = "系统正在重新校准“电池最大容量”，此过程可能需要数周时间完成。"
    else:
        # 移除 preference_battery_health_key 对应的项
        this.p("preference_battery_health_key")
elif value < 80:
    if value < 60:
        summary = "低于60%"
        summaryEx = "建议前往官方服务网点检测或更换电池。"
        action = "查看服务网点"
    else:
        summary = "{value}%"
        summaryEx = "建议前往官方服务网点检测或更换电池。"
else:
    summary = "{value}%"
    summaryEx = "电池容量状态良好。"
```

`k.a.f()` 返回 60，`k.a.g()` 返回 80。

低于 60 时，格式化字符串 `battery_health_view_less_than_sixty_percent` 是 `低于%s`，参数是 `k.a.f()` 的返回值 60。显示结果是 `低于60%`，不是 `低于{value}%`。

60 到 79 显示具体百分比，提示去服务网点。80 到 100 显示具体百分比，提示电池状态良好。

返回 -1、其他负数或大于 100 时，页面移除电池最大容量这一项。返回 -2 时显示校准中。

## 示例

以表 b、soh=52%、support=1、curve=0 为例。soh 只影响 cycle 小于 0 的情况。

| cycle | `k.a.d()` 返回 | 页面显示 |
| --- | --- | --- |
| 1577 | 60 | 60% |
| 1578 | 59 | 低于60% |
| 1618 | 58 | 低于60% |
| 1619 | 57 | 低于60% |
| 1655 | 57 | 低于60% |
| 1656 | 56 | 低于60% |
| 2551 | 1 | 低于60% |
| 2552 | 0 | 低于60% |

在表 b 中，cycle 每超过一个阈值，返回值减 1。表 b 在 1577 到 1578 之间跨过 60 的分界。

curve 为 300、cycle 为 1642 时表 d 返回 28。curve 为 301、cycle 为 1642 时表 c 返回 70。curve 为 0、cycle 为 1642 时表 b 返回 57。

## 显示值与 SOH

设置页的百分比来自 cycle 查表。cycle 大于 0 时，soh 不参与计算。这个百分比不能当作电池 SOH。

| 节点 | 含义 |
| --- | --- |
| `/sys/class/power_supply/bms/soh` | PM8150B 电量计的 SOH，百分比 |
| `/sys/class/fuelsummary/soh` | fuelsummary 的 SOH，百分比 |
| `/sys/class/fuelsummary/age_amount` | 当前满充容量，mAh |
| `/sys/class/fuelsummary/amount_total` | 设计容量，mAh |
| `/sys/class/power_supply/battery/bat_fcc` | 电池满充容量，mAh |
| `/sys/class/power_supply/bq_bms/charge_full_design` | BQ28Z610 的设计容量，mAh |

容量比可以用 `age_amount / amount_total` 或 `bat_fcc / charge_full_design` 计算。

`bms/charge_full` 与 `bms/charge_full_design` 的比值和上面几项不一致。`battery/charge_full` 等于 `battery/charge_full_design`。这两组读数与上面几项的容量比不一致。

在设备 shell 中执行：

```
cat /sys/class/fuelsummary/cycle
cat /sys/class/power_supply/bms/soh
cat /sys/class/fuelsummary/soh
cat /sys/class/fuelsummary/age_amount
cat /sys/class/fuelsummary/amount_total
cat /sys/class/power_supply/battery/bat_fcc
cat /sys/class/power_supply/bq_bms/charge_full_design
```

如果提示权限不足，在 `su -c` 下执行。

## 来源

- APK：`/system/app/IqooPowerSaving/IqooPowerSaving.apk`，`com.iqoo.powersaving` 9.4.6.3。
- 计算类：`Lk/a;`，方法 `a`、`b`、`c`、`d`、`e`、`f`、`g`、`h`、`i`。
- 节点读取：`Lk/b;` 的 `a()` 和 `b()`。
- 属性和页面：`Ly/g;`、`Ly/h;`、`com.iqoo.powersaving.BatteryHealthSettingsActivity`。

APK 更新后，曲线表和字符串资源可能变化。
