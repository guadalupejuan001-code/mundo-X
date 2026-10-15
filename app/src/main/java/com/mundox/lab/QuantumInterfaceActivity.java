package com.mundox.lab;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Debug;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Supplier;

public class QuantumInterfaceActivity extends Activity {
    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    private TextView summary;
    private TextView output;
    private EditText qubitInput;
    private EditText tInput;

    private static final int BG = Color.rgb(6, 9, 14);
    private static final int PANEL = Color.rgb(17, 23, 31);
    private static final int PANEL_2 = Color.rgb(25, 33, 44);
    private static final int TEXT = Color.rgb(238, 244, 250);
    private static final int MUTED = Color.rgb(145, 160, 176);
    private static final int ACCENT = Color.rgb(87, 205, 255);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(20), dp(18), dp(40));
        scroll.addView(root);

        TextView title = label("MUNDO X", 30, true, TEXT);
        title.setLetterSpacing(0.08f);
        root.addView(title);

        TextView subtitle = label("QUANTUM SOFTWARE INTERFACE", 13, true, ACCENT);
        subtitle.setLetterSpacing(0.12f);
        root.addView(subtitle);

        TextView note = label(
                "Representación cuántica estructurada sobre hardware convencional. " +
                "No requiere un statevector denso para las rutas Compact/Reduced.",
                12, false, MUTED);
        note.setPadding(0, dp(6), 0, dp(14));
        root.addView(note);

        root.addView(systemCard());

        root.addView(sectionTitle("CIRCUIT CONTROL"));

        LinearLayout inputs = new LinearLayout(this);
        inputs.setOrientation(LinearLayout.HORIZONTAL);

        qubitInput = numberField("8", "qubits");
        tInput = numberField("12", "T gates");

        inputs.addView(qubitInput, weighted());
        inputs.addView(spacer(dp(10)));
        inputs.addView(tInput, weighted());
        root.addView(inputs, fullWidth());

        Button magic = actionButton("VERIFY MAGIC  •  H → T", true);
        magic.setOnClickListener(v ->
                runTask("MAGIC WITNESS", NativeCore::magicWitness));
        root.addView(magic);

        Button reduced = actionButton("RUN REDUCED CLIFFORD + T", true);
        reduced.setOnClickListener(v -> runTask(
                "REDUCED EXACT T",
                () -> NativeCore.tReducedExperiment(
                        readBounded(qubitInput, 8, 1, 12),
                        readBounded(tInput, 12, 0, 128))));
        root.addView(reduced);

        Button compare = actionButton("COMPARE EXACT ↔ REDUCED", false);
        compare.setOnClickListener(v -> {
            int q = readBounded(qubitInput, 8, 1, 10);
            int t = readBounded(tInput, 12, 0, 12);
            runTask("EXACT VS REDUCED", () -> {
                String exact = NativeCore.tExperiment(q, t);
                String reducedOut = NativeCore.tReducedExperiment(q, t);
                return "=== EXACT / NAIVE BRANCH BASELINE ===\n" +
                        exact +
                        "\n=== REDUCED REPRESENTATION ===\n" +
                        reducedOut;
            });
        });
        root.addView(compare);

        Button trace = actionButton("TRACE DIFFERENT CIRCUIT", false);
        trace.setOnClickListener(v -> runTask(
                "INDEPENDENT TRACE",
                () -> NativeCore.tTraceVariant(
                        readBounded(qubitInput, 8, 4, 10),
                        readBounded(tInput, 12, 0, 24))));
        root.addView(trace);

        root.addView(sectionTitle("QUANTUM PROTOCOLS"));

        Button teleport = actionButton("TELEPORT QUANTUM STATE", false);
        teleport.setOnClickListener(v ->
                runTask("TELEPORTATION", NativeCore::teleport));
        root.addView(teleport);

        Button cluster = actionButton("RUN 32 × 32 CLUSTER  •  1024 QUBITS", false);
        cluster.setOnClickListener(v ->
                runTask("CLUSTER 32x32", () -> NativeCore.cluster(32, 32, false)));
        root.addView(cluster);

        root.addView(sectionTitle("LIVE RESULT"));

        summary = label(
                "STATE: READY\nNo circuit executed yet.",
                15, true, TEXT);
        summary.setTypeface(Typeface.MONOSPACE);
        summary.setPadding(dp(16), dp(16), dp(16), dp(16));
        summary.setBackground(panelDrawable(PANEL_2, dp(16)));
        root.addView(summary, fullWidth());

        root.addView(sectionTitle("TECHNICAL OUTPUT"));

        output = label("Mundo X quantum interface ready.\n", 12, false, TEXT);
        output.setTypeface(Typeface.MONOSPACE);
        output.setTextIsSelectable(true);
        output.setPadding(dp(14), dp(14), dp(14), dp(20));
        output.setBackground(panelDrawable(Color.rgb(9, 13, 18), dp(12)));
        LinearLayout.LayoutParams outputParams = fullWidth();
        outputParams.topMargin = dp(6);
        root.addView(output, outputParams);

        Button clear = actionButton("CLEAR OUTPUT", false);
        clear.setOnClickListener(v -> {
            output.setText("");
            summary.setText("STATE: READY\nNo circuit executed yet.");
        });
        root.addView(clear);

        setContentView(scroll);
    }

    private View systemCard() {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(16), dp(14), dp(16), dp(14));
        card.setBackground(panelDrawable(PANEL, dp(18)));

        TextView head = label("SYSTEM", 12, true, ACCENT);
        head.setLetterSpacing(0.10f);
        card.addView(head);

        TextView body = label(
                "ENGINE   Compact stabilizer + reduced Clifford/T\n" +
                "RUNTIME  Native C++17 / ARM64\n" +
                "MODE     Exact validation for tested circuits\n" +
                "QPU      none • software quantum computation",
                13, false, TEXT);
        body.setTypeface(Typeface.MONOSPACE);
        body.setPadding(0, dp(8), 0, 0);
        card.addView(body);

        LinearLayout.LayoutParams p = fullWidth();
        p.topMargin = dp(12);
        card.setLayoutParams(p);
        return card;
    }

    private TextView sectionTitle(String value) {
        TextView t = label(value, 12, true, MUTED);
        t.setLetterSpacing(0.12f);
        t.setPadding(0, dp(22), 0, dp(8));
        return t;
    }

    private EditText numberField(String value, String hint) {
        EditText e = new EditText(this);
        e.setText(value);
        e.setHint(hint);
        e.setTextColor(TEXT);
        e.setHintTextColor(MUTED);
        e.setTextSize(18);
        e.setSingleLine(true);
        e.setGravity(Gravity.CENTER);
        e.setInputType(InputType.TYPE_CLASS_NUMBER);
        e.setPadding(dp(12), dp(12), dp(12), dp(12));
        e.setBackground(panelDrawable(PANEL_2, dp(12)));
        return e;
    }

    private Button actionButton(String title, boolean primary) {
        Button b = new Button(this);
        b.setText(title);
        b.setAllCaps(false);
        b.setTextSize(14);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setTextColor(primary ? Color.rgb(0, 20, 30) : TEXT);
        b.setGravity(Gravity.CENTER);
        b.setPadding(dp(10), dp(12), dp(10), dp(12));
        b.setBackground(panelDrawable(
                primary ? ACCENT : PANEL_2,
                dp(13)));

        LinearLayout.LayoutParams p = fullWidth();
        p.topMargin = dp(10);
        b.setLayoutParams(p);
        return b;
    }

    private void runTask(String title, Supplier<String> task) {
        summary.setText("STATE: RUNNING\n" + title);
        output.append("\n[RUNNING] " + title + "\n");

        executor.submit(() -> {
            String result;
            try {
                result = task.get();
            } catch (Throwable t) {
                result = "ERROR: " + t;
            }

            final String finalResult = result;
            final long pss = Debug.getPss();
            runOnUiThread(() -> {
                output.append(finalResult);
                output.append("\nAndroid process PSS ~= " + pss + " kB\n");
                summary.setText(buildSummary(title, finalResult));
            });
        });
    }

    private String buildSummary(String title, String result) {
        String status = result.contains("result=PASS") ||
                result.contains("result=PASS") ||
                result.contains("PASS") ? "PASS" : "CHECK";

        StringBuilder s = new StringBuilder();
        s.append("STATE: ").append(status).append("\n");
        s.append(title).append("\n");

        addIfPresent(s, result, "qubits=");
        addIfPresent(s, result, "t_gates=");
        addIfPresent(s, result, "reduced_components=");
        addIfPresent(s, result, "active_coefficients=");
        addIfPresent(s, result, "logical_payload_bytes=");
        addIfPresent(s, result, "fidelity=");
        addIfPresent(s, result, "wall_time_ms=");
        addIfPresent(s, result, "non_stabilizer=");
        return s.toString().trim();
    }

    private void addIfPresent(StringBuilder out, String source, String key) {
        int i = source.indexOf(key);
        if (i < 0) return;
        int end = source.indexOf('\n', i);
        if (end < 0) end = source.length();
        out.append(source, i, end).append("\n");
    }

    private int readBounded(EditText field, int fallback, int min, int max) {
        try {
            int value = Integer.parseInt(field.getText().toString().trim());
            return Math.max(min, Math.min(value, max));
        } catch (Exception e) {
            return fallback;
        }
    }

    private TextView label(String value, int sp, boolean bold, int color) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextColor(color);
        t.setTextSize(sp);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private GradientDrawable panelDrawable(int color, int radius) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(radius);
        return d;
    }

    private LinearLayout.LayoutParams fullWidth() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams weighted() {
        return new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
    }

    private View spacer(int width) {
        View v = new View(this);
        v.setLayoutParams(new LinearLayout.LayoutParams(width, 1));
        return v;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    protected void onDestroy() {
        executor.shutdownNow();
        super.onDestroy();
    }
}
