package com.cosmicx.pc;

import android.animation.ValueAnimator;
import android.app.Activity;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.GridLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import android.view.animation.DecelerateInterpolator;

import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * MUNDO X · Fusion Lab
 *
 * A physically honest offline nuclear REACTION CALCULATOR, not an actual
 * many-body quantum dynamics solver. All 118 elements are selectable, but
 * precise Q values are shown ONLY for channels in the mass-data catalog.
 * The Android app hosts this separately from the MIL Core simulation;
 * there is no unverified "quantum acceleration" of nuclear calculations.
 */
public final class FusionLabActivity extends Activity {
    private static final int SAVE_EXPERIMENT = 721;
    private static final int BG = Color.rgb(7, 11, 19);
    private static final int SURFACE = Color.rgb(16, 25, 39);
    private static final int STROKE = Color.rgb(41, 66, 86);
    private static final int FOREGROUND = Color.rgb(234, 246, 252);
    private static final int MUTED = Color.rgb(151, 178, 193);
    private static final int ACCENT = Color.rgb(99, 230, 206);
    private static final int WARNING = Color.rgb(237, 191, 115);
    private static final int BLUE = Color.rgb(90, 163, 234);

    private int zA = 1, zB = 1;
    private int aA = 2, aB = 3;
    private int chosenSide = 0; // 0: first nucleus; 1: second nucleus.
    private Button firstButton, secondButton, simulateButton, saveButton;
    private EditText firstMass, secondMass;
    private TextView summary, result, selectionHint;
    private CollisionView graphic;
    private String latestReport = "";
    private boolean hasResult = false;
    private final TextView[] periodicCells = new TextView[118];

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        if (state != null) {
            zA = state.getInt("za", 1);
            zB = state.getInt("zb", 1);
            aA = state.getInt("aa", 2);
            aB = state.getInt("ab", 3);
            chosenSide = state.getInt("side", 0);
            latestReport = state.getString("result", "");
            hasResult = state.getBoolean("hasResult", false);
        }
        build();
        refreshSelection();
        if (hasResult && !latestReport.isEmpty()) {
            result.setText(latestReport);
            saveButton.setEnabled(true);
            try {
                FusionPhysics.Analysis analysis = FusionPhysics.analyze(zA, aA, zB, aB);
                graphic.setModel(analysis, 1f);
            } catch (IllegalArgumentException ignored) {
                // Do not crash the UI on stale or invalid saved instance data.
            }
        } else {
            showAnalysis(false);
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putInt("za", zA);
        out.putInt("zb", zB);
        out.putInt("aa", currentMass(firstMass, aA));
        out.putInt("ab", currentMass(secondMass, aB));
        out.putInt("side", chosenSide);
        if (latestReport.length() < 28_000) out.putString("result", latestReport);
        out.putBoolean("hasResult", hasResult);
    }

