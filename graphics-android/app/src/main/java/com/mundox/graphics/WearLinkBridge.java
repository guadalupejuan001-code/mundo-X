package com.mundox.graphics;

import android.app.AlertDialog;
import android.graphics.Color;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import com.google.android.gms.wearable.MessageClient;
import com.google.android.gms.wearable.MessageEvent;
import com.google.android.gms.wearable.Node;
import com.google.android.gms.wearable.Wearable;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Local Wear OS data bridge for the existing COSMIC X Graphics world.
 * Wear sensors are physical measurements; gates affect only NativeCore's simulated state.
 */
final class WearLinkBridge {
    private static final String SAMPLE_PATH = "/mundox/worldlink/v1/telemetry";
    private static final String HAPTIC_PATH = "/mundox/worldlink/v1/haptic";
    private final MainActivity host;
    private final WearLinkRules rules = new WearLinkRules();
    private final MessageClient.OnMessageReceivedListener listener = this::onMessage;
    private AlertDialog dialog;
    private TextView telemetry;
    private Button armButton;
    private boolean listening;
    private boolean armed;
    private String session = "";
    private long sequence = -1;
    private int count, gates;
    private String result = "Esperando señales del reloj...";

    WearLinkBridge(MainActivity host) {
        this.host = host;
    }

    void showPanel() {
        if (dialog != null && dialog.isShowing()) return;
        LinearLayout content = new LinearLayout(host);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(18), dp(8), dp(18), dp(6));
        TextView guide = new TextView(host);
        guide.setText("CUERPO → RELOJ → MUNDO X\n\n"
                + "Abre Mundo X Enlace en tu Watch4 Classic y pulsa INICIAR. "
                + "El reloj lee movimiento, magnetismo y el pulso si autorizas ese permiso. "
                + "Ninguna señal corporal sale a un servidor propio.\n\n"
                + "Solo un gesto activado aplica H al qubit seleccionado de la simulación.");
        guide.setTextSize(14);
        guide.setTextColor(Color.WHITE);
        content.addView(guide);
        telemetry = new TextView(host);
        telemetry.setTextSize(14);
        telemetry.setTextColor(0xffc6ef72);
        telemetry.setPadding(0,dp(12),0,dp(14));
        content.addView(telemetry);
        armButton = new Button(host);
        armButton.setAllCaps(false);
        armButton.setOnClickListener(v -> {
            armed = !armed;
            rules.reset();
            updateArmLabel();
            if (armed) Toast.makeText(host, "Giro brusco de muñeca → puerta H virtual", Toast.LENGTH_LONG).show();
        });
        content.addView(armButton);
        Button ping = new Button(host);
        ping.setText("Probar vibración del reloj");
        ping.setAllCaps(false);
        ping.setOnClickListener(v -> vibrateWatch());
        content.addView(ping);
        TextView note = new TextView(host);
        note.setText("La sincronización es digital y medible. No crea entrelazamiento "
                + "físico ni modifica el espacio-tiempo.");
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
            dialog = null;
        });
        updateArmLabel();
        refresh();
        dialog.show();
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
                        result = "No se pudo escuchar el reloj: " + e.getMessage();
                        refresh();
                    }));
            listening = true;
            result = "Escuchando... activa INICIAR en el reloj.";
            refresh();
        } catch (RuntimeException e) {
            result = "Servicios Wear OS no disponibles: " + e.getMessage();
            refresh();
        }
    }

    void stop() {
        armed = false;
        if (!listening) return;
        listening = false;
        try { Wearable.getMessageClient(host).removeListener(listener); }
        catch (RuntimeException ignored) { /* May stop while Play Services disconnects. */ }
    }

    private void onMessage(MessageEvent event) {
        if (!SAMPLE_PATH.equals(event.getPath()) || !listening) return;
        try {
            if (event.getData().length > 4096) return;
            JSONObject json = new JSONObject(new String(event.getData(), StandardCharsets.UTF_8));
            if (json.optInt("v", -1) != 1) return;
            String receivedSession = json.optString("sid", "");
            if (receivedSession.length() < 6 || receivedSession.length() > 80) return;
            long receivedSeq = json.optLong("seq", -1);
            if (!receivedSession.equals(session)) {
                session = receivedSession;
                sequence = -1;
                rules.reset();
            }
            if (receivedSeq <= sequence) return;
            sequence = receivedSeq;

            float ax = (float) json.optDouble("ax", Double.NaN);
            float ay = (float) json.optDouble("ay", Double.NaN);
            float az = (float) json.optDouble("az", Double.NaN);
            float gx = (float) json.optDouble("gx", Double.NaN);
            float gy = (float) json.optDouble("gy", Double.NaN);
            float gz = (float) json.optDouble("gz", Double.NaN);
            float mx = (float) json.optDouble("mx", Double.NaN);
            float my = (float) json.optDouble("my", Double.NaN);
            float mz = (float) json.optDouble("mz", Double.NaN);
            double hr = json.optDouble("hr", 0);
            int mask = json.optInt("mask", 0);

            count++;
            boolean gesture = ((mask & 3) == 3)
                    && rules.observe(SystemClock.uptimeMillis(),ax,ay,az,gx,gy,gz,armed);
            if (gesture) {
                gates++;
                host.applyWearGesture();
                vibrateWatch();
            }
            String magnet = (mask & 4) != 0
                    ? String.format(Locale.US,"%.1f, %.1f, %.1f µT",mx,my,mz)
                    : "No disponible";
            String heart = (mask & 8) != 0 && hr > 0
                    ? String.format(Locale.US,"%.0f lpm",hr)
                    : "No disponible / sin permiso";
            result = String.format(Locale.US,
                    "RECIBIENDO · muestras: %d\n"
                    + "Aceleración: %.1f, %.1f, %.1f m/s²\n"
                    + "Giroscopio: %.2f, %.2f, %.2f rad/s\n"
                    + "Campo magnético: %s\n"
                    + "Pulso: %s\n"
                    + "Puertas H ejecutadas: %d",
                    count,ax,ay,az,gx,gy,gz,magnet,heart,gates);
            refresh();
        } catch (Exception ignored) {
            result = "Se descartó una lectura inválida.";
            refresh();
        }
    }

    private void vibrateWatch() {
        try {
            Wearable.getNodeClient(host).getConnectedNodes()
                    .addOnSuccessListener(nodes -> {
                        if (nodes.isEmpty()) {
                            host.runOnUiThread(() ->
                                Toast.makeText(host, "Reloj no conectado", Toast.LENGTH_SHORT).show());
                            return;
                        }
                        for (Node node : nodes) {
                            Wearable.getMessageClient(host).sendMessage(
                                    node.getId(), HAPTIC_PATH, new byte[]{1});
                        }
                    });
        } catch (RuntimeException e) {
            Toast.makeText(host, "Wear OS no disponible", Toast.LENGTH_SHORT).show();
        }
    }

    private void updateArmLabel() {
        if (armButton != null) armButton.setText(armed
                ? "Gestos ACTIVADOS · tocar para apagar"
                : "Activar giro de muñeca → H virtual");
    }

    private void refresh() {
        if (telemetry != null) telemetry.setText(result);
    }

    private int dp(int value) {
        return Math.round(value * host.getResources().getDisplayMetrics().density);
    }
}
