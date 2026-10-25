package com.mundox.ghzwatch;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.AtomicFile;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Bounded foreground probe: real Android sensor events and haptic requests. */
public final class HardwareActivity extends Activity implements SensorEventListener {
    private static final long MS = ProbeAnalysis.MS;
    private static final long DURATION_MS = 13000;
    private static final int MAX_SAMPLES = 12000;
    private static final ExecutorService WRITER = Executors.newSingleThreadExecutor();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final List<ProbeAnalysis.Sample> samples = new ArrayList<>();
    private final List<Trial> trials = new ArrayList<>();
    private SensorManager sensors;
    private Sensor acceleration, magnetic;
    private Vibrator vibrator;
    private TextView status, result;
    private Button start, stop;
    private JSONObject inventory;
    private volatile boolean running, saving;
    private boolean destroyed;
    private int rejected;
    private long startedNs;

    private static final class Trial {
        final boolean pulse;
        final long plannedNs;
        long commandNs;
        boolean submitted;
        Trial(boolean pulse, long plannedNs) { this.pulse = pulse; this.plannedNs = plannedNs; }
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        sensors = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (sensors != null) {
            acceleration = sensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            magnetic = sensors.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED);
            if (magnetic == null) magnetic = sensors.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD);
        }
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.BLACK);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.setOnGenericMotionListener((view, event) -> {
            if (event.getAction() == MotionEvent.ACTION_SCROLL) {
                scroll.smoothScrollBy(0, Math.round(-event.getAxisValue(MotionEvent.AXIS_SCROLL) * dp(38)));
                return true;
            }
            return false;
        });
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(dp(30), dp(28), dp(30), dp(54));
        scroll.addView(column);
        text(column, "HARDWARE", 18, Color.WHITE);
        text(column, "MUNDO X · PRUEBA FÍSICA", 10, Color.rgb(237,72,82));
        status = text(column, "Listo · sensores detenidos", 13, Color.WHITE);
        status.setId(R.id.hardware_status);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        start = button(column, "INICIAR · 13 SEGUNDOS", R.id.hardware_start);
        stop = button(column, "DETENER", R.id.hardware_stop);
        button(column, "VOLVER AL GHZ", View.NO_ID).setOnClickListener(v -> finish());
        text(column, "Deja el reloj apoyado e inmóvil, fuera del cargador.\n\n2 pulsos de 200 ms y 2 controles sin vibración, en orden aleatorio.", 12, Color.LTGRAY);
        result = text(column, "Se comparan lecturas antes y después de cada orden. Los datos quedan en este reloj.", 12, Color.WHITE);
        String details;
        try {
            inventory = makeInventory();
            details = describeInventory();
        } catch (JSONException error) {
            inventory = new JSONObject();
            details = "No se pudo describir el hardware: " + error.getMessage();
        }
        TextView description = text(column, details, 11, Color.LTGRAY);
        description.setId(R.id.hardware_inventory);
        text(column, "Una variación puede venir del movimiento, del motor o del entorno. Esta prueba no identifica qubits ni mide entrelazamiento.\n\nEl magnetómetro lee campo; no lo genera. La frecuencia del motor no se configura aquí.", 11, Color.LTGRAY);
        setContentView(scroll);
        start.setOnClickListener(v -> startProbe());
        stop.setOnClickListener(v -> finishProbe("cancelled_by_user"));
        updateButtons();
        if (!available()) status.setText("Prueba no disponible · consulta el inventario");
    }

    private boolean available() {
        return acceleration != null && magnetic != null && vibrator != null && vibrator.hasVibrator();
    }

    private void startProbe() {
        if (running || saving || !available()) return;
        samples.clear(); trials.clear(); rejected = 0;
        startedNs = SystemClock.elapsedRealtimeNanos();
        List<Boolean> order = ProbeAnalysis.order(new SecureRandom());
        for (int i = 0; i < order.size(); i++)
            trials.add(new Trial(order.get(i), startedNs + (2000 + i * 3000L) * MS));
        running = true;
        result.setText("Registrando eventos del dispositivo…");
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        try {
            boolean a = sensors.registerListener(this, acceleration, 20000, 0, handler);
            boolean b = sensors.registerListener(this, magnetic, 20000, 0, handler);
            if (!a || !b) { finishProbe("sensor_registration_failed"); return; }
        } catch (RuntimeException error) {
            finishProbe("sensor_registration_exception: " + error.getClass().getSimpleName()); return;
        }
        for (Trial trial : trials) {
            long delay = Math.max(0, (trial.plannedNs - SystemClock.elapsedRealtimeNanos()) / MS);
            handler.postDelayed(() -> sendTrial(trial), delay);
        }
        handler.postDelayed(() -> finishProbe("completed"), Math.max(0,
            DURATION_MS - (SystemClock.elapsedRealtimeNanos() - startedNs) / MS));
        updateButtons();
        tick();
    }

    private void tick() {
        if (!running) return;
        long left = Math.max(0, DURATION_MS - (SystemClock.elapsedRealtimeNanos() - startedNs) / MS);
        status.setText(String.format(Locale.US, "Registrando · %.1f s\n%d muestras", left / 1000.0, samples.size()));
        handler.postDelayed(this::tick, 250);
    }

    private void sendTrial(Trial trial) {
        if (!running) return;
        trial.commandNs = SystemClock.elapsedRealtimeNanos();
        if (trial.commandNs - trial.plannedNs > 250 * MS) {
            finishProbe("scheduler_delay"); return;
        }
        if (trial.pulse) {
            try {
                vibrator.vibrate(VibrationEffect.createOneShot(200, VibrationEffect.DEFAULT_AMPLITUDE));
                trial.submitted = true; // Request accepted by the API, not physical onset confirmed.
            } catch (RuntimeException error) {
                finishProbe("vibration_request_failed");
            }
        }
    }

    @Override public void onSensorChanged(SensorEvent event) {
        if (!running) return;
        long received = SystemClock.elapsedRealtimeNanos();
        if (!ProbeAnalysis.valid(event.values) || event.timestamp < startedNs || event.timestamp > received) {
            rejected++; return;
        }
        if (samples.size() >= MAX_SAMPLES) { finishProbe("sample_limit"); return; }
        samples.add(new ProbeAnalysis.Sample(event.timestamp, received, event.sensor.getType(), event.accuracy, event.values));
    }
    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) { }

    private void finishProbe(String reason) {
        if (!running) return;
        running = false; saving = true;
        handler.removeCallbacksAndMessages(null);
        sensors.unregisterListener(this);
        if (vibrator != null) vibrator.cancel();
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        updateButtons();
        final JSONObject report;
        final String summary;
        try {
            report = makeReport(reason);
            summary = summarize(report);
        } catch (JSONException error) {
            saving = false; updateButtons();
            status.setText("Error al preparar los datos"); return;
        }
        status.setText("Guardando " + samples.size() + " muestras…");
        WRITER.execute(() -> {
            AtomicFile file = new AtomicFile(new File(getFilesDir(), "hardware-latest.json"));
            FileOutputStream stream = null;
            String failure = null;
            try {
                stream = file.startWrite();
                stream.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
                file.finishWrite(stream);
            } catch (Exception error) {
                if (stream != null) file.failWrite(stream);
                failure = error.getClass().getSimpleName();
            }
            final String error = failure;
            runOnUiThread(() -> {
                saving = false;
                if (destroyed) return;
                status.setText(error == null ? ("completed".equals(reason) ? "Prueba terminada · datos guardados" : "Prueba interrumpida · datos parciales") : "No se pudo guardar: " + error);
                result.setText(summary + (error == null ? "\n\nArchivo: hardware-latest.json\nLa siguiente prueba sustituye este registro." : ""));
                updateButtons();
            });
        });
    }

    private JSONObject makeReport(String reason) throws JSONException {
        JSONObject report = new JSONObject();
        report.put("schema", "mundox.hardware-probe.v1");
        report.put("status", reason);
        report.put("source", "Android SensorEvent; emulator readings are not physical-watch evidence");
        report.put("inventory", inventory);
        report.put("start_elapsed_realtime_ns", startedNs);
        report.put("end_elapsed_realtime_ns", SystemClock.elapsedRealtimeNanos());
        report.put("rejected_samples", rejected);
        report.put("requested_sensor_period_us", 20000);
        report.put("pulse_duration_ms", 200);
        report.put("physical_vibration_onset_measured", false);
        report.put("quantum_entanglement_test", false);
        JSONArray comparisons = new JSONArray();
        for (Trial trial : trials) {
            JSONObject row = new JSONObject();
            row.put("condition", trial.pulse ? "pulse_request" : "sham");
            row.put("planned_ns", trial.plannedNs);
            row.put("command_ns", trial.commandNs == 0 ? JSONObject.NULL : trial.commandNs);
            row.put("request_submitted", trial.submitted);
            if (trial.commandNs != 0) {
                row.put("command_lateness_ms", (trial.commandNs - trial.plannedNs) / (double) MS);
                row.put("acceleration", compare(trial.commandNs, acceleration.getType(), "m/s²"));
                row.put("magnetic_field", compare(trial.commandNs, magnetic.getType(), "µT"));
            }
            comparisons.put(row);
        }
        report.put("trials", comparisons);
        report.put("sample_columns", new JSONArray(new String[]{"event_ns", "received_ns", "sensor_type", "accuracy", "values"}));
        JSONArray data = new JSONArray();
        for (ProbeAnalysis.Sample sample : samples) {
            JSONArray values = new JSONArray();
            for (float value : sample.values) values.put((double)value);
            data.put(new JSONArray().put(sample.timeNs).put(sample.receivedNs).put(sample.type).put(sample.accuracy).put(values));
        }
        report.put("samples", data);
        return report;
    }

    private JSONObject compare(long time, int type, String unit) throws JSONException {
        long from = time - 750*MS, to = time - 50*MS, afterTo = time + 700*MS;
        ProbeAnalysis.Window before = ProbeAnalysis.window(samples, type, from, to);
        ProbeAnalysis.Window after = ProbeAnalysis.window(samples, type, time, afterTo);
        boolean enough = before.covers(from, to) && after.covers(time, afterTo);
        return new JSONObject().put("unit", unit).put("coverage_ok", enough)
            .put("before_count", before.count).put("after_count", after.count)
            .put("baseline_start_ns", from).put("baseline_end_ns", to)
            .put("response_start_ns", time).put("response_end_ns", afterTo)
            .put("mean_vector_change", enough ? ProbeAnalysis.delta(before, after) : JSONObject.NULL)
            .put("before_vector_rms", enough ? before.rms() : JSONObject.NULL)
            .put("after_vector_rms", enough ? after.rms() : JSONObject.NULL);
    }

    private String summarize(JSONObject report) throws JSONException {
        StringBuilder text = new StringBuilder("Comparación descriptiva\n");
        JSONArray rows = report.getJSONArray("trials");
        for (int i = 0; i < rows.length(); i++) {
            JSONObject row = rows.getJSONObject(i);
            text.append('\n').append(i + 1).append(" · ")
                .append("sham".equals(row.getString("condition")) ? "CONTROL" : "ORDEN DE PULSO");
            if (!row.has("acceleration")) { text.append("\nNo ejecutado\n"); continue; }
            JSONObject a = row.getJSONObject("acceleration"), b = row.getJSONObject("magnetic_field");
            text.append(b.getBoolean("coverage_ok") ? String.format(Locale.US, "\nΔB: %.3f µT", b.getDouble("mean_vector_change")) : "\nCampo: muestras insuficientes");
            text.append(a.getBoolean("coverage_ok") ? String.format(Locale.US, "\na RMS: %.3f → %.3f m/s²\n", a.getDouble("before_vector_rms"), a.getDouble("after_vector_rms")) : "\nMovimiento: muestras insuficientes\n");
        }
        return text.append("\nΔB = cambio del vector medio.\na RMS = variación alrededor del promedio.\n\n4 intervalos no bastan para atribuir una causa. Las cifras no demuestran entrelazamiento.").toString();
    }

    private JSONObject makeInventory() throws JSONException {
        JSONObject result = new JSONObject().put("manufacturer", Build.MANUFACTURER)
            .put("model", Build.MODEL).put("android_release", Build.VERSION.RELEASE)
            .put("api_level", Build.VERSION.SDK_INT).put("hardware", Build.HARDWARE)
            .put("vibrator_available", vibrator != null && vibrator.hasVibrator())
            .put("amplitude_control", vibrator != null && vibrator.hasAmplitudeControl())
            .put("selected_accelerometer_type", acceleration == null ? JSONObject.NULL : acceleration.getType())
            .put("selected_magnetometer_type", magnetic == null ? JSONObject.NULL : magnetic.getType());
        JSONArray list = new JSONArray();
        if (sensors != null) for (Sensor sensor : sensors.getSensorList(Sensor.TYPE_ALL)) {
            list.put(new JSONObject().put("name", sensor.getName()).put("vendor", sensor.getVendor())
                .put("type", sensor.getType()).put("string_type", sensor.getStringType())
                .put("version", sensor.getVersion()).put("resolution", sensor.getResolution())
                .put("maximum_range", sensor.getMaximumRange()).put("declared_power_ma", sensor.getPower())
                .put("min_delay_us", sensor.getMinDelay()).put("max_delay_us", sensor.getMaxDelay()));
        }
        return result.put("sensors", list);
    }

    private String describeInventory() throws JSONException {
        return "HARDWARE DETECTADO\n" + Build.MANUFACTURER + " " + Build.MODEL
            + "\nAndroid " + Build.VERSION.RELEASE + " · API " + Build.VERSION.SDK_INT
            + "\n\nVibrador: " + (vibrator != null && vibrator.hasVibrator() ? "disponible" : "no disponible")
            + "\nControl de amplitud: " + (vibrator != null && vibrator.hasAmplitudeControl() ? "sí" : "no")
            + "\n\nAcelerómetro:\n" + (acceleration == null ? "no disponible" : acceleration.getName())
            + "\n\nMagnetómetro:\n" + (magnetic == null ? "no disponible" : magnetic.getName())
            + (magnetic == null ? "" : magnetic.getType() == Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED ? "\nSin calibración de hierro duro" : "\nCalibrado por Android")
            + "\n\nSensores enumerados: " + inventory.getJSONArray("sensors").length()
            + "\nInventario completo en el registro.\nEl registro usa tiempo monotónico; no depende de la hora del reloj.";
    }

    private void updateButtons() {
        start.setEnabled(!running && !saving && available()); stop.setEnabled(running);
        start.setAlpha(start.isEnabled() ? 1f : .4f); stop.setAlpha(stop.isEnabled() ? 1f : .4f);
    }
    private TextView text(LinearLayout parent, String value, int size, int color) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size);
        view.setTextColor(color); view.setGravity(Gravity.CENTER); view.setPadding(0,dp(7),0,dp(7));
        parent.addView(view, new LinearLayout.LayoutParams(-1,-2)); return view;
    }
    private Button button(LinearLayout parent, String label, int id) {
        Button view = new Button(this); view.setId(id); view.setText(label); view.setTextSize(11);
        view.setTextColor(Color.WHITE); view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setMinHeight(0); view.setMinimumHeight(0); view.setPadding(0,0,0,0);
        GradientDrawable background = new GradientDrawable(); background.setColor(Color.rgb(36,36,43));
        background.setCornerRadius(dp(22)); view.setBackground(background);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, dp(44)); params.topMargin=dp(6);
        parent.addView(view, params); return view;
    }
    private int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    @Override protected void onPause() { finishProbe("activity_paused"); super.onPause(); }
    @Override protected void onDestroy() { destroyed = true; handler.removeCallbacksAndMessages(null); super.onDestroy(); }
    boolean runningForTest() { return running; }
    boolean savingForTest() { return saving; }
    JSONObject inventoryForTest() { return inventory; }
}
