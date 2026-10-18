package com.mundox.lab;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.function.IntConsumer;

/** Canvas instruments for the local simulator. Plot data is supplied by the activity. */
public final class LabPlotView extends View {
    private static final int PAPER = Color.rgb(236, 230, 214);
    private static final int INK = Color.rgb(19, 23, 21);
    private static final int LIME = Color.rgb(214, 237, 103);
    private static final int CORAL = Color.rgb(223, 129, 108);
    private static final int MUTED = Color.rgb(104, 106, 94);
    private static final int CIRCUIT = 0;
    private static final int HISTOGRAM = 1;
    private static final int PROBABILITIES = 2;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Typeface mono = Typeface.create("monospace", Typeface.NORMAL);
    private final Typeface condensed = Typeface.create("sans-serif-condensed", Typeface.BOLD);
    private final List<Gate> gates = new ArrayList<>();
    private final List<Integer> histogramStates = new ArrayList<>();
    private int mode = CIRCUIT;
    private int qubits = 1;
    private int executedGates = -1;
    private long[] counts = new long[0];
    private double[] probabilities = new double[0];
    private double totalShots;
    private int observedStates;
    private boolean invalidCounts;
    private IntConsumer qubitTapListener;

    private static final class Gate {
        final String op;
        final int first;
        final int second;

        Gate(String op, int first, int second) {
            this.op = op;
            this.first = first;
            this.second = second;
        }
    }

    public LabPlotView(Context context) {
        super(context);
        setBackgroundColor(PAPER);
        setFocusable(true);
        setClickable(true);
        setMinimumWidth(px(320));
        setContentDescription("Circuito del simulador local; toca un cable para elegir un qubit.");
    }

    /** step is the number of applied gates; -1 displays the complete circuit as applied. */
    public void showCircuit(int n, String script, int step) {
        mode = CIRCUIT;
        qubits = Math.max(1, n);
        gates.clear();
        if (script != null) {
            for (String raw : script.split("\\r?\\n")) {
                int comment = raw.indexOf('#');
                String line = (comment < 0 ? raw : raw.substring(0, comment)).trim();
                if (line.isEmpty()) continue;
                String[] words = line.split("\\s+");
                String op = words[0].toUpperCase(Locale.ROOT);
                boolean pair = op.equals("CX") || op.equals("CZ");
                boolean single = op.equals("H") || op.equals("X") || op.equals("Z") ||
                        op.equals("S") || op.equals("MEASURE");
                if (!pair && !single) continue;
                try {
                    if (words.length != (pair ? 3 : 2) || !words[1].matches("[0-9]+") ||
                            (pair && !words[2].matches("[0-9]+"))) continue;
                    int a = Integer.parseInt(words[1]);
                    int b = pair ? Integer.parseInt(words[2]) : -1;
                    if (a < 0 || a >= qubits || (pair && (b < 0 || b >= qubits || a == b)))
                        continue;
                    gates.add(new Gate(op, a, b));
                } catch (IndexOutOfBoundsException | NumberFormatException ignored) {
                    // The runtime reports invalid source; the plot never invents a gate.
                }
            }
        }
        executedGates = step < 0 ? -1 : Math.min(step, gates.size());
        setMinimumWidth(px(Math.max(320, 96 + gates.size() * 38)));
        setContentDescription("Circuito de " + qubits + " qubits y " + gates.size() +
                " puertas. q0 es el bit más significativo. Toca un cable para elegirlo.");
        requestLayout();
        invalidate();
    }

