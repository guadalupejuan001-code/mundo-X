package com.mundox.watchoptics;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Consent-driven, foreground-only instrumentation of the WATCH, not a hardware
 * override. On-watch UI displays digital sensor readings. No network access.
 */
public final class MainActivity extends Activity implements SensorEventListener {
    private static final int REQUEST_OPTICAL = 700;
    private static final long MAX_SESSION_MS = 60000L;
    private static final int MAX_ROWS = 12000;
    private SensorManager sensors;
    private final java.util.ArrayList<Sensor> activeSensors = new java.util.ArrayList<>();
    private TextView status, samples;
    private boolean capturing;
    private long startElapsedMs;
    private int rows;
    private StringBuilder csv;
    private OpticalReader ppg;
    private long lastUiUpdate;

    private TextView label(String value, int sp) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextSize(sp);
        t.setTextColor(Color.WHITE);
        t.setPadding(8, 7, 8, 7);
        t.setGravity(Gravity.CENTER);
        return t;
    }

    private void button(LinearLayout layout, String name, View.OnClickListener click) {
        Button b = new Button(this);
        b.setText(name);
        layout.addView(b);
        b.setOnClickListener(click);
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        sensors = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(15, 24, 15, 20);
        layout.setBackgroundColor(Color.rgb(15, 18, 24));
        layout.addView(label("X OPTICAL LAB", 20));
        layout.addView(label("Galaxy Watch4 • recepción óptica", 12));
        status = label("Listo: sin lectura activa", 12);
        samples = label("PPG bruto exige biblioteca Samsung oficial", 11);
        button(layout, "Ver sensores", v -> showInventory());
        button(layout, "Registrar 60 s", v -> beginSession());
        button(layout, "PPG óptico bruto", v -> startOptical());
        button(layout, "Detener y guardar", v -> finishSession());
        layout.addView(status);
        layout.addView(samples);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(layout);
        setContentView(scroll);
        showInventory();
    }

    private void showInventory() {
        List<Sensor> all = sensors.getSensorList(Sensor.TYPE_ALL);
        boolean acc = sensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) != null;
        boolean gyro = sensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE) != null;
        boolean light = sensors.getDefaultSensor(Sensor.TYPE_LIGHT) != null;
        boolean heart = sensors.getDefaultSensor(Sensor.TYPE_HEART_RATE) != null;
        status.setText("Inventario Android (" + all.size() + " sensores)\n" +
                "Acelerómetro: " + acc + " • Giro: " + gyro + "\n" +
                "Luz ambiental (frontal): " + light + "\n" +
                "Pulso procesado: " + heart + "\n" +
                "PPG óptico bruto: requiere Samsung SDK + compatibilidad");
    }

    private void beginSession() {
        if (capturing) {
            status.setText("Sesión ya activa");
            return;
        }
        csv = new StringBuilder(8192);
        csv.append("wall_ms,source,device_time,channel_1,channel_2,channel_3,status_1,status_2,status_3\n");
        capturing = true;
        rows = 0;
        startElapsedMs = SystemClock.elapsedRealtime();
        lastUiUpdate = 0L;
        for (int kind : new int[]{Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_GYROSCOPE, Sensor.TYPE_LIGHT}) {
            Sensor s = sensors.getDefaultSensor(kind);
            if (s != null && sensors.registerListener(this, s, SensorManager.SENSOR_DELAY_GAME)) {
                activeSensors.add(s);
            }
        }
        status.setText("Registro iniciado • máximo 60 s\nSin red y con datos locales");
    }

    private void startOptical() {
        if (!capturing) beginSession();
        if (!capturing) return;
        if (checkSelfPermission(Manifest.permission.BODY_SENSORS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.BODY_SENSORS}, REQUEST_OPTICAL);
            return;
        }
        if (ppg != null) {
            status.setText("PPG ya solicitado");
            return;
        }
        try {
            Class<?> type = Class.forName("com.mundox.watchoptics.SamsungPpgReader");
            OpticalReader.Callback sink = new OpticalReader.Callback() {
                @Override public void onPpg(long sensorMs, Integer g, Integer r, Integer ir,
                                            Integer gs, Integer rs, Integer is) {
                    runOnUiThread(() -> {
                        record("ppg", sensorMs, display(g), display(r), display(ir),
                                display(gs), display(rs), display(is));
                        if (capturing) samples.setText("PPG digital (ADC)\nVerde: " + display(g) +
                                " • Rojo: " + display(r) + "\nInfrarrojo: " + display(ir) +
                                "\nEstado G/R/IR: " + display(gs) + "/" + display(rs) + "/" + display(is));
                    });
                }
                @Override public void onStatus(String message) {
                    runOnUiThread(() -> status.setText("PPG: " + message));
                }
            };
            ppg = (OpticalReader) type.getConstructor(Activity.class, OpticalReader.Callback.class)
                    .newInstance(this, sink);
            ppg.start();
        } catch (ReflectiveOperationException | LinkageError e) {
            ppg = null;
            status.setText("PPG sin conexión: falta SDK Samsung oficial o módulo compatible.\n" +
                    "Acelerómetro, giroscopio y luz siguen disponibles.");
        }
    }

    private static String display(Integer number) {
        return number == null ? "" : Integer.toString(number);
    }

    private void record(String source, long sourceTime, String a, String b, String c,
                        String st1, String st2, String st3) {
        if (!capturing) return;
        if (SystemClock.elapsedRealtime() - startElapsedMs > MAX_SESSION_MS || rows >= MAX_ROWS) {
            finishSession();
            return;
        }
        csv.append(System.currentTimeMillis()).append(',').append(source).append(',')
                .append(sourceTime).append(',').append(a).append(',').append(b).append(',')
                .append(c).append(',').append(st1).append(',').append(st2).append(',')
                .append(st3).append('\n');
        ++rows;
    }

    @Override public void onSensorChanged(SensorEvent event) {
        if (!capturing) return;
        String kind = event.sensor.getType() == Sensor.TYPE_ACCELEROMETER ? "accelerometer" :
                      event.sensor.getType() == Sensor.TYPE_GYROSCOPE ? "gyroscope" :
                      event.sensor.getType() == Sensor.TYPE_LIGHT ? "ambient_light" : "other";
        float[] value = event.values;
        String a = value.length > 0 ? Float.toString(value[0]) : "";
        String b = value.length > 1 ? Float.toString(value[1]) : "";
        String c = value.length > 2 ? Float.toString(value[2]) : "";
        record(kind, event.timestamp, a, b, c, "", "", "");
        long now = SystemClock.elapsedRealtime();
        if (now - lastUiUpdate >= 400) {
            lastUiUpdate = now;
            samples.setText("Último dato: " + kind + "\n" + a + " " + b + " " + c +
                    "\nMuestras: " + rows + " / " + MAX_ROWS);
        }
    }

    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {
        // No fidelity/medical diagnosis is inferred from Android accuracy codes.
    }

    private void finishSession() {
        if (!capturing) return;
        capturing = false;
        sensors.unregisterListener(this);
        activeSensors.clear();
        if (ppg != null) {
            ppg.stop();
            ppg = null;
        }
        try {
            String stamp = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
            File file = new File(getFilesDir(), "optical_" + stamp + ".csv");
            try (FileOutputStream out = new FileOutputStream(file)) {
                out.write(csv.toString().getBytes(StandardCharsets.UTF_8));
            }
            status.setText("Guardado: " + rows + " muestras\n" + file.getName() +
                    "\nSolo en almacenamiento privado del reloj");
        } catch (Exception e) {
            status.setText("No se pudo guardar: " + e.getClass().getSimpleName());
        } finally {
            csv = null;
        }
    }

    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        if (code == REQUEST_OPTICAL && results.length > 0 &&
            results[0] == PackageManager.PERMISSION_GRANTED) {
            startOptical();
        } else if (code == REQUEST_OPTICAL) {
            status.setText("Sin permiso: PPG desactivado. Otras lecturas siguen disponibles.");
        }
    }

    @Override protected void onStop() {
        finishSession();  // Hard stop on leaving the foreground: no hidden capture.
        super.onStop();
    }

    @Override protected void onDestroy() {
        sensors.unregisterListener(this);
        if (ppg != null) { ppg.stop(); ppg = null; }
        super.onDestroy();
    }
}
