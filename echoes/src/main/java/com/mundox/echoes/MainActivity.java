package com.mundox.echoes;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Offline echo experiment. Native computation and memory sampling run off the UI thread. */
public final class MainActivity extends Activity {
    private static final int BG = Color.rgb(8, 15, 23);
    private static final int SURFACE = Color.rgb(16, 28, 39);
    private static final int FIELD = Color.rgb(9, 20, 30);
    private static final int BORDER = Color.rgb(40, 60, 72);
    private static final int TEXT = Color.rgb(239, 247, 250);
    private static final int MUTED = Color.rgb(169, 188, 202);
    private static final int ACCENT = Color.rgb(111, 232, 207);
    private static final int BLUE = Color.rgb(130, 187, 255);
    private static final int WARNING = Color.rgb(255, 207, 136);
    private static final int ERROR = Color.rgb(255, 151, 163);
    private static final int[] BUDGETS = {4096, 16384, 65536};
    private static final String LIBRARY_ERROR;

    static {
        String error = null;
        try {
            System.loadLibrary("mundox_echoes");
        } catch (LinkageError | SecurityException failure) {
            error = failure.toString();
        }
        LIBRARY_ERROR = error;
    }

    public static native long nativePrepare();
    public static native String nativeRun(long token, int rows, int cols, int depth, long seed,
            int perturb, int probe, int rounds, int maxComponents, boolean compareDense);
    public static native String nativeSelfTest(long token);
    public static native void nativeCancel(long token);

    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());
    private final List<View> controls = new ArrayList<>();
    private final List<EditText> inputs = new ArrayList<>();
    private volatile boolean destroyed;
    private boolean busy;
    private boolean referenceCapped;
    private boolean lastWasTest;
    private long activeToken;
    private String lastReport;
    private ScrollView scroll;
    private LinearLayout root;
    private LinearLayout resultCard;
    private LinearLayout data;
    private EditText rows, cols, depth, seed, perturb, probe;
    private Spinner rounds, budget;
    private CheckBox compare;
    private Button runButton, testButton, cancelButton, copyButton, shareButton;
    private ProgressBar progress;
    private TextView qubitCount, referenceHint, jobStatus, resultTitle, resultMessage;
    private TextView signal, controlSignal, normalization, timing, memory, components, error;
    private SignalBar signalBar;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

        scroll = new ScrollView(this);
        scroll.setBackgroundColor(BG);
        scroll.setFillViewport(true);
        FrameLayout stage = new FrameLayout(this);
        root = column();
        root.setPadding(dp(20), dp(24), dp(20), dp(32));
        root.setFocusableInTouchMode(true);
        int contentWidth = getResources().getConfiguration().screenWidthDp > 760
                ? dp(760) : ViewGroup.LayoutParams.MATCH_PARENT;
        stage.addView(root, new FrameLayout.LayoutParams(contentWidth,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.CENTER_HORIZONTAL));
        scroll.addView(stage);

        TextView brand = text("MUNDO X  /  ECOS", 12, ACCENT, true);
        brand.setLetterSpacing(.12f);
        root.addView(brand, space(0, 8));
        root.addView(text("Perturba. Invierte. Mide.", 28, TEXT, true), space(0, 8));
        root.addView(text("Explora cómo cambia una señal cuántica al recorrer un circuito "
                + "y su inversa. Todo se calcula en este dispositivo.", 14, MUTED, false), space(0, 18));

        LinearLayout settings = card();
        settings.addView(text("01  PREPARA EL EXPERIMENTO", 12, ACCENT, true), space(0, 14));
        rows = number("Filas", "2", false);
        cols = number("Columnas", "3", false);
        addPair(settings, labeled("Filas", rows), labeled("Columnas", cols));
        qubitCount = text("6 qubits · retícula 2 × 3", 13, BLUE, true);
        settings.addView(qubitCount, space(0, 14));
        depth = number("Profundidad del circuito", "3", false);
        seed = number("Semilla del circuito", "7", true);
        addPair(settings, labeled("Profundidad · 0–30", depth), labeled("Semilla", seed));
        perturb = number("Qubit de perturbación", "0", false);
        probe = number("Qubit de lectura", "1", false);
        addPair(settings, labeled("Perturbación · qubit", perturb), labeled("Lectura · qubit", probe));
        settings.addView(text("Los qubits se numeran desde 0. La semilla permite repetir "
                + "el mismo experimento.", 12, MUTED, false), space(0, 14));

        rounds = choices("Rondas del eco", new String[]{"1 ronda", "2 rondas"});
        budget = choices("Límite de componentes", new String[]{"4 096", "16 384", "65 536"});
        addPair(settings, labeled("Rondas", rounds), labeled("Componentes · límite", budget));
        settings.addView(text("El límite de componentes protege la memoria del motor. "
                + "Si se alcanza, el cálculo se detiene sin presentar un resultado completo. "
                + "No es un límite de qubits ni una comparación de velocidad.", 12, MUTED, false), space(0, 12));
        compare = new CheckBox(this);
        compare.setText("Comparar con referencia densa");
        compare.setTextSize(14);
        compare.setTextColor(TEXT);
        compare.setButtonTintList(ColorStateList.valueOf(ACCENT));
        compare.setChecked(true);
        compare.setMinHeight(dp(48));
        compare.setContentDescription("Comparar con referencia densa");
        controls.add(compare);
        settings.addView(compare, space(0, 3));
        referenceHint = text("Verificación independiente disponible hasta 16 qubits.", 12, MUTED, false);
        settings.addView(referenceHint);
        root.addView(settings, space(0, 16));

        runButton = button("Ejecutar eco", true);
        runButton.setContentDescription("Ejecutar eco");
        runButton.setOnClickListener(v -> runExperiment());
        root.addView(runButton, space(0, 8));
        cancelButton = button("Cancelar cálculo", false);
        cancelButton.setContentDescription("Cancelar cálculo");
        cancelButton.setVisibility(View.GONE);
        cancelButton.setOnClickListener(v -> requestCancel());
        root.addView(cancelButton, space(0, 8));
        testButton = button("Comprobar simulación", false);
        testButton.setContentDescription("Comprobar simulación");
        testButton.setOnClickListener(v -> beginJob(true, null));
        root.addView(testButton, space(0, 12));

        LinearLayout status = new LinearLayout(this);
        status.setGravity(Gravity.CENTER_VERTICAL);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleSmall);
        progress.setIndeterminateTintList(ColorStateList.valueOf(ACCENT));
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressLayout = new LinearLayout.LayoutParams(dp(20), dp(20));
        progressLayout.rightMargin = dp(10);
        status.addView(progress, progressLayout);
        jobStatus = text("Listo para explorar", 13, MUTED, false);
        jobStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        status.addView(jobStatus, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        root.addView(status, space(0, 16));
        buildResults();
        root.addView(text("Experimento controlado con circuitos Clifford + T. No reproduce "
                + "el experimento de Google ni demuestra una ventaja computacional.",
                12, MUTED, false), space(18, 0));

        setContentView(scroll);
        applyInsets();
        root.requestFocus();
        if (state != null) restore(state);
        TextWatcher watcher = new TextWatcher() {
            public void beforeTextChanged(CharSequence value, int start, int count, int after) {}
            public void onTextChanged(CharSequence value, int start, int before, int count) { updateQubits(); }
            public void afterTextChanged(Editable value) {}
        };
        rows.addTextChangedListener(watcher);
        cols.addTextChangedListener(watcher);
        updateQubits();
        if (LIBRARY_ERROR != null) {
            runButton.setEnabled(false);
            testButton.setEnabled(false);
            renderFailure("No se pudo cargar el motor nativo. " + LIBRARY_ERROR, false);
        }
    }

    private void buildResults() {
        resultCard = card();
        resultCard.addView(text("02  RESULTADO", 12, ACCENT, true), space(0, 12));
        resultTitle = text("Tu próximo eco, aquí", 21, TEXT, true);
        resultTitle.setContentDescription("Resultado del eco");
        resultTitle.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        resultCard.addView(resultTitle, space(0, 8));
        resultMessage = text("Ejecuta el experimento para ver la señal y comprobarla con "
                + "una referencia independiente.", 14, MUTED, false);
        resultMessage.setContentDescription("Detalle del resultado");
        resultCard.addView(resultMessage, space(0, 16));
        data = column();
        data.addView(text("SEÑAL DEL ECO · ⟨Z⟩", 12, MUTED, true), space(0, 5));
        signal = text("—", 38, ACCENT, true);
        signal.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        signal.setContentDescription("Señal Z");
        signal.setTextIsSelectable(true);
        data.addView(signal, space(0, 4));
        signalBar = new SignalBar(this);
        signalBar.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        data.addView(signalBar, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(42)));
        data.addView(text("La señal va de −1 a +1. No es una probabilidad ni una medida de velocidad.",
                12, MUTED, false), space(4, 16));
        controlSignal = metric(data, "Control sin perturbación");
        normalization = metric(data, "Norma del estado");
        timing = metric(data, "Tiempo de cálculo");
        components = metric(data, "Componentes del estado");
        memory = metric(data, "Memoria");
        error = metric(data, "Error frente a referencia");
        data.setVisibility(View.GONE);
        resultCard.addView(data);
        copyButton = button("Copiar informe JSON", false);
        copyButton.setContentDescription("Copiar informe JSON");
        copyButton.setEnabled(false);
        copyButton.setOnClickListener(v -> copyReport());
        resultCard.addView(copyButton, space(10, 8));
        shareButton = button("Compartir informe JSON", false);
        shareButton.setContentDescription("Compartir informe JSON");
        shareButton.setEnabled(false);
        shareButton.setOnClickListener(v -> shareReport());
        resultCard.addView(shareButton);
        root.addView(resultCard);
    }

    private TextView metric(LinearLayout parent, String label) {
        LinearLayout box = column();
        box.setPadding(dp(12), dp(12), dp(12), dp(12));
        box.setBackground(shape(FIELD, BORDER, 10));
        box.addView(text(label, 12, MUTED, true), space(0, 5));
        TextView value = text("—", 14, TEXT, false);
        value.setTextIsSelectable(true);
        box.addView(value);
        parent.addView(box, space(0, 8));
        return value;
    }

    private void updateQubits() {
        int r = readIntOrZero(rows), c = readIntOrZero(cols);
        long n = (long) r * c;
        boolean valid = r >= 1 && c >= 1 && n <= 63;
        qubitCount.setText(valid ? n + " qubits · retícula " + r + " × " + c
                : "Usa una retícula de 1 a 63 qubits en total");
        qubitCount.setTextColor(valid ? BLUE : WARNING);
        boolean cap = !valid || n > 16;
        if (cap) compare.setChecked(false);
        else if (referenceCapped) compare.setChecked(true);
        referenceCapped = cap;
        compare.setEnabled(!busy && !cap);
        referenceHint.setText(n > 16 && valid
                ? "Más de 16 qubits: se ejecuta sin referencia densa. El resultado no se marcará como verificado."
                : "Verificación independiente disponible hasta 16 qubits.");
    }

    private void runExperiment() {
        if (busy) return;
        try {
            Config config = new Config();
            config.rows = bounded(rows, "Filas", 1, 63);
            config.cols = bounded(cols, "Columnas", 1, 63);
            int n = config.rows * config.cols;
            if (n > 63) throw new IllegalArgumentException("La retícula admite hasta 63 qubits en total.");
            config.depth = bounded(depth, "Profundidad", 0, 30);
            try { config.seed = Long.parseUnsignedLong(seed.getText().toString().trim()); }
            catch (NumberFormatException invalid) { seed.setError("De 0 a 18446744073709551615"); throw new IllegalArgumentException("La semilla debe ser un entero de 64 bits sin signo."); }
            seed.setError(null);
            config.perturb = bounded(perturb, "Qubit de perturbación", 0, n - 1);
            config.probe = bounded(probe, "Qubit de lectura", 0, n - 1);
            config.rounds = rounds.getSelectedItemPosition() + 1;
            config.budget = BUDGETS[budget.getSelectedItemPosition()];
            config.compare = compare.isChecked() && n <= 16;
            beginJob(false, config);
        } catch (IllegalArgumentException invalid) {
            jobStatus.setText(invalid.getMessage());
            jobStatus.setTextColor(WARNING);
            Toast.makeText(this, invalid.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void beginJob(boolean selfTest, Config config) {
        if (busy || LIBRARY_ERROR != null) return;
        final long token;
        try {
            token = nativePrepare();
            activeToken = token;
        } catch (LinkageError | RuntimeException failure) {
            renderFailure("No se pudo preparar el motor: " + failure, selfTest);
            scrollToResult();
            return;
        }
        View focus = getCurrentFocus();
        if (focus != null) {
            InputMethodManager keyboard = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (keyboard != null) keyboard.hideSoftInputFromWindow(focus.getWindowToken(), 0);
            focus.clearFocus();
        }
        setBusy(true, selfTest);
        resultCard.setVisibility(View.GONE);
        executor.execute(() -> {
            JSONObject report;
            try {
                String raw = selfTest ? nativeSelfTest(token) : nativeRun(token, config.rows, config.cols,
                        config.depth, config.seed, config.perturb, config.probe, config.rounds,
                        config.budget, config.compare);
                report = new JSONObject(raw);
                if (!report.has("status")) throw new IllegalStateException("El motor no devolvió un estado válido.");
                JSONObject android = new JSONObject();
                android.put("device", Build.MANUFACTURER + " " + Build.MODEL);
                android.put("sdk", Build.VERSION.SDK_INT);
                long pss = readPss();
                android.put("process_pss_kib", pss >= 0 ? pss : JSONObject.NULL);
                android.put("pss_measurement", "Proceso Android completo, medido después del cálculo; fuera del tiempo del motor.");
                android.put("kind", selfTest ? "self_test" : "echo");
                report.put("android_observation", android);
            } catch (Exception | LinkageError failure) {
                report = failureReport("No se pudo completar el cálculo: " + failure, selfTest);
            }
            final JSONObject completed = report;
            main.post(() -> {
                if (destroyed) return;
                setBusy(false, selfTest);
                render(completed, selfTest);
                scrollToResult();
            });
        });
    }

    private void setBusy(boolean value, boolean selfTest) {
        busy = value;
        for (View control : controls) control.setEnabled(!value);
        runButton.setEnabled(!value && LIBRARY_ERROR == null);
        testButton.setEnabled(!value && LIBRARY_ERROR == null);
        cancelButton.setVisibility(value && !selfTest ? View.VISIBLE : View.GONE);
        cancelButton.setEnabled(value && !selfTest);
        progress.setVisibility(value ? View.VISIBLE : View.GONE);
        jobStatus.setTextColor(MUTED);
        jobStatus.setText(value ? (selfTest ? "Comprobando el motor y sus resultados…"
                : "Calculando eco, control y referencia solicitada…") : "Cálculo finalizado");
        updateQubits();
    }

    private void requestCancel() {
        if (!busy) return;
        try {
            nativeCancel(activeToken);
            cancelButton.setEnabled(false);
            jobStatus.setText("Cancelación solicitada. Esperando al motor…");
        } catch (LinkageError failure) {
            jobStatus.setText("No se pudo solicitar la cancelación: " + failure.getMessage());
        }
    }

    private void render(JSONObject report, boolean selfTest) {
        lastWasTest = selfTest;
        try { lastReport = report.toString(2); }
        catch (Exception impossible) { lastReport = report.toString(); }
        String status = report.optString("status", "ERROR");
        String validation = report.optString("validation", "UNVERIFIED");
        boolean passed = "PASS".equals(status);
        if (!selfTest && passed && (!Double.isFinite(report.optDouble("signal_z", Double.NaN))
                || !Double.isFinite(report.optDouble("norm", Double.NaN))
                || !Double.isFinite(report.optDouble("control_z", Double.NaN))
                || !Double.isFinite(report.optDouble("control_norm", Double.NaN)))) passed = false;
        String title;
        int color;
        String message = report.optString("message", "");
        if (selfTest) {
            int total = report.optInt("total", 0), count = report.optInt("passed", 0);
            boolean success = passed && total > 0 && count == total;
            title = success ? "PRUEBAS CORRECTAS" : "PRUEBAS FALLIDAS";
            color = success ? ACCENT : ERROR;
            message = count + " de " + total + " pruebas correctas."
                    + (message.isEmpty() ? "" : "\n" + message);
        } else if ("FAILED".equals(validation) || "MISMATCH".equals(status)) {
            title = "DIFERENCIA DETECTADA";
            color = ERROR;
        } else if (passed && "VERIFIED".equals(validation)
                && report.optBoolean("reference_performed", false)
                && report.optBoolean("reference_ok", false)) {
            title = "VERIFICADO";
            color = ACCENT;
            message = "Eco y control coinciden con la referencia densa."
                    + (message.isEmpty() ? "" : "\n" + message);
        } else if (passed && !"VERIFIED".equals(validation)) {
            title = "COMPLETADO · Sin verificar";
            color = BLUE;
            message = "Sin verificación densa independiente para esta ejecución."
                    + (message.isEmpty() ? "" : "\n" + message);
        } else if ("LIMIT".equals(status)) {
            title = "CÁLCULO DETENIDO · Límite";
            color = WARNING;
            message = "El cálculo se detuvo. No hay un resultado completo."
                    + (message.isEmpty() ? "" : "\n" + message);
            if (!report.isNull("peak_components")) message += "\nMáximo registrado: "
                    + integer(report, "peak_components") + " componentes.";
        } else if ("CANCELLED".equals(status)) {
            title = "CÁLCULO CANCELADO";
            color = WARNING;
        } else {
            title = "ERROR · Revisa el informe";
            color = ERROR;
            if (message.isEmpty()) message = "El informe no acredita un resultado válido.";
        }
        resultTitle.setText(title);
        resultTitle.setTextColor(color);
        resultMessage.setText(message.isEmpty() ? "Informe recibido del motor." : message);
        data.setVisibility(!selfTest && passed ? View.VISIBLE : View.GONE);
        signal.setText(fixed(report, "signal_z", 6));
        signalBar.setSignal(report.isNull("signal_z") ? Double.NaN : report.optDouble("signal_z", Double.NaN));
        controlSignal.setText("⟨Z⟩ = " + fixed(report, "control_z", 6));
        normalization.setText("Eco: " + fixed(report, "norm", 9)
                + "\nControl: " + fixed(report, "control_norm", 9));
        timing.setText("Motor · eco: " + microseconds(report, "elapsed_us")
                + "\nReferencia densa · eco: " + microseconds(report, "dense_us")
                + "\nMotor · control: " + microseconds(report, "control_elapsed_us")
                + "\nReferencia densa · control: " + microseconds(report, "dense_control_us")
                + "\nIncluyen inicialización, evolución y lectura; excluyen generar el circuito y la interfaz.");
        components.setText("Máximo alcanzado: " + integer(report, "peak_components")
                + "\nAl terminar: " + integer(report, "final_components"));
        JSONObject android = report.optJSONObject("android_observation");
        String pss = android == null ? "—" : integer(android, "process_pss_kib");
        memory.setText("Estado del motor · memoria lógica: " + bytes(report, "logical_bytes")
                + "\nApp completa · PSS posterior: " + pss + ("—".equals(pss) ? "" : " KiB")
                + "\nSon medidas distintas: memoria lógica no equivale a RAM total ni al pico del proceso.");
        error.setText(report.isNull("max_error") ? "Sin comparación densa para este resultado."
                : "Error máximo: " + scientific(report, "max_error"));
        copyButton.setEnabled(true);
        shareButton.setEnabled(true);
        resultCard.setVisibility(View.VISIBLE);
        jobStatus.setText(selfTest ? "Comprobación finalizada" : "Informe listo");
    }

    private void renderFailure(String message, boolean selfTest) { render(failureReport(message, selfTest), selfTest); }

    private static JSONObject failureReport(String message, boolean selfTest) {
        JSONObject report = new JSONObject();
        try {
            report.put("status", "ERROR");
            report.put("validation", "UNVERIFIED");
            report.put("message", message);
            report.put("origin", "android_ui");
            report.put("kind", selfTest ? "self_test" : "echo");
        } catch (Exception ignored) { /* All inserted values above are finite strings. */ }
        return report;
    }

    private void copyReport() {
        if (lastReport == null) return;
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard == null) { Toast.makeText(this, "Portapapeles no disponible", Toast.LENGTH_SHORT).show(); return; }
        clipboard.setPrimaryClip(ClipData.newPlainText("Mundo X Ecos · Informe JSON", lastReport));
        Toast.makeText(this, "Informe copiado", Toast.LENGTH_SHORT).show();
    }

    private void shareReport() {
        if (lastReport == null) return;
        Intent intent = new Intent(Intent.ACTION_SEND);
        intent.setType("text/plain");
        intent.putExtra(Intent.EXTRA_SUBJECT, "Mundo X Ecos · Informe JSON");
        intent.putExtra(Intent.EXTRA_TEXT, lastReport);
        try { startActivity(Intent.createChooser(intent, "Compartir informe")); }
        catch (RuntimeException unavailable) { Toast.makeText(this, "No hay una aplicación para compartir el informe", Toast.LENGTH_LONG).show(); }
    }

    private void scrollToResult() {
        scroll.post(() -> { if (!destroyed) scroll.smoothScrollTo(0, Math.max(0, resultCard.getTop() - dp(12))); });
    }

    private static long readPss() {
        try { Debug.MemoryInfo info = new Debug.MemoryInfo(); Debug.getMemoryInfo(info); return info.getTotalPss(); }
        catch (RuntimeException unavailable) { return -1; }
    }

    private static int readIntOrZero(EditText field) {
        try { return Integer.parseInt(field.getText().toString().trim()); }
        catch (NumberFormatException invalid) { return 0; }
    }

    private int bounded(EditText field, String label, int min, int max) {
        String value = field.getText().toString().trim();
        int number;
        try { number = Integer.parseInt(value); }
        catch (NumberFormatException invalid) { number = -1; }
        if (number < min || number > max) {
            field.setError("De " + min + " a " + max);
            field.requestFocus();
            throw new IllegalArgumentException(label + ": usa un entero entre " + min + " y " + max + ".");
        }
        field.setError(null);
        return number;
    }

    private static String fixed(JSONObject json, String key, int decimals) {
        double value = json.optDouble(key, Double.NaN);
        return Double.isFinite(value) ? String.format(Locale.US, "%." + decimals + "f", value) : "—";
    }

    private static String scientific(JSONObject json, String key) {
        double value = json.optDouble(key, Double.NaN);
        return Double.isFinite(value) ? String.format(Locale.US, "%.3e", value) : "—";
    }

    private static String integer(JSONObject json, String key) {
        return json.isNull(key) ? "—" : String.format(Locale.US, "%,d", json.optLong(key));
    }

    private static String microseconds(JSONObject json, String key) {
        double value = json.optDouble(key, Double.NaN);
        return Double.isFinite(value) ? String.format(Locale.US, "%.3f ms", value / 1000.0) : "—";
    }

    private static String bytes(JSONObject json, String key) {
        double value = json.optDouble(key, Double.NaN);
        if (!Double.isFinite(value)) return "—";
        return value >= 1048576 ? String.format(Locale.US, "%.2f MiB", value / 1048576.0)
                : String.format(Locale.US, "%.2f KiB", value / 1024.0);
    }

    private void applyInsets() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            scroll.setOnApplyWindowInsetsListener((view, insets) -> {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                android.graphics.Insets ime = insets.getInsets(WindowInsets.Type.ime());
                view.setPadding(bars.left, bars.top, bars.right, Math.max(bars.bottom, ime.bottom));
                return insets;
            });
            scroll.requestApplyInsets();
        }
    }

    private EditText number(String description, String initial, boolean signed) {
        EditText field = new EditText(this);
        field.setId(View.generateViewId());
        field.setSaveEnabled(false);
        field.setContentDescription(description);
        field.setInputType(InputType.TYPE_CLASS_NUMBER);
        field.setSingleLine(true);
        field.setTextColor(TEXT);
        field.setTextSize(17);
        field.setTypeface(Typeface.MONOSPACE);
        field.setText(initial);
        field.setFilters(new InputFilter[]{new InputFilter.LengthFilter(signed ? 20 : 3)});
        field.setPadding(dp(12), dp(10), dp(12), dp(10));
        field.setMinHeight(dp(50));
        field.setBackground(shape(FIELD, BORDER, 10));
        controls.add(field);
        inputs.add(field);
        return field;
    }

    private Spinner choices(String description, String[] values) {
        Spinner spinner = new Spinner(this);
        spinner.setContentDescription(description);
        spinner.setMinimumHeight(dp(50));
        spinner.setBackground(shape(FIELD, BORDER, 10));
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item, values) {
            @Override public View getView(int position, View reusable, ViewGroup parent) {
                TextView view = (TextView) super.getView(position, reusable, parent);
                view.setTextColor(TEXT); view.setTextSize(16); view.setPadding(dp(12), dp(12), dp(12), dp(12));
                return view;
            }
            @Override public View getDropDownView(int position, View reusable, ViewGroup parent) {
                TextView view = (TextView) super.getDropDownView(position, reusable, parent);
                view.setTextColor(TEXT); view.setBackgroundColor(SURFACE);
                view.setTextSize(16); view.setPadding(dp(16), dp(16), dp(16), dp(16));
                return view;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        controls.add(spinner);
        return spinner;
    }

    private LinearLayout labeled(String title, View input) {
        LinearLayout cell = column();
        TextView label = text(title, 12, MUTED, true);
        if (input.getId() != View.NO_ID) label.setLabelFor(input.getId());
        cell.addView(label, space(0, 6));
        cell.addView(input, space(0, 0));
        return cell;
    }

    private void addPair(LinearLayout parent, View left, View right) {
        LinearLayout row = new LinearLayout(this);
        row.setBaselineAligned(false);
        LinearLayout.LayoutParams a = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
        a.rightMargin = dp(6);
        LinearLayout.LayoutParams b = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
        b.leftMargin = dp(6);
        row.addView(left, a); row.addView(right, b);
        parent.addView(row, space(0, 12));
    }

    private LinearLayout column() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    private LinearLayout card() {
        LinearLayout layout = column();
        layout.setPadding(dp(16), dp(18), dp(16), dp(18));
        layout.setBackground(shape(SURFACE, BORDER, 18));
        return layout;
    }

    private TextView text(String value, int size, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setLineSpacing(dp(2), 1);
        if (bold) view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return view;
    }

    private Button button(String title, boolean primary) {
        Button view = new Button(this);
        view.setText(title); view.setAllCaps(false); view.setTextSize(15);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setTextColor(primary ? BG : TEXT);
        view.setMinHeight(dp(50)); view.setMinimumHeight(dp(50));
        view.setPadding(dp(12), dp(10), dp(12), dp(10));
        view.setBackground(new RippleDrawable(ColorStateList.valueOf(0x336FE8CF),
                shape(primary ? ACCENT : SURFACE, primary ? ACCENT : BORDER, 12), null));
        view.setStateListAnimator(null);
        return view;
    }

    private GradientDrawable shape(int fill, int stroke, int radius) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill); drawable.setCornerRadius(dp(radius)); drawable.setStroke(dp(1), stroke);
        return drawable;
    }

    private LinearLayout.LayoutParams space(int top, int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(top); params.bottomMargin = dp(bottom);
        return params;
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }

    @Override protected void onSaveInstanceState(Bundle state) {
        for (int i = 0; i < inputs.size(); i++) state.putString("input_" + i, inputs.get(i).getText().toString());
        state.putInt("rounds", rounds.getSelectedItemPosition());
        state.putInt("budget", budget.getSelectedItemPosition());
        state.putBoolean("compare", compare.isChecked());
        state.putString("report", lastReport);
        state.putBoolean("self_test", lastWasTest);
        state.putBoolean("interrupted", busy);
        super.onSaveInstanceState(state);
    }

    private void restore(Bundle state) {
        for (int i = 0; i < inputs.size(); i++) {
            if (state.containsKey("input_" + i)) inputs.get(i).setText(state.getString("input_" + i));
        }
        rounds.setSelection(Math.max(0, Math.min(1, state.getInt("rounds", 0))));
        budget.setSelection(Math.max(0, Math.min(BUDGETS.length - 1, state.getInt("budget", 0))));
        compare.setChecked(state.getBoolean("compare", true));
        String report = state.getString("report");
        if (report != null) {
            try { render(new JSONObject(report), state.getBoolean("self_test", false)); }
            catch (Exception invalid) { jobStatus.setText("Configuración restaurada. Ejecuta un nuevo eco."); }
        }
        if (state.getBoolean("interrupted", false)) jobStatus.setText("La vista cambió durante el cálculo. Ejecuta de nuevo para obtener un informe completo.");
    }

    @Override protected void onDestroy() {
        destroyed = true;
        if (busy && LIBRARY_ERROR == null) {
            try { nativeCancel(activeToken); } catch (LinkageError ignored) { /* Activity is leaving; no UI callback remains. */ }
        }
        executor.shutdownNow();
        main.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    private static final class Config {
        int rows, cols, depth, perturb, probe, rounds, budget;
        long seed;
        boolean compare;
    }

    private final class SignalBar extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private double value = Double.NaN;
        SignalBar(Context context) { super(context); }
        void setSignal(double number) { value = number; invalidate(); }
        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float left = dp(10), right = getWidth() - dp(10), y = dp(9);
            paint.setStrokeWidth(dp(4)); paint.setStrokeCap(Paint.Cap.ROUND);
            paint.setColor(BORDER); canvas.drawLine(left, y, right, y, paint);
            paint.setStrokeWidth(dp(1)); paint.setColor(MUTED);
            canvas.drawLine((left + right) / 2, y - dp(5), (left + right) / 2, y + dp(5), paint);
            if (Double.isFinite(value)) {
                float x = left + (float) ((Math.max(-1, Math.min(1, value)) + 1) / 2) * (right - left);
                paint.setColor(ACCENT); canvas.drawCircle(x, y, dp(6), paint);
            }
            paint.setColor(MUTED); paint.setTextSize(dp(11));
            paint.setTextAlign(Paint.Align.LEFT); canvas.drawText("−1", left, dp(32), paint);
            paint.setTextAlign(Paint.Align.CENTER); canvas.drawText("0", (left + right) / 2, dp(32), paint);
            paint.setTextAlign(Paint.Align.RIGHT); canvas.drawText("+1", right, dp(32), paint);
        }
    }
}
