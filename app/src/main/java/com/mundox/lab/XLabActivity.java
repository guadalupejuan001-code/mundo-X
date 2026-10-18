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
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.locks.ReentrantLock;

/** Small native experiments, observable results, and bounded resource work. */
public final class XLabActivity extends Activity {
    private static final int INK = Color.rgb(19, 23, 21);
    private static final int BONE = Color.rgb(236, 230, 214);
    private static final int LIME = Color.rgb(214, 237, 103);
    private static final int CORAL = Color.rgb(223, 129, 108);
    private static final int MUTED = Color.rgb(160, 169, 155);
    private static final int RULE = Color.rgb(60, 69, 59);
    private static final long MIB = 1024L * 1024L;
    // Survives Activity replacement: a finishing native call cannot overlap a new one.
    private static final ReentrantLock NATIVE = new ReentrantLock();
    private static final AtomicReference<XLabActivity> MEMORY_OWNER = new AtomicReference<>();
    private static final Object MEMORY_CONTROL = new Object();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService work = Executors.newSingleThreadExecutor();
    private final ScheduledExecutorService controls = Executors.newSingleThreadScheduledExecutor();
    private final AtomicReference<CircuitRequest> pending = new AtomicReference<>();
    private final AtomicBoolean draining = new AtomicBoolean();
    private final AtomicBoolean cancelMemory = new AtomicBoolean();
    private volatile boolean destroyed, memoryRequested, auxiliaryBusy;
    private volatile long circuitVersion, memoryVersion;
    private SharedPreferences prefs;
    private ScheduledFuture<?> poller;
    private FrameLayout pages;
    private final View[] panels = new View[4];
    private final Button[] tabs = new Button[4];
    private int activeTab, nextId = 0x004d0000, selectedStep = -1, previousQubits;
    private String previousScript = "";
    private boolean binding;
    private EditText script, seed, memoryQubits, protocolValue;
    private Spinner qubits, target, other, shots, budget, family, protocol;
    private TextView circuitStatus, circuitResult, generators, stepLabel;
    private TextView memoryStatus, memoryEstimate, memoryResult, memorySummary, memorySystem;
    private TextView trialFeedback, score, protocolOutput, protocolStatus, protocolSummary, protocolValueLabel, journal;
    private Button memoryRun, memoryStop, trialRun, protocolRun, protocolLoad;
    private LabPlotView circuitPlot, histogram, probabilities;
    private SeekBar steps;
    private JSONObject lastProtocol;
    private MemorySample latestMemory;
    private int trial = 0, prediction = -1;
    private final Button[] predictions = new Button[3];
    private boolean memoryWasRunning, memoryResultPinned;

