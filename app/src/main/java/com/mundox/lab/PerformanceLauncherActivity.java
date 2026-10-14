package com.mundox.lab;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.view.Display;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Locale;

public class PerformanceLauncherActivity extends Activity {
    private static final int REQ_PICK_APP = 1001;
    private static final String WILD_RIFT_PACKAGE = "com.riotgames.league.wildrift";

    private TextView status;
    private String pendingPackage;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(28, 28, 28, 40);
        root.setBackgroundColor(Color.rgb(7, 9, 13));
        scroll.addView(root);

        TextView title = text("PERFORMANCE LAUNCHER", 26, true);
        root.addView(title);
        root.addView(text(
                "Preflight del teléfono → abrir app → este proceso se elimina para liberar su RAM.",
                13, false));

        status = text("", 13, false);
        status.setTypeface(android.graphics.Typeface.MONOSPACE);
        status.setTextIsSelectable(true);
        root.addView(status, fullWidth());

        Button refresh = button("REFRESH PREFLIGHT");
        refresh.setOnClickListener(v -> status.setText(buildReport()));
        root.addView(refresh);

        Button wildRift = button("PREPARE + LAUNCH WILD RIFT");
        wildRift.setOnClickListener(v -> prepareAndLaunchPackage(WILD_RIFT_PACKAGE));
        root.addView(wildRift);

        Button choose = button("PREPARE + CHOOSE APP");
        choose.setOnClickListener(v -> chooseApp());
        root.addView(choose);

        Button battery = button("OPEN BATTERY SAVER SETTINGS");
        battery.setOnClickListener(v -> {
            try {
                startActivity(new Intent(Settings.ACTION_BATTERY_SAVER_SETTINGS));
            } catch (Throwable t) {
                status.append("\nCould not open battery saver settings: " + t);
            }
        });
        root.addView(battery);

        Button display = button("OPEN DISPLAY SETTINGS");
        display.setOnClickListener(v -> {
            try {
                startActivity(new Intent(Settings.ACTION_DISPLAY_SETTINGS));
            } catch (Throwable t) {
                status.append("\nCould not open display settings: " + t);
            }
        });
        root.addView(display);

