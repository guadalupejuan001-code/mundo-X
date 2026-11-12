package com.mundox.worldlink;

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
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.google.android.gms.wearable.MessageClient;
import com.google.android.gms.wearable.MessageEvent;
import com.google.android.gms.wearable.Node;
import com.google.android.gms.wearable.Wearable;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.UUID;

/**
 * Wrist interface for Mundo X. Only actual accessible sensors are used;
 * streams run while the activity is visible and the user has pressed Start.
 */
public final class WatchActivity extends Activity
        implements SensorEventListener, MessageClient.OnMessageReceivedListener {
    private static final String SAMPLE_PATH = "/mundox/worldlink/v1/telemetry";
    private static final String HAPTIC_PATH = "/mundox/worldlink/v1/haptic";
    private static final int HEART_PERMISSION_REQUEST = 13;
    private static final long SAMPLE_PERIOD_MS = 300L;
    private static final long MAX_SESSION_MS = 180_000L;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final float[] acceleration = new float[3];
    private final float[] gyro = new float[3];
    private final float[] magnet = new float[3];
    private final List<String> nodes = new ArrayList<>();
    private SensorManager sensors;
    private TextView status;
    private Button streamButton, heartButton;
    private boolean streaming, heartRequested;
    private boolean hasA, hasG, hasM, hasH, hasL;
    private float lux;
    private float heartRate;
    private long sequence, beganAt, lastNodesAt;
    private String sessionId;
    private String message = "Listo";

    @Override public void onCreate(Bundle bundle) {
        super.onCreate(bundle);
        sensors = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setPadding(dp(20),dp(12),dp(20),dp(14));
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        TextView title = new TextView(this);
        title.setText("MUNDO X\nENLACE");
        title.setTextColor(0xffd5ee76);
        title.setTextSize(21);
        title.setGravity(Gravity.CENTER);
        root.addView(title);
        status = new TextView(this);
        status.setTextSize(12);
        status.setTextColor(Color.WHITE);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0,dp(8),0,dp(8));
        root.addView(status);
        streamButton = new Button(this);
        streamButton.setAllCaps(false);
        streamButton.setText("INICIAR");
        streamButton.setOnClickListener(v -> {
            if (streaming) stopStreaming();
            else startStreaming();
        });
        root.addView(streamButton);
        heartButton = new Button(this);
        heartButton.setAllCaps(false);
        heartButton.setText("Activar pulso (opcional)");
        heartButton.setOnClickListener(v -> enableHeartSensor());
        root.addView(heartButton);
        TextView privacy = new TextView(this);
        privacy.setText("Señales reales. Control digital de un núcleo simulado.\n"
                + "Detener para apagar la lectura.");
        privacy.setTextSize(10);
        privacy.setTextColor(0xffb8b8b8);
        privacy.setGravity(Gravity.CENTER);
        root.addView(privacy);
        scroll.addView(root);
        setContentView(scroll);
        refresh();
    }

    @Override protected void onResume() {
        super.onResume();
        Wearable.getMessageClient(this).addListener(this);
    }

    @Override protected void onPause() {
        stopStreaming();
        Wearable.getMessageClient(this).removeListener(this);
        super.onPause();
    }

    @Override protected void onDestroy() {
        stopStreaming();
        super.onDestroy();
    }

    private void startStreaming() {
        if (streaming) return;
        streaming = true;
        sequence = 0;
        hasA = false; hasG = false; hasM = false; hasH = false; hasL = false;
        sessionId = UUID.randomUUID().toString();
        beganAt = SystemClock.uptimeMillis();
        lastNodesAt = -100_000L;
        streamButton.setText("DETENER");
        register(Sensor.TYPE_ACCELEROMETER);
        register(Sensor.TYPE_GYROSCOPE);
        register(Sensor.TYPE_MAGNETIC_FIELD);
        register(Sensor.TYPE_LIGHT);
        if (heartRequested && checkSelfPermission(Manifest.permission.BODY_SENSORS)
                == PackageManager.PERMISSION_GRANTED) register(Sensor.TYPE_HEART_RATE);
        message = "Buscando teléfono...";
        refresh();
        tick.run();
    }

    private void register(int type) {
        Sensor sensor = sensors.getDefaultSensor(type);
        if (sensor != null) sensors.registerListener(this, sensor, SensorManager.SENSOR_DELAY_UI);
    }

    private void stopStreaming() {
        streaming = false;
        handler.removeCallbacks(tick);
        if (sensors != null) sensors.unregisterListener(this);
        if (streamButton != null) streamButton.setText("INICIAR");
        message = "Detenido";
        refresh();
    }

    private void enableHeartSensor() {
        if (checkSelfPermission(Manifest.permission.BODY_SENSORS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.BODY_SENSORS}, HEART_PERMISSION_REQUEST);
            return;
        }
        heartRequested = true;
        heartButton.setText("Pulso autorizado");
        if (streaming) register(Sensor.TYPE_HEART_RATE);
        refresh();
    }

    @Override public void onRequestPermissionsResult(int req, String[] permissions, int[] grants) {
        super.onRequestPermissionsResult(req,permissions,grants);
        if (req != HEART_PERMISSION_REQUEST) return;
        if (grants.length > 0 && grants[0] == PackageManager.PERMISSION_GRANTED) enableHeartSensor();
        else {
            message = "Sin permiso de pulso; movimiento y magnetismo siguen disponibles.";
            refresh();
        }
    }

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!streaming) return;
            long now = SystemClock.uptimeMillis();
            if (now - beganAt >= MAX_SESSION_MS) {
                stopStreaming();
                message = "Sesión de 3 minutos terminada; inicia de nuevo.";
                refresh();
                return;
            }
            if (now - lastNodesAt >= 5_000L) findNodes();
            publish();
            refresh();
            handler.postDelayed(this, SAMPLE_PERIOD_MS);
        }
    };

    private void findNodes() {
        lastNodesAt = SystemClock.uptimeMillis();
        Wearable.getNodeClient(this).getConnectedNodes().addOnSuccessListener(connected -> {
            nodes.clear();
            for (Node n : connected) nodes.add(n.getId());
        }).addOnFailureListener(e -> message = "No hay conexión Wear OS: " + e.getMessage());
    }

    private void publish() {
        if (!hasA || !hasG) return;
        try {
            int mask = 1 | 2 | (hasM ? 4 : 0) | (hasH ? 8 : 0) | (hasL ? 16 : 0);
            JSONObject data = new JSONObject();
            data.put("v", 1);
            data.put("sid", sessionId);
            data.put("seq", ++sequence);
            data.put("t", System.currentTimeMillis());
            data.put("mask", mask);
            data.put("ax", acceleration[0]); data.put("ay", acceleration[1]); data.put("az", acceleration[2]);
            data.put("gx", gyro[0]); data.put("gy", gyro[1]); data.put("gz", gyro[2]);
            data.put("mx", magnet[0]); data.put("my", magnet[1]); data.put("mz", magnet[2]);
            data.put("hr", hasH ? heartRate : 0);
            data.put("lux", hasL ? lux : 0);
            byte[] payload = data.toString().getBytes(StandardCharsets.UTF_8);
            for (String id : nodes) Wearable.getMessageClient(this).sendMessage(id, SAMPLE_PATH, payload);
        } catch (Exception e) {
            message = "Lectura inválida: " + e.getMessage();
        }
    }

    @Override public void onSensorChanged(SensorEvent event) {
        float[] values = event.values;
        switch (event.sensor.getType()) {
            case Sensor.TYPE_ACCELEROMETER:
                if (values.length >= 3) { System.arraycopy(values,0,acceleration,0,3); hasA = true; }
                break;
            case Sensor.TYPE_GYROSCOPE:
                if (values.length >= 3) { System.arraycopy(values,0,gyro,0,3); hasG = true; }
                break;
            case Sensor.TYPE_MAGNETIC_FIELD:
                if (values.length >= 3) { System.arraycopy(values,0,magnet,0,3); hasM = true; }
                break;
            case Sensor.TYPE_LIGHT:
                if (values.length > 0 && Float.isFinite(values[0]) && values[0] >= 0) {
                    lux = values[0]; hasL = true;
                }
                break;
            case Sensor.TYPE_HEART_RATE:
                if (values.length > 0 && values[0] > 0) { heartRate = values[0]; hasH = true; }
                break;
            default: break;
        }
    }

    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {}

    @Override public void onMessageReceived(MessageEvent event) {
        if (!HAPTIC_PATH.equals(event.getPath())) return;
        Vibrator vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator != null && vibrator.hasVibrator()) {
            vibrator.vibrate(VibrationEffect.createOneShot(90, VibrationEffect.DEFAULT_AMPLITUDE));
        }
    }

    private void refresh() {
        if (status == null) return;
        String t = streaming ? String.format(Locale.US,
                "ENLACE ACTIVO\nDestino(s): %d\nEnviadas: %d\nCampo: %s\nPulso: %s\nLuz: %s",
                nodes.size(), sequence, hasM ? "detectado" : "sin lectura",
                hasH ? (int)heartRate + " lpm" : "no disponible",
                hasL ? String.format(Locale.US,"%.1f lux",lux) : "sin sensor") : message;
        status.setText(t);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
