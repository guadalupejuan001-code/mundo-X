package com.mundox.graphics;

import android.app.AlertDialog;
import android.graphics.Color;
import android.os.SystemClock;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import android.view.Gravity;
import com.google.android.gms.wearable.MessageClient;
import com.google.android.gms.wearable.MessageEvent;
import com.google.android.gms.wearable.Node;
import com.google.android.gms.wearable.Wearable;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Bridge from physical measurements to a simulated quantum state AND an optional
 * real LED photon output, with watch ambient-light feedback.
 * Does not assert physical entanglement, quantum advantage, or spacetime control.
 */
final class WearLinkBridge {
    private static final String SAMPLE_PATH = "/mundox/worldlink/v1/telemetry";
    private static final String HAPTIC_PATH = "/mundox/worldlink/v1/haptic";
    private final MainActivity host;
    private final WearLinkRules rules = new WearLinkRules();
    private final PhoneLightEmitter emitter;
    private final MessageClient.OnMessageReceivedListener listener = this::onMessage;
    private AlertDialog dialog;
    private TextView telemetry;
    private Button armButton, photonButton;
    private boolean listening, armed;
    private String session = "";
    private long sequence = -1;
    private int count, gates;
    private String result = "Esperando señales del reloj...";
    private String photonResult = "Aún no se ha intentado la prueba luminosa";
    private float lastLux = Float.NaN;
    private float baseLux = Float.NaN, peakLux = Float.NaN;
    private long photonStart;
    private boolean measuringPhoton;

    WearLinkBridge(MainActivity host) {
        this.host = host;
        this.emitter = new PhoneLightEmitter(host);
    }

    void showPanel() {
        if (dialog != null && dialog.isShowing()) return;
        LinearLayout content = new LinearLayout(host);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(18),dp(8),dp(18),dp(8));
        TextView guide = new TextView(host);
        guide.setText("CUERPO → RELOJ → MUNDO X → LUZ REAL → RELOJ\n\n"
                + "Abre Mundo X Enlace en tu Watch4 Classic y pulsa INICIAR. "
                + "El reloj mide movimiento, magnetismo, luz ambiental y opcionalmente pulso.\n\n"
                + "Un giro de muñeca activado aplica H al núcleo simulado. "
                + "Si habilitas la linterna del teléfono, el gesto también emite fotones reales "
                + "durante 1,2 segundos. Acerca la cara del reloj hacia el LED sin mirar directamente la luz.");
        guide.setTextSize(13);
        guide.setTextColor(Color.WHITE);
        content.addView(guide);
        telemetry = new TextView(host);
        telemetry.setTextSize(12);
        telemetry.setTextColor(0xffc6ef72);
        telemetry.setPadding(0,dp(10),0,dp(10));
        content.addView(telemetry);
        armButton = new Button(host);
        armButton.setAllCaps(false);
        armButton.setOnClickListener(v -> {
            armed = !armed;
            rules.reset();
            updateArmLabel();
        });
        content.addView(armButton);
        photonButton = new Button(host);
        photonButton.setAllCaps(false);
        photonButton.setOnClickListener(v -> {
            photonResult = emitter.toggle();
            updatePhotonLabel();
            refresh();
        });
        content.addView(photonButton);
        Button probe = new Button(host);
        probe.setAllCaps(false);
        probe.setText("Probar luz física y medir lux");
        probe.setOnClickListener(v -> triggerPhotonPulse());
        content.addView(probe);
        Button ping = new Button(host);
        ping.setAllCaps(false);
        ping.setText("Probar vibración del reloj");
        ping.setOnClickListener(v -> vibrateWatch());
        content.addView(ping);
        TextView note = new TextView(host);
        note.setText("La variación de lux debe compararse con la luz apagada. "
                + "Los fotones emitidos son físicos; no hay entrelazamiento cuántico externo demostrado.");
        note.setTextSize(11);
        note.setGravity(Gravity.CENTER);
        content.addView(note);