    public void showHistogram(int n, long[] values) {
        mode = HISTOGRAM;
        qubits = Math.max(1, n);
        counts = values == null ? new long[0] : values.clone();
        histogramStates.clear();
        totalShots = 0;
        observedStates = 0;
        invalidCounts = false;
        for (int i = 0; i < counts.length; i++) {
            if (counts[i] < 0) invalidCounts = true;
            if (counts[i] > 0) {
                totalShots += counts[i];
                observedStates++;
                histogramStates.add(i);
            }
        }
        if (counts.length <= 16) {
            histogramStates.clear();
            for (int i = 0; i < counts.length; i++) histogramStates.add(i);
        } else {
            Collections.sort(histogramStates, (a, b) -> {
                int byCount = Long.compare(counts[b], counts[a]);
                return byCount != 0 ? byCount : Integer.compare(a, b);
            });
            if (histogramStates.size() > 12)
                histogramStates.subList(12, histogramStates.size()).clear();
        }
        setMinimumWidth(px(320));
        setContentDescription("Histograma de mediciones Z; " + shotText() +
                " shots. Etiquetas binarias con q0 a la izquierda.");
        requestLayout();
        invalidate();
    }

    public void showProbabilities(double[] p1) {
        mode = PROBABILITIES;
        probabilities = p1 == null ? new double[0] : p1.clone();
        qubits = probabilities.length;
        setMinimumWidth(px(320));
        setContentDescription("Probabilidad marginal de obtener uno al medir cada qubit en Z.");
        requestLayout();
        invalidate();
    }

    public void setQubitTapListener(IntConsumer listener) {
        qubitTapListener = listener;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int rows = mode == CIRCUIT ? qubits :
                (mode == HISTOGRAM ? Math.max(1, histogramStates.size()) : Math.max(1, qubits));
        int desiredHeight = px(100 + rows * (mode == CIRCUIT ? 40 : 29));
        setMeasuredDimension(resolveSize(getSuggestedMinimumWidth(), widthMeasureSpec),
                resolveSize(Math.max(getSuggestedMinimumHeight(), desiredHeight), heightMeasureSpec));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        drawPaper(canvas);
        if (mode == CIRCUIT) drawCircuit(canvas);
        else if (mode == HISTOGRAM) drawHistogram(canvas);
        else drawProbabilities(canvas);
    }

