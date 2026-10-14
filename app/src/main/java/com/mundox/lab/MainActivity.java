package com.mundox.lab;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.text.InputType;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Supplier;

public class MainActivity extends Activity {
    private static final String WILD_RIFT_PACKAGE = "com.riotgames.league.wildrift";

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private TextView output;
    private EditText qubits;
    private EditText practice;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(28, 28, 28, 40);
        root.setBackgroundColor(Color.rgb(10, 12, 16));
        scroll.addView(root);

        root.addView(text("MUNDO X LAB", 28, true));
        root.addView(text("Quantum Core • Compact Core • Android native lab", 14, false));

        root.addView(text("Wild Rift low-end mode", 18, true));
        root.addView(text(
                "No root. No Riot files modified. Preflight → launch → Mundo X exits.",
                13, false));

        Button wrCheck = button("CHECK DEVICE FOR WILD RIFT");
        wrCheck.setOnClickListener(v -> append("\n" + wildRiftReport() + "\n"));
        root.addView(wrCheck);

        Button wrLaunch = button("LAUNCH WILD RIFT • LOW-END MODE");
        wrLaunch.setOnClickListener(v -> launchWildRiftLowEnd());
        root.addView(wrLaunch);

        Button batterySettings = button("OPEN BATTERY SAVER SETTINGS");
        batterySettings.setOnClickListener(v -> {
            try {
                startActivity(new Intent(Settings.ACTION_BATTERY_SAVER_SETTINGS));
            } catch (Throwable t) {
                append("\nCould not open battery settings: " + t + "\n");
            }
        });
        root.addView(batterySettings);

        Button hello = button("HELLO X");
        hello.setOnClickListener(v -> runTask(NativeCore::hello));
        root.addView(hello);

        root.addView(text("Qubits para GHZ / escala", 14, true));
        qubits = new EditText(this);
        qubits.setText("1000");
        qubits.setTextColor(Color.WHITE);
        qubits.setHintTextColor(Color.GRAY);
        qubits.setInputType(InputType.TYPE_CLASS_NUMBER);
        root.addView(qubits, fullWidth());

        Button compact = button("RUN COMPACT");
        compact.setOnClickListener(v -> runTask(() -> NativeCore.bench(readQubits(), false)));
        root.addView(compact);

        Button compare = button("COMPARE DENSE (dense limitado a 22 qubits)");
        compare.setOnClickListener(v -> runTask(() -> NativeCore.bench(readQubits(), true)));
        root.addView(compare);

        root.addView(text("2D cluster-state stress test", 14, true));

        LinearLayout clusterDims = new LinearLayout(this);
        clusterDims.setOrientation(LinearLayout.HORIZONTAL);

        EditText clusterRows = new EditText(this);
        clusterRows.setText("32");
        clusterRows.setTextColor(Color.WHITE);
        clusterRows.setInputType(InputType.TYPE_CLASS_NUMBER);
        clusterDims.addView(clusterRows, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        EditText clusterCols = new EditText(this);
        clusterCols.setText("32");
        clusterCols.setTextColor(Color.WHITE);
        clusterCols.setInputType(InputType.TYPE_CLASS_NUMBER);
        clusterDims.addView(clusterCols, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        root.addView(clusterDims, fullWidth());

        Button clusterRun = button("RUN 2D CLUSTER");
        clusterRun.setOnClickListener(v -> runTask(() -> {
            int rows = readBounded(clusterRows, 32, 1, 64);
            int cols = readBounded(clusterCols, 32, 1, 64);
            return NativeCore.cluster(rows, cols, false);
        }));
        root.addView(clusterRun);

        Button clusterCompare = button("COMPARE 2D CLUSTER (dense <= 20 qubits)");
        clusterCompare.setOnClickListener(v -> runTask(() -> {
            int rows = readBounded(clusterRows, 4, 1, 64);
            int cols = readBounded(clusterCols, 4, 1, 64);
            return NativeCore.cluster(rows, cols, true);
        }));
        root.addView(clusterCompare);

        root.addView(text("Practice script", 14, true));
        practice = new EditText(this);
        practice.setText("h 0\ncx 0 1\nshow\nbytes");
        practice.setTextColor(Color.WHITE);
        practice.setHintTextColor(Color.GRAY);
        practice.setMinLines(5);
        practice.setGravity(android.view.Gravity.TOP);
        practice.setInputType(InputType.TYPE_CLASS_TEXT |
                InputType.TYPE_TEXT_FLAG_MULTI_LINE |
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        root.addView(practice, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 320));

        Button practiceRun = button("RUN PRACTICE (2 qubits)");
        practiceRun.setOnClickListener(v ->
                runTask(() -> NativeCore.practice(2, practice.getText().toString())));
        root.addView(practiceRun);

        Button clear = button("CLEAR OUTPUT");
        clear.setOnClickListener(v -> output.setText(""));
        root.addView(clear);

        output = text("Mundo X ready.\n", 13, false);
        output.setTypeface(android.graphics.Typeface.MONOSPACE);
        output.setTextIsSelectable(true);
        root.addView(output, fullWidth());

        setContentView(scroll);
    }

    private String wildRiftReport() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(memory);

        long totalMiB = memory.totalMem / (1024L * 1024L);
        long availableMiB = memory.availMem / (1024L * 1024L);
        long thresholdMiB = memory.threshold / (1024L * 1024L);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        boolean powerSave = pm != null && pm.isPowerSaveMode();

        int thermal = -1;
        if (pm != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            thermal = pm.getCurrentThermalStatus();
        }

        float tempC = -1f;
        int batteryPercent = -1;
        Intent battery = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (battery != null) {
            int rawTemp = battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, -1);
            if (rawTemp >= 0) tempC = rawTemp / 10.0f;

            int level = battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
            int scale = battery.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
            if (level >= 0 && scale > 0) batteryPercent = Math.round(level * 100f / scale);
        }