    private final Runnable preview = () -> requestCircuit(0, false);

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(INK);
        getWindow().setNavigationBarColor(INK);
        prefs = getSharedPreferences("xlab.instrument.v1", MODE_PRIVATE);
        binding = true;
        LinearLayout root = column();
        root.setBackgroundColor(INK);
        root.setPadding(dp(18), dp(12), dp(18), 0);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(dp(18) + insets.getSystemWindowInsetLeft(),
                    dp(12) + insets.getSystemWindowInsetTop(),
                    dp(18) + insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets.consumeSystemWindowInsets();
        });
        LinearLayout masthead = row();
        TextView brand = text("MUNDO X   /   LAB", 16, LIME, true);
        masthead.addView(brand, weighted());
        Button legacy = button("Anterior ↗", false);
        legacy.setTextSize(11);
        legacy.setOnClickListener(v -> startActivity(new Intent(this, MainActivity.class)));
        masthead.addView(legacy);
        root.addView(masthead);
        root.addView(text("INSTRUMENTOS DE COMPUTACIÓN · SIMULACIÓN SOFTWARE", 9, MUTED, true));
        root.addView(rule(), new LinearLayout.LayoutParams(-1, dp(1)));
        pages = new FrameLayout(this);
        root.addView(pages, new LinearLayout.LayoutParams(-1, 0, 1));
        panels[0] = mesa();
        panels[1] = pruebas();
        panels[2] = carga();
        panels[3] = bitacora();
        for (View panel : panels) pages.addView(panel, new FrameLayout.LayoutParams(-1, -1));
        LinearLayout nav = row();
        String[] names = {"Mesa", "Pruebas", "Carga", "Bitácora"};
        String[] tags = {"tab_mesa", "tab_pruebas", "tab_carga", "tab_bitacora"};
        for (int i = 0; i < 4; i++) {
            final int index = i;
            tabs[i] = tagged(button(names[i], false), tags[i]);
            tabs[i].setTextSize(12);
            tabs[i].setOnClickListener(v -> selectTab(index));
            nav.addView(tabs[i], weighted());
        }
        root.addView(rule(), new LinearLayout.LayoutParams(-1, dp(1)));
        root.addView(nav);
        setContentView(root);
        root.requestApplyInsets();
        qubits.setSelection(Math.max(0, Math.min(7, prefs.getInt("qubits", 2) - 1)));
        script.setText(prefs.getString("script", "h 0\ncx 0 1"));
        seed.setText(prefs.getString("seed", "2026"));
        shots.setSelection(Math.max(0, Math.min(3, prefs.getInt("shots", 1))));
        selectedStep = prefs.getInt("step", -1);
        memoryQubits.setText(prefs.getString("memory_qubits", ""));
        previousQubits = currentQubits();
        previousScript = script.getText().toString();
        binding = false;
        updateTimeline();
        updateScore();
        refreshJournal();
        selectTab(prefs.getInt("tab", 0));
        boolean restored = false;
        try {
            JSONObject saved = new JSONObject(prefs.getString("last_circuit", "{}"));
            if (saved.optBoolean("ok") && saved.optString("client_script").equals(script.getText().toString())
                    && saved.optInt("qubits") == currentQubits()) { applyCircuit(saved); restored = true; }
        } catch (Exception ignored) { }
        try { applyProtocol(new JSONObject(prefs.getString("last_protocol", "{}"))); }
        catch (Exception ignored) { }
        poller = controls.scheduleWithFixedDelay(this::pollMemory, 0, 800, TimeUnit.MILLISECONDS);
        if (!restored) ui.post(preview);
    }

    private View mesa() {
        LinearLayout body = page("01 / MESA", "Mesa de circuitos.",
                "Escribe puertas. Observa probabilidades, interferencia y colapso. El cálculo se realiza en la CPU de tu teléfono.");
        LinearLayout presets = row();
        String[] names = {"Bell", "GHZ · 4", "Interferencia", "Cluster · 4"};
        for (int i = 0; i < names.length; i++) {
            final int preset = i;
            Button b = button(names[i], false);
            if (i == 0) tagged(b, "preset_bell");
            if (i == 2) tagged(b, "preset_interference");
            b.setTextSize(10);
            b.setOnClickListener(v -> loadPreset(preset));
            presets.addView(b, weighted());
        }
        body.addView(presets);
        LinearLayout settings = row();
        qubits = tagged(spinner(new String[]{"1 qubit", "2 qubits", "3 qubits", "4 qubits", "5 qubits", "6 qubits", "7 qubits", "8 qubits"}), "qubit_count");
        target = spinner(new String[]{"q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7"});
        other = spinner(new String[]{"q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7"});
        other.setSelection(1);
        settings.addView(qubits, weighted()); settings.addView(target, weighted()); settings.addView(other, weighted());
        body.addView(settings);
        LinearLayout palette = row();
        for (String gate : new String[]{"H", "X", "Z", "S", "CX", "CZ", "M"}) {
            Button b = button(gate, false); b.setTextSize(11);
            b.setOnClickListener(v -> appendGate(gate)); palette.addView(b, new LinearLayout.LayoutParams(dp(48), dp(48)));
        }
        HorizontalScrollView paletteScroll = new HorizontalScrollView(this);
        paletteScroll.addView(palette); body.addView(paletteScroll, full());
        script = tagged(editor("h 0\ncx 0 1", true), "script_editor");
        script.setMinLines(4); script.setMaxLines(8);
        body.addView(script, full());
        LinearLayout runSettings = row();
        seed = tagged(editor("2026", false), "seed_input");
        seed.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_SIGNED);
        seed.setHint("Semilla");
        shots = tagged(spinner(new String[]{"128 lecturas", "512 lecturas", "2048 lecturas", "4096 lecturas"}), "shots_count");
        runSettings.addView(seed, weighted()); runSettings.addView(shots, weighted()); body.addView(runSettings);
        Button run = tagged(button("EJECUTAR Y LEER →", true), "run_circuit");
        run.setOnClickListener(v -> { selectedStep = -1; updateTimeline(); requestCircuit(currentShots(), true); }); body.addView(run, full());
        LinearLayout transport = row();
        Button back = tagged(button("← Paso", false), "step_back");
        Button next = tagged(button("Paso →", false), "step_next");
        Button all = tagged(button("Todo", false), "step_all");
        back.setOnClickListener(v -> stepBy(-1)); next.setOnClickListener(v -> stepBy(1));
        all.setOnClickListener(v -> { selectedStep = -1; updateTimeline(); requestCircuit(0, false); });
        transport.addView(back, weighted()); transport.addView(next, weighted()); transport.addView(all, weighted());
        body.addView(transport);
        stepLabel = text("PASO 0", 11, LIME, true); body.addView(stepLabel);
        steps = tagged(new SeekBar(this), "step_slider");
        steps.setMinimumHeight(dp(48));
        steps.setProgressTintList(ColorStateList.valueOf(LIME));
        steps.setThumbTintList(ColorStateList.valueOf(CORAL));
        steps.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int value, boolean fromUser) {
                if (fromUser) { selectedStep = value; updateTimelineLabel(); debouncePreview(); }
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) { }
        });
        body.addView(steps, full());
        circuitStatus = tagged(text("Preparando la mesa…", 12, MUTED, true), "circuit_status");
        status(circuitStatus, "idle", "Preparando la mesa…"); body.addView(circuitStatus);
        HorizontalScrollView wireScroll = new HorizontalScrollView(this);
        wireScroll.setFillViewport(true);
        circuitPlot = new LabPlotView(this);
        circuitPlot.setQubitTapListener(q -> target.setSelection(q));
        wireScroll.addView(circuitPlot, new ViewGroup.LayoutParams(-2, -2));
        body.addView(wireScroll, full());
        body.addView(section("LECTURAS DEL ESTADO ACTUAL"));
        histogram = new LabPlotView(this); body.addView(histogram, full());
        body.addView(text("Las mediciones del circuito se reproducen con la semilla. Cada lectura usa una copia del estado resultante; no vuelve a ejecutar el circuito.", 11, MUTED, false));
        probabilities = new LabPlotView(this); body.addView(probabilities, full());
        generators = text("Generadores · sin ejecutar", 11, BONE, true); body.addView(generators, full());
        Button details = button("Ver / ocultar JSON", false);
        circuitResult = tagged(text("", 10, MUTED, true), "circuit_result");
        circuitResult.setTextIsSelectable(true); circuitResult.setVisibility(View.GONE);
        details.setOnClickListener(v -> circuitResult.setVisibility(circuitResult.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE));
        body.addView(details); body.addView(circuitResult, full());
        LinearLayout maintenance = row();
        Button clear = button("Limpiar", false), replay = button("Reproducir ↺", false);
        clear.setOnClickListener(v -> { script.setText(""); selectedStep = 0; requestCircuit(0, false); });
        replay.setOnClickListener(v -> { selectedStep = -1; updateTimeline(); requestCircuit(currentShots(), true); });
        maintenance.addView(clear, weighted()); maintenance.addView(replay, weighted()); body.addView(maintenance);
        script.addTextChangedListener(watcher(() -> {
            String source = script.getText().toString();
            if (source.equals(previousScript)) return;
            previousScript = source; selectedStep = -1; updateTimeline(); debouncePreview();
        }));
        qubits.setOnItemSelectedListener(selection(() -> {
            if (previousQubits == currentQubits()) return;
            previousQubits = currentQubits();
            if (target.getSelectedItemPosition() >= currentQubits()) target.setSelection(0);
            if (other.getSelectedItemPosition() >= currentQubits()) other.setSelection(Math.min(1, currentQubits() - 1));
            updateTimeline(); debouncePreview();
        }));
        // Keep the circuit and its transport visible before the editor.
        int instrumentIndex = body.indexOfChild(presets) + 1;
        for (View instrument : new View[]{wireScroll, transport, stepLabel, steps, circuitStatus}) {
            body.removeView(instrument);
            body.addView(instrument, instrumentIndex++);
        }
        return scroll(body);
    }

    private View pruebas() {
        LinearLayout body = page("02 / PRUEBAS", "Primero predice.\nDespués comprueba.",
                "Cada respuesta se contrasta con el motor nativo. El acierto no depende de una animación.");
        Spinner choices = spinner(new String[]{"Interferencia · H Z H", "Bell · medir y repetir", "La fase y las probabilidades"});
        body.addView(choices, full());
        choices.setOnItemSelectedListener(selection(() -> { trial = choices.getSelectedItemPosition(); resetPrediction(); }));
        LinearLayout answers = row();
        for (int i = 0; i < 3; i++) {
            final int answer = i; predictions[i] = button(new String[]{"0", "1", "50 / 50"}[i], false);
            predictions[i].setOnClickListener(v -> {
                prediction = answer;
                for (int j = 0; j < 3; j++) predictions[j].setTextColor(j == answer ? LIME : BONE);
                trialRun.setEnabled(!auxiliaryBusy && !memoryRequested);
            }); answers.addView(predictions[i], weighted());
        }
        body.addView(answers);
        trialRun = tagged(button("COMPROBAR MI PREDICCIÓN", true), "trial_run");
        trialRun.setEnabled(false); trialRun.setOnClickListener(v -> runTrial()); body.addView(trialRun, full());
        trialFeedback = tagged(text("HZH: ¿qué resultado observarás al final?", 14, BONE, false), "trial_feedback"); body.addView(trialFeedback, full());
        score = text("", 12, LIME, true); body.addView(score);
        body.addView(rule(), new LinearLayout.LayoutParams(-1, dp(1)));
        body.addView(section("INVESTIGACIÓN / PROTOCOLOS"));
        body.addView(text("Mensajes, oráculos y corrección de un error. Protocolos cuánticos simulados y verificables; no una QPU ni corrección de errores general.", 13, MUTED, false));
        protocol = tagged(spinner(new String[]{"Codificación superdensa", "Bernstein–Vazirani · 4 bits", "Repetición · un bit flip"}), "protocol_selector");
        protocolValueLabel = text("Mensaje: entero 0…3 (00, 01, 10, 11)", 12, MUTED, false);
        protocolValue = tagged(editor("3", false), "protocol_value");
        protocolValue.setInputType(InputType.TYPE_CLASS_NUMBER);
        body.addView(protocol, full()); body.addView(protocolValueLabel, full()); body.addView(protocolValue, full());
        protocol.setOnItemSelectedListener(selection(() -> {
            int p = protocol.getSelectedItemPosition();
            protocolValueLabel.setText(p == 0 ? "Mensaje: entero 0…3 (00, 01, 10, 11)" : p == 1 ?
                    "Secreto: entero 0…15, leído como cuatro bits" : "Error: 0 ninguno · 1 X(q0) · 2 X(q1) · 3 X(q2)");
            protocolValue.setText(p == 1 ? "10" : p == 2 ? "1" : "3");
        }));
        protocolRun = tagged(button("EJECUTAR PROTOCOLO →", true), "protocol_run");
        protocolRun.setOnClickListener(v -> runProtocol()); body.addView(protocolRun, full());
        protocolStatus = tagged(text("Selecciona un protocolo y su mensaje, secreto o error.", 12, MUTED, true), "protocol_status");
        status(protocolStatus, "idle", "Selecciona un protocolo y su mensaje, secreto o error.");
        body.addView(protocolStatus, full());
        protocolSummary = text("", 13, BONE, false); body.addView(protocolSummary, full());
        Button protocolDetails = button("Ver / ocultar JSON del protocolo", false);
        protocolOutput = tagged(text("{}", 10, MUTED, true), "protocol_result");
        protocolOutput.setTextIsSelectable(true); protocolOutput.setVisibility(View.GONE);
        protocolDetails.setOnClickListener(v -> protocolOutput.setVisibility(protocolOutput.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE));
        body.addView(protocolDetails); body.addView(protocolOutput, full());
        protocolLoad = button("Abrir este circuito en Mesa ↗", false); protocolLoad.setEnabled(false);
        protocolLoad.setOnClickListener(v -> {
            if (lastProtocol == null) return;
            JSONObject result = lastProtocol.optJSONObject("result");
            if (result == null) return;
            loadCircuit(result.optInt("qubits", 2), lastProtocol.optString("script", "")); selectTab(0);
        }); body.addView(protocolLoad, full());
        return scroll(body);
    }

    private View carga() {
        LinearLayout body = page("03 / CARGA", "Ensayos\nde memoria.",
                "Construye estados GHZ o cluster con el presupuesto que elijas. Observa el proceso y detén el ensayo cuando quieras.");
        body.addView(text("Referencia de 2 GB", 12, LIME, true));
        Spinner meaning = spinner(new String[]{"RAM total del teléfono (por defecto)", "Quiero dedicar hasta 2 GB al motor"});
        body.addView(meaning, full());
        budget = tagged(spinner(new String[]{"64 MiB", "128 MiB", "256 MiB", "512 MiB", "1024 MiB", "2048 MiB"}), "memory_budget");
        body.addView(budget, full());
        memorySystem = text("Consultando RAM real del sistema…", 12, BONE, true); body.addView(memorySystem, full());
        memoryEstimate = tagged(text("El presupuesto permitido se calcula antes de habilitar la prueba.", 12, MUTED, true), "memory_estimate");
        body.addView(memoryEstimate, full());
        body.addView(text("Qubits del ensayo (desde 2, máximo derivado del presupuesto)", 12, BONE, false));
        memoryQubits = tagged(editor("", false), "memory_qubits"); memoryQubits.setInputType(InputType.TYPE_CLASS_NUMBER);
        body.addView(memoryQubits, full());
        Button fitBudget = button("Ajustar qubits al presupuesto", false);
        fitBudget.setOnClickListener(v -> refreshMemoryEstimate(true)); body.addView(fitBudget, full());
        family = tagged(spinner(new String[]{"GHZ", "Cluster lineal"}), "memory_family"); body.addView(family, full());
        LinearLayout actions = row();
        memoryRun = tagged(button("INICIAR ENSAYO", true), "memory_run"); memoryRun.setEnabled(false);
        memoryStop = tagged(button("Parar", false), "memory_cancel"); memoryStop.setEnabled(false);
        memoryRun.setOnClickListener(v -> startMemory()); memoryStop.setOnClickListener(v -> stopMemory());
        actions.addView(memoryRun, weighted()); actions.addView(memoryStop, weighted()); body.addView(actions);
        memoryStatus = tagged(text("Preparando presupuesto…", 13, LIME, true), "memory_status");
        status(memoryStatus, "idle", "Preparando presupuesto…"); body.addView(memoryStatus, full());
        memorySummary = text("", 12, BONE, true); body.addView(memorySummary, full());
        memoryResult = tagged(text("{}", 10, MUTED, true), "memory_result"); memoryResult.setTextIsSelectable(true); body.addView(memoryResult, full());
        body.addView(text("Límite: mínimo entre 2048 MiB, 30 % de RAM total y 50 % de RAM disponible. Se bloquea con lowMemory. El estado ensayado se libera al terminar; PSS es la RAM real del proceso. La validación comprueba generadores muestreados, no todos los componentes de un statevector.", 12, MUTED, false));
        meaning.setOnItemSelectedListener(selection(() -> { if (meaning.getSelectedItemPosition() == 1) budget.setSelection(5); refreshMemoryEstimate(false); }));
        budget.setOnItemSelectedListener(selection(() -> refreshMemoryEstimate(true)));
        memoryQubits.addTextChangedListener(watcher(() -> refreshMemoryEstimate(false)));
        return scroll(body);
    }

    private View bitacora() {
        LinearLayout body = page("04 / BITÁCORA", "Cuaderno\nde resultados.", "Los últimos veinte ensayos se guardan solo en este dispositivo.");
        LinearLayout actions = row();
        Button share = button("Compartir", false), copy = button("Copiar JSON", false), clear = button("Borrar", false);
        share.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_SEND); intent.setType("text/plain");
            intent.putExtra(Intent.EXTRA_TEXT, journal.getText().toString()); startActivity(Intent.createChooser(intent, "Compartir bitácora"));
        });
        copy.setOnClickListener(v -> {
            ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            clipboard.setPrimaryClip(ClipData.newPlainText("Mundo X Lab", prefs.getString("journal", "[]")));
            Toast.makeText(this, "JSON copiado", Toast.LENGTH_SHORT).show();
        });
        clear.setOnClickListener(v -> { prefs.edit().putString("journal", "[]").apply(); refreshJournal(); });
        actions.addView(share, weighted()); actions.addView(copy, weighted()); actions.addView(clear, weighted()); body.addView(actions);
        journal = tagged(text("Aún no hay ensayos guardados.", 12, BONE, true), "bitacora_content"); journal.setTextIsSelectable(true); body.addView(journal, full());
        return scroll(body);
    }

    private void requestCircuit(int readings, boolean log) {
        if (binding || destroyed) return;
        ui.removeCallbacks(preview);
        final long version = ++circuitVersion;
        pending.set(null);
        if (memoryRequested || auxiliaryBusy) { status(circuitStatus, "running", "Otro ensayo está usando el motor."); return; }
        try {
            String source = script.getText().toString();
            if (gateCount(source) > 128 || source.length() > 16384) throw new IllegalArgumentException("Máximo: 128 puertas por circuito.");
            CircuitRequest request = new CircuitRequest(currentQubits(), source, selectedStep, readings, readSeed(), version, log);
            pending.set(request); persistInputs(); status(circuitStatus, "running", "Calculando el estado…");
            startDrain();
        } catch (Exception error) { circuitError(error.getMessage()); }
    }

    private void startDrain() {
        if (destroyed || !draining.compareAndSet(false, true)) return;
        try { work.execute(() -> {
            try {
                CircuitRequest request;
                while (!destroyed && !memoryRequested && !auxiliaryBusy && (request = pending.getAndSet(null)) != null) {
                    final CircuitRequest current = request;
                    String result;
                    if (!NATIVE.tryLock()) result = errorJson("El motor está terminando otro ensayo. Intenta de nuevo.");
                    else {
                        try { result = NativeCore.labCircuit(current.n, current.script, current.step, current.shots, current.seed); }
                        catch (Throwable error) { result = errorJson(error.toString()); }
                        finally { NATIVE.unlock(); }
                    }
                    final String raw = result;
                    ui.post(() -> {
                        if (destroyed || current.version != circuitVersion) return;
                        try {
                            JSONObject value = new JSONObject(raw); value.put("client_script", current.script);
                            applyCircuit(value);
                            if (value.optBoolean("ok")) {
                                prefs.edit().putString("last_circuit", value.toString()).apply();
                                if (current.log) logRun("Circuito", value);
                            }
                        } catch (Exception error) { circuitError("Respuesta inválida: " + error.getMessage()); }
                    });
                }
            } finally {
                draining.set(false);
                if (!destroyed && !memoryRequested && !auxiliaryBusy && pending.get() != null) startDrain();
            }
        }); } catch (RejectedExecutionException ignored) { draining.set(false); }
    }

    private void applyCircuit(JSONObject value) {
        if (!value.optBoolean("ok")) { circuitError(value.optString("error", "Error del motor")); return; }
        int n = value.optInt("qubits", currentQubits()), step = value.optInt("step"), total = value.optInt("total_gates");
        status(circuitStatus, "ready", String.format(Locale.US, "%d QUBITS · PASO %d / %d · %s · %.2f ms", n, step, total, bytes(value.optLong("state_bytes")), value.optDouble("elapsed_ms")));
        circuitPlot.showCircuit(n, value.optString("client_script", script.getText().toString()), step);
        JSONArray counts = value.optJSONArray("counts"); long[] bins = new long[counts == null ? 0 : counts.length()];
        for (int i = 0; i < bins.length; i++) bins[i] = counts.optLong(i);
        histogram.showHistogram(n, bins);
        JSONArray p = value.optJSONArray("prob_one"); double[] marginals = new double[p == null ? 0 : p.length()];
        for (int i = 0; i < marginals.length; i++) marginals[i] = p.optDouble(i);
        probabilities.showProbabilities(marginals);
        StringBuilder g = new StringBuilder("GENERADORES / REPRESENTACIÓN\n");
        JSONArray rows = value.optJSONArray("generators");
        if (rows != null) for (int i = 0; i < rows.length(); i++) g.append(rows.optString(i)).append('\n');
        JSONArray m = value.optJSONArray("measurements");
        if (m != null && m.length() > 0) {
            g.append("\nMEDICIONES DEL CIRCUITO\n");
            for (int i = 0; i < m.length(); i++) {
                JSONObject item = m.optJSONObject(i);
                if (item != null) g.append("q").append(item.optInt("qubit")).append(" → ").append(item.optInt("outcome"))
                        .append("  p=").append(item.optDouble("probability")).append('\n');
            }
        }
        generators.setText(g.toString()); circuitResult.setText(value.toString());
        binding = true; steps.setMax(Math.max(0, total)); steps.setProgress(step); binding = false;
        stepLabel.setText("PASO " + step + " / " + total + "   ·   q0 a la izquierda");
    }

    private void runTrial() {
        if (prediction < 0 || auxiliaryBusy || memoryRequested) return;
        final int chosen = prediction, kind = trial;
        final long chosenSeed;
        try { chosenSeed = readSeed(); } catch (Exception error) { trialFeedback.setText(error.getMessage()); return; }
        auxiliaryBusy = true; pending.set(null); trialRun.setEnabled(false); trialFeedback.setText("Comprobando con el motor nativo…");
        work.execute(() -> {
            JSONObject result = null, comparison = null; String failure = null;
            if (!NATIVE.tryLock()) failure = "El motor está ocupado.";
            else {
                try {
                    String source = kind == 0 ? "h 0\nz 0\nh 0" : kind == 1 ? "h 0\ncx 0 1\nmeasure 0\nmeasure 0\nmeasure 1" : "h 0";
                    result = new JSONObject(NativeCore.labCircuit(kind == 1 ? 2 : 1, source, -1, 512, chosenSeed));
                    if (kind == 2) comparison = new JSONObject(NativeCore.labCircuit(1, "h 0\nz 0", -1, 0, chosenSeed));
                    if (!result.optBoolean("ok") || (comparison != null && !comparison.optBoolean("ok"))) throw new IllegalStateException(result.optString("error", "No se pudo verificar."));
                } catch (Throwable error) { failure = error.toString(); }
                finally { NATIVE.unlock(); }
            }
            final JSONObject actual = result, second = comparison; final String error = failure;
            ui.post(() -> {
                auxiliaryBusy = false; if (destroyed) return; trialRun.setEnabled(prediction >= 0);
                if (error != null) { trialFeedback.setText("ERROR · " + error); return; }
                int correct; String explanation;
                if (kind == 0) {
                    double p1 = actual.optJSONArray("prob_one").optDouble(0);
                    correct = p1 > 0.999999 ? 1 : p1 < 0.000001 ? 0 : 2;
                    explanation = String.format(Locale.US, "HZH · P(1) = %.6f. La fase se convirtió en un resultado observable.", p1);
                } else if (kind == 1) {
                    JSONArray outcomes = actual.optJSONArray("measurements");
                    if (outcomes == null || outcomes.length() < 3) { trialFeedback.setText("ERROR · Faltan mediciones verificables."); return; }
                    int a = outcomes.optJSONObject(0).optInt("outcome"), b = outcomes.optJSONObject(1).optInt("outcome"), c = outcomes.optJSONObject(2).optInt("outcome");
                    correct = a == b && b == c ? 0 : a != c ? 2 : 1;
                    explanation = "Esta ejecución: q0=" + a + ", repetición=" + b + ", q1=" + c + ". La segunda medida conserva el resultado; el par Bell está correlacionado.";
                } else {
                    double a = actual.optJSONArray("prob_one").optDouble(0), b = second.optJSONArray("prob_one").optDouble(0);
                    correct = Math.abs(a - 0.5) < 1e-9 && Math.abs(b - 0.5) < 1e-9 ? 2 : a < 0.5 ? 0 : 1;
                    explanation = String.format(Locale.US, "H: P(1)=%.3f · HZ: P(1)=%.3f. La fase difiere aunque estas probabilidades coincidan. H final permite distinguirlas.", a, b);
                }
                boolean hit = chosen == correct;
                prefs.edit().putInt("attempts", prefs.getInt("attempts", 0) + 1).putInt("correct", prefs.getInt("correct", 0) + (hit ? 1 : 0)).apply();
                trialFeedback.setText((hit ? "ACIERTO\n" : "REVISAMOS LA PREDICCIÓN\n") + explanation);
                trialFeedback.setTextColor(hit ? LIME : CORAL); updateScore(); logRun("Prueba " + (kind + 1), actual);
            });
        });
    }

    private void runProtocol() {
        if (auxiliaryBusy || memoryRequested) return;
        final int kind = protocol.getSelectedItemPosition(), value;
        final long chosenSeed;
        try {
            value = Integer.parseInt(protocolValue.getText().toString().trim()); chosenSeed = readSeed();
            if (value < 0 || value > (kind == 1 ? 15 : 3)) throw new IllegalArgumentException("El valor queda fuera del rango del protocolo.");
        } catch (Exception error) {
            status(protocolStatus, "error", "ERROR · " + error.getMessage()); protocolOutput.setText(errorJson(error.getMessage())); return;
        }
        auxiliaryBusy = true; pending.set(null); protocolRun.setEnabled(false);
        status(protocolStatus, "running", "Ejecutando el protocolo…");
        work.execute(() -> {
            String raw;
            if (!NATIVE.tryLock()) raw = errorJson("El motor está ocupado.");
            else { try { raw = NativeCore.labProtocol(kind, value, 512, chosenSeed); }
                catch (Throwable error) { raw = errorJson(error.toString()); } finally { NATIVE.unlock(); } }
            final String response = raw;
            ui.post(() -> {
                auxiliaryBusy = false; if (destroyed) return; protocolRun.setEnabled(true);
                try {
                    JSONObject result = new JSONObject(response);
                    applyProtocol(result);
                    if (result.optBoolean("ok")) { prefs.edit().putString("last_protocol", result.toString()).apply(); logRun("Investigación", result); }
                } catch (Exception error) {
                    status(protocolStatus, "error", "ERROR · " + error); protocolOutput.setText(errorJson(error.toString()));
                }
            });
        });
    }

    private void applyProtocol(JSONObject result) {
        if (!result.has("ok")) return;
        protocolOutput.setText(result.toString());
        if (!result.optBoolean("ok")) {
            status(protocolStatus, "error", "ERROR · " + result.optString("error")); protocolLoad.setEnabled(false); return;
        }
        lastProtocol = result; protocolLoad.setEnabled(result.optJSONObject("result") != null);
        status(protocolStatus, result.optBoolean("passed") ? "ready" : "error",
                result.optBoolean("passed") ? "VERIFICACIÓN NATIVA · PASS" : "VERIFICACIÓN NATIVA · FAIL");
        protocolSummary.setText(result.optString("title") + "\n\n" + result.optString("explanation") +
                "\n\nEsperado: " + result.opt("expected") + "\nObservado: " + result.opt("observed") +
                (result.has("syndrome") ? "\nSíndrome: " + result.optString("syndrome") : "") +
                (result.has("oracle_calls") ? "\nLlamadas al oráculo: " + result.optInt("oracle_calls") : ""));
    }

    private void startMemory() {
        if (memoryRequested || auxiliaryBusy || destroyed) return;
        final int requestedBudget = budgetMiB(), n, selectedFamily = family.getSelectedItemPosition();
        try { n = Integer.parseInt(memoryQubits.getText().toString().trim()); }
        catch (Exception error) { status(memoryStatus, "error", "Introduce un número de qubits válido."); return; }
        String preflight = memoryProblem(requestedBudget, n, latestMemory);
        if (preflight != null) { status(memoryStatus, "error", preflight); return; }
        cancelMemory.set(false); memoryRequested = true; memoryResultPinned = false; ++memoryVersion; pending.set(null);
        memoryRun.setEnabled(false); memoryStop.setEnabled(true); status(memoryStatus, "running", "Preparando el ensayo…");
        prefs.edit().putString("memory_qubits", String.valueOf(n)).apply();
        work.execute(() -> {
            String response;
            if (cancelMemory.get() || destroyed) response = cancelledJson();
            else {
                String problem = memoryProblem(requestedBudget, n, readMemory());
                if (problem != null) response = errorJson(problem);
                else if (!NATIVE.tryLock()) response = errorJson("El motor sigue ocupado; espera a que termine.");
                else {
                    try {
                        synchronized (MEMORY_CONTROL) { MEMORY_OWNER.set(this); }
                        response = cancelMemory.get() || destroyed ? cancelledJson() : NativeCore.labMemoryRun(requestedBudget, n, selectedFamily);
                    }
                    catch (Throwable error) { response = errorJson(error.toString()); }
                    finally {
                        synchronized (MEMORY_CONTROL) { MEMORY_OWNER.compareAndSet(this, null); }
                        NATIVE.unlock();
                    }
                }
            }
            final String result = response;
            ui.post(() -> {
                memoryRequested = false; ++memoryVersion; if (destroyed) return;
                memoryResultPinned = true;
                try { JSONObject value = new JSONObject(result); applyMemory(value); logRun("Carga", value); }
                catch (Exception error) { status(memoryStatus, "error", "Respuesta inválida: " + error); }
                refreshMemoryEstimate(false);
            });
        });
    }

    private void stopMemory() {
        cancelMemory.set(true); status(memoryStatus, "running", "Deteniendo de forma cooperativa…");
        if (!controls.isShutdown()) controls.execute(this::cancelOwnedMemory);
    }

    private void cancelOwnedMemory() {
        synchronized (MEMORY_CONTROL) {
            if (MEMORY_OWNER.get() == this) try { NativeCore.labMemoryCancel(); } catch (Throwable ignored) { }
        }
    }

    private void pollMemory() {
        if (destroyed) return;
        final long version = memoryVersion;
        MemorySample sample = readMemory(); JSONObject progress = null;
        try {
            progress = new JSONObject(NativeCore.labMemoryProgress());
            if (cancelMemory.get() && progress.optBoolean("running")) cancelOwnedMemory();
        } catch (Throwable ignored) { }
        final JSONObject value = progress;
        ui.post(() -> {
            if (destroyed) return; latestMemory = sample;
            memorySystem.setText(String.format(Locale.US, "RAM total  %s\nDisponible %s\nPSS app    %s\nlowMemory  %s", bytes(sample.total), bytes(sample.available), bytes(sample.pss), sample.low ? "sí" : "no"));
            if (value != null && version == memoryVersion && !memoryResultPinned
                    && (!memoryRequested || value.optBoolean("running"))) applyMemory(value);
            refreshMemoryEstimate(false);
        });
    }

    private void applyMemory(JSONObject value) {
        String state = value.optString("status", value.optBoolean("ok", true) ? "idle" : "error");
        boolean running = value.optBoolean("running") || memoryRequested;
        memoryWasRunning = running; memoryStop.setEnabled(running && (memoryRequested || MEMORY_OWNER.get() == this));
        String human = "idle".equals(state) ? "Listo para un ensayo limitado." : "pass".equals(state) ? "ENSAYO COMPLETADO · validación parcial" :
                "cancelled".equals(state) ? "Ensayo detenido." : "error".equals(state) ? "ERROR · " + value.optString("error") :
                state + " · " + value.optLong("completed_gates") + " / " + value.optLong("total_gates") + " puertas";
        status(memoryStatus, running ? "running" : "error".equals(state) ? "error" : "cancelled".equals(state) ? "cancelled" : "ready", human);
        memorySummary.setText("Estado ensayado: " + bytes(value.optLong("state_bytes")) + "\n" +
                "Tiempo: " + String.format(Locale.US, "%.2f ms", value.optDouble("elapsed_ms")) + "\n" +
                "Generadores verificados: " + value.optLong("verified_rows") + " · validación muestreada");
        memoryResult.setText(value.toString());
    }

    private MemorySample readMemory() {
        ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
        ((ActivityManager) getSystemService(ACTIVITY_SERVICE)).getMemoryInfo(info);
        return new MemorySample(info.totalMem, info.availMem, info.lowMemory, Debug.getPss() * 1024L);
    }

    private void refreshMemoryEstimate(boolean replaceQubits) {
        if (memoryEstimate == null || budget == null || memoryQubits == null) return;
        int max = qubitsForBudget(budgetMiB());
        if (replaceQubits || memoryQubits.getText().length() == 0) {
            binding = true; memoryQubits.setText(String.valueOf(max)); binding = false;
        }
        int n; try { n = Integer.parseInt(memoryQubits.getText().toString().trim()); } catch (Exception e) { n = 0; }
        String problem = memoryProblem(budgetMiB(), n, latestMemory);
        memoryEstimate.setText("Presupuesto elegido: " + budgetMiB() + " MiB\nMáximo de qubits: " + max +
                "\nTableau solicitado: " + bytes(n > 0 ? tableauBytes(n) : 0) + "\nPresupuesto permitido ahora: " +
                (latestMemory == null ? "consultando…" : bytes(latestMemory.safe)) + (problem == null ? "" : "\n" + problem));
        memoryRun.setEnabled(problem == null && !memoryRequested && !memoryWasRunning && !auxiliaryBusy);
        if (problem == null && !memoryRequested && !memoryWasRunning && "idle".contentEquals(memoryStatus.getContentDescription()))
            status(memoryStatus, "ready", "Presupuesto comprobado. Puedes iniciar el ensayo.");
    }

    private static String memoryProblem(int budget, int n, MemorySample system) {
        if (system == null) return "Esperando la lectura real de RAM.";
        if (system.low) return "Android indica poca memoria: ejecución bloqueada.";
        if (budget * MIB > system.safe) return "Ese presupuesto supera el máximo permitido ahora.";
        if (n < 2 || n > qubitsForBudget(budget)) return "Elige entre 2 y " + qubitsForBudget(budget) + " qubits para ese presupuesto.";
        return null;
    }

    private static long tableauBytes(int n) { return 16L * n * ((n + 63L) / 64L) + n; }
    private static int qubitsForBudget(int mib) {
        int lo = 1, hi = 200000;
        while (lo < hi) { int mid = (lo + hi + 1) / 2; if (tableauBytes(mid) <= mib * MIB) lo = mid; else hi = mid - 1; }
        return lo;
    }

    private void loadPreset(int which) {
        String[] programs = {"h 0\ncx 0 1", "h 0\ncx 0 1\ncx 1 2\ncx 2 3", "h 0\nz 0\nh 0", "h 0\nh 1\nh 2\nh 3\ncz 0 1\ncz 1 2\ncz 2 3"};
        loadCircuit(which == 0 ? 2 : which == 2 ? 1 : 4, programs[which]);
    }
    private void loadCircuit(int n, String source) {
        binding = true; qubits.setSelection(Math.max(0, Math.min(7, n - 1))); previousQubits = currentQubits(); script.setText(source); previousScript = source; selectedStep = 0; binding = false;
        updateTimeline(); requestCircuit(0, false);
    }
    private void appendGate(String name) {
        int q = target.getSelectedItemPosition(), r = other.getSelectedItemPosition();
        if (q >= currentQubits() || ((name.equals("CX") || name.equals("CZ")) && (r >= currentQubits() || r == q))) {
            circuitError("Selecciona qubits distintos dentro del circuito."); return;
        }
        String command = name.equals("M") ? "measure" : name.toLowerCase(Locale.ROOT);
        String line = command + " " + q + ((name.equals("CX") || name.equals("CZ")) ? " " + r : "");
        String existing = script.getText().toString(); script.setText(existing + (existing.isEmpty() ? "" : "\n") + line); script.setSelection(script.length());
    }
    private void stepBy(int delta) {
        int now = selectedStep < 0 ? (delta > 0 ? 0 : gateCount(script.getText().toString())) : selectedStep;
        selectedStep = Math.max(0, Math.min(gateCount(script.getText().toString()), now + delta)); updateTimeline(); requestCircuit(0, false);
    }
    private void updateTimeline() {
        if (steps == null || script == null) return;
        int count = gateCount(script.getText().toString());
        binding = true; steps.setMax(count); steps.setProgress(selectedStep < 0 ? count : Math.min(selectedStep, count)); binding = false;
        updateTimelineLabel(); circuitPlot.showCircuit(currentQubits(), script.getText().toString(), selectedStep);
    }
    private void updateTimelineLabel() { if (stepLabel != null) stepLabel.setText("PASO " + (selectedStep < 0 ? gateCount(script.getText().toString()) : selectedStep) + " / " + gateCount(script.getText().toString())); }
    private static int gateCount(String source) { int count = 0; for (String line : source.split("\n")) if (!line.split("#", 2)[0].trim().isEmpty()) count++; return Math.min(129, count); }
    private int currentQubits() { return qubits.getSelectedItemPosition() + 1; }
    private int currentShots() { return new int[]{128, 512, 2048, 4096}[shots.getSelectedItemPosition()]; }
    private int budgetMiB() { return new int[]{64, 128, 256, 512, 1024, 2048}[budget.getSelectedItemPosition()]; }
    private long readSeed() { try { return Long.parseLong(seed.getText().toString().trim()); } catch (Exception e) { throw new IllegalArgumentException("La semilla debe ser un entero de 64 bits."); } }
    private void debouncePreview() { if (!binding) { ++circuitVersion; pending.set(null); ui.removeCallbacks(preview); ui.postDelayed(preview, 160); } }
    private void circuitError(String message) { status(circuitStatus, "error", "ERROR · " + message); circuitResult.setText(errorJson(message)); }

    private void resetPrediction() {
        prediction = -1; if (trialRun == null) return; trialRun.setEnabled(false);
        String[] labels = trial == 1 ? new String[]{"Iguales", "Independientes", "Opuestos"} : new String[]{"0", "1", "50 / 50"};
        for (int i = 0; i < 3; i++) { predictions[i].setText(labels[i]); predictions[i].setTextColor(BONE); }
        trialFeedback.setText(trial == 0 ? "HZH: ¿qué resultado observarás al final?" : trial == 1 ? "¿Cómo se relacionan q0, su repetición y q1?" : "Compara H con HZ: ¿qué probabilidades de Z tendrán ambos?");
    }
    private void updateScore() { score.setText("ACIERTOS " + prefs.getInt("correct", 0) + " / " + prefs.getInt("attempts", 0)); }
    private void logRun(String kind, JSONObject result) {
        try {
            JSONArray old = new JSONArray(prefs.getString("journal", "[]")), entries = new JSONArray();
            JSONObject entry = new JSONObject(); entry.put("at", new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date())); entry.put("kind", kind); entry.put("result", result); entries.put(entry);
            for (int i = 0; i < Math.min(19, old.length()); i++) entries.put(old.get(i));
            prefs.edit().putString("journal", entries.toString()).apply(); refreshJournal();
        } catch (Exception ignored) { }
    }
    private void refreshJournal() {
        if (journal == null) return;
        StringBuilder out = new StringBuilder();
        int entryCount = 0;
        try {
            JSONArray entries = new JSONArray(prefs.getString("journal", "[]"));
            entryCount = entries.length();
            for (int i = 0; i < entries.length(); i++) {
                JSONObject entry = entries.getJSONObject(i), result = entry.optJSONObject("result");
                out.append(String.format(Locale.US, "%02d / ", i + 1)).append(entry.optString("kind")).append("\n").append(entry.optString("at")).append('\n');
                if (result != null) out.append(result.toString(2)); out.append("\n\n────────────────\n\n");
            }
        } catch (Exception ignored) { }
        journal.setText(out.length() == 0 ? "Aún no hay ensayos guardados." : out.toString());
        journal.setContentDescription(String.valueOf(entryCount));
    }
    private void persistInputs() { prefs.edit().putInt("qubits", currentQubits()).putString("script", script.getText().toString()).putString("seed", seed.getText().toString()).putInt("shots", shots.getSelectedItemPosition()).putInt("step", selectedStep).putInt("tab", activeTab).apply(); }
    @Override protected void onSaveInstanceState(Bundle out) { persistInputs(); super.onSaveInstanceState(out); }
    @Override protected void onStop() {
        persistInputs();
        if (memoryRequested || MEMORY_OWNER.get() == this) stopMemory();
        super.onStop();
    }
    @Override public void onTrimMemory(int level) {
        if (level >= TRIM_MEMORY_RUNNING_LOW && (memoryRequested || MEMORY_OWNER.get() == this)) stopMemory();
        super.onTrimMemory(level);
    }
    @Override protected void onDestroy() {
        destroyed = true; cancelMemory.set(true); pending.set(null); ui.removeCallbacksAndMessages(null);
        if (poller != null) poller.cancel(false);
        if (memoryRequested || memoryWasRunning) for (long delay : new long[]{0, 100, 350, 800})
            controls.schedule(this::cancelOwnedMemory, delay, TimeUnit.MILLISECONDS);
        controls.shutdown(); work.shutdownNow(); super.onDestroy();
    }

    private void selectTab(int tab) { activeTab = Math.max(0, Math.min(3, tab)); for (int i = 0; i < panels.length; i++) { panels[i].setVisibility(i == activeTab ? View.VISIBLE : View.GONE); tabs[i].setTextColor(i == activeTab ? LIME : MUTED); } }
    private void status(TextView view, String machine, String message) { view.setContentDescription(machine); view.setText(message); view.setTextColor(machine.equals("error") ? CORAL : machine.equals("ready") ? LIME : MUTED); }
    private LinearLayout page(String index, String title, String explanation) {
        LinearLayout body = column(); body.setPadding(0, dp(22), 0, dp(36)); body.addView(text(index, 10, LIME, true));
        TextView heading = text(title, 34, BONE, false); heading.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD)); heading.setLineSpacing(dp(1), 1f); body.addView(heading, full());
        TextView note = text(explanation, 13, MUTED, false); note.setPadding(0, dp(8), 0, dp(16)); body.addView(note, full()); return body;
    }
    private ScrollView scroll(LinearLayout body) { ScrollView s = new ScrollView(this); s.setFillViewport(true); s.setClipToPadding(false); s.addView(body); return s; }
    private LinearLayout column() { LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.VERTICAL); return l; }
    private LinearLayout row() { LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.HORIZONTAL); l.setGravity(Gravity.CENTER_VERTICAL); return l; }
    private LinearLayout.LayoutParams full() { LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2); p.topMargin = dp(8); return p; }
    private LinearLayout.LayoutParams weighted() { return new LinearLayout.LayoutParams(0, -2, 1); }
    private View rule() { View line = new View(this); line.setBackgroundColor(RULE); return line; }
    private TextView section(String value) { TextView t = text(value, 11, LIME, true); t.setPadding(0, dp(20), 0, dp(6)); return t; }
    private TextView text(String value, int size, int color, boolean mono) { TextView t = new TextView(this); t.setText(value); t.setTextColor(color); t.setTextSize(size); t.setFontFeatureSettings("kern"); t.setTypeface(Typeface.create(mono ? "monospace" : "sans-serif", Typeface.NORMAL)); t.setLineSpacing(dp(3), 1f); return t; }
    private Button button(String value, boolean primary) {
        Button b = new Button(this); b.setText(value); b.setAllCaps(false); b.setTextSize(12); b.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        b.setTextColor(primary ? INK : BONE); b.setMinWidth(0); b.setMinimumWidth(0); b.setMinHeight(dp(48)); b.setMinimumHeight(dp(48)); b.setPadding(dp(7), dp(6), dp(7), dp(6));
        GradientDrawable background = new GradientDrawable(); background.setColor(primary ? LIME : INK); background.setStroke(dp(1), primary ? LIME : RULE); background.setCornerRadius(dp(2)); b.setBackground(background); return b;
    }
    private EditText editor(String value, boolean multiline) { EditText e = new EditText(this); e.setText(value); e.setTextColor(BONE); e.setHintTextColor(MUTED); e.setTextSize(13); e.setTypeface(Typeface.MONOSPACE); e.setGravity(Gravity.TOP | Gravity.START); e.setPadding(dp(10), dp(12), dp(10), dp(12)); e.setSingleLine(!multiline); e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS | (multiline ? InputType.TYPE_TEXT_FLAG_MULTI_LINE : 0)); e.setBackgroundTintList(ColorStateList.valueOf(RULE)); return e; }
    private Spinner spinner(String[] values) { Spinner s = new Spinner(this); s.setAdapter(adapter(values)); s.setBackgroundTintList(ColorStateList.valueOf(MUTED)); s.setMinimumHeight(dp(48)); return s; }
    private ArrayAdapter<String> adapter(String[] values) {
        return new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item, values) {
            @Override public View getView(int position, View convert, ViewGroup parent) { TextView t = (TextView) super.getView(position, convert, parent); t.setTextColor(BONE); t.setTextSize(12); t.setTypeface(Typeface.MONOSPACE); return t; }
            @Override public View getDropDownView(int position, View convert, ViewGroup parent) { TextView t = new TextView(XLabActivity.this); t.setText(getItem(position)); t.setTextColor(INK); t.setBackgroundColor(BONE); t.setTextSize(15); t.setPadding(dp(14), dp(14), dp(14), dp(14)); return t; }
        };
    }
    private AdapterView.OnItemSelectedListener selection(Runnable callback) { return new AdapterView.OnItemSelectedListener() { @Override public void onItemSelected(AdapterView<?> p, View v, int position, long id) { if (!binding) callback.run(); } @Override public void onNothingSelected(AdapterView<?> p) { } }; }
    private TextWatcher watcher(Runnable callback) { return new TextWatcher() { @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) { } @Override public void onTextChanged(CharSequence s, int start, int before, int count) { if (!binding) callback.run(); } @Override public void afterTextChanged(Editable e) { } }; }
    private <T extends View> T tagged(T v, String tag) { v.setTag(tag); v.setId(nextId++); return v; }
    private int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private static String bytes(long value) { return String.format(Locale.US, "%.2f MiB", value / (double) MIB); }
    private static String errorJson(String error) { try { return new JSONObject().put("ok", false).put("status", "error").put("error", error).toString(); } catch (Exception ignored) { return "{\"ok\":false,\"status\":\"error\"}"; } }
    private static String cancelledJson() { return "{\"status\":\"cancelled\",\"running\":false,\"state_bytes\":0}"; }
    private static final class CircuitRequest {
        final int n, step, shots; final String script; final long seed, version; final boolean log;
        CircuitRequest(int n, String script, int step, int shots, long seed, long version, boolean log) { this.n = n; this.script = script; this.step = step; this.shots = shots; this.seed = seed; this.version = version; this.log = log; }
    }
    private static final class MemorySample {
        final long total, available, pss, safe; final boolean low;
        MemorySample(long total, long available, boolean low, long pss) { this.total = total; this.available = available; this.low = low; this.pss = pss; this.safe = Math.max(0, Math.min(2048L * MIB, Math.min(total * 3 / 10, available / 2))); }
    }
}