        Button appBattery = button("OPEN APP BATTERY SETTINGS");
        appBattery.setOnClickListener(v -> {
            try {
                Intent i = new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS);
                startActivity(i);
            } catch (Throwable t) {
                status.append("\nCould not open battery optimization settings: " + t);
            }
        });
        root.addView(appBattery);

        root.addView(text(
                "No root. No CPU/GPU overclock. No modification of target apps. " +
                "This launcher only measures, recommends, launches, and exits.",
                12, false));

        setContentView(scroll);
        status.setText(buildReport());
    }

    private void chooseApp() {
        Intent base = new Intent(Intent.ACTION_MAIN);
        base.addCategory(Intent.CATEGORY_LAUNCHER);

        Intent picker = new Intent(Intent.ACTION_PICK_ACTIVITY);
        picker.putExtra(Intent.EXTRA_INTENT, base);
        picker.putExtra(Intent.EXTRA_TITLE, "Choose app to launch");

        try {
            startActivityForResult(picker, REQ_PICK_APP);
        } catch (Throwable t) {
            status.append("\nApp picker unavailable: " + t);
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode != REQ_PICK_APP || resultCode != RESULT_OK || data == null) {
            return;
        }

        ComponentName component = data.getComponent();
        if (component == null) {
            status.append("\nNo launchable component selected.");
            return;
        }

        pendingPackage = component.getPackageName();
        prepareAndLaunchComponent(component);
    }

    private void prepareAndLaunchPackage(String packageName) {
        Intent launch = getPackageManager().getLaunchIntentForPackage(packageName);
        if (launch == null) {
            status.setText(buildReport() + "\nTarget app is not installed or not launchable.");
            return;
        }

        pendingPackage = packageName;
        prepareAndLaunchIntent(launch);
    }

    private void prepareAndLaunchComponent(ComponentName component) {
        Intent launch = new Intent(Intent.ACTION_MAIN);
        launch.addCategory(Intent.CATEGORY_LAUNCHER);
        launch.setComponent(component);
        prepareAndLaunchIntent(launch);
    }

    private void prepareAndLaunchIntent(Intent launch) {
        String report = buildReport();

        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(memory);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        boolean powerSave = pm != null && pm.isPowerSaveMode();
        int thermal = -1;
        if (pm != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            thermal = pm.getCurrentThermalStatus();
        }

        StringBuilder blockers = new StringBuilder();
        if (powerSave) blockers.append("Battery Saver is ON. ");
        if (thermal >= PowerManager.THERMAL_STATUS_SEVERE && thermal >= 0) {
            blockers.append("Thermal status is SEVERE or higher. ");
        }
        if (memory.lowMemory) blockers.append("Android reports low-memory pressure. ");

        if (blockers.length() > 0) {
            status.setText(report + "\nPRE-LAUNCH WARNING: " + blockers +
                    "\nLaunching anyway because you requested it.");
        } else {
            status.setText(report + "\nPRE-LAUNCH: READY");
        }

        // Release objects owned by this screen before handing control to target.
        System.gc();

        launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);

        try {
            startActivity(launch);
        } catch (Throwable t) {
            status.append("\nLaunch failed: " + t);
            return;
        }

        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            finishAndRemoveTask();
            android.os.Process.killProcess(android.os.Process.myPid());
        }, 900);
    }

    private String buildReport() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(memory);

        long totalMiB = memory.totalMem / (1024L * 1024L);
        long availableMiB = memory.availMem / (1024L * 1024L);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        boolean powerSave = pm != null && pm.isPowerSaveMode();

        int thermal = -1;
        if (pm != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            thermal = pm.getCurrentThermalStatus();
        }

        float batteryTemp = -1f;
        int batteryPct = -1;
        Intent battery = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (battery != null) {
            int raw = battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, -1);
            if (raw >= 0) batteryTemp = raw / 10f;

            int level = battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
            int scale = battery.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
            if (level >= 0 && scale > 0) batteryPct = Math.round(level * 100f / scale);
        }

        float refreshHz = -1f;
        try {
            Display display = getWindowManager().getDefaultDisplay();
            refreshHz = display.getRefreshRate();
        } catch (Throwable ignored) {}

        String profile;
        if (totalMiB <= 3072) {
            profile = "LOW-END: prioritize stable FPS, low shadows/effects, conservative resolution.";
        } else if (totalMiB <= 4096) {
            profile = "LOW/MID: prioritize FPS; start low/medium, raise quality only if thermals stay stable.";
        } else {
            profile = "MID+: prioritize frame stability; tune quality upward only after stable thermals.";
        }

        StringBuilder out = new StringBuilder();
        out.append("DEVICE PREFLIGHT\n");
        out.append("RAM total=").append(totalMiB).append(" MiB\n");
        out.append("RAM available=").append(availableMiB).append(" MiB\n");
        out.append("lowMemory=").append(memory.lowMemory).append("\n");
        out.append("batterySaver=").append(powerSave ? "ON" : "OFF").append("\n");
        out.append("thermal=").append(thermalName(thermal)).append("\n");
        if (batteryTemp >= 0) {
            out.append(String.format(Locale.US, "batteryTemp=%.1f C\n", batteryTemp));
        }
        if (batteryPct >= 0) out.append("battery=").append(batteryPct).append("%\n");
        if (refreshHz > 0) {
            out.append(String.format(Locale.US, "displayRefresh=%.1f Hz\n", refreshHz));
        }
        out.append("profile=").append(profile).append("\n");

        if (powerSave) {
            out.append("ACTION: disable Battery Saver for maximum performance.\n");
        }
        if (thermal >= PowerManager.THERMAL_STATUS_MODERATE && thermal >= 0) {
            out.append("ACTION: thermal load is elevated; sustained FPS may throttle.\n");
        }
        if (availableMiB < 700) {
            out.append("ACTION: available RAM is low; avoid keeping heavy apps open.\n");
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

    private Button button(String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        LinearLayout.LayoutParams p = fullWidth();
        p.setMargins(0, 14, 0, 0);
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
}
