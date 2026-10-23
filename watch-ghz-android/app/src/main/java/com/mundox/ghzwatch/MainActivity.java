package com.mundox.ghzwatch;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Debug;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONObject;
import java.security.SecureRandom;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    public static final int PREPARE_ID = R.id.prepare, PHASE_ID = R.id.phase, MEASURE_ID = R.id.measure;
    public static final int RESET_ID = R.id.reset, SIZE_ID = R.id.size, STATUS_ID = R.id.status;
    private static final int RED = Color.rgb(237, 72, 82);
    private static final int WHITE = Color.rgb(246, 246, 250);
    private static final int MUTED = Color.rgb(170, 170, 182);
    private static final int[] SIZES = {3, 16, 64, 256, 1000};
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
    private static final SecureRandom RANDOM = new SecureRandom();
    private TextView status, metrics;
    private Button prepare, phase, measure, reset, size;
    private ProbabilityView probabilities;
    private ScrollView scroll;
    private JSONObject report;
    private int sizeIndex;
    private volatile boolean busy = true;
    private volatile boolean destroyed;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().setStatusBarColor(Color.BLACK);
        getWindow().setNavigationBarColor(Color.BLACK);
        sizeIndex = getPreferences(MODE_PRIVATE).getInt("sizeIndex", 0);
        if (sizeIndex < 0 || sizeIndex >= SIZES.length) sizeIndex = 0;
        scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.BLACK);
        scroll.setFillViewport(true);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.setOnGenericMotionListener((view, event) -> {
            if (event.getAction() == MotionEvent.ACTION_SCROLL) {
                float movement = event.getAxisValue(MotionEvent.AXIS_SCROLL);
                if (movement != 0) {
                    scroll.smoothScrollBy(0, Math.round(-movement * dp(38)));
                    return true;
                }
            }
            return false;
        });
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        // Safe readable column for both 42 mm and 46 mm circular Watch4 screens.
        column.setPadding(dp(30), dp(28), dp(30), dp(54));
        scroll.addView(column);
        addText(column, "MUNDO X", 17, WHITE, true);
        addText(column, "NÚCLEO GHZ · SIMULACIÓN LOCAL", 10, RED, true);
        status = addText(column, "Cargando núcleo…", 13, WHITE, false);
        status.setId(STATUS_ID);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        probabilities = new ProbabilityView(this);
        LinearLayout.LayoutParams chartParams = new LinearLayout.LayoutParams(-1, dp(62));
        chartParams.topMargin = dp(6);
        chartParams.bottomMargin = dp(6);
        column.addView(probabilities, chartParams);
        prepare = addButton(column, PREPARE_ID, "PREPARAR GHZ", true);
        size = addButton(column, SIZE_ID, "", false);
        phase = addButton(column, PHASE_ID, "FASE +90°", false);
        measure = addButton(column, MEASURE_ID, "MEDIR EN Z", true);
        reset = addButton(column, RESET_ID, "REPOSO · LIBERAR ESTADO", false);
        Button map = addButton(column, R.id.map_remote, "MAPA 4D · MANDO", true);
        map.setOnClickListener(view -> startActivity(new Intent(this, MapRemoteActivity.class)));
        Button hardware = addButton(column, R.id.hardware, "HARDWARE · PRUEBA REAL", false);
        hardware.setOnClickListener(view -> startActivity(new Intent(this, HardwareActivity.class)));
        metrics = addText(column, "", 12, MUTED, false);
        metrics.setPadding(0, dp(10), 0, dp(6));
        addText(column, "Coherencia calculada\nX…X y YX…X conservan la información de fase.\n\nTras medir, todos los bits coinciden y esa coherencia desaparece.", 12, MUTED, false);
        addText(column, "Mundo X / Ether / CompactCore\nCálculo local en CPU.\nSin entrelazamiento físico medido.", 11, MUTED, false);
        setContentView(scroll);
        prepare.setOnClickListener(view -> submit(0));
        phase.setOnClickListener(view -> submit(1));
        measure.setOnClickListener(view -> submit(2));
        reset.setOnClickListener(view -> submit(3));
        size.setOnClickListener(view -> {
            sizeIndex = (sizeIndex + 1) % SIZES.length;
            getPreferences(MODE_PRIVATE).edit().putInt("sizeIndex", sizeIndex).apply();
            updateButtons();
        });
        submit(4);
    }

    private void submit(int operation) {
        if (busy && operation != 4) return;
        busy = true;
        updateButtons();
        status.setText(operation == 0 ? "Preparando GHZ de " + SIZES[sizeIndex] + "…" : "Calculando…");
        final int n = SIZES[sizeIndex];
        WORKER.execute(() -> {
            try {
                JSONObject result = new JSONObject(NativeGhz.command(operation, n, RANDOM.nextLong()));
                if (!result.getBoolean("simulated")) throw new IllegalStateException("Formato de estado no válido");
                final long pssKiB = Debug.getPss();
                runOnUiThread(() -> {
                    if (destroyed) return;
                    report = result;
                    busy = false;
                    render(pssKiB);
                });
            } catch (Exception | LinkageError error) {
                final String message = error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage();
                runOnUiThread(() -> {
                    if (destroyed) return;
                    busy = false;
                    status.setText("No se completó: " + message);
                    updateButtons();
                });
            }
        });
    }

    private void render(long pssKiB) {
        int n = report.optInt("qubits");
        boolean collapsed = report.optBoolean("collapsed");
        if (n == 0) {
            status.setText("Reposo · 0 qubits activos");
        } else if (collapsed) {
            int bit = report.optInt("outcome");
            status.setText(n + " qubits · resultado " + bit + bit + "…" + bit);
        } else {
            status.setText(n + " qubits · GHZ preparado");
        }
        probabilities.setReport(n, report.optDouble("probability_one"));
        int degrees = report.optInt("phase_quarters") * 90;
        phase.setText("FASE +90° · " + degrees + "°");
        String coherence = n == 0 ? "Coherencia: sin estado" : String.format(Locale.US,
            "Coherencia del modelo\nX…X  %+.0f     YX…X  %+.0f\nZ primero × Z último  %+.0f",
            report.optDouble("x_all"), report.optDouble("yx_all"), report.optDouble("zz_ends"));
        metrics.setText(coherence + String.format(Locale.US,
            "\n\nEstado: %.2f KiB\nDirecciones: %d bytes\nRAM app (PSS): %.1f MiB\nÚltima operación: %.2f ms",
            report.optLong("tableau_bytes") / 1024.0, report.optLong("address_bytes"),
            pssKiB / 1024.0, report.optDouble("calculation_ms"))
            + (collapsed && report.optBoolean("all_equal_verified") ? "\n\nTodos los bits comprobados." : ""));
        updateButtons();
    }

    private void updateButtons() {
        if (prepare == null) return;
        int n = report == null ? 0 : report.optInt("qubits");
        boolean collapsed = report != null && report.optBoolean("collapsed");
        prepare.setText("PREPARAR " + SIZES[sizeIndex] + " QUBITS");
        size.setText(SIZES[sizeIndex] + " QUBITS · CAMBIAR");
        prepare.setEnabled(!busy);
        size.setEnabled(!busy);
        phase.setEnabled(!busy && n > 0 && !collapsed);
        measure.setEnabled(!busy && n > 0);
        reset.setEnabled(!busy && n > 0);
        for (Button button : new Button[]{prepare, size, phase, measure, reset})
            button.setAlpha(button.isEnabled() ? 1f : 0.38f);
    }

    private TextView addText(LinearLayout parent, String value, int sp, int color, boolean bold) {
        TextView text = new TextView(this);
        text.setText(value);
        text.setTextColor(color);
        text.setTextSize(sp);
        text.setGravity(Gravity.CENTER);
        text.setIncludeFontPadding(false);
        text.setTypeface(Typeface.create("sans-serif", bold ? Typeface.BOLD : Typeface.NORMAL));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.topMargin = dp(5);
        parent.addView(text, params);
        return text;
    }

    private Button addButton(LinearLayout parent, int id, String label, boolean primary) {
        Button button = new Button(this);
        button.setId(id);
        button.setText(label);
        button.setTextSize(12);
        button.setTextColor(WHITE);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setAllCaps(false);
        button.setMinHeight(0);
        button.setMinimumHeight(0);
        button.setPadding(dp(4), dp(8), dp(4), dp(8));
        GradientDrawable background = new GradientDrawable();
        background.setColor(primary ? Color.rgb(153, 31, 43) : Color.rgb(29, 29, 35));
        background.setCornerRadius(dp(22));
        button.setBackground(background);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, dp(44));
        params.topMargin = dp(6);
        parent.addView(button, params);
        return button;
    }

    private int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    @Override protected void onDestroy() { destroyed = true; super.onDestroy(); }

    // Instrumented smoke tests observe actual native results after UI actions.
    boolean isBusyForTest() { return busy; }
    JSONObject reportForTest() { return report; }

    private final class ProbabilityView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private int qubits;
        private double one;
        ProbabilityView(Context context) { super(context); }
        void setReport(int n, double p) {
            qubits = n;
            one = p;
            setContentDescription(n == 0 ? "Sin estado preparado" :
                String.format(Locale.US, "Probabilidades Z: todos cero %.0f por ciento, todos uno %.0f por ciento", 100 * (1-p), 100 * p));
            invalidate();
        }
        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float gap = dp(7), width = (getWidth() - gap) / 2f;
            for (int bit = 0; bit < 2; bit++) {
                float left = bit * (width + gap);
                paint.setColor(Color.rgb(23, 23, 29));
                canvas.drawRoundRect(left, 0, left + width, getHeight(), dp(12), dp(12), paint);
                paint.setTextAlign(Paint.Align.CENTER);
                paint.setTypeface(Typeface.DEFAULT_BOLD);
                paint.setTextSize(dp(12));
                paint.setColor(MUTED);
                canvas.drawText(bit == 0 ? "00…0" : "11…1", left + width / 2, dp(20), paint);
                paint.setTextSize(dp(21));
                paint.setColor(WHITE);
                String value = qubits == 0 ? "—" : String.format(Locale.US, "%.0f%%", 100 * (bit == 0 ? 1-one : one));
                canvas.drawText(value, left + width / 2, dp(46), paint);
            }
        }
    }
}
