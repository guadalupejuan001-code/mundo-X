package com.mundox.lab;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Build;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import android.text.InputType;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.EditText;
import android.widget.TextView;

import org.json.JSONObject;
import org.json.JSONArray;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** An observable experiment in rule-based reconstruction and persistent edits. */
public final class ObjectLabActivity extends Activity implements ObjectSceneRenderer.Listener {
    private static final int INK = Color.rgb(19, 23, 21), BONE = Color.rgb(236, 230, 214);
    private static final int LIME = Color.rgb(214, 237, 103), MUTED = Color.rgb(160, 169, 155);
    private static final int CORAL = Color.rgb(223, 129, 108), RULE = Color.rgb(60, 69, 59);
    private static final int[] COUNTS = {1024, 4096, 16384, 65536, 262144, 1048576};
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final List<View> controls = new ArrayList<>();
    private GLSurfaceView surface;
    private ObjectSceneRenderer renderer;
    private ObjectSceneModel model;
    private SharedPreferences prefs;
    private Spinner count;
    private EditText seed, selection;
    private TextView stats, detail, status, result, selectedInfo;
    private Button animated, ruleRoute, explicitRoute;
    private int selected;
    private boolean animation = true, busy, destroyed;
    private long sceneRevision;
    private long requestId;
    private boolean campaign;
    private int campaignStage, campaignOriginalSelection;
    private long campaignStarted;
    private String campaignOriginal;
    private JSONArray campaignRuns;
    private String verification = "{}", benchmark = "{}";
    private ObjectSceneRenderer.Stats latest;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(INK); getWindow().setNavigationBarColor(INK);
        prefs = getSharedPreferences("xlab.objects.v1", MODE_PRIVATE);
        try { model = ObjectSceneModel.importText(prefs.getString("scene", "")); }
        catch (IllegalArgumentException error) { model = new ObjectSceneModel(4096, 2026); }
        selected = Math.min(model.count() - 1, Math.max(0, prefs.getInt("selected", 0)));
        LinearLayout root = column(); root.setBackgroundColor(INK);
        root.setPadding(dp(16), dp(8), dp(16), 0);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(dp(16) + insets.getSystemWindowInsetLeft(),
                    dp(8) + insets.getSystemWindowInsetTop(), dp(16) + insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets.consumeSystemWindowInsets();
        });
        LinearLayout mast = row();
        mast.addView(label("MUNDO X  /  OBJETOS", 15, LIME, true), weight());
        Button back = button("← Lab", false); back.setOnClickListener(v -> finish());
        mast.addView(back); root.addView(mast);
        root.addView(label("IDENTIDAD · RECONSTRUCCIÓN · CAMBIOS PERSISTENTES", 9, MUTED, true));

        FrameLayout viewport = new FrameLayout(this);
        surface = new GLSurfaceView(this); surface.setTag("objects_surface");
        surface.setEGLContextClientVersion(3); surface.setPreserveEGLContextOnPause(true);
        renderer = new ObjectSceneRenderer(this); surface.setRenderer(renderer);
        viewport.addView(surface, new FrameLayout.LayoutParams(-1, -1));
        TextView gesture = label("ARRASTRA: ÓRBITA    PELLIZCA: ZOOM    TOCA: CELDA", 9, BONE, true);
        gesture.setPadding(dp(8), dp(6), dp(8), dp(6)); gesture.setBackgroundColor(0xb3131715);
        FrameLayout.LayoutParams hint = new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM);
        viewport.addView(gesture, hint);
        root.addView(viewport, new LinearLayout.LayoutParams(-1, 0, 1.1f));
        bindGestures();
        stats = tag(label("Preparando contexto OpenGL ES…", 10, LIME, true), "objects_stats");
        root.addView(stats);
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(false);
        LinearLayout panel = column(); panel.setPadding(0, dp(6), 0, dp(18)); scroll.addView(panel);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1f));

        LinearLayout routes = row();
        ruleRoute = control(button("Reglas + cambios", true)); ruleRoute.setTag("objects_rules");
        explicitRoute = control(button("Explícita", false)); explicitRoute.setTag("objects_explicit");
        ruleRoute.setOnClickListener(v -> route(false)); explicitRoute.setOnClickListener(v -> route(true));
        routes.addView(ruleRoute, weight()); routes.addView(explicitRoute, weight()); panel.addView(routes);
        detail = tag(label("", 10, MUTED, true), "objects_memory"); panel.addView(detail);
        LinearLayout setup = row();
        count = tag(new Spinner(this), "objects_count");
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item,
                new String[]{"1.024 objetos", "4.096 objetos", "16.384 objetos", "65.536 objetos", "262.144 objetos", "1.048.576 objetos"});
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item); count.setAdapter(adapter);
        count.setBackgroundTintList(ColorStateList.valueOf(LIME));
        for (int i = 0; i < COUNTS.length; i++) if (COUNTS[i] == model.count()) count.setSelection(i);
        setup.addView(control(count), weight());
        seed = tag(editor(Integer.toString(model.seed()), true), "objects_seed");
        seed.setHint("Semilla"); setup.addView(control(seed), weight());
        Button regenerate = taggedControl("Nueva escena", "objects_new", setup);
        regenerate.setOnClickListener(v -> {
            try {
                int value = Integer.parseInt(seed.getText().toString().trim());
                int n = COUNTS[count.getSelectedItemPosition()]; String limit = resourceLimit(n);
                if (limit != null) { state("error", limit); return; }
                model = new ObjectSceneModel(n, value);
                selected = Math.min(selected, model.count() - 1); publishScene();
                state("ready", "Nueva escena · semilla " + value);
            } catch (IllegalArgumentException error) { state("error", "La semilla debe ser un entero de 32 bits."); }
        });
        panel.addView(setup);

        LinearLayout selectRow = row();
        selection = tag(editor(Integer.toString(selected), false), "objects_id");
        selectRow.addView(control(selection), weight());
        Button choose = taggedControl("Elegir ID", "objects_select", selectRow);
        choose.setOnClickListener(v -> {
            try { choose(Integer.parseInt(selection.getText().toString().trim())); }
            catch (IllegalArgumentException error) { state("error", "ID válido: 0 a " + (model.count() - 1)); }
        });
        animated = taggedControl("Campo: activo", "objects_animate", selectRow);
        animated.setOnClickListener(v -> {
            animation = !animation; surface.queueEvent(() -> renderer.setAnimation(animation));
            animated.setText(animation ? "Campo: activo" : "Campo: quieto");
        });
        panel.addView(selectRow);
        selectedInfo = tag(label("", 10, BONE, true), "objects_selected"); panel.addView(selectedInfo);
        LinearLayout edits = row();
        taggedControl("Elevar", "objects_raise", edits).setOnClickListener(v -> editSelected(false));
        taggedControl("Ocultar", "objects_hide", edits).setOnClickListener(v -> editSelected(true));
        taggedControl("Revertir", "objects_revert", edits).setOnClickListener(v -> {
            model.edit(selected, 0); publishScene(); state("ready", "Objeto restaurado desde su regla.");
        });
        panel.addView(edits);
        LinearLayout density = row();
        taggedControl("+256 cambios", "objects_scatter", density).setOnClickListener(v -> scatter(false));
        taggedControl("Cambiar todos¹", "objects_all", density).setOnClickListener(v -> scatter(true));
        taggedControl("Limpiar", "objects_clear", density).setOnClickListener(v -> {
            model.clearEdits(); publishScene(); state("ready", "Sin excepciones; sólo reglas.");
        });
        panel.addView(density);
        panel.addView(label("¹ Hasta 4.096 cambios. Prueba cómo crece el coste al perder regularidad.", 10, MUTED, false));
        LinearLayout tests = row();
        taggedControl("Imagen A/B", "objects_verify", tests).setOnClickListener(v -> {
            busy(true); verification = "{}"; state("checking", "Misma cámara y tiempo · comparando píxeles…");
            final long id = ++requestId; surface.queueEvent(() -> renderer.verify(id));
        });
        taggedControl("Medir A/B", "objects_benchmark", tests).setOnClickListener(v -> {
            busy(true); benchmark = "{}"; state("benchmarking", "Calentamiento y muestra en ambas rutas. Escena fija.");
            final long id = ++requestId; surface.queueEvent(() -> renderer.benchmark(id));
        });
        Button stop = button("Parar", false); stop.setTag("objects_stop");
        stop.setOnClickListener(v -> {
            if (campaign) { finishCampaign("cancelled", "Detenida por el usuario."); return; }
            requestId++; surface.queueEvent(renderer::cancelBenchmark); busy(false);
            state("cancelled", "Medición detenida. Puedes volver a ejecutarla.");
        }); tests.addView(stop, weight()); panel.addView(tests);
        Button extreme = control(button("Escala extrema · 1.024 → 1.048.576", true));
        extreme.setTag("objects_extreme"); extreme.setOnClickListener(v -> startCampaign()); panel.addView(extreme);
        panel.addView(label("Escala automática: hasta 60 s; para con P95 ≥ 100 ms, presión de memoria o temperatura severa. Conserva tu escena al terminar.", 10, MUTED, false));
        status = tag(label("Iniciando…", 11, LIME, true), "objects_status");
        status.setContentDescription("starting"); panel.addView(status);
        result = tag(label("", 10, BONE, true), "objects_result"); panel.addView(result);
        LinearLayout output = row();
        Button copy = button("Copiar informe", false); copy.setTag("objects_copy");
        copy.setOnClickListener(v -> {
            ((ClipboardManager) getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("Mundo X · objetos", report()));
            if (!busy) state("ready", "Informe copiado con semilla, escena y mediciones.");
        }); output.addView(copy, weight());
        Button share = button("Compartir", false);
        share.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_SEND); intent.setType("text/plain");
            intent.putExtra(Intent.EXTRA_TEXT, report()); startActivity(Intent.createChooser(intent, "Compartir ensayo"));
        }); output.addView(share, weight()); panel.addView(output);
        panel.addView(label("Mismo trabajo visual para ambas rutas. P50/P95 son tiempos entre fotogramas; no miden tiempo GPU aislado. El campo es analítico, sin física de colisiones. PSS incluye el proceso; los bytes de representación son otra medida.", 10, MUTED, false));
        setContentView(root); root.requestApplyInsets(); publishScene();
    }

    private void route(boolean value) {
        surface.queueEvent(() -> renderer.setExplicit(value));
        ruleRoute.setTextColor(value ? MUTED : LIME); explicitRoute.setTextColor(value ? LIME : MUTED);
    }

    private void choose(int id) {
        if (id < 0 || id >= model.count()) throw new IllegalArgumentException("Invalid ID");
        selected = id; selection.setText(Integer.toString(id));
        surface.queueEvent(() -> renderer.select(id)); selectedInfo();
    }

    private void selectedInfo() {
        float[] t = new float[4]; model.transform(selected, t, 0);
        selectedInfo.setText(String.format(Locale.US, "ID %d · x %.1f / z %.1f · altura %.2f%s · %d cambios",
                selected, t[0], t[1], t[2], t[2] == 0 ? " (oculto)" : "", model.editCount()));
        selectedInfo.setContentDescription(Integer.toString(model.editCount()));
    }

    private void editSelected(boolean hide) {
        try {
            if (hide) model.edit(selected, 7);
            else {
                int current = model.editState(selected);
                model.edit(selected, current >= 1 && current < 6 ? current + 1 : 1);
            }
            publishScene(); state("ready", "Cambio guardado · ID " + selected);
        } catch (IllegalArgumentException | IllegalStateException error) { state("error", error.getMessage()); }
    }

    private void scatter(boolean all) {
        if (all && model.count() > ObjectSceneModel.MAX_EDITS) {
            state("error", "Esta escena supera el límite de 4.096 cambios. Prueba «Cambiar todos» con 1.024 o 4.096 objetos.");
            return;
        }
        int n = all ? model.count() : Math.min(256, Math.min(
                model.count() - model.editCount(), ObjectSceneModel.MAX_EDITS - model.editCount()));
        if (n == 0) {
            state("error", model.editCount() == model.count()
                    ? "Todos los objetos ya tienen cambios. Revierte alguno o limpia la escena."
                    : "Se alcanzó el límite de 4.096 cambios. Revierte alguno antes de añadir más.");
            return;
        }
        int[] ids = new int[n];
        if (all) {
            for (int i = 0; i < n; i++) ids[i] = i;
        } else {
            // A coprime stride visits every ID once, spreading each new batch
            // across the scene while preserving all existing exceptions.
            int step = (int) Math.max(1L, model.count() * 40503L / 65536L);
            while (true) {
                int a = step, b = model.count();
                while (b != 0) { int remainder = a % b; a = b; b = remainder; }
                if (a == 1) break;
                step++;
            }
            int start = Math.floorMod(ObjectSceneModel.hash(0, model.seed()), model.count());
            int found = 0;
            for (int i = 0; i < model.count() && found < n; i++) {
                int id = (int) ((start + (long) i * step) % model.count());
                if (model.editState(id) == 0) ids[found++] = id;
            }
            if (found != n) {
                state("error", "No hay suficientes objetos sin modificar; la escena conserva su estado anterior.");
                return;
            }
        }
        ObjectSceneModel.Snapshot prior = model.snapshot();
        try {
            for (int i = 0; i < n; i++) {
                int id = ids[i];
                model.edit(id, 1 + Math.floorMod(ObjectSceneModel.hash(id, model.seed()), 6));
            }
            publishScene(); state("ready", (all ? "Cambios distribuidos" : n + " cambios añadidos")
                    + " · " + model.editCount() + " excepciones.");
        } catch (IllegalArgumentException | IllegalStateException error) {
            model = ObjectSceneModel.importText(prior.exportText());
            state("error", "Se alcanzó el límite de cambios; la escena conserva su estado anterior.");
        }
    }

    private void publishScene() {
        ObjectSceneModel.Snapshot snapshot = model.snapshot();
        sceneRevision++; verification = "{}"; benchmark = "{}";
        if (result != null) result.setText("");
        surface.queueEvent(() -> { renderer.setScene(snapshot); renderer.select(selected); });
        selection.setText(Integer.toString(selected)); selectedInfo(); persist();
    }

    private void persist() {
        if (!campaign) prefs.edit().putString("scene", model.exportText()).putInt("selected", selected).apply();
    }

    private void busy(boolean value) {
        busy = value; for (View control : controls) control.setEnabled(!value);
    }

    private String resourceLimit(int n) {
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        ((ActivityManager) getSystemService(ACTIVITY_SERVICE)).getMemoryInfo(memory);
        // Both routes plus transient Java/direct buffers; driver allocation is opaque.
        long estimate = 64L * n + 8L * 1024L * 1024L;
        if (memory.lowMemory || estimate > Math.min(memory.totalMem * 3 / 10, memory.availMem / 2)) {
            return "Etapa bloqueada por presión de memoria; estimación preventiva " + (estimate / 1048576) + " MiB.";
        }
        if (Build.VERSION.SDK_INT >= 29) {
            PowerManager power = (PowerManager) getSystemService(POWER_SERVICE);
            if (power.getCurrentThermalStatus() >= PowerManager.THERMAL_STATUS_SEVERE) {
                return "Etapa detenida: Android informa temperatura severa.";
            }
        }
        return null;
    }

    private final Runnable campaignWatch = new Runnable() {
        @Override public void run() {
            if (!campaign || destroyed) return;
            String limit = resourceLimit(model.count());
            if (limit != null) { finishCampaign("resource_limit", limit); return; }
            if (SystemClock.uptimeMillis() - campaignStarted >= 60_000L) {
                finishCampaign("time_limit", "Se alcanzó el límite de 60 segundos."); return;
            }
            ui.postDelayed(this, 500L);
        }
    };

    private void startCampaign() {
        if (busy) return;
        campaignOriginal = model.exportText(); campaignOriginalSelection = selected; campaignRuns = new JSONArray();
        campaignStage = 0; campaignStarted = SystemClock.uptimeMillis(); campaign = true;
        busy(true); campaignNext(); ui.postDelayed(campaignWatch, 500L);
    }

    private void campaignNext() {
        if (!campaign) return;
        if (campaignStage >= COUNTS.length) { finishCampaign("completed", "Todas las etapas medidas."); return; }
        int n = COUNTS[campaignStage]; String limit = resourceLimit(n);
        if (limit != null) { finishCampaign("resource_limit", limit); return; }
        model = new ObjectSceneModel(n, model.seed()); selected = Math.min(selected, n - 1);
        count.setSelection(campaignStage); ObjectSceneModel.Snapshot snapshot = model.snapshot();
        long id = ++requestId;
        state("benchmarking", "Etapa " + (campaignStage + 1) + "/" + COUNTS.length + " · " + n + " objetos · misma escena para A/B.");
        result.setText("Etapas terminadas: " + campaignRuns.length()); selectedInfo();
        surface.queueEvent(() -> { renderer.setScene(snapshot); renderer.benchmark(id); });
    }

    private void campaignResult(String json) {
        try {
            JSONObject measurement = new JSONObject(json);
            measurement.put("process_pss_kib_after", Debug.getPss()); campaignRuns.put(measurement);
            if (!"completed".equals(measurement.optString("status")) || !measurement.optBoolean("passed")) {
                finishCampaign("incomplete", "La etapa no terminó con muestras válidas."); return;
            }
            double p95 = Math.max(measurement.getJSONObject("procedural").getDouble("p95_ms"),
                    measurement.getJSONObject("explicit").getDouble("p95_ms"));
            if (p95 >= 100.0) { finishCampaign("frame_limit", "P95 ≥ 100 ms en al menos una ruta."); return; }
            campaignStage++; campaignNext();
        } catch (Exception error) { finishCampaign("error", "No se pudo leer una etapa: " + error.getMessage()); }
    }

    private void finishCampaign(String reason, String explanation) {
        if (!campaign) return;
        campaign = false; requestId++; ui.removeCallbacks(campaignWatch);
        surface.queueEvent(renderer::cancelBenchmark);
        String finalReport;
        try {
            JSONObject out = new JSONObject(); out.put("type", "progressive_scale"); out.put("status", reason);
            out.put("reason", explanation); out.put("elapsed_ms", SystemClock.uptimeMillis() - campaignStarted);
            out.put("stages", campaignRuns); out.put("max_requested", COUNTS[Math.min(campaignStage, COUNTS.length - 1)]);
            out.put("p95_stop_ms", 100); out.put("max_duration_ms", 60000); finalReport = out.toString(2);
        } catch (Exception error) { finalReport = "{}"; }
        model = ObjectSceneModel.importText(campaignOriginal); selected = campaignOriginalSelection;
        for (int i = 0; i < COUNTS.length; i++) if (COUNTS[i] == model.count()) count.setSelection(i);
        publishScene(); benchmark = finalReport; result.setText(finalReport); busy(false);
        state("cancelled".equals(reason) ? "cancelled" : "error".equals(reason) ? "error" : "ready", explanation + " Tu escena se restauró.");
    }

    private void state(String name, String message) {
        status.setContentDescription(name); status.setText(message); status.setTextColor("error".equals(name) ? CORAL : LIME);
    }

    @Override public void onStats(ObjectSceneRenderer.Stats value) {
        ui.post(() -> {
            if (destroyed) return; latest = value;
            stats.setText(String.format(Locale.US, "%s · %,d objetos · P50 %.2f / P95 %.2f ms\n%s",
                    value.route, value.count, value.frameP50Ms, value.frameP95Ms, value.renderer));
            stats.setContentDescription("frames=" + value.frames + ";benchmarking=" + value.benchmarking);
            detail.setText(String.format(Locale.US,
                    "Regla + cambios (formato): %,d B · transformaciones: %,d B\nGPU reservada: VBO %,d B + textura %,d B · malla %,d B\nPSS proceso: %.1f MiB · CPU envío: %.3f ms",
                    model.logicalRuleBytes(), value.explicitBytes, value.allocatedExplicitBytes, value.allocatedSparseBytes,
                    value.meshBytes, Debug.getPss() / 1024.0, value.cpuSubmitMs));
            if ("starting".contentEquals(status.getContentDescription())) state("ready", "Escena lista. Selecciona, cambia y contrasta.");
        });
    }

    @Override public void onPick(int id) { ui.post(() -> { if (!destroyed && !busy && id >= 0) choose(id); }); }

    @Override public void onVerification(long id, String json) {
        ui.post(() -> {
            if (destroyed || id != requestId || !busy || !"checking".contentEquals(status.getContentDescription())) return;
            verification = json; busy(false); result.setText(json);
            try {
                JSONObject parsed = new JSONObject(json);
                state(parsed.optBoolean("passed") ? "ready" : "error",
                        parsed.optBoolean("passed") ? "Imagen A/B coincide dentro de la tolerancia declarada." : "Imagen A/B no coincide; revisa el informe.");
            } catch (Exception error) { state("error", "No se pudo leer la comprobación de imagen."); }
        });
    }

    @Override public void onBenchmark(long id, String json) {
        ui.post(() -> {
            if (destroyed || id != requestId || !busy || !"benchmarking".contentEquals(status.getContentDescription())) return;
            if (campaign) { campaignResult(json); return; }
            benchmark = json; busy(false); result.setText(json);
            try {
                JSONObject parsed = new JSONObject(json);
                state(parsed.optBoolean("passed") ? "ready" : "error", parsed.optBoolean("passed")
                        ? "Medición A/B terminada con imagen comprobada en ambas rutas."
                        : "Medición detenida: " + parsed.optString("status", "sin muestras válidas"));
            } catch (Exception error) { state("error", "No se pudo leer el resultado de la medición."); }
            persist();
        });
    }

    @Override public void onFailure(String message) {
        ui.post(() -> { if (!destroyed) {
            if (campaign) { finishCampaign("error", message); return; }
            requestId++; busy(false); state("error", message); result.setText(message);
        } });
    }

    private String report() {
        try {
            JSONObject out = new JSONObject(); out.put("experiment", "Mundo X / objetos"); out.put("version", "0.3.0");
            out.put("scene", model.exportText()); out.put("count", model.count()); out.put("seed", model.seed());
            out.put("edits", model.editCount()); out.put("scene_revision", sceneRevision);
            out.put("logical_rule_bytes", model.snapshot().logicalRuleBytes()); out.put("explicit_transform_bytes", 16L * model.count());
            out.put("process_pss_kib", Debug.getPss()); out.put("image_ab", new JSONObject(verification));
            out.put("benchmark", new JSONObject(benchmark)); out.put("timing", "wall frame intervals and CPU submit; not isolated GPU time");
            if (latest != null) { out.put("renderer", latest.renderer); out.put("vendor", latest.vendor); }
            return out.toString(2);
        } catch (Exception error) { return "Error creando informe: " + error.getMessage(); }
    }

    private void bindGestures() {
        ScaleGestureDetector pinch = new ScaleGestureDetector(this, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override public boolean onScale(ScaleGestureDetector detector) {
                if (!busy) { float scale = 1f / detector.getScaleFactor(); surface.queueEvent(() -> renderer.zoom(scale)); }
                return true;
            }
        });
        surface.setOnTouchListener(new View.OnTouchListener() {
            float lastX, lastY, startX, startY; boolean scaled;
            @Override public boolean onTouch(View view, MotionEvent event) {
                pinch.onTouchEvent(event); if (busy) return true;
                int action = event.getActionMasked();
                if (action == MotionEvent.ACTION_DOWN) {
                    lastX = startX = event.getX(); lastY = startY = event.getY(); scaled = false;
                } else if (action == MotionEvent.ACTION_POINTER_DOWN) { scaled = true; }
                else if (action == MotionEvent.ACTION_MOVE && !pinch.isInProgress() && event.getPointerCount() == 1) {
                    float dx = (event.getX() - lastX) / Math.max(1, view.getWidth());
                    float dy = (event.getY() - lastY) / Math.max(1, view.getHeight());
                    surface.queueEvent(() -> renderer.orbit(dx, dy)); lastX = event.getX(); lastY = event.getY();
                } else if (action == MotionEvent.ACTION_UP && !scaled
                        && Math.hypot(event.getX() - startX, event.getY() - startY) < dp(8)) {
                    float x = event.getX() / Math.max(1, view.getWidth()), y = event.getY() / Math.max(1, view.getHeight());
                    surface.queueEvent(() -> renderer.pick(x, y)); view.performClick();
                }
                return true;
            }
        });
    }

    @Override protected void onPause() {
        if (campaign) finishCampaign("cancelled", "Aplicación en segundo plano.");
        requestId++;
        if (surface != null) { surface.queueEvent(renderer::cancelBenchmark); surface.onPause(); }
        if (prefs != null && model != null) persist();
        if (busy && status != null) { busy(false); state("cancelled", "Medición detenida al salir de la pantalla."); }
        super.onPause();
    }
    @Override protected void onResume() { super.onResume(); if (surface != null) surface.onResume(); }
    @Override protected void onDestroy() { destroyed = true; ui.removeCallbacksAndMessages(null); super.onDestroy(); }

    private int dp(float n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    private LinearLayout column() { LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.VERTICAL); return l; }
    private LinearLayout row() { LinearLayout l = new LinearLayout(this); l.setGravity(Gravity.CENTER_VERTICAL); return l; }
    private LinearLayout.LayoutParams weight() { return new LinearLayout.LayoutParams(0, -2, 1f); }
    private TextView label(String value, int size, int color, boolean mono) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setPadding(0, dp(3), 0, dp(3)); view.setTypeface(mono ? Typeface.MONOSPACE : Typeface.SANS_SERIF); return view;
    }
    private Button button(String value, boolean primary) {
        Button view = new Button(this); view.setText(value); view.setAllCaps(false); view.setTextSize(11);
        view.setTextColor(primary ? LIME : BONE); view.setMinWidth(0); view.setMinimumWidth(0);
        view.setPadding(dp(6), dp(5), dp(6), dp(5)); view.setMinHeight(dp(44));
        GradientDrawable background = new GradientDrawable(); background.setColor(INK); background.setStroke(dp(1), RULE);
        background.setCornerRadius(dp(3)); view.setBackground(background);
        return view;
    }
    private EditText editor(String value, boolean signed) {
        EditText view = new EditText(this); view.setSingleLine(true); view.setText(value); view.setTextColor(BONE);
        view.setTextSize(12); view.setTypeface(Typeface.MONOSPACE); view.setInputType(InputType.TYPE_CLASS_NUMBER
                | (signed ? InputType.TYPE_NUMBER_FLAG_SIGNED : 0)); view.setBackgroundTintList(ColorStateList.valueOf(RULE)); return view;
    }
    private <T extends View> T tag(T view, String value) { view.setTag(value); return view; }
    private <T extends View> T control(T view) { controls.add(view); return view; }
    private Button taggedControl(String value, String tag, LinearLayout row) {
        Button button = control(tag(button(value, false), tag)); row.addView(button, weight()); return button;
    }
}