    private void drawPaper(Canvas canvas) {
        canvas.drawColor(PAPER);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(0.5f));
        float gap = dp(12);
        for (int i = 0; i * gap <= getWidth(); i++) {
            paint.setColor(i % 5 == 0 ? Color.rgb(204, 201, 183) : Color.rgb(222, 218, 201));
            canvas.drawLine(i * gap, 0, i * gap, getHeight(), paint);
        }
        for (int i = 0; i * gap <= getHeight(); i++) {
            paint.setColor(i % 5 == 0 ? Color.rgb(204, 201, 183) : Color.rgb(222, 218, 201));
            canvas.drawLine(0, i * gap, getWidth(), i * gap, paint);
        }
        paint.setStyle(Paint.Style.FILL);
    }

    private void drawCircuit(Canvas canvas) {
        text(canvas, "CIRCUITO / q0 = MSB", 16, 23, 12, INK, condensed, Paint.Align.LEFT);
        String progress = executedGates < 0 ? gates.size() + " puertas" :
                executedGates + " / " + gates.size() + " aplicadas";
        text(canvas, progress, 16, 42, 10, MUTED, mono, Paint.Align.LEFT);
        for (int q = 0; q < qubits; q++) {
            float y = wireY(q);
            paint.setColor(INK);
            paint.setStrokeWidth(dp(1));
            canvas.drawLine(dp(58), y, getWidth() - dp(18), y, paint);
            textPixels(canvas, "q" + q, dp(16), y + dp(4), 11, INK, mono, Paint.Align.LEFT);
        }
        if (gates.isEmpty()) {
            text(canvas, "Añade puertas al programa", 68, 61, 10, MUTED, mono, Paint.Align.LEFT);
        }
        for (int i = 0; i < gates.size(); i++) {
            Gate gate = gates.get(i);
            float x = dp(78 + i * 38);
            float y = wireY(gate.first);
            boolean applied = executedGates < 0 || i < executedGates;
            boolean current = executedGates > 0 && i == executedGates - 1;
            int color = applied ? INK : MUTED;
            int fill = current ? CORAL : applied ? LIME : PAPER;
            paint.setStrokeWidth(dp(1.4f));
            paint.setColor(color);
            if (gate.second >= 0) {
                float targetY = wireY(gate.second);
                canvas.drawLine(x, y, x, targetY, paint);
                canvas.drawCircle(x, y, dp(3.5f), paint);
                if (gate.op.equals("CZ")) {
                    canvas.drawCircle(x, targetY, dp(3.5f), paint);
                } else {
                    paint.setColor(fill);
                    canvas.drawCircle(x, targetY, dp(10), paint);
                    paint.setColor(color);
                    paint.setStyle(Paint.Style.STROKE);
                    canvas.drawCircle(x, targetY, dp(10), paint);
                    canvas.drawLine(x - dp(6), targetY, x + dp(6), targetY, paint);
                    canvas.drawLine(x, targetY - dp(6), x, targetY + dp(6), paint);
                    paint.setStyle(Paint.Style.FILL);
                }
            } else {
                RectF box = new RectF(x - dp(12), y - dp(12), x + dp(12), y + dp(12));
                paint.setColor(fill);
                canvas.drawRect(box, paint);
                paint.setColor(color);
                paint.setStyle(Paint.Style.STROKE);
                canvas.drawRect(box, paint);
                if (gate.op.equals("MEASURE")) {
                    canvas.drawArc(x - dp(8), y - dp(5), x + dp(8), y + dp(8),
                            195, 150, false, paint);
                    canvas.drawLine(x, y + dp(5), x + dp(6), y - dp(6), paint);
                }
                paint.setStyle(Paint.Style.FILL);
                if (!gate.op.equals("MEASURE"))
                    textPixels(canvas, gate.op, x, y + dp(4), gate.op.length() > 1 ? 8 : 12,
                            color, mono, Paint.Align.CENTER);
            }
            textPixels(canvas, Integer.toString(i + 1), x, getHeight() - dp(12),
                    8, MUTED, mono, Paint.Align.CENTER);
        }
    }

    private void drawHistogram(Canvas canvas) {
        text(canvas, "MEDICIONES Z / q0 A LA IZQUIERDA", 16, 23, 12,
                INK, condensed, Paint.Align.LEFT);
        text(canvas, "shots: " + shotText() + " · resultados observados: " + observedStates,
                16, 42, 9, MUTED, mono, Paint.Align.LEFT);
        if (invalidCounts) {
            text(canvas, "Datos inválidos: conteos negativos", 16, 76, 11, INK, mono, Paint.Align.LEFT);
            return;
        }
        if (totalShots == 0) {
            text(canvas, "Sin shots; aún no hay frecuencias", 16, 76, 11, INK, mono, Paint.Align.LEFT);
            return;
        }
        float labelWidth = dp(Math.min(154, Math.max(52, qubits * 7 + 14)));
        float left = dp(16) + labelWidth;
        float right = getWidth() - dp(102);
        float barWidth = Math.max(dp(20), right - left);
        int rows = histogramStates.size();
        float rowHeight = Math.max(dp(12), (getHeight() - dp(101)) / Math.max(1, rows));
        for (int i = 0; i < rows; i++) {
            int state = histogramStates.get(i);
            double fraction = counts[state] / totalShots;
            float y = dp(67) + i * rowHeight;
            textPixels(canvas, binary(state), dp(16), y + dp(4), 10, INK, mono, Paint.Align.LEFT);
            paint.setColor(Color.rgb(214, 211, 194));
            canvas.drawRect(left, y - dp(8), left + barWidth, y + dp(8), paint);
            paint.setColor(i == 0 ? CORAL : LIME);
            canvas.drawRect(left, y - dp(8), left + barWidth * (float) fraction, y + dp(8), paint);
            textPixels(canvas, String.format(Locale.US, "%.1f%%  %d", fraction * 100, counts[state]),
                    getWidth() - dp(12), y + dp(4), 9, INK, mono, Paint.Align.RIGHT);
        }
        String footer = observedStates > rows ?
                "Top " + rows + ": " + (observedStates - rows) + " resultados más fuera del gráfico" :
                "Frecuencia = conteo / shots · escala 0–100%";
        textPixels(canvas, footer, dp(16), getHeight() - dp(14), 9, MUTED, mono, Paint.Align.LEFT);
    }

    private void drawProbabilities(Canvas canvas) {
        text(canvas, "P(1) / MARGINALES EN BASE Z", 16, 23, 12, INK, condensed, Paint.Align.LEFT);
        text(canvas, "Probabilidad del estado actual; sin medirlo", 16, 42, 9,
                MUTED, mono, Paint.Align.LEFT);
        if (probabilities.length == 0) {
            text(canvas, "Sin datos", 16, 76, 11, MUTED, mono, Paint.Align.LEFT);
            return;
        }
        float left = dp(53);
        float right = getWidth() - dp(62);
        float width = Math.max(dp(20), right - left);
        float rowHeight = Math.max(dp(12), (getHeight() - dp(103)) / probabilities.length);
        for (int q = 0; q < probabilities.length; q++) {
            float y = dp(67) + q * rowHeight;
            textPixels(canvas, "q" + q, dp(16), y + dp(4), 11, INK, mono, Paint.Align.LEFT);
            paint.setColor(Color.rgb(214, 211, 194));
            canvas.drawRect(left, y - dp(8), left + width, y + dp(8), paint);
            double p = probabilities[q];
            boolean valid = !Double.isNaN(p) && !Double.isInfinite(p) && p >= 0 && p <= 1;
            if (valid) {
                paint.setColor(LIME);
                canvas.drawRect(left, y - dp(8), left + width * (float) p, y + dp(8), paint);
            }
            paint.setColor(MUTED);
            paint.setStrokeWidth(dp(0.75f));
            canvas.drawLine(left + width / 2, y - dp(9), left + width / 2, y + dp(9), paint);
            textPixels(canvas, valid ? String.format(Locale.US, "%.3f", p) : "—",
                    getWidth() - dp(14), y + dp(4), 11, INK, mono, Paint.Align.RIGHT);
        }
        textPixels(canvas, "0", left, getHeight() - dp(14), 9, MUTED, mono, Paint.Align.LEFT);
        textPixels(canvas, "0.5", left + width / 2, getHeight() - dp(14), 9, MUTED, mono, Paint.Align.CENTER);
        textPixels(canvas, "1", right, getHeight() - dp(14), 9, MUTED, mono, Paint.Align.RIGHT);
    }

    private float wireY(int qubit) {
        float top = dp(75);
        float bottom = Math.max(top, getHeight() - dp(43));
        return qubits <= 1 ? (top + bottom) / 2 : top + (bottom - top) * qubit / (qubits - 1);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (mode != CIRCUIT || qubitTapListener == null) return super.onTouchEvent(event);
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) return true;
        if (event.getActionMasked() == MotionEvent.ACTION_UP) {
            int nearest = 0;
            float distance = Float.MAX_VALUE;
            for (int q = 0; q < qubits; q++) {
                float candidate = Math.abs(event.getY() - wireY(q));
                if (candidate < distance) {
                    distance = candidate;
                    nearest = q;
                }
            }
            if (distance <= dp(18)) qubitTapListener.accept(nearest);
            performClick();
            return true;
        }
        return true;
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    private String binary(int state) {
        String bits = Integer.toBinaryString(state);
        StringBuilder result = new StringBuilder();
        for (int i = bits.length(); i < qubits; i++) result.append('0');
        return result.append(bits).toString();
    }

    private String shotText() {
        return String.format(Locale.US, "%.0f", totalShots);
    }

    private void text(Canvas canvas, String value, float x, float y, float size,
                      int color, Typeface typeface, Paint.Align align) {
        textPixels(canvas, value, dp(x), dp(y), size, color, typeface, align);
    }

    private void textPixels(Canvas canvas, String value, float x, float y, float size,
                            int color, Typeface typeface, Paint.Align align) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(color);
        paint.setTypeface(typeface);
        paint.setTextSize(dp(size));
        paint.setTextAlign(align);
        canvas.drawText(value, x, y, paint);
    }

    private float dp(float value) {
        return value * getResources().getDisplayMetrics().density;
    }

    private int px(int value) {
        return Math.round(dp(value));
    }
}