    private void build() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(15), dp(18), dp(15), dp(38));
        scroll.addView(root, new ScrollView.LayoutParams(-1, -2));
        setContentView(scroll);

        TextView tag = label("COSMIC X ENGINE / MUNDO X", 11, ACCENT);
        tag.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        root.addView(tag);
        TextView title = label("FUSION LAB", 29, FOREGROUND);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        title.setLetterSpacing(.06f);
        title.setPadding(0, dp(5), 0, dp(5));
        root.addView(title);
        root.addView(label("Selecciona dos núcleos, especifica sus isótopos y compara reacciones físicas conocidas con combinaciones hipotéticas.", 12, MUTED));

        graphic = new CollisionView(this);
        LinearLayout.LayoutParams graphParams = new LinearLayout.LayoutParams(-1, dp(205));
        graphParams.topMargin = dp(16);
        root.addView(graphic, graphParams);

        LinearLayout choice = panel(root);
        choice.addView(header("NÚCLEOS · ENTRADAS"));
        selectionHint = label("", 11, MUTED);
        choice.addView(selectionHint);
        LinearLayout choices = row(choice);
        firstButton = button("Núcleo A", true);
        secondButton = button("Núcleo B", false);
        inRow(choices, firstButton, secondButton);
        firstButton.setOnClickListener(v -> selectSide(0));
        secondButton.setOnClickListener(v -> selectSide(1));
        LinearLayout massRow = row(choice);
        LinearLayout massA = column();
        massA.addView(label("Isótopo A · número másico A", 11, MUTED));
        firstMass = massEditor(aA);
        massA.addView(firstMass);
        LinearLayout massB = column();
        massB.addView(label("Isótopo B · número másico A", 11, MUTED));
        secondMass = massEditor(aB);
        massB.addView(secondMass);
        inRow(massRow, massA, massB);
        choice.addView(label("Número másico A = protones + neutrones. Los valores sugeridos son ejemplos editables, no garantías de existencia ni estabilidad del isótopo.", 11, MUTED));

        LinearLayout presets = panel(root);
        presets.addView(header("REACCIONES CON MASAS TABULADAS"));
        LinearLayout presetOne = row(presets);
        Button dt = button("²H + ³H", false);
        Button dd = button("²H + ²H", false);
        inRow(presetOne, dt, dd);
        dt.setOnClickListener(v -> setInputs(1, 2, 1, 3));
        dd.setOnClickListener(v -> setInputs(1, 2, 1, 2));
        LinearLayout presetTwo = row(presets);
        Button he = button("²H + ³He", false);
        Button li = button("²H + ⁶Li", false);
        inRow(presetTwo, he, li);
        he.setOnClickListener(v -> setInputs(1, 2, 2, 3));
        li.setOnClickListener(v -> setInputs(1, 2, 3, 6));

        LinearLayout periodic = panel(root);
        periodic.addView(header("TABLA PERIÓDICA · 118 ELEMENTOS"));
        periodic.addView(label("Desliza horizontalmente. Toca cualquier elemento para asignarlo al núcleo A o B; después ajusta su número másico.", 11, MUTED));
        HorizontalScrollView horizontal = new HorizontalScrollView(this);
        horizontal.setFillViewport(false);
        horizontal.setHorizontalScrollBarEnabled(true);
        LinearLayout.LayoutParams tableParams = new LinearLayout.LayoutParams(-1, -2);
        tableParams.topMargin = dp(10);
        periodic.addView(horizontal, tableParams);
        GridLayout grid = new GridLayout(this);
        grid.setColumnCount(18);
        grid.setRowCount(9);
        grid.setUseDefaultMargins(false);
        grid.setPadding(dp(3), dp(3), dp(3), dp(3));
        horizontal.addView(grid, new HorizontalScrollView.LayoutParams(-2, -2));
        for (int z = 1; z <= 118; ++z) {
            final int atomicNumber = z;
            TextView cell = new TextView(this);
            cell.setText(z + "\n" + FusionPhysics.SYMBOLS[z - 1]);
            cell.setTextSize(11);
            cell.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            cell.setTextColor(FOREGROUND);
            cell.setGravity(Gravity.CENTER);
            cell.setContentDescription(FusionPhysics.NAMES[z - 1] + ", número atómico " + z);
            GridLayout.LayoutParams location = new GridLayout.LayoutParams(
                    GridLayout.spec(FusionPhysics.periodicRow(z)),
                    GridLayout.spec(FusionPhysics.periodicColumn(z)));
            location.width = dp(54);
            location.height = dp(53);
            location.setMargins(dp(2), dp(2), dp(2), dp(2));
            grid.addView(cell, location);
            periodicCells[z - 1] = cell;
            cell.setOnClickListener(v -> {
                if (chosenSide == 0) {
                    zA = atomicNumber;
                    aA = FusionPhysics.SUGGESTED_A[atomicNumber - 1];
                    firstMass.setText(String.valueOf(aA));
                } else {
                    zB = atomicNumber;
                    aB = FusionPhysics.SUGGESTED_A[atomicNumber - 1];
                    secondMass.setText(String.valueOf(aB));
                }
                refreshSelection();
                hasResult = false;
                showAnalysis(false);
                chosenSide = 1 - chosenSide;
                refreshSelection();
            });
        }

        LinearLayout action = panel(root);
        action.addView(header("REACCIÓN NUCLEAR"));
        simulateButton = button("CALCULAR Y VISUALIZAR", true);
        action.addView(simulateButton, height(52));
        simulateButton.setOnClickListener(v -> showAnalysis(true));
        summary = label("Modelo de masas / reacción", 12, WARNING);
        summary.setPadding(0, dp(13), 0, dp(7));
        action.addView(summary);
        result = label("Selecciona isótopos y calcula.", 12, FOREGROUND);
        result.setTypeface(Typeface.MONOSPACE);
        result.setTextIsSelectable(true);
        result.setContentDescription("fusion-model-output");
        result.setPadding(dp(5), dp(7), dp(4), dp(10));
        action.addView(result);

        saveButton = button("GUARDAR RESULTADO .TXT", false);
        saveButton.setEnabled(false);
        action.addView(saveButton, height(46));
        saveButton.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.setType("text/plain");
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.putExtra(Intent.EXTRA_TITLE, "fusion-mundo-x.txt");
            startActivityForResult(intent, SAVE_EXPERIMENT);
        });

        LinearLayout explanation = panel(root);
        explanation.addView(header("ALCANCE DEL CÁLCULO"));
        explanation.addView(label("Los valores Q se calculan con masas isotópicas de NIST en canales concretos. El gráfico es una ilustración de colisión, no una dinámica nuclear resuelta con MIL Core.", 11, MUTED));
        explanation.addView(label("Para otros núcleos se muestra únicamente una composición formal (suma de Z y A) y una escala electrostática orientativa. No se predicen isótopos nuevos, reacciones posibles, rendimiento ni estabilidad.", 11, MUTED));
        explanation.addView(label("Fuentes: physics.nist.gov (Atomic Weights and Isotopic Compositions) · IAEA (fusión D–T).", 11, ACCENT));
    }

    private void selectSide(int side) {
        chosenSide = side;
        refreshSelection();
    }

    private void setInputs(int z1, int mass1, int z2, int mass2) {
        zA = z1;
        zB = z2;
        aA = mass1;
        aB = mass2;
        firstMass.setText(String.valueOf(mass1));
        secondMass.setText(String.valueOf(mass2));
        chosenSide = 0;
        refreshSelection();
        showAnalysis(false);
    }

    private void refreshSelection() {
        firstButton.setText((chosenSide == 0 ? "● " : "") + FusionPhysics.SYMBOLS[zA-1] + " · Z=" + zA);
        secondButton.setText((chosenSide == 1 ? "● " : "") + FusionPhysics.SYMBOLS[zB-1] + " · Z=" + zB);
        selectionHint.setText("Seleccionando núcleo " + (chosenSide == 0 ? "A" : "B")
                + " · pulsa otro núcleo o elige su elemento");
        firstButton.setTextColor(chosenSide == 0 ? BG : FOREGROUND);
        secondButton.setTextColor(chosenSide == 1 ? BG : FOREGROUND);
        firstButton.setBackground(shape(chosenSide == 0 ? ACCENT : SURFACE));
        secondButton.setBackground(shape(chosenSide == 1 ? ACCENT : SURFACE));
        for (int i = 0; i < periodicCells.length; i++) {
            TextView cell = periodicCells[i];
            if (cell == null) continue;
            int z = i + 1;
            boolean selected = z == zA || z == zB;
            cell.setBackground(shape(selected ? Color.rgb(34, 99, 101) : periodicColor(z)));
        }
    }

    private void showAnalysis(boolean animate) {
        if (firstMass == null || secondMass == null || result == null) return;
        try {
            int first = parseMass(firstMass, zA);
            int second = parseMass(secondMass, zB);
            aA = first;
            aB = second;
            FusionPhysics.Analysis analysis = FusionPhysics.analyze(zA, first, zB, second);
            latestReport = analysis.explanation();
            hasResult = true;
            result.setText(latestReport);
            summary.setText(analysis.hasDocumentedChannel()
                    ? "MASAS TABULADAS · canal(es) físico(s) conocido(s)"
                    : "HIPÓTESIS · sin canal de reacción validado");
            summary.setTextColor(analysis.hasDocumentedChannel() ? ACCENT : WARNING);
            graphic.setModel(analysis, animate ? 0f : 0f);
            if (animate) graphic.animateImpact();
            saveButton.setEnabled(true);
        } catch (NumberFormatException error) {
            summary.setText("Número másico no válido");
            result.setText("Introduce un número entero entre Z y 320 para cada isótopo.");
            saveButton.setEnabled(false);
            hasResult = false;
        } catch (IllegalArgumentException error) {
            summary.setText("Isótopo no válido");
            result.setText(error.getMessage());
            saveButton.setEnabled(false);
            hasResult = false;
        }
    }

    private static int parseMass(EditText editor, int z) {
        String text = editor.getText().toString().trim();
        if (!text.matches("[0-9]{1,3}"))
            throw new NumberFormatException("Número másico no entero");
        int a = Integer.parseInt(text);
        if (a < z || a > 320)
            throw new IllegalArgumentException("El número másico debe estar entre Z=" + z + " y 320");
        return a;
    }

    private static int currentMass(EditText editor, int fallback) {
        try { return Integer.parseInt(editor.getText().toString().trim()); }
        catch (Exception ignored) { return fallback; }
    }

    private int periodicColor(int z) {
        if (z == 2 || z == 10 || z == 18 || z == 36 || z == 54 || z == 86 || z == 118)
            return Color.rgb(35, 65, 102);
        if (z == 1 || z == 6 || z == 7 || z == 8 || z == 9 || z == 15 || z == 16 || z == 17)
            return Color.rgb(32, 72, 83);
        if (z == 3 || z == 11 || z == 19 || z == 37 || z == 55 || z == 87)
            return Color.rgb(92, 51, 69);
        if (z >= 58 && z <= 71 || z >= 90 && z <= 103)
            return Color.rgb(62, 53, 85);
        return Color.rgb(28, 46, 65);
    }

    private LinearLayout panel(LinearLayout parent) {
        LinearLayout panel = column();
        panel.setPadding(dp(13), dp(15), dp(13), dp(15));
        panel.setBackground(shape(SURFACE));
        LinearLayout.LayoutParams p = height(-2);
        p.topMargin = dp(15);
        parent.addView(panel, p);
        return panel;
    }

    private LinearLayout row(LinearLayout parent) {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams params = height(-2);
        params.topMargin = dp(10);
        parent.addView(layout, params);
        return layout;
    }

    private LinearLayout column() {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        return c;
    }

    private void inRow(LinearLayout row, View first, View second) {
        LinearLayout.LayoutParams a = new LinearLayout.LayoutParams(0, -2, 1f);
        a.rightMargin = dp(7);
        row.addView(first, a);
        row.addView(second, new LinearLayout.LayoutParams(0, -2, 1f));
    }

    private TextView label(String content, int sp, int color) {
        TextView v = new TextView(this);
        v.setText(content);
        v.setTextSize(sp);
        v.setTextColor(color);
        v.setLineSpacing(dp(2), 1.03f);
        return v;
    }

    private TextView header(String content) {
        TextView v = label(content, 12, ACCENT);
        v.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        v.setLetterSpacing(.1f);
        v.setPadding(0, 0, 0, dp(9));
        return v;
    }

    private Button button(String content, boolean primary) {
        Button b = new Button(this);
        b.setText(content);
        b.setTextSize(12);
        b.setAllCaps(false);
        b.setTextColor(primary ? BG : FOREGROUND);
        b.setBackground(shape(primary ? ACCENT : SURFACE));
        return b;
    }

    private EditText massEditor(int mass) {
        EditText editor = new EditText(this);
        editor.setText(Integer.toString(mass));
        editor.setInputType(InputType.TYPE_CLASS_NUMBER);
        editor.setSelectAllOnFocus(true);
        editor.setSingleLine(true);
        editor.setTextSize(16);
        editor.setTextColor(FOREGROUND);
        editor.setBackground(shape(BG));
        editor.setPadding(dp(12), dp(9), dp(12), dp(9));
        return editor;
    }

    private GradientDrawable shape(int fill) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(fill);
        shape.setCornerRadius(dp(10));
        shape.setStroke(dp(1), STROKE);
        return shape;
    }

    private LinearLayout.LayoutParams height(int h) {
        return new LinearLayout.LayoutParams(-1, h < 0 ? h : dp(h));
    }

    private int dp(int px) {
        return (int)(px * getResources().getDisplayMetrics().density + 0.5f);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != SAVE_EXPERIMENT || resultCode != RESULT_OK
                || data == null || data.getData() == null || !hasResult) return;
        final Uri destination = data.getData();
        byte[] bytes = ("MUNDO X / FUSION LAB\n"
                + "Modelo educativo. Sin predicción de síntesis ni dinámica nuclear.\n\n"
                + latestReport + "\n").getBytes(StandardCharsets.UTF_8);
        if (bytes.length > 30_000) {
            toast("El resultado excede el límite de exportación");
            return;
        }
        try (OutputStream stream = getContentResolver().openOutputStream(destination, "wt")) {
            if (stream == null) throw new IllegalStateException("No hay acceso al documento");
            stream.write(bytes);
            stream.flush();
            toast("Resultado exportado a tu archivo");
        } catch (Exception error) {
            toast("No se pudo guardar: " + error.getMessage());
        }
    }

    private void toast(String message) {
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }

    private static final class CollisionView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint words = new Paint(Paint.ANTI_ALIAS_FLAG);
        private FusionPhysics.Analysis analysis;
        private ValueAnimator animator;
        private float phase;
        private final RectF scene = new RectF();

        CollisionView(Activity activity) {
            super(activity);
            setBackgroundColor(SURFACE);
            setContentDescription("Diagrama conceptual de dos núcleos aproximándose. No representa una colisión física resuelta.");
            words.setColor(FOREGROUND);
            words.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
            words.setTextAlign(Paint.Align.CENTER);
            words.setTextSize(14 * getResources().getDisplayMetrics().scaledDensity);
        }

        void setModel(FusionPhysics.Analysis state, float progress) {
            analysis = state;
            if (animator != null) animator.cancel();
            phase = progress;
            invalidate();
        }

        void animateImpact() {
            if (animator != null) animator.cancel();
            animator = ValueAnimator.ofFloat(0f, 1f);
            animator.setInterpolator(new DecelerateInterpolator());
            animator.setDuration(1000);
            animator.addUpdateListener(a -> {
                phase = (float) a.getAnimatedValue();
                invalidate();
            });
            animator.start();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float width = getWidth(), height = getHeight();
            if (width <= 0 || height <= 0) return;
            scene.set(1f, 1f, width - 1f, height - 1f);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(1f);
            paint.setColor(Color.rgb(31, 56, 77));
            canvas.drawRoundRect(scene, 16f, 16f, paint);
            float cy = height * .48f;
            float middle = width * .50f;
            float span = width * .29f * (1f - Math.min(.9f, phase * .85f));
            float rad = Math.min(36f, width * .09f);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.rgb(49, 137, 160));
            canvas.drawCircle(middle - span, cy, rad, paint);
            paint.setColor(Color.rgb(132, 111, 218));
            canvas.drawCircle(middle + span, cy, rad, paint);
            paint.setColor(FOREGROUND);
            if (analysis != null) {
                canvas.drawText(FusionPhysics.SYMBOLS[analysis.first.z - 1],
                        middle - span, cy + words.getTextSize() / 3, words);
                canvas.drawText(FusionPhysics.SYMBOLS[analysis.second.z - 1],
                        middle + span, cy + words.getTextSize() / 3, words);
            }
            words.setTextSize(11 * getResources().getDisplayMetrics().scaledDensity);
            words.setColor(MUTED);
            canvas.drawText("COLISIÓN ESQUEMÁTICA · NO SIMULACIÓN DE DINÁMICA",
                    middle, height - 19f, words);
            words.setTextSize(14 * getResources().getDisplayMetrics().scaledDensity);
            words.setColor(FOREGROUND);
        }

        @Override
        protected void onDetachedFromWindow() {
            if (animator != null) animator.cancel();
            super.onDetachedFromWindow();
        }
    }
}
