package com.mundox.lab;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Bundle;
import android.os.Debug;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.Locale;

public class VisualExperimentActivity extends Activity {
    private VisualView visual;
    private TextView stats;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);

        stats = new TextView(this);
        stats.setTextColor(Color.WHITE);
        stats.setTextSize(12);
        stats.setTypeface(android.graphics.Typeface.MONOSPACE);
        stats.setPadding(12, 8, 12, 8);
        root.addView(stats, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        visual = new VisualView();
        root.addView(visual, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);

        Button classic = new Button(this);
        classic.setText("CLASSIC");
        classic.setOnClickListener(v -> visual.setMode(false));
        controls.addView(classic, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button generative = new Button(this);
        generative.setText("GENERATIVE");
        generative.setOnClickListener(v -> visual.setMode(true));
        controls.addView(generative, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button stress = new Button(this);
        stress.setText("STRESS");
        stress.setOnClickListener(v -> visual.toggleStress());
        controls.addView(stress, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button reset = new Button(this);
        reset.setText("RESET");
        reset.setOnClickListener(v -> visual.reset());
        controls.addView(reset, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        root.addView(controls);
        setContentView(root);
    }

    private class VisualView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);

        private boolean generative = true;
        private boolean stress = false;
        private int count = 6000;

        private float[] x, y, vx, vy, phase, size;
        private int[] colors;

        private long lastFrameNs = 0;
        private float fps = 0f;
        private float frameMs = 0f;
        private float feedback = 0f;
        private float touchX = 0.5f;
        private float touchY = 0.5f;
        private long seed = 0x5EEDBEEFL;
        private long startMs = System.currentTimeMillis();

        VisualView() {
            super(VisualExperimentActivity.this);
            paint.setStrokeWidth(1f);
            linePaint.setStyle(Paint.Style.STROKE);
            linePaint.setStrokeWidth(1f);
            allocateClassic();
            setOnTouchListener((v, e) -> {
                if (e.getAction() == MotionEvent.ACTION_DOWN ||
                        e.getAction() == MotionEvent.ACTION_MOVE) {
                    touchX = e.getX() / Math.max(1f, getWidth());
                    touchY = e.getY() / Math.max(1f, getHeight());
                    feedback = Math.min(4f, feedback + 0.9f);
                    return true;
                }
                return true;
            });
        }

        void setMode(boolean useGenerative) {
            generative = useGenerative;
            if (!generative) allocateClassic();
            feedback = 0f;
            invalidate();
        }

        void toggleStress() {
            stress = !stress;
            count = stress ? 24000 : 6000;
            allocateClassic();
            invalidate();
        }

        void reset() {
            touchX = 0.5f;
            touchY = 0.5f;
            feedback = 0f;
            seed ^= 0x9E3779B97F4A7C15L;
            allocateClassic();
            startMs = System.currentTimeMillis();
            invalidate();
        }

        private void allocateClassic() {
            x = new float[count];
            y = new float[count];
            vx = new float[count];
            vy = new float[count];
            phase = new float[count];
            size = new float[count];
            colors = new int[count];

            for (int i = 0; i < count; i++) {
                float a = unit(hash(i * 7L + 1));
                float b = unit(hash(i * 7L + 2));
                float c = unit(hash(i * 7L + 3));
                float d = unit(hash(i * 7L + 4));
                x[i] = a;
                y[i] = b;
                vx[i] = (c - 0.5f) * 0.0030f;
                vy[i] = (d - 0.5f) * 0.0030f;
                phase[i] = unit(hash(i * 7L + 5)) * 6.2831853f;
                size[i] = 1.5f + unit(hash(i * 7L + 6)) * 5.0f;
                colors[i] = hsv(unit(hash(i * 7L + 7)));
            }
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);

            long nowNs = System.nanoTime();
            if (lastFrameNs != 0) {
                frameMs = (nowNs - lastFrameNs) / 1_000_000f;
                if (frameMs > 0f) {
                    float instant = 1000f / frameMs;
                    fps = fps == 0f ? instant : fps * 0.90f + instant * 0.10f;
                }
            }
            lastFrameNs = nowNs;

            final int w = getWidth();
            final int h = getHeight();
            final float t = (System.currentTimeMillis() - startMs) / 1000f;

            canvas.drawColor(Color.rgb(3, 5, 12));

            feedback *= 0.985f;
            float fold = (float) Math.sin(t * 0.8f + feedback) * 0.35f + feedback * 0.08f;

            if (generative) {
                drawGenerative(canvas, w, h, t, fold);
            } else {
                drawClassic(canvas, w, h, t, fold);
            }

            long explicitBytes = generative ? 64L : (long) count * (6L * 4L + 4L);
            long pssKb = Debug.getPss();

            String text = String.format(Locale.US,
                    "mode=%s  objects=%d  fps=%.1f  frame=%.2f ms\nexplicit_scene_state=%d bytes (%.3f MiB)  process_PSS=%d kB\nformula: minimal state + rule + phase + perspective + time -> visible state\nfeedback=%.3f  touch=(%.2f, %.2f)",
                    generative ? "GENERATIVE" : "CLASSIC",
                    count, fps, frameMs,
                    explicitBytes, explicitBytes / 1048576.0,
                    pssKb, feedback, touchX, touchY);

            stats.setText(text);
            postInvalidateOnAnimation();
        }

        private void drawClassic(Canvas canvas, int w, int h, float t, float fold) {
            for (int i = 0; i < count; i++) {
                x[i] += vx[i];
                y[i] += vy[i];

                if (x[i] < 0f) x[i] += 1f;
                if (x[i] > 1f) x[i] -= 1f;
                if (y[i] < 0f) y[i] += 1f;
                if (y[i] > 1f) y[i] -= 1f;

                float dx = x[i] - touchX;
                float dy = y[i] - touchY;
                float dist2 = dx * dx + dy * dy + 0.002f;
                float warp = 0.018f * feedback / dist2;

                float px = x[i] + (float) Math.sin(phase[i] + t * 1.4f) * 0.018f + dx * warp * 0.002f;
                float py = y[i] + (float) Math.cos(phase[i] * 0.7f + t * 1.1f) * 0.018f + dy * warp * 0.002f;

                float z = 0.35f + 0.65f * (0.5f + 0.5f * (float) Math.sin(phase[i] + t + fold));
                float sx = (px - 0.5f) * z + 0.5f;
                float sy = (py - 0.5f) * z + 0.5f;

                paint.setColor(colors[i]);
                paint.setAlpha((int) (70 + 185 * z));
                canvas.drawCircle(sx * w, sy * h, size[i] * z, paint);
            }
        }

        private void drawGenerative(Canvas canvas, int w, int h, float t, float fold) {
            for (int i = 0; i < count; i++) {
                float a = unit(hash(i * 11L + 1));
                float b = unit(hash(i * 11L + 2));
                float f1 = 0.35f + unit(hash(i * 11L + 3)) * 2.8f;
                float f2 = 0.35f + unit(hash(i * 11L + 4)) * 2.8f;
                float p = unit(hash(i * 11L + 5)) * 6.2831853f;

                float baseX = fract(a + 0.05f * (float)Math.sin(t * f1 + p));
                float baseY = fract(b + 0.05f * (float)Math.cos(t * f2 + p * 0.73f));

                float dx = baseX - touchX;
                float dy = baseY - touchY;
                float radial = (float)Math.sqrt(dx * dx + dy * dy);
                float wave = (float)Math.sin(radial * 34f - t * 4.2f + fold * 3f);

                float z = 0.20f + 0.80f * (0.5f + 0.5f * (float)Math.sin(p + t * 0.7f + wave));
                float lens = 0.72f + 0.30f * z;

                float px = (baseX - 0.5f) * lens + 0.5f + dx * wave * 0.025f * feedback;
                float py = (baseY - 0.5f) * lens + 0.5f + dy * wave * 0.025f * feedback;

                float hue = fract(a * 0.71f + b * 0.37f + t * 0.025f + wave * 0.03f);
                paint.setColor(hsv(hue));
                paint.setAlpha((int)(55 + 200 * z));

                float radius = 1.2f + 5.8f * z;
                int shape = (int)(unit(hash(i * 11L + 6)) * 3f);
                float cx = px * w;
                float cy = py * h;

                if (shape == 0) {
                    canvas.drawCircle(cx, cy, radius, paint);
                } else if (shape == 1) {
                    canvas.drawRect(cx - radius, cy - radius, cx + radius, cy + radius, paint);
                } else {
                    linePaint.setColor(paint.getColor());
                    linePaint.setAlpha(paint.getAlpha());
                    canvas.drawLine(cx - radius, cy, cx + radius, cy, linePaint);
                    canvas.drawLine(cx, cy - radius, cx, cy + radius, linePaint);
                }
            }
        }

        private long hash(long x) {
            x ^= seed;
            x ^= (x >>> 33);
            x *= 0xff51afd7ed558ccdL;
            x ^= (x >>> 33);
            x *= 0xc4ceb9fe1a85ec53L;
            x ^= (x >>> 33);
            return x;
        }

        private float unit(long x) {
            return ((x >>> 40) & 0xFFFFFFL) / 16777215f;
        }

        private float fract(float x) {
            return x - (float)Math.floor(x);
        }

        private int hsv(float hue) {
            return Color.HSVToColor(new float[]{hue * 360f, 0.78f, 1.0f});
        }
    }
}
