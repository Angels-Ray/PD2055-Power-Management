package com.pd2055.powermanagement;

import android.app.Activity;
import android.content.res.Configuration;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MainActivity extends Activity {
    private static final String PDCHARGE =
            "/data/adb/modules/pd2055_power_management/bin/pdcharge";
    private static final int ROOT_TIMEOUT_S = 30;
    private static final long STATUS_INTERVAL_MS = 10000;
    private static final Pattern STATUS_PAIR =
            Pattern.compile("(\\w+)=([^=]*?)(?=\\s+\\w+=|$)");

    private final List<FieldSpec> fieldSpecs = new ArrayList<>();
    private final Map<String, EditText> fields = new LinkedHashMap<>();

    private Switch enabled;
    private Switch boostEnabled;
    private Switch thermalCpu;
    private Switch thermalCharge;
    private Switch thermalOnline;
    private EditText rulesInput;
    private TextView statusHeadline;
    private TextView statusDetail;
    private TextView statusModule;
    private TextView statusThermal;
    private TextView kernelText;
    private TextView advancedToggle;
    private LinearLayout advancedContainer;
    private Button readButton;
    private Button applyButton;
    private Handler handler;
    private ExecutorService executor;
    private volatile boolean busy;
    private volatile boolean foreground;
    private volatile boolean statusBusy;

    private final Runnable refreshRunnable = new Runnable() {
        @Override
        public void run() {
            if (!foreground) {
                return;
            }
            refreshStatus();
            handler.postDelayed(this, STATUS_INTERVAL_MS);
        }
    };

    private static final class FieldSpec {
        final String key;
        final String label;
        final double min;
        final double max;
        final boolean temp;

        FieldSpec(String key, String label, double min, double max,
                  boolean temp) {
            this.key = key;
            this.label = label;
            this.min = min;
            this.max = max;
            this.temp = temp;
        }
    }

    private static final class Result {
        final int code;
        final String output;

        Result(int code, String output) {
            this.code = code;
            this.output = output;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        executor = Executors.newSingleThreadExecutor();
        handler = new Handler(Looper.getMainLooper());
        setContentView(buildView());
        readConfig();
    }

    @Override
    protected void onResume() {
        super.onResume();
        foreground = true;
        handler.removeCallbacks(refreshRunnable);
        handler.postDelayed(refreshRunnable, STATUS_INTERVAL_MS);
    }

    @Override
    protected void onPause() {
        foreground = false;
        handler.removeCallbacks(refreshRunnable);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        executor.shutdownNow();
        super.onDestroy();
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }

    private TextView textView(String text, float size, boolean bold) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(size);
        if (bold) {
            view.setTypeface(Typeface.DEFAULT_BOLD);
        }
        return view;
    }

    private TextView section(String text) {
        TextView view = textView(text, 16, true);
        view.setPadding(0, dp(20), 0, dp(4));
        return view;
    }

    private TextView majorSection(String text) {
        TextView view = textView(text, 20, true);
        view.setPadding(0, dp(22), 0, dp(6));
        return view;
    }

    private TextView subSection(String text) {
        TextView view = textView(text, 15, true);
        view.setPadding(0, dp(16), 0, dp(6));
        view.setAlpha(0.9f);
        return view;
    }

    private View divider() {
        int mode = getResources().getConfiguration().uiMode
                & Configuration.UI_MODE_NIGHT_MASK;
        View view = new View(this);
        view.setBackgroundColor(mode == Configuration.UI_MODE_NIGHT_YES
                ? 0x33FFFFFF : 0x22000000);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(1));
        params.topMargin = dp(18);
        view.setLayoutParams(params);
        return view;
    }

    private TextView note(String text) {
        TextView view = textView(text, 12, false);
        view.setPadding(dp(6), dp(2), dp(6), dp(8));
        view.setAlpha(0.7f);
        return view;
    }

    private int surfaceColor() {
        int mode = getResources().getConfiguration().uiMode
                & Configuration.UI_MODE_NIGHT_MASK;
        return mode == Configuration.UI_MODE_NIGHT_YES
                ? 0x1FFFFFFF : 0x0D000000;
    }

    private int fieldColor() {
        int mode = getResources().getConfiguration().uiMode
                & Configuration.UI_MODE_NIGHT_MASK;
        return mode == Configuration.UI_MODE_NIGHT_YES
                ? 0x26FFFFFF : 0x12000000;
    }

    private GradientDrawable rounded(int color, int radiusDp) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(color);
        background.setCornerRadius(dp(radiusDp));
        return background;
    }

    private LinearLayout block() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(9), dp(10), dp(9));
        row.setBackground(rounded(surfaceColor(), 10));
        return row;
    }

    private LinearLayout.LayoutParams blockParams() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        params.bottomMargin = dp(6);
        return params;
    }

    private Switch addSwitch(LinearLayout parent, String label) {
        LinearLayout row = block();

        TextView text = textView(label, 15, false);
        row.addView(text, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Switch toggle = new Switch(this);
        toggle.setShowText(false);
        row.addView(toggle, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        row.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                toggle.toggle();
            }
        });

        parent.addView(row, blockParams());
        return toggle;
    }

    private void addField(LinearLayout parent, FieldSpec spec) {
        fieldSpecs.add(spec);

        LinearLayout row = block();

        TextView label = textView(spec.label, 14, false);
        row.addView(label, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        EditText input = new EditText(this);
        input.setInputType(spec.temp
                ? InputType.TYPE_CLASS_NUMBER
                        | InputType.TYPE_NUMBER_FLAG_DECIMAL
                : InputType.TYPE_CLASS_NUMBER);
        input.setSingleLine(true);
        input.setTextSize(15);
        input.setGravity(Gravity.END);
        input.setBackground(rounded(fieldColor(), 8));
        input.setPadding(dp(10), dp(6), dp(10), dp(6));
        input.setMinHeight(dp(38));
        row.addView(input, new LinearLayout.LayoutParams(
                dp(118), ViewGroup.LayoutParams.WRAP_CONTENT));

        fields.put(spec.key, input);
        parent.addView(row, blockParams());
    }

    private LinearLayout statusCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(14), dp(12), dp(14), dp(12));
        card.setBackground(rounded(surfaceColor(), 12));
        return card;
    }

    private Button presetButton(String text) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextSize(12);
        button.setAllCaps(false);
        button.setPadding(0, 0, 0, 0);
        return button;
    }

    private View buildView() {
        LinearLayout outer = new LinearLayout(this);
        outer.setOrientation(LinearLayout.VERTICAL);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        outer.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(16), dp(16), dp(16));
        scroll.addView(root);

        LinearLayout card = statusCard();
        LinearLayout.LayoutParams cardParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        cardParams.topMargin = dp(10);
        root.addView(card, cardParams);

        statusHeadline = textView("读取中…", 20, true);
        card.addView(statusHeadline);

        statusDetail = textView("", 13, false);
        statusDetail.setLineSpacing(0, 1.15f);
        statusDetail.setPadding(0, dp(6), 0, 0);
        card.addView(statusDetail);

        statusModule = textView("", 12, false);
        statusModule.setPadding(0, dp(5), 0, 0);
        card.addView(statusModule);

        statusThermal = textView("", 12, false);
        statusThermal.setPadding(0, dp(4), 0, 0);
        card.addView(statusThermal);

        LinearLayout presetCard = block();
        presetCard.setOrientation(LinearLayout.VERTICAL);
        presetCard.setGravity(Gravity.NO_GRAVITY);

        presetCard.addView(textView("预设", 14, true));

        LinearLayout presets = new LinearLayout(this);
        presets.setOrientation(LinearLayout.HORIZONTAL);
        String[] names = { "全部关闭", "日常", "保养", "快充" };
        for (int i = 0; i < names.length; i++) {
            final int index = i;
            Button button = presetButton(names[i]);
            button.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View view) {
                    applyPreset(index);
                }
            });
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            params.setMargins(dp(2), 0, dp(2), 0);
            presets.addView(button, params);
        }
        LinearLayout.LayoutParams presetsParams =
                new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT);
        presetsParams.topMargin = dp(8);
        presetCard.addView(presets, presetsParams);

        LinearLayout.LayoutParams presetCardParams = blockParams();
        presetCardParams.topMargin = dp(12);
        root.addView(presetCard, presetCardParams);
        root.addView(note("预设只改表单，点应用后写入。"));

        root.addView(majorSection("充电"));
        root.addView(note("输入电流上限限制充电器输入电流，提速请求电池电流。"
                + "温度保护同时调整两者：到降额温度时都乘温度降额后电流。"
                + "到停止温度时输入降到温度停止限流，提速停止。"
                + "到停充上限时输入和提速都停止。"
                + "注：实际电池电流受输入功率和充电器转换限制。"));

        root.addView(subSection("开关与停充"));
        enabled = addSwitch(root, "启用模块控制");
        addField(root, new FieldSpec("charge_limit_soc", "停充上限 [%]",
                50, 100, false));
        root.addView(note("停充上限 100% 表示不限制。"));

        root.addView(subSection("输入限流"));
        addField(root, new FieldSpec("max_input_ma", "输入电流上限 [mA]",
                100, 5000, false));
        addField(root, new FieldSpec("soc_mid", "高 SOC 降额起始 [%]",
                0, 100, false));
        addField(root, new FieldSpec("soc_mid_scale", "高 SOC 降额后电流 [%]",
                10, 100, false));
        root.addView(note("高于降额起始 SOC 后，输入电流乘以电流比例，100% 表示不降额。"
                + "只影响快充输入电流，不改变停充上限。"));

        root.addView(subSection("温度保护"));
        addField(root, new FieldSpec("temp_derate_dc", "电池降额温度 [°C]",
                20.0, 60.0, true));
        addField(root, new FieldSpec("temp_stop_dc", "电池停止温度 [°C]",
                21.0, 70.0, true));
        addField(root, new FieldSpec("board_temp_derate_dc",
                "主板降额温度 [°C]", 30.0, 80.0, true));
        addField(root, new FieldSpec("board_temp_stop_dc",
                "主板停止温度 [°C]", 31.0, 90.0, true));
        addField(root, new FieldSpec("temp_derate_scale", "温度降额后电流 [%]",
                10, 100, false));
        addField(root, new FieldSpec("min_input_ma", "温度停止限流 [mA]",
                100, 5000, false));
        root.addView(note("四个温度共用温度降额后电流和温度停止限流。"
                + "任一传感器到降额温度，输入和提速目标乘降额后电流。"
                + "到停止温度，输入电流降到停止限流，提速停止。"));

        root.addView(subSection("直充提速"));
        boostEnabled = addSwitch(root, "启用直充提速");
        addField(root, new FieldSpec("boost_soc_max", "提速最高 SOC [%]",
                0, 100, false));
        addField(root, new FieldSpec("boost_ma", "提速电池电流 [mA]",
                1000, 6600, false));
        root.addView(note("只在直充且 SOC 低于提速最高 SOC 时生效。"));

        root.addView(divider());
        root.addView(majorSection("温控接管"));
        thermalCpu = addSwitch(root, "接管 CPU 频率");
        thermalCharge = addSwitch(root, "按温控限制充电");
        thermalOnline = addSwitch(root, "管理 CPU 在线状态");
        addField(root, new FieldSpec("thermal_interval_ms", "温控检测间隔 [ms]",
                1000, 60000, false));
        addField(root, new FieldSpec("thermal_idle_interval_ms",
                "非充电温控间隔 [ms]", 1000, 60000, false));
        root.addView(note("电池未充电、没有规则生效，且温度比最低规则低超过 2.0 °C 时，温度检测使用非充电温控间隔；其他情况使用温控检测间隔。"));
        root.addView(note("规则温度取温度区读数。本机是 skin-therm，对应机身后盖附近的外壳温度。"));
        root.addView(note("打开“按温控限制充电”后，温控规则里的充电电流会压低输入电流和提速目标。"
                + "关闭后，温控规则不再限制充电，电池和主板温度保护仍然有效。"));

        TextView ruleHint = note("每行一档：温度 [°C]，小核 [MHz]，大核 [MHz]，"
                + "超大核 [MHz]，充电电流 [mA]，大核在线数，超大核在线数。"
                + "后两项可省略。");
        ruleHint.setPadding(dp(6), dp(10), dp(6), dp(4));
        root.addView(ruleHint);

        LinearLayout rulesCard = block();
        rulesCard.setOrientation(LinearLayout.VERTICAL);
        rulesCard.setGravity(Gravity.NO_GRAVITY);
        rulesCard.addView(textView("温控规则", 14, true));

        rulesInput = new EditText(this);
        rulesInput.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        rulesInput.setTypeface(Typeface.MONOSPACE);
        rulesInput.setTextSize(12);
        rulesInput.setMinLines(5);
        rulesInput.setMaxLines(12);
        rulesInput.setGravity(Gravity.TOP | Gravity.START);
        rulesInput.setHint("40.0,1804,2419,2265,3000,3,1");
        rulesInput.setBackground(rounded(fieldColor(), 8));
        rulesInput.setPadding(dp(10), dp(8), dp(10), dp(8));
        LinearLayout.LayoutParams rulesParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        rulesParams.topMargin = dp(6);
        rulesCard.addView(rulesInput, rulesParams);
        root.addView(rulesCard, blockParams());

        advancedToggle = textView("展开高级参数", 15, true);
        advancedToggle.setGravity(Gravity.CENTER);
        advancedToggle.setPadding(0, dp(12), 0, dp(12));
        advancedToggle.setBackground(rounded(surfaceColor(), 10));
        advancedToggle.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                setAdvancedOpen(advancedContainer.getVisibility()
                        != View.VISIBLE);
            }
        });
        LinearLayout.LayoutParams toggleParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        toggleParams.topMargin = dp(18);
        toggleParams.bottomMargin = dp(6);
        root.addView(advancedToggle, toggleParams);

        advancedContainer = new LinearLayout(this);
        advancedContainer.setOrientation(LinearLayout.VERTICAL);
        advancedContainer.setVisibility(View.GONE);
        root.addView(advancedContainer);

        advancedContainer.addView(section("轮询与线阻"));
        addField(advancedContainer, new FieldSpec("poll_active_ms",
                "控制中轮询 [ms]", 1000, 60000, false));
        addField(advancedContainer, new FieldSpec("poll_idle_ms",
                "空闲轮询 [ms]", 1000, 60000, false));
        addField(advancedContainer, new FieldSpec("boost_cable_mohm",
                "提速线阻 [mOhm]", 0, 1000, false));

        advancedContainer.addView(section("内核状态"));
        LinearLayout kernelCard = block();
        kernelCard.setOrientation(LinearLayout.VERTICAL);
        kernelCard.setGravity(Gravity.NO_GRAVITY);
        kernelText = textView("cpu-status 读取中…", 11, false);
        kernelText.setTypeface(Typeface.MONOSPACE);
        kernelText.setTextIsSelectable(true);
        kernelCard.addView(kernelText);
        advancedContainer.addView(kernelCard, blockParams());

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setPadding(dp(16), dp(8), dp(16), dp(14));

        readButton = new Button(this);
        readButton.setText("读取");
        applyButton = new Button(this);
        applyButton.setText("应用");

        LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        buttonParams.setMargins(dp(4), 0, dp(4), 0);
        bar.addView(readButton, buttonParams);
        bar.addView(applyButton, buttonParams);

        readButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                readConfig();
            }
        });
        applyButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                applyConfig();
            }
        });
        outer.addView(bar);

        return outer;
    }

    private void setAdvancedOpen(boolean open) {
        advancedContainer.setVisibility(open ? View.VISIBLE : View.GONE);
        advancedToggle.setText(open ? "收起高级参数" : "展开高级参数");
    }

    private void setBusy(boolean value) {
        busy = value;
        readButton.setEnabled(!value);
        applyButton.setEnabled(!value);
    }

    private void setField(String key, String value) {
        EditText field = fields.get(key);
        if (field != null) {
            field.setText(value);
        }
    }

    private void applyPreset(int index) {
        if (index == 0) {
            enabled.setChecked(false);
        } else if (index == 1) {
            enabled.setChecked(true);
            boostEnabled.setChecked(true);
            setField("charge_limit_soc", "100");
            setField("max_input_ma", "5000");
            setField("boost_soc_max", "40");
        } else if (index == 2) {
            enabled.setChecked(true);
            boostEnabled.setChecked(false);
            setField("charge_limit_soc", "80");
            setField("max_input_ma", "3000");
        } else {
            enabled.setChecked(true);
            boostEnabled.setChecked(true);
            setField("charge_limit_soc", "100");
            setField("max_input_ma", "5000");
            setField("boost_soc_max", "50");
        }
        Toast.makeText(this, "预设已填入表单，点应用后生效",
                Toast.LENGTH_SHORT).show();
    }

    private static String suBinary() {
        File su = new File("/system/bin/su");
        return su.exists() ? su.getAbsolutePath() : "su";
    }

    private static String rootError(Exception e) {
        String message = e.getMessage();
        if (message != null && (message.contains("Cannot run program")
                || message.contains("Operation not permitted"))) {
            return "无法执行 su。请在 KernelSU 的超级用户列表中给"
                    + " PD2055 Power Management 授权。";
        }
        return e.toString();
    }

    private static Result runRoot(String command, String stdin) throws Exception {
        ProcessBuilder builder = new ProcessBuilder(suBinary(), "-c", command);
        builder.redirectErrorStream(true);
        Process process = builder.start();

        OutputStream os = process.getOutputStream();
        if (stdin != null) {
            os.write(stdin.getBytes(StandardCharsets.UTF_8));
        }
        os.close();

        final StringBuffer output = new StringBuffer();
        Thread readerThread = new Thread(new Runnable() {
            @Override
            public void run() {
                try (BufferedReader reader = new BufferedReader(
                        new InputStreamReader(process.getInputStream(),
                                StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        output.append(line).append('\n');
                    }
                } catch (Exception e) {
                    output.append(e).append('\n');
                }
            }
        });
        readerThread.start();

        boolean done = process.waitFor(ROOT_TIMEOUT_S, TimeUnit.SECONDS);
        if (!done) {
            process.destroy();
        }
        readerThread.join(1000);
        if (!done) {
            return new Result(-1, output + "su 超时\n");
        }
        return new Result(process.exitValue(), output.toString());
    }

    private static void parseConfig(String text, Map<String, String> values,
                                    List<String> rules) {
        for (String raw : text.split("\n")) {
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            int eq = line.indexOf('=');
            if (eq <= 0) {
                continue;
            }
            String key = line.substring(0, eq).trim();
            String value = line.substring(eq + 1).trim();
            if ("thermal_rule".equals(key)) {
                rules.add(value);
            } else {
                values.put(key, value);
            }
        }
    }

    private static String displayField(FieldSpec spec, String value) {
        if (value == null) {
            return "";
        }
        if (!spec.temp) {
            return value;
        }
        try {
            return String.format(Locale.US, "%.1f",
                    Integer.parseInt(value) / 10.0);
        } catch (NumberFormatException e) {
            return value;
        }
    }

    private static String displayRule(String rule) {
        String[] parts = rule.split(",", -1);
        if (parts.length == 0) {
            return rule;
        }
        int tempDc;
        try {
            tempDc = Integer.parseInt(parts[0].trim());
        } catch (NumberFormatException e) {
            return rule;
        }
        StringBuilder sb = new StringBuilder(
                String.format(Locale.US, "%.1f", tempDc / 10.0));
        for (int i = 1; i < parts.length; i++) {
            sb.append(',').append(parts[i].trim());
        }
        return sb.toString();
    }

    private void populate(Map<String, String> values, List<String> rules) {
        enabled.setChecked("1".equals(values.get("enabled")));
        boostEnabled.setChecked("1".equals(values.get("boost_enabled")));
        thermalCpu.setChecked("1".equals(values.get("thermal_cpu")));
        thermalCharge.setChecked("1".equals(values.get("thermal_charge")));
        thermalOnline.setChecked("1".equals(values.get("thermal_online")));
        if (!values.containsKey("thermal_idle_interval_ms")) {
            values.put("thermal_idle_interval_ms", "15000");
        }

        for (FieldSpec spec : fieldSpecs) {
            fields.get(spec.key).setText(
                    displayField(spec, values.get(spec.key)));
        }

        StringBuilder text = new StringBuilder();
        for (String rule : rules) {
            if (text.length() > 0) {
                text.append('\n');
            }
            text.append(displayRule(rule));
        }
        rulesInput.setText(text.toString());
    }

    private void readConfig() {
        setBusy(true);
        statusHeadline.setText("读取中…");
        statusDetail.setText("");
        statusModule.setText("");
        statusThermal.setText("");

        executor.execute(new Runnable() {
            @Override
            public void run() {
                String error = null;
                final Map<String, String> values = new LinkedHashMap<>();
                final List<String> rules = new ArrayList<>();
                String statusOutput = "";
                String cpuOutput = "";
                try {
                    Result config = runRoot(PDCHARGE + " config-get", null);
                    if (config.code != 0) {
                        error = "config-get 失败\n" + config.output;
                    } else {
                        parseConfig(config.output, values, rules);
                        Result status = runRoot(PDCHARGE + " status", null);
                        Result cpu = runRoot(PDCHARGE + " cpu-status", null);
                        statusOutput = status.output;
                        cpuOutput = cpu.output;
                    }
                } catch (Exception e) {
                    error = rootError(e);
                }

                final String failure = error;
                final String status = statusOutput;
                final String cpu = cpuOutput;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (isFinishing() || isDestroyed()) {
                            return;
                        }
                        setBusy(false);
                        if (failure != null) {
                            statusHeadline.setText("读取失败");
                            statusDetail.setText(failure.trim());
                            return;
                        }
                        populate(values, rules);
                        updateStatus(status);
                        updateKernel(cpu);
                    }
                });
            }
        });
    }

    private void refreshStatus() {
        if (!foreground || busy || statusBusy) {
            return;
        }
        statusBusy = true;
        executor.execute(new Runnable() {
            @Override
            public void run() {
                String output;
                try {
                    Result result = runRoot(PDCHARGE + " status", null);
                    output = result.output;
                } catch (Exception e) {
                    output = rootError(e);
                }
                final String text = output;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        statusBusy = false;
                        if (foreground && !isFinishing() && !isDestroyed()) {
                            updateStatus(text);
                        }
                    }
                });
            }
        });
    }

    private static Map<String, String> parseStatusLine(String line) {
        Map<String, String> values = new LinkedHashMap<>();
        Matcher matcher = STATUS_PAIR.matcher(line);
        while (matcher.find()) {
            values.put(matcher.group(1), matcher.group(2).trim());
        }
        return values;
    }

    private static String findLine(String text, String prefix) {
        for (String raw : text.split("\n")) {
            String line = raw.trim();
            if (line.startsWith(prefix)) {
                return line;
            }
        }
        return null;
    }

    private static int intValue(String text, int fallback) {
        if (text == null) {
            return fallback;
        }
        try {
            return Integer.parseInt(text.trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static String temperature(String value) {
        if (value == null || "-".equals(value) || value.isEmpty()) {
            return "—";
        }
        String number = value.endsWith("C")
                ? value.substring(0, value.length() - 1) : value;
        return number + " °C";
    }

    private static String unitValue(String value, String suffix, String unit) {
        if (value == null || "-".equals(value) || value.isEmpty()) {
            return "—";
        }
        String number = value.endsWith(suffix)
                ? value.substring(0, value.length() - suffix.length()) : value;
        return number + " " + unit;
    }

    private static String statusText(String status) {
        if (status == null) {
            return "状态未知";
        }
        if ("Charging".equals(status)) {
            return "充电中";
        }
        if ("Full".equals(status)) {
            return "已充满";
        }
        if ("Discharging".equals(status)) {
            return "放电中";
        }
        if ("Not charging".equals(status)) {
            return "未充电";
        }
        return status;
    }

    private static String reasonText(String reason) {
        if (reason == null) {
            return "—";
        }
        if ("active".equals(reason)) {
            return "正常";
        }
        if ("disabled".equals(reason)) {
            return "模块已关闭";
        }
        if ("module-off".equals(reason)) {
            return "模块已关闭";
        }
        if ("full".equals(reason)) {
            return "已充满";
        }
        if ("soc".equals(reason) || "soc-limit".equals(reason)) {
            return "达到停充上限";
        }
        if ("soc-unknown".equals(reason)) {
            return "SOC 未知";
        }
        if ("battery-temp".equals(reason)) {
            return "电池温度保护";
        }
        if ("board-temp".equals(reason)) {
            return "主板温度保护";
        }
        if ("battery-derate".equals(reason)) {
            return "电池温度降额";
        }
        if ("board-derate".equals(reason)) {
            return "主板温度降额";
        }
        if ("soc-derate".equals(reason)) {
            return "高 SOC 降额";
        }
        if ("temp-derate".equals(reason)) {
            return "电池和主板温度降额";
        }
        if ("protocol".equals(reason)) {
            return "非快充协议";
        }
        if ("thermal".equals(reason)) {
            return "温控上限";
        }
        if ("offline".equals(reason)) {
            return "未连接";
        }
        if ("not-dchg".equals(reason)) {
            return "非直充";
        }
        return reason;
    }

    private static String ruleTemperature(String rule) {
        if (rule == null || "-".equals(rule) || rule.isEmpty()) {
            return null;
        }
        int comma = rule.indexOf(',');
        String first = comma < 0 ? rule : rule.substring(0, comma);
        int tempDc = intValue(first, -1);
        if (tempDc < 0) {
            return null;
        }
        return String.format(Locale.US, "%.1f °C", tempDc / 10.0);
    }

    private void updateStatus(String output) {
        String text = output == null ? "" : output.trim();
        if (text.isEmpty()) {
            statusHeadline.setText("状态不可用");
            statusDetail.setText("没有读到 pdcharge status 的输出。");
            statusModule.setText("");
            statusThermal.setText("");
            return;
        }

        int newline = text.indexOf('\n');
        String firstLine = newline < 0 ? text : text.substring(0, newline);
        Map<String, String> values = parseStatusLine(firstLine);
        if (!values.containsKey("soc")) {
            statusHeadline.setText("状态不可用");
            statusDetail.setText(text);
            statusModule.setText("");
            statusThermal.setText("");
            return;
        }

        String soc = values.get("soc");
        String status = statusText(values.get("status"));
        String headline = (soc == null || "-".equals(soc)
                ? "SOC 未知" : "SOC " + soc + "%") + " · " + status;
        if ("1".equals(values.get("fast"))) {
            headline += " · 快充";
        }
        statusHeadline.setText(headline);

        String detail = "电池 " + temperature(values.get("batt"))
                + " · 主板 " + temperature(values.get("board"))
                + "\n输入 " + unitValue(values.get("vbus"), "V", "V")
                + " / " + unitValue(values.get("imax"), "A", "A")
                + " · 电池电流 " + unitValue(values.get("ibat"), "A", "A")
                + "\n目标 " + targetText(values.get("target"))
                + " · " + limitText(values.get("limit"))
                + " · " + reasonText(values.get("reason"));

        int limit = intValue(values.get("limit"), 100);
        int keep = intValue(values.get("keep"), -1);
        if (limit < 100 && keep == 100) {
            detail += "\n系统停充投票未生效";
        } else if (keep > 0 && keep < 100 && keep != limit) {
            detail += "\n系统停充投票 " + keep + "%";
        }
        statusDetail.setText(detail);

        StringBuilder module = new StringBuilder();
        module.append("模块 ").append("on".equals(values.get("module"))
                ? "开" : "关");
        module.append(" · 守护进程 ").append(
                "running".equals(values.get("daemon")) ? "运行" : "已停止");
        if ("1".equals(values.get("dchg"))) {
            module.append(" · 直充");
        }
        if ("on".equals(values.get("boost"))) {
            module.append(" · 提速中");
        }
        statusModule.setText(module.toString());

        String thermalLine = findLine(text, "thermal ");
        if (thermalLine == null) {
            statusThermal.setText("");
            return;
        }
        updateThermal(thermalLine.substring("thermal ".length()));
    }

    private static String targetText(String value) {
        if (value == null || "-".equals(value)) {
            return "—";
        }
        String number = value.endsWith("mA")
                ? value.substring(0, value.length() - 2) : value;
        int target = intValue(number, -1);
        if (target > 0) {
            return target + " mA";
        }
        if (target == 0) {
            return "停止";
        }
        return "—";
    }

    private static String limitText(String value) {
        int limit = intValue(value, -1);
        if (limit < 0) {
            return "停充 —";
        }
        if (limit >= 100) {
            return "停充 不限制";
        }
        return "停充 " + limit + "%";
    }

    private void updateThermal(String line) {
        Map<String, String> values = parseStatusLine(line);
        StringBuilder text = new StringBuilder("温控 ")
                .append(temperature(values.get("temp")));

        String ruleTemp = ruleTemperature(values.get("rule"));
        if (ruleTemp != null) {
            text.append(" · 规则 ").append(ruleTemp);
        }

        String cpu = values.get("cpu");
        if (cpu != null && !"-".equals(cpu) && !cpu.isEmpty()) {
            text.append(" · CPU ").append(cpu.replace("MHz", " MHz"));
        }

        String charge = unitValue(values.get("charge"), "mA", "mA");
        if (!"—".equals(charge)) {
            text.append(" · 充电 ").append(charge);
        }

        String online = values.get("online");
        if (online != null && !"-".equals(online) && !online.isEmpty()) {
            text.append(" · 在线核 ").append(online);
        }

        statusThermal.setText(text.toString());
    }

    private void updateKernel(String output) {
        if (output == null || output.trim().isEmpty()) {
            kernelText.setText("cpu-status 无输出");
        } else {
            kernelText.setText(output.trim());
        }
    }

    private int fieldValue(Map<String, Integer> numbers, String key) {
        return numbers.get(key);
    }

    private Map<String, Integer> readNumbers() {
        Map<String, Integer> numbers = new LinkedHashMap<>();
        for (FieldSpec spec : fieldSpecs) {
            String text = fields.get(spec.key).getText().toString().trim();
            if (text.isEmpty()) {
                throw new IllegalArgumentException(spec.label + " 为空");
            }

            int value;
            try {
                if (spec.temp) {
                    double celsius = Double.parseDouble(text);
                    if (celsius < spec.min || celsius > spec.max) {
                        throw new IllegalArgumentException(spec.label + " 超出 "
                                + formatBound(spec.min) + "–"
                                + formatBound(spec.max));
                    }
                    value = (int) Math.round(celsius * 10.0);
                } else {
                    value = Integer.parseInt(text);
                    if (value < spec.min || value > spec.max) {
                        throw new IllegalArgumentException(spec.label + " 超出 "
                                + formatBound(spec.min) + "–"
                                + formatBound(spec.max));
                    }
                }
            } catch (NumberFormatException e) {
                throw new IllegalArgumentException(spec.label + " 不是数字");
            }
            numbers.put(spec.key, value);
        }

        int maxInput = fieldValue(numbers, "max_input_ma");
        int minInput = fieldValue(numbers, "min_input_ma");
        if (minInput > maxInput) {
            throw new IllegalArgumentException("温度停止限流不能大于输入电流上限");
        }

        int thermalInterval = fieldValue(numbers, "thermal_interval_ms");
        int thermalIdle = fieldValue(numbers, "thermal_idle_interval_ms");
        if (thermalIdle < thermalInterval) {
            throw new IllegalArgumentException("非充电温控间隔不能小于温控检测间隔");
        }

        int limit = fieldValue(numbers, "charge_limit_soc");
        int socMid = fieldValue(numbers, "soc_mid");
        if (socMid > limit) {
            throw new IllegalArgumentException("高 SOC 降额起始不能大于停充上限");
        }

        int boostSoc = fieldValue(numbers, "boost_soc_max");
        if (boostSoc > limit) {
            throw new IllegalArgumentException("提速最高 SOC 不能大于停充上限");
        }

        int tempDerate = fieldValue(numbers, "temp_derate_dc");
        int tempStop = fieldValue(numbers, "temp_stop_dc");
        if (tempStop < tempDerate + 10) {
            throw new IllegalArgumentException("电池停止温度至少比降额温度高 1.0 °C");
        }

        int boardDerate = fieldValue(numbers, "board_temp_derate_dc");
        int boardStop = fieldValue(numbers, "board_temp_stop_dc");
        if (boardStop < boardDerate + 10) {
            throw new IllegalArgumentException("主板停止温度至少比降额温度高 1.0 °C");
        }
        return numbers;
    }

    private static String formatBound(double value) {
        if (value == Math.rint(value)) {
            return Integer.toString((int) value);
        }
        return String.format(Locale.US, "%.1f", value);
    }

    private List<String> readRules() {
        List<String> rules = new ArrayList<>();
        int lastTemp = -1;
        String[] lines = rulesInput.getText().toString().split("\n", -1);
        for (String raw : lines) {
            String line = raw.trim();
            if (line.isEmpty()) {
                continue;
            }
            String[] parts = line.split(",", -1);
            if (parts.length != 5 && parts.length != 7) {
                throw new IllegalArgumentException("规则需要 5 或 7 个字段: "
                        + line);
            }

            int[] value = new int[7];
            try {
                double celsius = Double.parseDouble(parts[0].trim());
                value[0] = (int) Math.round(celsius * 10.0);
            } catch (NumberFormatException e) {
                throw new IllegalArgumentException("规则温度不是数字: " + line);
            }
            for (int i = 1; i < parts.length; i++) {
                try {
                    value[i] = Integer.parseInt(parts[i].trim());
                } catch (NumberFormatException e) {
                    throw new IllegalArgumentException("规则字段不是整数: "
                            + line);
                }
            }
            if (parts.length == 5) {
                value[5] = 3;
                value[6] = 1;
            }

            if (value[0] < 300 || value[0] > 750) {
                throw new IllegalArgumentException("温度超出 30.0–75.0 °C: "
                        + line);
            }
            if (value[1] < 300 || value[1] > 2000) {
                throw new IllegalArgumentException("小核频率超出 300–2000 MHz: "
                        + line);
            }
            if (value[2] < 300 || value[2] > 2600) {
                throw new IllegalArgumentException("大核频率超出 300–2600 MHz: "
                        + line);
            }
            if (value[3] < 300 || value[3] > 3300) {
                throw new IllegalArgumentException("超大核频率超出 300–3300 MHz: "
                        + line);
            }
            if (value[4] < 100 || value[4] > 5000) {
                throw new IllegalArgumentException("充电电流超出 100–5000 mA: "
                        + line);
            }
            if (value[5] < 1 || value[5] > 3) {
                throw new IllegalArgumentException("大核在线数超出 1–3: "
                        + line);
            }
            if (value[6] < 0 || value[6] > 1) {
                throw new IllegalArgumentException("超大核在线数超出 0–1: "
                        + line);
            }
            if (value[0] <= lastTemp) {
                throw new IllegalArgumentException("温度必须严格递增: " + line);
            }
            lastTemp = value[0];

            rules.add(value[0] + "," + value[1] + "," + value[2] + ","
                    + value[3] + "," + value[4] + "," + value[5] + ","
                    + value[6]);
            if (rules.size() > 10) {
                throw new IllegalArgumentException("温控规则最多 10 档");
            }
        }
        if (rules.isEmpty()) {
            throw new IllegalArgumentException("至少需要一档温控规则");
        }
        return rules;
    }

    private static void append(StringBuilder sb, String key, int value) {
        sb.append(key).append('=').append(value).append('\n');
    }

    private String buildConfig() {
        Map<String, Integer> numbers = readNumbers();
        List<String> rules = readRules();

        StringBuilder sb = new StringBuilder();
        append(sb, "enabled", enabled.isChecked() ? 1 : 0);
        append(sb, "boost_enabled", boostEnabled.isChecked() ? 1 : 0);
        append(sb, "boost_soc_max", fieldValue(numbers, "boost_soc_max"));
        append(sb, "boost_ma", fieldValue(numbers, "boost_ma"));
        append(sb, "boost_cable_mohm",
                fieldValue(numbers, "boost_cable_mohm"));
        append(sb, "max_input_ma", fieldValue(numbers, "max_input_ma"));
        append(sb, "min_input_ma", fieldValue(numbers, "min_input_ma"));
        append(sb, "charge_limit_soc",
                fieldValue(numbers, "charge_limit_soc"));
        append(sb, "soc_mid", fieldValue(numbers, "soc_mid"));
        append(sb, "soc_mid_scale", fieldValue(numbers, "soc_mid_scale"));
        append(sb, "temp_derate_dc", fieldValue(numbers, "temp_derate_dc"));
        append(sb, "temp_stop_dc", fieldValue(numbers, "temp_stop_dc"));
        append(sb, "board_temp_derate_dc",
                fieldValue(numbers, "board_temp_derate_dc"));
        append(sb, "board_temp_stop_dc",
                fieldValue(numbers, "board_temp_stop_dc"));
        append(sb, "temp_derate_scale",
                fieldValue(numbers, "temp_derate_scale"));
        append(sb, "poll_active_ms", fieldValue(numbers, "poll_active_ms"));
        append(sb, "poll_idle_ms", fieldValue(numbers, "poll_idle_ms"));
        append(sb, "thermal_cpu", thermalCpu.isChecked() ? 1 : 0);
        append(sb, "thermal_charge", thermalCharge.isChecked() ? 1 : 0);
        append(sb, "thermal_online", thermalOnline.isChecked() ? 1 : 0);
        append(sb, "thermal_interval_ms",
                fieldValue(numbers, "thermal_interval_ms"));
        append(sb, "thermal_idle_interval_ms",
                fieldValue(numbers, "thermal_idle_interval_ms"));
        for (String rule : rules) {
            sb.append("thermal_rule=").append(rule).append('\n');
        }
        return sb.toString();
    }

    private void applyConfig() {
        String config;
        try {
            config = buildConfig();
        } catch (IllegalArgumentException e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            return;
        }

        setBusy(true);
        statusModule.setText("正在写入配置…");
        final String text = config;
        executor.execute(new Runnable() {
            @Override
            public void run() {
                String error = null;
                String statusOutput = "";
                String cpuOutput = "";
                String applyOutput = "";
                try {
                    Result apply = runRoot(PDCHARGE + " config-set", text);
                    applyOutput = apply.output.trim();
                    if (apply.code != 0) {
                        error = "config-set 失败\n" + applyOutput;
                    } else {
                        Thread.sleep(400);
                        Result status = runRoot(PDCHARGE + " status", null);
                        Result cpu = runRoot(PDCHARGE + " cpu-status", null);
                        statusOutput = status.output;
                        cpuOutput = cpu.output;
                    }
                } catch (Exception e) {
                    error = rootError(e);
                }

                final String failure = error;
                final String applied = applyOutput;
                final String status = statusOutput;
                final String cpu = cpuOutput;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (isFinishing() || isDestroyed()) {
                            return;
                        }
                        setBusy(false);
                        if (failure != null) {
                            statusModule.setText("");
                            statusDetail.setText(failure.trim());
                            Toast.makeText(MainActivity.this, failure,
                                    Toast.LENGTH_LONG).show();
                            return;
                        }
                        String message = applied;
                        if (applied.contains("daemon=stopped")) {
                            message = "配置已写入。守护进程未运行，启动后生效。";
                        } else if (applied.contains("daemon=running")) {
                            message = "配置已写入，守护进程已重载。";
                        }
                        Toast.makeText(MainActivity.this, message,
                                Toast.LENGTH_SHORT).show();
                        updateStatus(status);
                        updateKernel(cpu);
                    }
                });
            }
        });
    }
}