        dialog = new AlertDialog.Builder(host)
                .setTitle("Mundo X · Enlace físico")
                .setView(content)
                .setNegativeButton("Cerrar", null)
                .create();
        dialog.setOnDismissListener(d -> {
            armed = false;
            stop();
            telemetry = null;
            armButton = null;
            photonButton = null;
            dialog = null;
        });
        dialog.show();
        updateArmLabel();
        updatePhotonLabel();
        refresh();
        if (host.resumed) start();
    }

    void resumeIfVisible() {
        if (dialog != null && dialog.isShowing()) start();
    }

    void start() {
        if (listening) return;
        try {
            Wearable.getMessageClient(host).addListener(listener)
                    .addOnFailureListener(e -> host.runOnUiThread(() -> {
                        result = "No se puede escuchar el reloj: " + e.getMessage();
                        refresh();
                    }));
            listening = true;
            result = "Escuchando... activa INICIAR en el reloj.";
            refresh();
        } catch (RuntimeException e) {
            result = "Wear OS no disponible: " + e.getMessage();
            refresh();
        }
    }

    void stop() {
        armed = false;
        emitter.off();
        if (!listening) return;
        listening = false;
        try { Wearable.getMessageClient(host).removeListener(listener); }
        catch (RuntimeException ignored) { }
    }

    void close() {
        stop();
        emitter.close();
        if (dialog != null) dialog.dismiss();
    }

    void permissionResult(boolean granted) {
        photonResult = emitter.onPermissionResult(granted);
        updatePhotonLabel();
        refresh();
    }

    private void onMessage(MessageEvent event) {
        if (!SAMPLE_PATH.equals(event.getPath()) || !listening) return;
        byte[] packet = event.getData();
        if (packet == null || packet.length > 4096) return;
        host.runOnUiThread(() -> onSample(packet));
    }

    private void onSample(byte[] packet) {
        if (!host.resumed || !listening || dialog == null || !dialog.isShowing()) return;
        try {
            JSONObject json = new JSONObject(new String(packet,StandardCharsets.UTF_8));
            if (json.optInt("v",-1) != 1) return;
            String sid = json.optString("sid","");
            if (sid.length() < 6 || sid.length() > 80) return;
            long seq = json.optLong("seq",-1);
            if (!sid.equals(session)) {
                session = sid;
                sequence = -1;
                rules.reset();
            }
            if (seq <= sequence) return;
            sequence = seq;
            float ax = (float) json.optDouble("ax",Double.NaN);
            float ay = (float) json.optDouble("ay",Double.NaN);
            float az = (float) json.optDouble("az",Double.NaN);
            float gx = (float) json.optDouble("gx",Double.NaN);
            float gy = (float) json.optDouble("gy",Double.NaN);
            float gz = (float) json.optDouble("gz",Double.NaN);
            float mx = (float) json.optDouble("mx",Double.NaN);
            float my = (float) json.optDouble("my",Double.NaN);
            float mz = (float) json.optDouble("mz",Double.NaN);
            int mask = json.optInt("mask",0);
            float lux = (float) json.optDouble("lux",Double.NaN);
            double heartRate = json.optDouble("hr",0.0);

            if ((mask & 16) != 0 && Float.isFinite(lux) && lux >= 0) {
                lastLux = lux;
                if (measuringPhoton && SystemClock.uptimeMillis() - photonStart <= 2200) {
                    if (!Float.isFinite(peakLux) || lux > peakLux) peakLux = lux;
                }
            }
            if (measuringPhoton && SystemClock.uptimeMillis() - photonStart > 2200) {
                measuringPhoton = false;
                photonResult = Float.isFinite(peakLux) && Float.isFinite(baseLux)
                        ? String.format(Locale.US,"Cambio observado: %+.1f lux (repetir con control)",peakLux-baseLux)
                        : "Sin datos suficientes de lux para verificar";
            }

            count++;
            boolean gesture = (mask & 3) == 3
                    && rules.observe(SystemClock.uptimeMillis(),ax,ay,az,gx,gy,gz,armed);
            if (gesture) {
                gates++;
                host.applyWearGesture();
                vibrateWatch();
                if (emitter.enabled()) triggerPhotonPulse();
            }

            result = String.format(Locale.US,
                "Muestras: %d | H virtuales: %d\n"
                + "Movimiento: %.1f, %.1f, %.1f m/s²\n"
                + "Giro: %.2f, %.2f, %.2f rad/s\n"
                + "Magnetómetro: %s\n"
                + "Pulso: %s\n"
                + "Luz ambiental: %s",
                count,gates,ax,ay,az,gx,gy,gz,
                (mask & 4) != 0 ? String.format(Locale.US,"%.1f, %.1f, %.1f µT",mx,my,mz) : "sin lectura",
                (mask & 8) != 0 && heartRate > 0 ? String.format(Locale.US,"%.0f lpm",heartRate) : "no disponible",
                (mask & 16) != 0 && Float.isFinite(lux) ? String.format(Locale.US,"%.1f lux",lux) : "sin lectura");
            refresh();
        } catch (Exception ignored) {
            result = "Lectura descartada: datos inválidos";
            refresh();
        }
    }

    private void triggerPhotonPulse() {
        if (!emitter.enabled()) {
            photonResult = "Habilita la salida física primero.";
        } else if (emitter.pulse()) {
            baseLux = lastLux;
            peakLux = lastLux;
            photonStart = SystemClock.uptimeMillis();
            measuringPhoton = true;
            photonResult = Float.isFinite(baseLux)
                    ? String.format(Locale.US,"Pulso real emitido. Base %.1f lux; midiendo...",baseLux)
                    : "Pulso real emitido, pero falta lectura de luz previa.";
        } else {
            photonResult = "La linterna no está disponible o ya está emitiendo.";
        }
        refresh();
    }

    private void vibrateWatch() {
        try {
            Wearable.getNodeClient(host).getConnectedNodes()
                    .addOnSuccessListener(nodes -> {
                        if (nodes.isEmpty()) Toast.makeText(host,"Reloj no conectado",Toast.LENGTH_SHORT).show();
                        for (Node node : nodes) {
                            Wearable.getMessageClient(host).sendMessage(node.getId(),HAPTIC_PATH,new byte[]{1});
                        }
                    })
                    .addOnFailureListener(e -> Toast.makeText(host,"No se pudo localizar el reloj",Toast.LENGTH_SHORT).show());
        } catch (RuntimeException e) {
            Toast.makeText(host,"Wear OS no disponible",Toast.LENGTH_SHORT).show();
        }
    }

    private void updateArmLabel() {
        if (armButton != null) armButton.setText(armed
                ? "Gesto ACTIVADO · tocar para apagar"
                : "Activar giro de muñeca → H virtual");
    }
    private void updatePhotonLabel() {
        if (photonButton != null) photonButton.setText(emitter.enabled()
                ? "Fotones HABILITADOS · tocar para apagar"
                : "Habilitar LED físico (opcional)");
    }
    private void refresh() {
        if (telemetry != null) telemetry.setText(result + "\n\n" + photonResult);
    }
    private int dp(int value) {
        return Math.round(value * host.getResources().getDisplayMetrics().density);
    }
}
