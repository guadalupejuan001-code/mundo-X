package com.mundox.lab;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.text.InputFilter;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/** UI for the bounded eight-bit basis-state VM; native work never runs on the UI thread. */
public final class BasisVmActivity extends Activity {
    private static final int BACKGROUND = Color.rgb(9, 15, 27);
    private static final int SURFACE = Color.rgb(18, 28, 45);
    private static final int FIELD = Color.rgb(11, 19, 33);
    private static final int BORDER = Color.rgb(43, 60, 82);
    private static final int TEXT = Color.rgb(242, 247, 255);
    private static final int MUTED = Color.rgb(176, 192, 214);
    private static final int ACCENT = Color.rgb(120, 240, 196);
    private static final int WARNING = Color.rgb(255, 201, 121);
    private static final int ERROR = Color.rgb(255, 155, 164);
    private static final int MAX_STEPS = 10000;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());
    private final List<Button> jobButtons = new ArrayList<>();
    private final TextView[] registers = new TextView[4];
    private final TextView[] ram = new TextView[16];
    private Future<?> running;
    private volatile boolean destroyed;
    private boolean busy;
    private ScrollView scroll;
    private LinearLayout root;
    private LinearLayout resultCard;
    private LinearLayout executionData;
    private EditText editor;
    private EditText stepLimit;
    private ProgressBar progress;
    private TextView jobStatus;
    private TextView resultTitle;
    private TextView resultMessage;
    private TextView output;
    private TextView metrics;
    private TextView coreMemory;
    private TextView processMemory;
    private TextView trace;
    private Button traceButton;
    private Button copyButton;
    private String lastJson;
    private String lastSource = "";
    private String copyReport;
    private long lastPssKiB = -1;
    private boolean lastWasTest;
    private int lastStepLimit = 1000;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setBackgroundDrawableResource(android.R.color.black);
        getWindow().setStatusBarColor(BACKGROUND);
        getWindow().setNavigationBarColor(BACKGROUND);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

        scroll = new ScrollView(this);
        scroll.setBackgroundColor(BACKGROUND);
        scroll.setFillViewport(true);
        root = column();
        root.setPadding(dp(20), dp(8), dp(20), dp(32));
        root.setFocusableInTouchMode(true);
        scroll.addView(root, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        Button back = button("‹ Volver al laboratorio", false);
        back.setOnClickListener(v -> finish());
        root.addView(back, spaced(0, 12));
        TextView eyebrow = text("MUNDO X / PROTOTIPO", 12, ACCENT, true);
        eyebrow.setLetterSpacing(0.08f);
        root.addView(eyebrow, spaced(0, 6));
        root.addView(text("Máquina de 8 bits", 28, TEXT, true), spaced(0, 8));
        root.addView(text("Prototipo de máquina de 8 bits. Registros y RAM en CompactCore; "
                + "control clásico. Aún no arranca Android.", 15, MUTED, false), spaced(0, 20));

        LinearLayout examples = new LinearLayout(this);
        examples.setOrientation(LinearLayout.HORIZONTAL);
        String[] labels = {"Suma", "Fibonacci", "Memoria"};
        for (int i = 0; i < labels.length; i++) {
            final int example = i;
            Button choice = button(labels[i], false);
            choice.setTextSize(14);
            choice.setOnClickListener(v -> loadExample(example));
            jobButtons.add(choice);
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            if (i > 0) p.leftMargin = dp(8);
            examples.addView(choice, p);
        }
        root.addView(examples, spaced(0, 16));

        TextView editorLabel = text("Programa", 17, TEXT, true);
        root.addView(editorLabel, spaced(0, 4));
        root.addView(text("Edita las instrucciones o carga uno de los ejemplos.",
                13, MUTED, false), spaced(0, 10));
        editor = new EditText(this);
        editor.setId(View.generateViewId());
        editorLabel.setLabelFor(editor.getId());
        editor.setTextColor(TEXT);
        editor.setHintTextColor(MUTED);
        editor.setHint("Cargando el programa de suma…");
        editor.setTextSize(15);
        editor.setTypeface(Typeface.MONOSPACE);
        editor.setGravity(Gravity.TOP | Gravity.START);
        editor.setMinLines(11);
        editor.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        editor.setFilters(new InputFilter[]{new InputFilter.LengthFilter(16384)});
        editor.setHorizontallyScrolling(false);
        editor.setPadding(dp(14), dp(14), dp(14), dp(14));
        editor.setBackground(shape(FIELD, BORDER, 14));
        editor.setSaveEnabled(false);
        root.addView(editor, spaced(0, 16));

        TextView stepsLabel = text("Límite de instrucciones", 15, TEXT, true);
        root.addView(stepsLabel, spaced(0, 6));
        stepLimit = new EditText(this);
        stepLimit.setId(View.generateViewId());
        stepsLabel.setLabelFor(stepLimit.getId());
        stepLimit.setInputType(InputType.TYPE_CLASS_NUMBER);
        stepLimit.setFilters(new InputFilter[]{new InputFilter.LengthFilter(5)});
        stepLimit.setSingleLine(true);
        stepLimit.setTextColor(TEXT);
        stepLimit.setTextSize(17);
        stepLimit.setText("1000");
        stepLimit.setPadding(dp(14), dp(10), dp(14), dp(10));
        stepLimit.setBackground(shape(FIELD, BORDER, 12));
        root.addView(stepLimit, spaced(0, 6));
        root.addView(text("De 1 a 10 000. Detiene programas que no terminan; "
                + "no es una medida de rendimiento.", 13, MUTED, false), spaced(0, 14));

        Button run = button("Ejecutar programa", true);
        run.setContentDescription("Ejecutar programa");
        run.setOnClickListener(v -> executeProgram());
        jobButtons.add(run);
        root.addView(run, spaced(0, 8));
        Button test = button("Comprobar motor", false);
        test.setContentDescription("Comprobar motor");
        test.setOnClickListener(v -> runSelfTest());
        jobButtons.add(test);
        root.addView(test, spaced(0, 14));

        LinearLayout statusRow = new LinearLayout(this);
        statusRow.setGravity(Gravity.CENTER_VERTICAL);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleSmall);
        progress.setIndeterminateTintList(ColorStateList.valueOf(ACCENT));
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(dp(22), dp(22));
        progressParams.rightMargin = dp(10);
        statusRow.addView(progress, progressParams);
        jobStatus = text("Preparando el ejemplo…", 14, MUTED, false);
        jobStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        statusRow.addView(jobStatus, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        root.addView(statusRow, spaced(0, 18));

        buildResults();
        setContentView(scroll);
        applyInsets();
        root.requestFocus();

        if (savedInstanceState != null && savedInstanceState.containsKey("source")) {
            editor.setText(savedInstanceState.getString("source", ""));
            stepLimit.setText(savedInstanceState.getString("step_limit", "1000"));
            lastSource = savedInstanceState.getString("last_source", "");
            lastPssKiB = savedInstanceState.getLong("last_pss", -1);
            lastWasTest = savedInstanceState.getBoolean("last_test", false);
            lastStepLimit = savedInstanceState.getInt("last_limit", 1000);
            String restoredJson = savedInstanceState.getString("last_json");
            if (restoredJson != null) {
                try {
                    renderResult(new JSONObject(restoredJson), lastWasTest, lastPssKiB,
                            lastSource, lastStepLimit);
                } catch (Exception invalidSavedResult) {
                    jobStatus.setText("Programa restaurado. Vuelve a ejecutarlo.");
                }
            }
            jobStatus.setText("Programa restaurado. Listo para ejecutar.");
        } else {
            loadExample(0);
        }
    }

    private void buildResults() {
        resultCard = column();
        resultCard.setPadding(dp(16), dp(18), dp(16), dp(18));
        resultCard.setBackground(shape(SURFACE, BORDER, 16));
        resultCard.addView(text("ÚLTIMO RESULTADO", 12, MUTED, true), spaced(0, 8));
        resultTitle = text("Aún no se ha ejecutado", 19, TEXT, true);
        resultTitle.setContentDescription("Resultado de la máquina");
        resultTitle.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        resultCard.addView(resultTitle, spaced(0, 8));
        resultMessage = text("La ejecución comparará el resultado con una referencia clásica.",
                14, MUTED, false);
        resultCard.addView(resultMessage, spaced(0, 16));

        executionData = column();
        executionData.addView(text("Salida", 14, MUTED, true), spaced(0, 5));
        output = text("—", 25, ACCENT, true);
        output.setContentDescription("Salida del programa");
        output.setTypeface(Typeface.MONOSPACE);
        output.setTextIsSelectable(true);
        executionData.addView(output, spaced(0, 20));
        executionData.addView(text("Registros · 4 × 8 bits", 15, TEXT, true), spaced(0, 10));
        LinearLayout registerRow = new LinearLayout(this);
        for (int i = 0; i < registers.length; i++) {
            LinearLayout cell = column();
            cell.addView(text("R" + i, 13, MUTED, true));
            registers[i] = text("—", 17, TEXT, false);
            registers[i].setTypeface(Typeface.MONOSPACE);
            cell.addView(registers[i], spaced(4, 0));
            registerRow.addView(cell, new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        }
        executionData.addView(registerRow, spaced(0, 20));
        executionData.addView(text("RAM · 16 bytes", 15, TEXT, true), spaced(0, 4));
        executionData.addView(text("Dirección: valor decimal", 12, MUTED, false), spaced(0, 8));
        for (int r = 0; r < 4; r++) {
            LinearLayout row = new LinearLayout(this);
            for (int c = 0; c < 4; c++) {
                int index = r * 4 + c;
                ram[index] = text(String.format(Locale.US, "%02d: —", index), 13, TEXT, false);
                ram[index].setTypeface(Typeface.MONOSPACE);
                ram[index].setPadding(0, dp(6), 0, dp(6));
                row.addView(ram[index], new LinearLayout.LayoutParams(0,
                        ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            }
            executionData.addView(row);
        }
        metrics = text("", 14, MUTED, false);
        executionData.addView(metrics, spaced(16, 14));
        coreMemory = text("", 14, TEXT, true);
        executionData.addView(coreMemory, spaced(0, 6));
        executionData.addView(text("Carga lógica del estado; no incluye toda la aplicación.",
                12, MUTED, false), spaced(0, 18));
        resultCard.addView(executionData);

        processMemory = text("", 14, MUTED, false);
        resultCard.addView(processMemory, spaced(0, 12));
        traceButton = button("Ver ejecución paso a paso", false);
        traceButton.setVisibility(View.GONE);
        traceButton.setOnClickListener(v -> {
            boolean show = trace.getVisibility() != View.VISIBLE;
            trace.setVisibility(show ? View.VISIBLE : View.GONE);
            traceButton.setText(show ? "Ocultar ejecución paso a paso" : "Ver ejecución paso a paso");
        });
        resultCard.addView(traceButton, spaced(0, 8));
        trace = text("", 12, MUTED, false);
        trace.setTypeface(Typeface.MONOSPACE);
        trace.setTextIsSelectable(true);
        trace.setVisibility(View.GONE);
        resultCard.addView(trace, spaced(0, 10));
        copyButton = button("Copiar informe", false);
        copyButton.setEnabled(false);
        copyButton.setAlpha(0.5f);
        copyButton.setOnClickListener(v -> copyResult());
        resultCard.addView(copyButton);
        root.addView(resultCard, spaced(0, 0));
        executionData.setVisibility(View.GONE);
    }

    private void loadExample(final int index) {
        submit("Cargando ejemplo…", () -> {
            String source = NativeCore.basisVmExample(index);
            if (source == null || source.trim().isEmpty())
                throw new IllegalStateException("El motor no devolvió un ejemplo.");
            return JobResult.example(source, index);
        });
    }

    private void executeProgram() {
        if (busy) return;
        final String source = editor.getText().toString();
        if (source.trim().isEmpty()) {
            editor.setError("Escribe un programa o carga un ejemplo.");
            return;
        }
        final int maxSteps;
        try {
            maxSteps = Integer.parseInt(stepLimit.getText().toString().trim());
            if (maxSteps < 1 || maxSteps > MAX_STEPS) throw new NumberFormatException();
        } catch (NumberFormatException invalidLimit) {
            stepLimit.setError("Escribe un número entre 1 y 10 000.");
            stepLimit.requestFocus();
            return;
        }
        editor.clearFocus();
        InputMethodManager keyboard = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        if (keyboard != null) keyboard.hideSoftInputFromWindow(editor.getWindowToken(), 0);
        submit("Ejecutando y comprobando…", () -> JobResult.report(
                new JSONObject(NativeCore.basisVmRun(source, maxSteps)), false,
                readProcessPss(), source, maxSteps));
    }

    private void runSelfTest() {
        submit("Comprobando instrucciones y memoria…", () -> JobResult.report(
                new JSONObject(NativeCore.basisVmSelfTest()), true, readProcessPss(), "", 0));
    }

    private void submit(String message, Job job) {
        if (busy || destroyed) return;
        setBusy(true, message);
        running = executor.submit(() -> {
            try {
                final JobResult result = job.run();
                if (destroyed || Thread.currentThread().isInterrupted()) return;
                main.post(() -> {
                    if (destroyed || isFinishing() || isDestroyed()) return;
                    if (result.example != null) {
                        editor.setText(result.example);
                        editor.setSelection(0);
                        String[] names = {"Suma del 1 al 10", "Fibonacci", "Copia de memoria"};
                        setBusy(false, names[result.exampleIndex] + ". Listo para ejecutar.");
                    } else {
                        renderResult(result.json, result.selfTest, result.pss,
                                result.source, result.limit);
                        setBusy(false, result.selfTest ? "Comprobación terminada." : "Ejecución terminada.");
                        scrollToResults();
                    }
                });
            } catch (Exception | LinkageError failure) {
                if (destroyed || Thread.currentThread().isInterrupted()) return;
                final String detail = failure.getMessage() == null
                        ? failure.getClass().getSimpleName() : failure.getMessage();
                main.post(() -> {
                    if (destroyed || isFinishing() || isDestroyed()) return;
                    setBusy(false, "No se pudo completar la operación.");
                    showFailure(detail);
                    scrollToResults();
                });
            }
        });
    }

    private void setBusy(boolean value, String message) {
        busy = value;
        progress.setVisibility(value ? View.VISIBLE : View.GONE);
        jobStatus.setText(message);
        editor.setEnabled(!value);
        stepLimit.setEnabled(!value);
        for (Button button : jobButtons) {
            button.setEnabled(!value);
            button.setAlpha(value ? 0.5f : 1f);
        }
    }

    private void renderResult(JSONObject json, boolean selfTest, long pss,
                              String source, int maxSteps) {
        lastJson = json.toString();
        lastWasTest = selfTest;
        lastPssKiB = pss;
        lastSource = source;
        lastStepLimit = maxSteps;
        String status = json.optString("status", "ERROR");
        boolean verified = selfTest
                ? json.optInt("total", 0) > 0 && json.optInt("passed", -1) == json.optInt("total", 0)
                : json.optBoolean("reference_ok", false);
        if ("PASS".equals(status) && !verified) status = "MISMATCH";
        int color;
        String title;
        switch (status) {
            case "PASS":
                title = selfTest ? "PASS · Pruebas completadas" : "PASS · Resultado verificado";
                color = ACCENT;
                break;
            case "LIMIT":
                title = "LIMIT · Límite de pasos alcanzado";
                color = WARNING;
                break;
            case "MISMATCH":
                title = "MISMATCH · Diferencia con la referencia";
                color = ERROR;
                break;
            default:
                title = "ERROR · Revisa el programa";
                color = ERROR;
                break;
        }
        resultTitle.setText(title);
        resultTitle.setTextColor(color);
        String message = json.optString("message", "");
        if (selfTest) {
            message = json.optInt("passed", 0) + " de " + json.optInt("total", 0)
                    + " pruebas correctas." + (message.isEmpty() ? "" : "\n" + message);
        }
        resultMessage.setText(message.isEmpty() ? "Resultado recibido del motor." : message);
        executionData.setVisibility(selfTest ? View.GONE : View.VISIBLE);
        String outputText = join(json.optJSONArray("output"));
        output.setText(outputText);
        JSONArray registerValues = json.optJSONArray("registers");
        JSONArray memoryValues = json.optJSONArray("ram");
        for (int i = 0; i < registers.length; i++) {
            int value = registerValues == null ? -1 : registerValues.optInt(i, -1);
            registers[i].setText(value < 0 ? "—" : String.format(Locale.US, "%d\n0x%02X", value, value));
        }
        for (int i = 0; i < ram.length; i++) {
            int value = memoryValues == null ? -1 : memoryValues.optInt(i, -1);
            ram[i].setText(String.format(Locale.US, "%02d: %s", i,
                    value < 0 ? "—" : Integer.toString(value)));
        }
        String metricText = "Instrucciones: " + json.optInt("steps", 0)
                + "   ·   PC: " + json.optInt("pc", 0)
                + "\nDetenida: " + (json.optBoolean("halted", false) ? "sí" : "no")
                + "   ·   Z: " + flag(json, "zero") + "   C: " + flag(json, "carry")
                + "\nVM + registro de estados: " + String.format(Locale.getDefault(), "%.3f ms",
                json.optDouble("elapsed_us", 0) / 1000.0)
                + "\nReferencia fuera del tiempo."
                + "\nPuertas X / CX / CCX: " + json.optLong("gate_x", 0) + " / "
                + json.optLong("gate_cx", 0) + " / " + json.optLong("gate_ccx", 0)
                + "\nReferencia clásica: " + (json.optBoolean("reference_ok", false)
                ? "coincide" : "no confirmada");
        metrics.setText(metricText);
        String coreText = "Estado CompactCore: " + json.optLong("core_state_bytes", 0)
                + " bytes\n" + json.optInt("core_qubits", 0) + " qubits de base";
        coreMemory.setText(coreText);
        String pssText = pss >= 0
                ? String.format(Locale.getDefault(), "Proceso Android · PSS después: %.2f MiB", pss / 1024.0)
                : "Proceso Android · PSS no disponible";
        processMemory.setText(pssText + "\nIncluye interfaz, runtime y bibliotecas. Es una lectura actual, no el pico.");
        String traceText = json.optString("trace", "");
        trace.setText(traceText);
        trace.setVisibility(View.GONE);
        traceButton.setText("Ver ejecución paso a paso");
        traceButton.setVisibility(traceText.isEmpty() ? View.GONE : View.VISIBLE);

        StringBuilder report = new StringBuilder("MUNDO X · MÁQUINA DE 8 BITS\n");
        report.append(title).append('\n').append(resultMessage.getText()).append('\n');
        if (!selfTest) {
            report.append("Salida: ").append(outputText)
                    .append("\nRegistros: ").append(registerValues)
                    .append("\nRAM: ").append(memoryValues)
                    .append('\n').append(metricText).append('\n').append(coreText)
                    .append("\nLímite de instrucciones: ").append(maxSteps);
        }
        report.append('\n').append(processMemory.getText());
        if (!selfTest) report.append("\n\nPROGRAMA EJECUTADO\n").append(source);
        report.append("\n\nRESPUESTA DEL MOTOR\n").append(json.toString());
        copyReport = report.toString();
        copyButton.setEnabled(true);
        copyButton.setAlpha(1f);
    }

    private void showFailure(String detail) {
        resultTitle.setText("ERROR · Operación no disponible");
        resultTitle.setTextColor(ERROR);
        resultMessage.setText(detail);
        executionData.setVisibility(View.GONE);
        processMemory.setText("");
        trace.setVisibility(View.GONE);
        traceButton.setVisibility(View.GONE);
        copyReport = "Mundo X · Máquina de 8 bits\nERROR\n" + detail;
        copyButton.setEnabled(true);
        copyButton.setAlpha(1f);
        lastJson = null;
    }

    private static String flag(JSONObject json, String key) {
        Object value = json.opt(key);
        return Boolean.TRUE.equals(value) || value instanceof Number && ((Number) value).intValue() != 0
                ? "1" : "0";
    }

    private static String join(JSONArray values) {
        if (values == null || values.length() == 0) return "Sin salida";
        StringBuilder result = new StringBuilder();
        for (int i = 0; i < values.length(); i++) {
            if (i > 0) result.append(" · ");
            result.append(values.opt(i));
        }
        return result.toString();
    }

    private static long readProcessPss() {
        try {
            Debug.MemoryInfo memory = new Debug.MemoryInfo();
            Debug.getMemoryInfo(memory);
            return memory.getTotalPss();
        } catch (RuntimeException unavailable) {
            return -1;
        }
    }

    private void copyResult() {
        if (copyReport == null) return;
        try {
            ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (clipboard == null) throw new IllegalStateException("Portapapeles no disponible");
            clipboard.setPrimaryClip(ClipData.newPlainText("Mundo X · Resultado", copyReport));
            Toast.makeText(this, "Informe copiado", Toast.LENGTH_SHORT).show();
        } catch (RuntimeException unavailable) {
            Toast.makeText(this, "No se pudo copiar el informe", Toast.LENGTH_SHORT).show();
        }
    }

    private void scrollToResults() {
        scroll.post(() -> {
            if (!destroyed) scroll.smoothScrollTo(0, Math.max(0, resultCard.getTop() - dp(16)));
        });
    }

    private void applyInsets() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            scroll.setOnApplyWindowInsetsListener((view, insets) -> {
                android.graphics.Insets bars = insets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                android.graphics.Insets ime = insets.getInsets(WindowInsets.Type.ime());
                view.setPadding(bars.left, bars.top, bars.right, Math.max(bars.bottom, ime.bottom));
                return insets;
            });
            scroll.requestApplyInsets();
        }
    }

    private LinearLayout column() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    private TextView text(String value, int sp, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setLineSpacing(dp(2), 1f);
        if (bold) view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return view;
    }

    private Button button(String label, boolean primary) {
        Button view = new Button(this);
        view.setText(label);
        view.setAllCaps(false);
        view.setTextSize(16);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setTextColor(primary ? BACKGROUND : TEXT);
        view.setMinHeight(dp(50));
        view.setMinimumHeight(dp(50));
        view.setPadding(dp(12), dp(10), dp(12), dp(10));
        GradientDrawable background = shape(primary ? ACCENT : SURFACE,
                primary ? ACCENT : BORDER, 12);
        view.setBackground(new RippleDrawable(ColorStateList.valueOf(0x3378F0C4), background, null));
        view.setStateListAnimator(null);
        return view;
    }

    private GradientDrawable shape(int fill, int stroke, int radius) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill);
        drawable.setCornerRadius(dp(radius));
        drawable.setStroke(dp(1), stroke);
        return drawable;
    }

    private LinearLayout.LayoutParams spaced(int top, int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(top);
        params.bottomMargin = dp(bottom);
        return params;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        state.putString("source", editor.getText().toString());
        state.putString("step_limit", stepLimit.getText().toString());
        state.putString("last_json", lastJson);
        state.putString("last_source", lastSource);
        state.putLong("last_pss", lastPssKiB);
        state.putBoolean("last_test", lastWasTest);
        state.putInt("last_limit", lastStepLimit);
        super.onSaveInstanceState(state);
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        if (running != null) running.cancel(true);
        executor.shutdownNow();
        main.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    private interface Job { JobResult run() throws Exception; }

    private static final class JobResult {
        String example;
        int exampleIndex;
        JSONObject json;
        boolean selfTest;
        long pss;
        String source;
        int limit;

        static JobResult example(String source, int index) {
            JobResult result = new JobResult();
            result.example = source;
            result.exampleIndex = index;
            return result;
        }

        static JobResult report(JSONObject json, boolean selfTest, long pss, String source, int limit) {
            JobResult result = new JobResult();
            result.json = json;
            result.selfTest = selfTest;
            result.pss = pss;
            result.source = source;
            result.limit = limit;
            return result;
        }
    }
}