        boolean installed =
                getPackageManager().getLaunchIntentForPackage(WILD_RIFT_PACKAGE) != null;

        String tier;
        String preset;
        if (totalMiB <= 3072) {
            tier = "LOW-END";
            preset = "Low resolution • low effects • shadows off • start at 30 FPS; try 60 only if stable.";
        } else if (totalMiB <= 4096) {
            tier = "LOW/MID";
            preset = "Low resolution/effects • shadows off • try 60 FPS; use 30 if heat or frame drops appear.";
        } else {
            tier = "MID+";
            preset = "Start Low/Medium and prioritize stable FPS over visual quality.";
        }

        StringBuilder out = new StringBuilder();
        out.append("WILD RIFT PREFLIGHT\n");
        out.append("installed=").append(installed ? "YES" : "NO").append("\n");
        out.append("device_tier=").append(tier).append("\n");
        out.append("RAM_total=").append(totalMiB).append(" MiB\n");
        out.append("RAM_available=").append(availableMiB).append(" MiB\n");
        out.append("low_memory=").append(memory.lowMemory).append("\n");
        out.append("low_memory_threshold=").append(thresholdMiB).append(" MiB\n");
        out.append("power_saver=").append(powerSave ? "ON" : "OFF").append("\n");
        out.append("thermal_status=").append(thermalName(thermal)).append("\n");
        if (tempC >= 0) {
            out.append(String.format(Locale.US, "battery_temp=%.1f C\n", tempC));
        }
        if (batteryPercent >= 0) {
            out.append("battery=").append(batteryPercent).append("%\n");
        }
        out.append("recommended=").append(preset).append("\n");

        if (powerSave) {
            out.append("WARNING: Battery Saver is ON and may limit performance.\n");
        }
        if (thermal >= PowerManager.THERMAL_STATUS_SEVERE && thermal >= 0) {
            out.append("WARNING: Device is already thermally stressed. Let it cool before ranked play.\n");
        }
        if (availableMiB < 700) {
            out.append("WARNING: Available RAM is low; close heavy apps before launch.\n");
        }

        return out.toString();
    }

    private String thermalName(int thermal) {
        if (thermal < 0) return "UNAVAILABLE";
        switch (thermal) {
            case PowerManager.THERMAL_STATUS_NONE: return "NONE";
            case PowerManager.THERMAL_STATUS_LIGHT: return "LIGHT";
            case PowerManager.THERMAL_STATUS_MODERATE: return "MODERATE";
            case PowerManager.THERMAL_STATUS_SEVERE: return "SEVERE";
            case PowerManager.THERMAL_STATUS_CRITICAL: return "CRITICAL";
            case PowerManager.THERMAL_STATUS_EMERGENCY: return "EMERGENCY";
            case PowerManager.THERMAL_STATUS_SHUTDOWN: return "SHUTDOWN";
            default: return "UNKNOWN(" + thermal + ")";
        }
    }

    private void launchWildRiftLowEnd() {
        append("\n" + wildRiftReport() + "\n");

        Intent launch = getPackageManager().getLaunchIntentForPackage(WILD_RIFT_PACKAGE);
        if (launch == null) {
            append("Wild Rift is not installed or cannot be discovered.\n");
            return;
        }

        launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        startActivity(launch);
        append("Wild Rift launched. Mundo X will exit to release its own RAM.\n");

        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            finishAndRemoveTask();
            android.os.Process.killProcess(android.os.Process.myPid());
        }, 1200);
    }

    private int readQubits() {
        try {
            int n = Integer.parseInt(qubits.getText().toString().trim());
            return Math.max(2, Math.min(n, 10000));
        } catch (Exception e) {
            return 1000;
        }
    }

    private int readBounded(EditText field, int fallback, int min, int max) {
        try {
            int n = Integer.parseInt(field.getText().toString().trim());
            return Math.max(min, Math.min(n, max));
        } catch (Exception e) {
            return fallback;
        }
    }

    private void runTask(Supplier<String> task) {
        append("\n[RUNNING]\n");
        executor.submit(() -> {
            String result;
            try {
                result = task.get();
            } catch (Throwable t) {
                result = "ERROR: " + t;
            }
            final String out = result +
                    "\nAndroid process PSS ~= " + Debug.getPss() + " kB\n";
            runOnUiThread(() -> append(out));
        });
    }

    private void append(String s) {
        output.append(s);
    }

    private Button button(String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        LinearLayout.LayoutParams p = fullWidth();
        p.setMargins(0, 16, 0, 0);
        b.setLayoutParams(p);
        return b;
    }

    private TextView text(String value, int sp, boolean bold) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextColor(Color.WHITE);
        t.setTextSize(sp);
        if (bold) t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        t.setPadding(0, 8, 0, 8);
        return t;
    }

    private LinearLayout.LayoutParams fullWidth() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    @Override
    protected void onDestroy() {
        executor.shutdownNow();
        super.onDestroy();
    }
}
