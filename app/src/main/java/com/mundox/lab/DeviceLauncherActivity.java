package com.mundox.lab;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ResolveInfo;
import android.graphics.Color;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.text.DateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class DeviceLauncherActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());

    private TextView clock;
    private TextView hud;
    private EditText search;
    private GridLayout appGrid;

    private final List<AppEntry> apps = new ArrayList<>();

    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            updateClock();
            updateHud();
            handler.postDelayed(this, 1000);
        }
    };

    private static final class AppEntry {
        String label;
        String packageName;
        String className;

        AppEntry(String label, String packageName, String className) {
            this.label = label;
            this.packageName = packageName;
            this.className = className;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().setStatusBarColor(Color.BLACK);
        getWindow().setNavigationBarColor(Color.BLACK);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(18, 18, 18, 18);
        root.setBackgroundColor(Color.rgb(5, 7, 11));

        clock = text("", 28, true);
        clock.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(clock, fullWidth());

        hud = text("", 12, false);
        hud.setTypeface(android.graphics.Typeface.MONOSPACE);
        hud.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(hud, fullWidth());

        search = new EditText(this);
        search.setHint("Search apps");
        search.setSingleLine(true);
        search.setTextColor(Color.WHITE);
        search.setHintTextColor(Color.GRAY);
        search.setBackgroundColor(Color.rgb(18, 21, 28));
        search.setPadding(18, 8, 18, 8);
        LinearLayout.LayoutParams searchParams = fullWidth();
        searchParams.setMargins(0, 10, 0, 8);
        root.addView(search, searchParams);

        ScrollView scroll = new ScrollView(this);
        appGrid = new GridLayout(this);
        appGrid.setColumnCount(4);
        appGrid.setAlignmentMode(GridLayout.ALIGN_BOUNDS);
        appGrid.setUseDefaultMargins(false);
        scroll.addView(appGrid);
        root.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout dock = new LinearLayout(this);
        dock.setOrientation(LinearLayout.HORIZONTAL);

        Button refresh = dockButton("REFRESH");
        refresh.setOnClickListener(v -> loadApps());
        dock.addView(refresh, weighted());

        Button performance = dockButton("PERFORMANCE");
        performance.setOnClickListener(v ->
                startActivity(new Intent(this, PerformanceLauncherActivity.class)));
        dock.addView(performance, weighted());

        Button lab = dockButton("MUNDO X");
        lab.setOnClickListener(v ->
                startActivity(new Intent(this, MainActivity.class)));
        dock.addView(lab, weighted());

        Button settings = dockButton("SETTINGS");
        settings.setOnClickListener(v -> {
            try {
                startActivity(new Intent(Settings.ACTION_SETTINGS));
            } catch (Throwable ignored) {}
        });
        dock.addView(settings, weighted());

        root.addView(dock, fullWidth());

        setContentView(root);

        search.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                renderApps(s == null ? "" : s.toString());
            }
            @Override public void afterTextChanged(Editable s) {}
        });

        loadApps();
        updateClock();
        updateHud();
    }

    @Override
    protected void onResume() {
        super.onResume();
        handler.removeCallbacks(ticker);
        handler.post(ticker);
    }

    @Override
    protected void onPause() {
        handler.removeCallbacks(ticker);
        super.onPause();
    }

    private void loadApps() {
        apps.clear();

        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.addCategory(Intent.CATEGORY_LAUNCHER);

        List<ResolveInfo> found = getPackageManager().queryIntentActivities(intent, 0);
        for (ResolveInfo info : found) {
            if (info.activityInfo == null) continue;

            String pkg = info.activityInfo.packageName;
            String cls = info.activityInfo.name;

            // Do not show this HOME activity as a regular launch target.
            if (getPackageName().equals(pkg) &&
                    DeviceLauncherActivity.class.getName().equals(cls)) {
                continue;
            }

            CharSequence labelCs = info.loadLabel(getPackageManager());
            String label = labelCs == null ? pkg : labelCs.toString();
            apps.add(new AppEntry(label, pkg, cls));
        }

        Collections.sort(apps, Comparator.comparing(
                a -> a.label.toLowerCase(Locale.US)));

        renderApps(search == null ? "" : search.getText().toString());
    }

    private void renderApps(String query) {
        if (appGrid == null) return;

        String q = query == null ? "" : query.trim().toLowerCase(Locale.US);
        appGrid.removeAllViews();

        for (AppEntry app : apps) {
            if (!q.isEmpty() &&
                    !app.label.toLowerCase(Locale.US).contains(q) &&
                    !app.packageName.toLowerCase(Locale.US).contains(q)) {
                continue;
            }

            Button b = new Button(this);
            b.setAllCaps(false);
            b.setText(app.label);
            b.setTextSize(11);
            b.setTextColor(Color.WHITE);
            b.setBackgroundColor(Color.rgb(14, 17, 23));
            b.setPadding(4, 8, 4, 8);
            b.setGravity(Gravity.CENTER);

            GridLayout.LayoutParams p = new GridLayout.LayoutParams();
            p.width = 0;
            p.height = 150;
            p.columnSpec = GridLayout.spec(GridLayout.UNDEFINED, 1f);
            p.setMargins(4, 4, 4, 4);
            b.setLayoutParams(p);

            b.setOnClickListener(v -> launch(app));
            b.setOnLongClickListener(v -> {
                openAppInfo(app.packageName);
                return true;
            });

            appGrid.addView(b);
        }
    }

    private void launch(AppEntry app) {
        Intent i = new Intent(Intent.ACTION_MAIN);
        i.addCategory(Intent.CATEGORY_LAUNCHER);
        i.setComponent(new ComponentName(app.packageName, app.className));
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);

        try {
            startActivity(i);
        } catch (Throwable t) {
            Intent fallback = getPackageManager().getLaunchIntentForPackage(app.packageName);
            if (fallback != null) {
                fallback.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(fallback);
            }
        }
    }

    private void openAppInfo(String packageName) {
        try {
            Intent i = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
            i.setData(android.net.Uri.parse("package:" + packageName));
            startActivity(i);
        } catch (Throwable ignored) {}
    }

    private void updateClock() {
        if (clock == null) return;
        String now = DateFormat.getTimeInstance(DateFormat.SHORT).format(new Date());
        String date = DateFormat.getDateInstance(DateFormat.MEDIUM).format(new Date());
        clock.setText(now + "\n" + date);
    }

    private void updateHud() {
        if (hud == null) return;

        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(memory);

        long freeMiB = memory.availMem / (1024L * 1024L);
        long totalMiB = memory.totalMem / (1024L * 1024L);

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        boolean saver = pm != null && pm.isPowerSaveMode();

        int thermal = -1;
        if (pm != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            thermal = pm.getCurrentThermalStatus();
        }

        float temp = -1f;
        Intent battery = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (battery != null) {
            int raw = battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, -1);
            if (raw >= 0) temp = raw / 10f;
        }

        StringBuilder s = new StringBuilder();
        s.append("RAM ")
                .append(freeMiB).append("/")
                .append(totalMiB).append(" MiB");
        if (temp >= 0) {
            s.append(String.format(Locale.US, "  %.1f°C", temp));
        }
        s.append("  thermal=").append(thermalName(thermal));
        if (saver) s.append("  SAVER");
        hud.setText(s.toString());
    }

    private String thermalName(int thermal) {
        if (thermal < 0) return "N/A";
        switch (thermal) {
            case PowerManager.THERMAL_STATUS_NONE: return "NONE";
            case PowerManager.THERMAL_STATUS_LIGHT: return "LIGHT";
            case PowerManager.THERMAL_STATUS_MODERATE: return "MOD";
            case PowerManager.THERMAL_STATUS_SEVERE: return "SEVERE";
            case PowerManager.THERMAL_STATUS_CRITICAL: return "CRIT";
            case PowerManager.THERMAL_STATUS_EMERGENCY: return "EMERG";
            case PowerManager.THERMAL_STATUS_SHUTDOWN: return "STOP";
            default: return "?";
        }
    }

    private Button dockButton(String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(10);
        b.setAllCaps(false);
        return b;
    }

    private TextView text(String value, int sp, boolean bold) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextColor(Color.WHITE);
        t.setTextSize(sp);
        if (bold) t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        return t;
    }

    private LinearLayout.LayoutParams fullWidth() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams weighted() {
        return new LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1f);
    }
}
