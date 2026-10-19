package com.cosmicx.pc;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ComponentCallbacks2;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.provider.OpenableColumns;
import android.system.Os;
import android.system.OsConstants;
import android.text.InputFilter;
import android.text.InputType;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import com.mundox.graphics.NativeCore;
import com.mundox.graphics.QuantumRenderer;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.ByteBuffer;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import com.mundox.graphics.RenderResolution;

/** A local, native Android frontend. It never substitutes a simulated desktop for guest output. */
public final class MainActivity extends Activity {
    private static final int OPEN_IMAGE = 410;
    private static final int SAVE_QUANTUM = 411;
    private static final int OPEN_QUANTUM = 412;
    private static final int MAX_QUANTUM_SNAPSHOT_BYTES = 16 * 1024 * 1024;
    private static final int LOGICAL_RAM_MIB = 2048;
    private static final int RESIDENT_RAM_BUDGET_MIB = 1024;
    private static final int SLICE_BUDGET = 5000;
    private static final long FRAME_INTERVAL_MS = 50;
    private static final long MEMORY_SAMPLE_INTERVAL_MS = 1000;
    private static final String PREFS = "cosmic_pc_images";
    private static final String LAST_URI = "last_image_uri";
    private static final String LAST_NAME = "last_image_name";

    private static final int BACKGROUND = Color.rgb(7, 12, 10);
    private static final int PANEL = Color.rgb(15, 25, 20);
    private static final int BORDER = Color.rgb(35, 61, 46);
    private static final int GREEN = Color.rgb(135, 240, 164);
    private static final int WHITE = Color.rgb(237, 247, 239);
    private static final int MUTED = Color.rgb(154, 177, 163);
    private static final int AMBER = Color.rgb(240, 200, 131);

    private final Handler main = new Handler(Looper.getMainLooper());
    private final AtomicBoolean runRequested = new AtomicBoolean(false);
    private final AtomicBoolean foreground = new AtomicBoolean(false);
    private final AtomicBoolean destroyed = new AtomicBoolean(false);
    private final AtomicBoolean sliceQueued = new AtomicBoolean(false);
    private final AtomicInteger runGeneration = new AtomicInteger(0);
    private final ExecutorService machineThread = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "COSMIC-X-CPU");
        thread.setPriority(Thread.NORM_PRIORITY - 1);
        return thread;
    });

    // CPU mutations remain on its executor. GL reads immutable JNI snapshots under the native lock.
    private volatile long machine;
    private long lastFrameAt;
    private long lastPssSampleAt = -1;
    private long processPssBytes = -1;
    private final Debug.MemoryInfo processMemoryInfo = new Debug.MemoryInfo();

    // While paused, refresh the process measurement without executing guest CPU steps.
    private final Runnable idleMemoryRefresh = new Runnable() {
        @Override
        public void run() {
            if (!foreground.get() || destroyed.get()) return;
            if (!busy && !runRequested.get()) inspectInWorker();
            main.postDelayed(this, MEMORY_SAMPLE_INTERVAL_MS);
        }
    };

    // View state is confined to the UI thread.
    private boolean busy;
    private boolean machineAvailable;
    private boolean memoryPressurePaused;
    private String frontendMessage = "";
    private String nativeStatus = "empty";
    private String bootKind = "none";
    private String lastDiagnostic = "COSMIC X PC\nInicializando el motor propio…";
    private TextView statusView;
    private TextView metricsView;
    private TextView cpuModeView;
    private TextView memoryView;
    private TextView messageView;
    private TextView imageView;
    private PcVideoView videoView;
    private TextView videoInfoView;
    private TextView emptyVideoView;
    private EditText keyboard;
    private Button demoButton;
    private Button longModeDemoButton;
    private Button graphicsDemoButton;
    private Button openButton;
    private Button reopenButton;
    private Button runButton;
    private Button pauseButton;
    private Button stepButton;
    private Button resetButton;
    private Button sendButton;
    private Button escapeButton;
    private Button backspaceButton;
    private final AtomicReference<NativeCore> graphicsOwner = new AtomicReference<>();
    private GLSurfaceView quantumSurface;
    private QuantumRenderer quantumRenderer;
    private FrameLayout quantumViewport;
    private TextView quantumStatsView, quantumResultView, quantumGraphicsStatus;
    private EditText quantumProgram, quantumFirst, quantumSecond;
    private Button quantumRunButton, quantumZeroButton, quantumBellButton, quantumProbeButton;
    private Button quantumSaveButton, quantumOpenButton, quantumDemoButton, quantum64DemoButton;
    private Button quantumPhaseDemoButton;
    private Button quantumCouplingButton;
    private boolean quantumCoupled = true;
    private float quantumTouchX, quantumTouchY;
    private String renderedFirst = "0", renderedSecond = "1000000000";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(BACKGROUND);
        getWindow().setNavigationBarColor(BACKGROUND);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
        }
        buildInterface();
        busy = true;
        refreshControls();
        executeWorker(() -> {
            try {
                machine = NativeMachine.createWithBudget(LOGICAL_RAM_MIB, RESIDENT_RAM_BUDGET_MIB);
                if (machine == 0) {
                    throw new IllegalStateException("No native machine handle");
                }
                publish(NativeMachine.inspect(machine), true);
                prepareQuantumGraphics();
            } catch (Exception | LinkageError error) {
                fail("No se pudo iniciar el motor nativo", error, true);
            }
        });
    }

    private void buildInterface() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BACKGROUND);
        if (Build.VERSION.SDK_INT >= 30) {
            root.setOnApplyWindowInsetsListener((view, insets) -> {
                android.graphics.Insets bars = insets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                android.graphics.Insets ime = insets.getInsets(WindowInsets.Type.ime());
                view.setPadding(bars.left, bars.top, bars.right, Math.max(bars.bottom, ime.bottom));
                return insets;
            });
        } else {
            root.setFitsSystemWindows(true);
        }

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(16), dp(20), dp(16), dp(24));
        scroll.addView(content, new ScrollView.LayoutParams(-1, -2));
        root.addView(scroll, new LinearLayout.LayoutParams(-1, -1));
        setContentView(root);

        TextView eyebrow = text(getString(R.string.local_engine), 11, GREEN);
        eyebrow.setLetterSpacing(0.15f);
        eyebrow.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        content.addView(eyebrow);
        TextView title = text(getString(R.string.app_name), 28, WHITE);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        title.setPadding(0, dp(6), 0, dp(8));
        content.addView(title);
        TextView version = text("Versión " + packageVersion(), 12, GREEN);
        version.setContentDescription("pc-version");
        content.addView(version);
        TextView scope = text(getString(R.string.scope_notice), 14, MUTED);
        scope.setLineSpacing(dp(2), 1f);
        content.addView(scope);

        LinearLayout source = card(content, 20);
        source.addView(section(getString(R.string.boot_section)));
        imageView = text(getString(R.string.no_image), 13, MUTED);
        imageView.setPadding(0, dp(8), 0, dp(12));
        imageView.setMaxLines(3);
        source.addView(imageView);
        LinearLayout sourceRow = row(source);
        demoButton = button(getString(R.string.boot_demo), true);
        longModeDemoButton = button(getString(R.string.boot_x64_demo), false);
        pair(sourceRow, demoButton, longModeDemoButton, 56);
        demoButton.setOnClickListener(v -> loadDemo());
        longModeDemoButton.setOnClickListener(v -> loadLongModeDemo());

        graphicsDemoButton = button(getString(R.string.boot_graphics_demo), false);
        LinearLayout.LayoutParams graphicsParams = new LinearLayout.LayoutParams(-1, dp(48));
        graphicsParams.topMargin = dp(8);
        source.addView(graphicsDemoButton, graphicsParams);
        graphicsDemoButton.setOnClickListener(v -> new AlertDialog.Builder(this)
                .setTitle(R.string.graphics_demo_resolution)
                .setItems(new String[]{"1280 × 720", "3840 × 2160 (4K)", "7680 × 4320 (8K)"},
                        (dialog, choice) -> {
                            int[] widths = {1280, 3840, 7680};
                            int[] heights = {720, 2160, 4320};
                            loadGraphicsDemo(widths[choice], heights[choice]);
                        }).setNegativeButton(android.R.string.cancel, null).show());

        openButton = button(getString(R.string.open_image), false);
        LinearLayout.LayoutParams openParams = new LinearLayout.LayoutParams(-1, dp(48));
        openParams.topMargin = dp(8);
        source.addView(openButton, openParams);
        openButton.setOnClickListener(v -> chooseImage());

        reopenButton = button(getString(R.string.reopen_image), false);
        LinearLayout.LayoutParams reopenParams = new LinearLayout.LayoutParams(-1, dp(48));
        reopenParams.topMargin = dp(8);
        source.addView(reopenButton, reopenParams);
        reopenButton.setOnClickListener(v -> reopenImage());
        updateReopenButton();

        // Keep execution controls above the video on a phone, so starting a VM needs no scrolling.
        LinearLayout firstControls = row(content);
        ((LinearLayout.LayoutParams) firstControls.getLayoutParams()).topMargin = dp(12);
        runButton = button(getString(R.string.run), true);
        pauseButton = button(getString(R.string.pause), false);
        pair(firstControls, runButton, pauseButton, 50);
        LinearLayout secondControls = row(content);
        ((LinearLayout.LayoutParams) secondControls.getLayoutParams()).topMargin = dp(8);
        stepButton = button(getString(R.string.step), false);
        resetButton = button(getString(R.string.reset), false);
        pair(secondControls, stepButton, resetButton, 50);
        runButton.setOnClickListener(v -> startRun());
        pauseButton.setOnClickListener(v -> pauseMachine());
        stepButton.setOnClickListener(v -> operate(() -> NativeMachine.run(machine, 1)));
        resetButton.setOnClickListener(v -> operate(() -> NativeMachine.reset(machine)));

        LinearLayout state = card(content, 12);
        LinearLayout stateRow = row(state);
        statusView = text(getString(R.string.initializing), 14, GREEN);
        statusView.setContentDescription("pc-status");
        statusView.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        stateRow.addView(statusView, new LinearLayout.LayoutParams(0, -2, 1));
        cpuModeView = text("x86 / x64", 12, MUTED);
        cpuModeView.setTypeface(Typeface.MONOSPACE);
        stateRow.addView(cpuModeView);
        metricsView = text("CS:RIP —\n0 pasos CPU", 11, MUTED);
        metricsView.setTypeface(Typeface.MONOSPACE);
        metricsView.setPadding(0, dp(8), 0, 0);
        state.addView(metricsView);
        memoryView = text(getString(R.string.ram_virtual, "—") + "\n"
                + getString(R.string.ram_guest, "—", "—") + "\n"
                + getString(R.string.app_pss, "—"), 11, MUTED);
        memoryView.setContentDescription("pc-memory");
        memoryView.setPadding(0, dp(10), 0, 0);
        memoryView.setLineSpacing(dp(2), 1f);
        state.addView(memoryView);
        TextView memoryHint = text(getString(R.string.ram_budget_hint), 11, MUTED);
        memoryHint.setPadding(0, dp(6), 0, 0);
        state.addView(memoryHint);

        messageView = text(getString(R.string.start_hint), 13, MUTED);
        messageView.setContentDescription("pc-diagnostic");
        messageView.setLineSpacing(dp(2), 1f);
        messageView.setPadding(0, dp(12), 0, 0);
        content.addView(messageView);

        buildQuantumPanel(content);

        TextView screenTitle = section(getString(R.string.video_section));
        LinearLayout.LayoutParams screenTitleParams = new LinearLayout.LayoutParams(-1, -2);
        screenTitleParams.topMargin = dp(20);
        screenTitleParams.bottomMargin = dp(8);
        content.addView(screenTitle, screenTitleParams);
        videoInfoView = text(getString(R.string.video_waiting), 12, GREEN);
        videoInfoView.setContentDescription("pc-video-metrics");
        videoInfoView.setPadding(0, 0, 0, dp(8));
        content.addView(videoInfoView);
        FrameLayout videoPanel = new FrameLayout(this);
        videoPanel.setBackgroundColor(Color.BLACK);
        videoView = new PcVideoView(this);
        videoView.setContentDescription("pc-screen");
        videoView.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
        videoView.setStatusListener(this::updateVideoStatus);
        videoView.setFrameSource(new PcVideoRenderer.FrameSource() {
            @Override public PcVideoRenderer.FrameInfo acquireFrame() {
                long handle = machine;
                if (handle == 0 || destroyed.get()) return null;
                String raw = NativeMachine.acquireVideoFrame(handle);
                if (raw == null) return null;
                long revision = 0;
                try {
                    JSONObject info = new JSONObject(raw);
                    revision = info.getLong("revision");
                    return new PcVideoRenderer.FrameInfo(info.getInt("kind"), info.getInt("width"),
                            info.getInt("height"), revision);
                } catch (JSONException | RuntimeException error) {
                    if (revision != 0) NativeMachine.releaseVideoFrame(revision);
                    throw new IllegalStateException("Metadatos de vídeo inválidos", error);
                }
            }
            @Override public int copyText(long revision, ByteBuffer target) {
                return NativeMachine.copyVideoText(revision, target);
            }
            @Override public int copyRows(long revision, int first, int rows, ByteBuffer target) {
                return NativeMachine.copyVideoRows(revision, first, rows, target);
            }
            @Override public void releaseFrame(long revision) { NativeMachine.releaseVideoFrame(revision); }
        });
        videoPanel.addView(videoView, new FrameLayout.LayoutParams(-1, -1));
        emptyVideoView = text(getString(R.string.empty_video), 14, MUTED);
        emptyVideoView.setGravity(Gravity.CENTER);
        emptyVideoView.setPadding(dp(16), dp(8), dp(16), dp(8));
        emptyVideoView.setContentDescription("pc-video-empty");
        videoPanel.addView(emptyVideoView, new FrameLayout.LayoutParams(-1, -1));
        LinearLayout.LayoutParams videoParams = new LinearLayout.LayoutParams(-1, dp(250));
        content.addView(videoPanel, videoParams);
        videoPanel.addOnLayoutChangeListener((view, l, t, r, b, ol, ot, or, ob) -> {
            int width = r - l;
            int height = Math.max(dp(180), Math.round(width * 9f / 16f));
            if (width > 0 && view.getLayoutParams().height != height) {
                view.getLayoutParams().height = height;
                view.requestLayout();
            }
        });
        LinearLayout resolutionRow = row(content);
        Button adaptive = button(getString(R.string.video_adaptive), false);
        Button fourK = button("4K", false);
        Button eightK = button("8K", false);
        for (Button control : new Button[]{adaptive, fourK, eightK}) {
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, dp(48), 1);
            params.topMargin = dp(8);
            params.rightMargin = dp(4);
            resolutionRow.addView(control, params);
        }
        adaptive.setOnClickListener(v -> videoView.setOutputMode(RenderResolution.ADAPTIVE));
        fourK.setOnClickListener(v -> videoView.setOutputMode(RenderResolution.UHD_4K));
        eightK.setOnClickListener(v -> videoView.setOutputMode(RenderResolution.UHD_8K));
        TextView screenHint = text(getString(R.string.video_hint), 11, MUTED);
        screenHint.setPadding(0, dp(6), 0, dp(8));
        content.addView(screenHint);

        LinearLayout inputCard = card(content, 16);
        inputCard.addView(section(getString(R.string.keyboard_section)));
        LinearLayout inputRow = row(inputCard);
        ((LinearLayout.LayoutParams) inputRow.getLayoutParams()).topMargin = dp(10);
        keyboard = new EditText(this);
        keyboard.setSingleLine(true);
        keyboard.setTextSize(14);
        keyboard.setTextColor(WHITE);
        keyboard.setHintTextColor(MUTED);
        keyboard.setHint(R.string.keyboard_hint);
        keyboard.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        keyboard.setImeOptions(EditorInfo.IME_ACTION_SEND | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        keyboard.setFilters(new InputFilter[]{new InputFilter.LengthFilter(120)});
        keyboard.setBackground(rounded(BACKGROUND, BORDER, 8));
        keyboard.setBackgroundTintList(null);
        keyboard.setPadding(dp(10), 0, dp(10), 0);
        LinearLayout.LayoutParams inputParams = new LinearLayout.LayoutParams(0, dp(48), 1);
        inputParams.rightMargin = dp(8);
        inputRow.addView(keyboard, inputParams);
        sendButton = button(getString(R.string.send), true);
        inputRow.addView(sendButton, new LinearLayout.LayoutParams(dp(86), dp(48)));
        sendButton.setOnClickListener(v -> sendText());
        keyboard.setOnEditorActionListener((view, action, event) -> {
            if (action == EditorInfo.IME_ACTION_SEND || (event != null
                    && event.getKeyCode() == KeyEvent.KEYCODE_ENTER
                    && event.getAction() == KeyEvent.ACTION_DOWN)) {
                sendText();
                return true;
            }
            return false;
        });
        LinearLayout specialKeys = row(inputCard);
        ((LinearLayout.LayoutParams) specialKeys.getLayoutParams()).topMargin = dp(8);
        backspaceButton = button(getString(R.string.backspace), false);
        escapeButton = button(getString(R.string.escape), false);
        pair(specialKeys, backspaceButton, escapeButton, 48);
        backspaceButton.setOnClickListener(v -> sendKeys("\b"));
        escapeButton.setOnClickListener(v -> sendKeys("\u001b"));
        TextView keyHint = text(getString(R.string.keyboard_detail), 11, MUTED);
        keyHint.setPadding(0, dp(8), 0, 0);
        inputCard.addView(keyHint);

        Button copy = button(getString(R.string.copy_diagnostic), false);
        LinearLayout.LayoutParams copyParams = new LinearLayout.LayoutParams(-1, dp(50));
        copyParams.topMargin = dp(16);
        content.addView(copy, copyParams);
        copy.setOnClickListener(v -> copyDiagnostic());
    }

    private void buildQuantumPanel(LinearLayout content) {
        LinearLayout panel = card(content, 16);
        panel.addView(section("NÚCLEO CUÁNTICO · MOTOR GRÁFICO"));
        quantumStatsView = text("Preparando la sesión del PC…", 11, MUTED);
        quantumStatsView.setContentDescription("pc-quantum-stats");
        quantumStatsView.setPadding(0, dp(8), 0, dp(8));
        panel.addView(quantumStatsView);
        quantumViewport = new FrameLayout(this);
        quantumViewport.setBackgroundColor(Color.BLACK);
        panel.addView(quantumViewport, new LinearLayout.LayoutParams(-1, dp(230)));
        quantumGraphicsStatus = text("Preparando el mundo…", 10, MUTED);
        quantumGraphicsStatus.setContentDescription("pc-quantum-graphics-status");
        quantumGraphicsStatus.setMaxLines(3);
        quantumGraphicsStatus.setPadding(0, dp(6), 0, dp(6));
        panel.addView(quantumGraphicsStatus);
        LinearLayout visualControls = row(panel);
        Button camera = button("Cambiar vista", false);
        quantumCouplingButton = button("Acoplamiento: sí", false);
        pair(visualControls, camera, quantumCouplingButton, 48);
        camera.setOnClickListener(v -> renderAction(() -> quantumRenderer.toggleView()));
        quantumCouplingButton.setOnClickListener(v -> {
            quantumCoupled = !quantumCoupled;
            quantumCouplingButton.setText(quantumCoupled ? "Acoplamiento: sí" : "Acoplamiento: no");
            final boolean enabled = quantumCoupled;
            renderAction(() -> quantumRenderer.coupling(enabled));
        });
        panel.addView(text("El mundo usa observables de la misma sesión que el huésped. Arrastra para mover la cámara.", 11, MUTED));
        LinearLayout addresses = row(panel);
        quantumFirst = quantumInput("Dirección A", "0", false);
        quantumSecond = quantumInput("Dirección B", "1000000000", false);
        pair(addresses, quantumFirst, quantumSecond, 48);
        quantumProbeButton = button("Observar estas direcciones", false);
        panel.addView(quantumProbeButton, new LinearLayout.LayoutParams(-1, dp(48)));
        quantumProbeButton.setOnClickListener(v -> {
            final String first = quantumFirst.getText().toString();
            final String second = quantumSecond.getText().toString();
            operate(() -> NativeMachine.quantumProbe(machine, first, second));
        });
        quantumProgram = quantumInput("Programa .xi", "h 0\ncx 0 1000000000\nexpect X:0 X:1000000000\nexpect Y:0 Y:1000000000\nstats", true);
        quantumProgram.setContentDescription("pc-quantum-program");
        panel.addView(quantumProgram, new LinearLayout.LayoutParams(-1, dp(140)));
        quantumRunButton = button("Aplicar programa", true);
        panel.addView(quantumRunButton, new LinearLayout.LayoutParams(-1, dp(48)));
        quantumRunButton.setOnClickListener(v -> {
            final String program = quantumProgram.getText().toString();
            operate(() -> NativeMachine.quantumCommand(machine, program));
        });
        LinearLayout resets = row(panel);
        quantumZeroButton = button("Fondo cero", false);
        quantumBellButton = button("Fondo Bell", false);
        pair(resets, quantumZeroButton, quantumBellButton, 48);
        quantumZeroButton.setOnClickListener(v -> operate(() -> NativeMachine.quantumReset(machine, "zero")));
        quantumBellButton.setOnClickListener(v -> operate(() -> NativeMachine.quantumReset(machine, "bell")));
        quantumResultView = text("Los resultados del núcleo aparecerán aquí.", 11, WHITE);
        quantumResultView.setTypeface(Typeface.MONOSPACE);
        quantumResultView.setContentDescription("pc-quantum-result");
        quantumResultView.setTextIsSelectable(true);
        quantumResultView.setMaxLines(8);
        quantumResultView.setPadding(0, dp(8), 0, dp(8));
        panel.addView(quantumResultView);
        LinearLayout storage = row(panel);
        quantumSaveButton = button("Guardar núcleo", false);
        quantumOpenButton = button("Abrir núcleo", false);
        pair(storage, quantumSaveButton, quantumOpenButton, 48);
        quantumSaveButton.setOnClickListener(v -> chooseQuantumFile(true));
        quantumOpenButton.setOnClickListener(v -> chooseQuantumFile(false));
        LinearLayout demos = row(panel);
        quantumDemoButton = button("Prueba núcleo x86", false);
        quantum64DemoButton = button("Prueba núcleo x64", false);
        pair(demos, quantumDemoButton, quantum64DemoButton, 52);
        quantumDemoButton.setOnClickListener(v -> loadQuantumDemo(false));
        quantum64DemoButton.setOnClickListener(v -> loadQuantumDemo(true));
        quantumPhaseDemoButton = button("Prueba Z del huésped", false);
        panel.addView(quantumPhaseDemoButton, new LinearLayout.LayoutParams(-1, dp(48)));
        quantumPhaseDemoButton.setOnClickListener(v -> {
            imageView.setText("Prueba de fase Z desde x86 · pulsa Ejecutar");
            operate(() -> NativeMachine.bootQuantumPhaseDemo(machine));
        });
        panel.addView(text("Las pruebas cargan otro arranque. Reiniciar el PC restablece también el núcleo; guardar conserva su estado y secuencia aleatoria.", 11, MUTED));
    }

    private EditText quantumInput(String hint, String value, boolean multiline) {
        EditText input = new EditText(this);
        input.setTextColor(WHITE); input.setHintTextColor(MUTED);
        input.setHint(hint); input.setText(value); input.setTextSize(12);
        input.setTypeface(Typeface.MONOSPACE);
        input.setSingleLine(!multiline);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                | (multiline ? InputType.TYPE_TEXT_FLAG_MULTI_LINE : 0));
        // Preserve the complete pasted input. JNI rejects its original length;
        // truncating here could turn it into another address or a valid prefix.
        input.setGravity(Gravity.TOP | Gravity.START);
        input.setBackground(rounded(BACKGROUND, BORDER, 8)); input.setBackgroundTintList(null);
        input.setPadding(dp(8), dp(8), dp(8), dp(8));
        return input;
    }

    // Create an independently owned bridge handle, retaining the actual shared
    // PcMachine session. Closing either owner does not invalidate the other.
    private void prepareQuantumGraphics() {
        NativeCore prepared = null;
        try {
            NativeCore.ensureLoaded();
            prepared = NativeCore.adoptInfiniteHandle(NativeMachine.quantumGraphicsHandle(machine));
            if (destroyed.get()) { prepared.close(); return; }
            graphicsOwner.set(prepared);
            if (destroyed.get()) {
                NativeCore owner = graphicsOwner.getAndSet(null);
                if (owner != null) owner.close();
                return;
            }
            main.post(this::installQuantumGraphics);
        } catch (Exception | LinkageError error) {
            if (prepared != null) prepared.close();
            postUi(() -> quantumGraphicsStatus.setText("No se pudo abrir el visor: " + error.getMessage()));
        }
    }

    private void installQuantumGraphics() {
        NativeCore owner = graphicsOwner.get();
        if (destroyed.get() || owner == null) return;
        try {
            quantumRenderer = new QuantumRenderer(this, owner,
                    value -> postUi(() -> quantumGraphicsStatus.setText(value)));
            quantumRenderer.observeAddresses(renderedFirst, renderedSecond);
            quantumRenderer.resolution(360);
            quantumRenderer.objectCount(256);
            quantumRenderer.scenicView();
            quantumRenderer.coupling(quantumCoupled);
            quantumSurface = new GLSurfaceView(this);
            quantumSurface.setContentDescription("pc-quantum-viewport");
            quantumSurface.setEGLContextClientVersion(3);
            quantumSurface.setEGLConfigChooser(8, 8, 8, 8, 24, 0);
            quantumSurface.setPreserveEGLContextOnPause(true);
            quantumSurface.setRenderer(quantumRenderer);
            quantumSurface.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
            quantumSurface.setOnTouchListener((view, event) -> {
                if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                    quantumTouchX = event.getX(); quantumTouchY = event.getY();
                    view.getParent().requestDisallowInterceptTouchEvent(true);
                } else if (event.getActionMasked() == MotionEvent.ACTION_MOVE) {
                    final float dx = event.getX() - quantumTouchX, dy = event.getY() - quantumTouchY;
                    quantumTouchX = event.getX(); quantumTouchY = event.getY();
                    renderAction(() -> quantumRenderer.orbit(dx, dy));
                } else if (event.getActionMasked() == MotionEvent.ACTION_UP) {
                    view.getParent().requestDisallowInterceptTouchEvent(false); view.performClick();
                }
                return true;
            });
            quantumViewport.addView(quantumSurface, new FrameLayout.LayoutParams(-1, -1));
            if (!foreground.get()) quantumSurface.onPause();
            quantumSurface.requestRender();
        } catch (Exception | LinkageError error) {
            quantumGraphicsStatus.setText("No se pudo abrir el visor: " + error.getMessage());
            NativeCore current = graphicsOwner.getAndSet(null);
            if (current != null) current.close();
        }
    }

    private void renderAction(Runnable action) {
        if (quantumSurface == null || quantumRenderer == null || destroyed.get()) return;
        quantumSurface.queueEvent(() -> {
            if (destroyed.get()) return;
            try { action.run(); }
            catch (Exception error) { postUi(() -> quantumGraphicsStatus.setText("Visor: " + error.getMessage())); }
        });
        quantumSurface.requestRender();
    }

    private void loadQuantumDemo(boolean longMode) {
        imageView.setText(longMode ? "Prueba del núcleo desde x64 · pulsa Ejecutar" : "Prueba del núcleo desde x86 · pulsa Ejecutar");
        operate(() -> NativeMachine.bootQuantumDemo(machine, longMode));
    }

    private void chooseQuantumFile(boolean save) {
        if (busy || !machineAvailable || destroyed.get()) return;
        pauseMachine();
        try {
            Intent intent = new Intent(save ? Intent.ACTION_CREATE_DOCUMENT : Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(save ? "text/plain" : "*/*");
            if (save) intent.putExtra(Intent.EXTRA_TITLE, "COSMIC-PC-nucleo.mxi");
            startActivityForResult(intent, save ? SAVE_QUANTUM : OPEN_QUANTUM);
        } catch (RuntimeException error) {
            quantumResultView.setText("No se pudo abrir el selector: " + error.getMessage());
        }
    }

    private void quantumFile(Uri uri, boolean save) {
        if (busy || !machineAvailable || destroyed.get()) return;
        runGeneration.incrementAndGet(); runRequested.set(false); busy = true; refreshControls();
        executeWorker(() -> {
            try {
                if (save) {
                    String snapshot = NativeMachine.quantumSnapshot(machine);
                    byte[] bytes = snapshot.getBytes(StandardCharsets.US_ASCII);
                    if (bytes.length > MAX_QUANTUM_SNAPSHOT_BYTES) throw new IOException("Estado del núcleo demasiado grande");
                    try (OutputStream output = getContentResolver().openOutputStream(uri, "wt")) {
                        if (output == null) throw new IOException("No se pudo abrir el destino");
                        output.write(bytes); output.flush();
                    }
                    publish(NativeMachine.inspect(machine), true);
                    postUi(() -> quantumResultView.setText("Núcleo guardado: estado exacto y generador aleatorio."));
                } else {
                    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                    try (InputStream input = getContentResolver().openInputStream(uri)) {
                        if (input == null) throw new IOException("No se pudo abrir el archivo");
                        byte[] block = new byte[8192]; int count;
                        while ((count = input.read(block)) != -1) {
                            if (count > MAX_QUANTUM_SNAPSHOT_BYTES - bytes.size()) throw new IOException("Archivo del núcleo excede 16 MiB");
                            for (int i = 0; i < count; ++i) if (block[i] <= 0) throw new IOException("El estado debe ser texto ASCII sin NUL");
                            bytes.write(block, 0, count);
                        }
                    }
                    String snapshot = new String(bytes.toByteArray(), StandardCharsets.US_ASCII);
                    JSONObject state = new JSONObject(NativeMachine.quantumRestore(machine, snapshot));
                    publish(state, true);
                    if (state.getJSONObject("quantum").optString("error").isEmpty())
                        postUi(() -> quantumResultView.setText("Núcleo recuperado. La CPU y el medio del PC se conservan."));
                }
            } catch (Exception error) {
                postUi(() -> {
                    busy = false;
                    quantumResultView.setText("Archivo del núcleo: " + error.getMessage());
                    refreshControls();
                });
            }
        });
    }

    private void loadDemo() {
        imageView.setText(R.string.demo_selected);
        operate(() -> NativeMachine.bootDemo(machine));
    }

    private void loadLongModeDemo() {
        imageView.setText(R.string.x64_demo_selected);
        operate(() -> NativeMachine.bootLongModeDemo(machine));
    }

    private void loadGraphicsDemo(int width, int height) {
        imageView.setText(getString(R.string.graphics_demo_selected, width, height));
        operate(() -> NativeMachine.bootGraphicsDemo(machine, width, height));
    }

    private String packageVersion() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
        catch (android.content.pm.PackageManager.NameNotFoundException error) { return "—"; }
    }

    /** Loading, reset and stepping are serialized with CPU slices and never run on the UI thread. */
    private void operate(NativeOperation operation) {
        if (busy || !machineAvailable || destroyed.get()) return;
        memoryPressurePaused = false;
        frontendMessage = "";
        runGeneration.incrementAndGet();
        runRequested.set(false);
        busy = true;
        refreshControls();
        executeWorker(() -> {
            try {
                publish(operation.call(), true);
            } catch (Exception | LinkageError error) {
                fail("El motor no pudo completar la operación", error, true);
            }
        });
    }

    private void startRun() {
        if (busy || !machineAvailable || !canAdvanceCpu()
                || destroyed.get() || !foreground.get()) return;
        memoryPressurePaused = false;
        frontendMessage = "";
        runRequested.set(true);
        refreshControls();
        queueSlice();
    }

    private void queueSlice() {
        if (!runRequested.get() || !foreground.get() || destroyed.get()) return;
        if (!sliceQueued.compareAndSet(false, true)) return;
        boolean accepted = executeWorker(() -> {
            sliceQueued.set(false);
            if (!runRequested.get() || !foreground.get()) return;
            try {
                String json = NativeMachine.run(machine, SLICE_BUDGET);
                JSONObject state = new JSONObject(json);
                String status = state.optString("status", "fault");
                boolean canContinue = "running".equals(status) || "ready".equals(status);
                if (!canContinue) runRequested.set(false);
                long now = SystemClock.uptimeMillis();
                if (now - lastFrameAt >= FRAME_INTERVAL_MS || !runRequested.get()
                        || !foreground.get()) {
                    publish(state, false);
                    lastFrameAt = now;
                }
                if (canContinue) queueSlice();
            } catch (Exception | LinkageError error) {
                runRequested.set(false);
                fail("Se detuvo la ejecución del motor", error, false);
            }
        });
        if (!accepted) sliceQueued.set(false);
    }

    private void pauseMachine() {
        runGeneration.incrementAndGet();
        runRequested.set(false);
        refreshControls();
        inspectInWorker();
    }

    private void inspectInWorker() {
        executeWorker(() -> {
            if (machine == 0) return;
            try {
                publish(NativeMachine.inspect(machine), false);
            } catch (Exception | LinkageError error) {
                fail("No se pudo leer el estado del motor", error, false);
            }
        });
    }

    private void chooseImage() {
        if (busy || !machineAvailable) return;
        pauseMachine();
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, OPEN_IMAGE);
        } catch (RuntimeException error) {
            showMessage(getString(R.string.file_picker_unavailable));
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == SAVE_QUANTUM || requestCode == OPEN_QUANTUM) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null)
                quantumFile(data.getData(), requestCode == SAVE_QUANTUM);
            return;
        }
        if (requestCode != OPEN_IMAGE || resultCode != RESULT_OK || data == null) return;
        Uri uri = data.getData();
        if (uri == null || destroyed.get()) return;
        boolean persistent = false;
        int readGrant = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try {
            if (readGrant != 0) {
                getContentResolver().takePersistableUriPermission(uri, readGrant);
                persistent = true;
            }
        } catch (SecurityException ignored) {
            // A provider may offer only a session grant. The current selection still works.
        }
        mountUri(uri, persistent);
    }

    private void reopenImage() {
        String saved = preferences().getString(LAST_URI, null);
        if (saved != null && !busy) mountUri(Uri.parse(saved), true);
    }

    private void mountUri(Uri uri, boolean persistent) {
        if (busy || !machineAvailable || destroyed.get()) return;
        memoryPressurePaused = false;
        frontendMessage = "";
        runGeneration.incrementAndGet();
        runRequested.set(false);
        busy = true;
        imageView.setText(R.string.opening_image);
        refreshControls();
        executeWorker(() -> {
            try (ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(uri, "r")) {
                if (descriptor == null) throw new IOException("No descriptor");
                long size = descriptor.getStatSize();
                if (size < 0) throw new UnseekableImageException();
                try {
                    Os.lseek(descriptor.getFileDescriptor(), 0, OsConstants.SEEK_CUR);
                } catch (Exception error) {
                    throw new UnseekableImageException();
                }
                String name = displayName(uri);
                // Native mount duplicates this descriptor synchronously. No ISO contents are copied.
                JSONObject state = new JSONObject(NativeMachine.mount(machine,
                        descriptor.getFd(), size, "auto"));
                if ("fault".equals(state.optString("status"))) {
                    // Report the native loader failure without replacing the last working image.
                    postUi(() -> imageView.setText(R.string.image_not_opened));
                    publish(state, true);
                    return;
                }
                if (persistent) {
                    preferences().edit().putString(LAST_URI, uri.toString())
                            .putString(LAST_NAME, name).apply();
                }
                final String description = name + " · " + sizeLabel(size)
                        + "\n" + getString(R.string.read_only_image);
                postUi(() -> {
                    imageView.setText(description);
                    updateReopenButton();
                });
                publish(state, true);
            } catch (UnseekableImageException error) {
                fileFailure(getString(R.string.seekable_file_required));
            } catch (SecurityException error) {
                fileFailure(getString(R.string.image_permission_lost));
            } catch (Exception | LinkageError error) {
                // Provider error messages may contain private external paths or URIs.
                fileFailure(getString(R.string.image_open_failed)
                        + " (" + error.getClass().getSimpleName() + ")");
            }
        });
    }

    private String displayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                String name = cursor.getString(0).replace('\\', '/');
                int slash = name.lastIndexOf('/');
                if (slash >= 0) name = name.substring(slash + 1);
                name = name.replaceAll("[\\p{Cntrl}]", " ").trim();
                if (!name.isEmpty()) return name.substring(0, Math.min(100, name.length()));
            }
        } catch (RuntimeException ignored) {
            // A display name is optional; the content URI is never exposed as a substitute.
        }
        return getString(R.string.local_image);
    }

    private void fileFailure(String message) {
        runRequested.set(false);
        postUi(() -> {
            busy = false;
            frontendMessage = message;
            imageView.setText(R.string.image_not_opened);
            messageView.setText(message);
            messageView.setTextColor(AMBER);
            lastDiagnostic = "COSMIC X PC\nError al abrir imagen: " + message + "\n\n" + lastDiagnostic;
            refreshControls();
        });
    }

    private void sendText() {
        if (busy || !machineAvailable) return;
        String text = keyboard.getText().toString();
        for (int i = 0; i < text.length(); i++) {
            if (text.charAt(i) > 127) {
                showMessage(getString(R.string.ascii_only));
                return;
            }
        }
        keyboard.setText("");
        sendKeys(text + "\r");
    }

    private void sendKeys(String text) {
        if (busy || !machineAvailable || destroyed.get()) return;
        final boolean inputFromForeground = foreground.get();
        final int generation = runGeneration.get();
        executeWorker(() -> {
            try {
                // The CPU may have entered a keyboard wait before its last UI snapshot arrives.
                // Inspect and send on this executor so a real wait is resumed consistently.
                JSONObject beforeInput = new JSONObject(NativeMachine.inspect(machine));
                final boolean resumeWaiting = inputFromForeground
                        && "waiting_key".equals(beforeInput.optString("status"));
                for (int i = 0; i < text.length(); i++) {
                    int ascii = text.charAt(i);
                    NativeMachine.key(machine, ascii, scanCode(ascii));
                }
                if (resumeWaiting && foreground.get() && !destroyed.get()
                        && generation == runGeneration.get()) {
                    runRequested.set(true);
                    postUi(() -> {
                        if (foreground.get() && generation == runGeneration.get()) {
                            memoryPressurePaused = false;
                            frontendMessage = "";
                        }
                    });
                }
                publish(NativeMachine.inspect(machine), false);
                queueSlice();
            } catch (Exception | LinkageError error) {
                fail("No se pudo enviar el teclado al motor", error, false);
            }
        });
    }

    private static int scanCode(int ascii) {
        switch (ascii) {
            case 27: return 0x01;
            case '1': case '!': return 0x02;
            case '2': case '@': return 0x03;
            case '3': case '#': return 0x04;
            case '4': case '$': return 0x05;
            case '5': case '%': return 0x06;
            case '6': case '^': return 0x07;
            case '7': case '&': return 0x08;
            case '8': case '*': return 0x09;
            case '9': case '(': return 0x0a;
            case '0': case ')': return 0x0b;
            case '-': case '_': return 0x0c;
            case '=': case '+': return 0x0d;
            case 8: return 0x0e;
            case 9: return 0x0f;
            case 'q': case 'Q': return 0x10;
            case 'w': case 'W': return 0x11;
            case 'e': case 'E': return 0x12;
            case 'r': case 'R': return 0x13;
            case 't': case 'T': return 0x14;
            case 'y': case 'Y': return 0x15;
            case 'u': case 'U': return 0x16;
            case 'i': case 'I': return 0x17;
            case 'o': case 'O': return 0x18;
            case 'p': case 'P': return 0x19;
            case '[': case '{': return 0x1a;
            case ']': case '}': return 0x1b;
            case 10: case 13: return 0x1c;
            case 'a': case 'A': return 0x1e;
            case 's': case 'S': return 0x1f;
            case 'd': case 'D': return 0x20;
            case 'f': case 'F': return 0x21;
            case 'g': case 'G': return 0x22;
            case 'h': case 'H': return 0x23;
            case 'j': case 'J': return 0x24;
            case 'k': case 'K': return 0x25;
            case 'l': case 'L': return 0x26;
            case ';': case ':': return 0x27;
            case '\'': case '"': return 0x28;
            case '`': case '~': return 0x29;
            case '\\': case '|': return 0x2b;
            case 'z': case 'Z': return 0x2c;
            case 'x': case 'X': return 0x2d;
            case 'c': case 'C': return 0x2e;
            case 'v': case 'V': return 0x2f;
            case 'b': case 'B': return 0x30;
            case 'n': case 'N': return 0x31;
            case 'm': case 'M': return 0x32;
            case ',': case '<': return 0x33;
            case '.': case '>': return 0x34;
            case '/': case '?': return 0x35;
            case ' ': return 0x39;
            default: return 0;
        }
    }

    private void publish(String json, boolean operationFinished) throws JSONException {
        publish(new JSONObject(json), operationFinished);
    }

    private void publish(JSONObject state, boolean operationFinished) throws JSONException {
        sampleProcessMemory(state);
        String screen = NativeMachine.screen(machine);
        postUi(() -> applySnapshot(state, screen, operationFinished));
    }

    /** Called only on the machine executor. PSS measures this Android process, not just guest RAM. */
    private void sampleProcessMemory(JSONObject state) throws JSONException {
        long now = SystemClock.uptimeMillis();
        if (lastPssSampleAt < 0 || now - lastPssSampleAt >= MEMORY_SAMPLE_INTERVAL_MS) {
            try {
                Debug.getMemoryInfo(processMemoryInfo);
                int pssKiB = processMemoryInfo.getTotalPss();
                processPssBytes = pssKiB < 0 ? -1 : (long) pssKiB * 1024L;
            } catch (RuntimeException ignored) {
                // Unavailable telemetry must never stop the CPU or masquerade as zero usage.
                processPssBytes = -1;
            }
            lastPssSampleAt = SystemClock.uptimeMillis();
        }
        state.put("process_pss_bytes", processPssBytes < 0 ? JSONObject.NULL : processPssBytes);
        state.put("process_pss_sample_age_ms",
                Math.max(0L, SystemClock.uptimeMillis() - lastPssSampleAt));
    }

    private void applySnapshot(JSONObject state, String screen, boolean operationFinished) {
        if (operationFinished) busy = false;
        machineAvailable = true;
        nativeStatus = state.optString("status", "fault");
        bootKind = state.optString("boot_kind", "none");
        // A queued display snapshot must not cancel a newer Run or keyboard-resume request.
        // Only the executor and explicit UI controls own runRequested transitions.
        cpuModeView.setText(state.optString("cpu_mode", "—"));
        metricsView.setText(String.format(Locale.US, "CS:RIP %04X:%s\n%,d pasos CPU",
                state.optInt("cs") & 0xffff, instructionPointer(state), state.optLong("instructions")));
        memoryView.setText(getString(R.string.ram_virtual,
                memorySizeLabel(state.optLong("ram_bytes", -1))) + "\n"
                + getString(R.string.ram_guest,
                memorySizeLabel(state.optLong("ram_allocated_bytes", -1)),
                memorySizeLabel(state.optLong("ram_budget_bytes", -1))) + "\n"
                + getString(R.string.app_pss,
                memorySizeLabel(state.optLong("process_pss_bytes", -1))));
        String message = state.optString("message", "").trim();
        String pending = state.optString("pending", "").trim();
        if (!pending.isEmpty() && !message.contains(pending)) {
            message += (message.isEmpty() ? "" : "\n") + "Pendiente: " + pending;
        }
        if (message.isEmpty()) message = getString(R.string.start_hint);
        // Periodic memory snapshots must not erase a loader/provider error before it can be read.
        if (!frontendMessage.isEmpty()) message = frontendMessage + "\n" + message;
        if (memoryPressurePaused) message = getString(R.string.memory_pressure_paused) + "\n" + message;
        messageView.setText(message);
        messageView.setTextColor("fault".equals(nativeStatus) || memoryPressurePaused
                || !frontendMessage.isEmpty() ? AMBER : MUTED);
        boolean graphics = "vbe32".equals(state.optString("video_kind"));
        boolean emptyText = !graphics && (screen == null || screen.trim().isEmpty());
        emptyVideoView.setVisibility(emptyText ? View.VISIBLE : View.GONE);
        if (emptyText) emptyVideoView.setText("fault".equals(nativeStatus)
                ? getString(R.string.empty_video_fault) : getString(R.string.empty_video));
        videoView.setGuestText(graphics ? "" : screen);
        videoView.refresh();
        updateVideoStatus(videoView.getStatus());
        applyQuantumSnapshot(state.optJSONObject("quantum"));
        lastDiagnostic = diagnostic(state, screen);
        refreshControls();
    }

    private void applyQuantumSnapshot(JSONObject quantum) {
        if (quantum == null) return;
        JSONArray values = quantum.optJSONArray("values");
        StringBuilder correlations = new StringBuilder();
        String[] labels = {"XX", "YY", "ZZ", "ZI", "IZ"};
        if (values != null) for (int i = 0; i < Math.min(labels.length, values.length()); ++i) {
            if (i != 0) correlations.append(" · ");
            correlations.append(labels[i]).append('=').append(String.format(Locale.US, "%.0f", values.optDouble(i)));
        }
        quantumStatsView.setText("Fondo " + quantum.optString("background")
                + " · " + quantum.optLong("active_qubits") + "/" + quantum.optLong("max_active_qubits") + " activos"
                + " · " + quantum.optLong("components") + " componentes\n"
                + "Componente mayor: " + quantum.optLong("largest_component") + "/" + quantum.optLong("max_component_qubits")
                + " · tableau " + memorySizeLabel(quantum.optLong("tableau_bytes"))
                + " · revisión " + quantum.optLong("revision") + " / época " + quantum.optLong("epoch")
                + "\n" + correlations);
        String error = quantum.optString("error");
        String output = quantum.optString("output").trim();
        if (quantum.optBoolean("output_truncated")) output += "\n[Vista de resultados limitada a 8192 bytes]";
        if ((quantum.optInt("port_status") & 2) != 0) output += "\nEl huésped recibió un error del dispositivo.";
        quantumResultView.setText(!error.isEmpty() ? "Error del núcleo: " + error
                : output.isEmpty() ? "Sesión lista. " + correlations : output);
        quantumResultView.setTextColor(error.isEmpty() ? WHITE : AMBER);
        final String first = quantum.optString("first", "0"), second = quantum.optString("second", "1000000000");
        if (!first.equals(renderedFirst) || !second.equals(renderedSecond)) {
            renderedFirst = first; renderedSecond = second;
            renderAction(() -> quantumRenderer.observeAddresses(first, second));
        }
        if (quantumSurface != null) quantumSurface.requestRender();
    }

    /** Called on the UI thread; reports allocated output dimensions, including any rejected mode. */
    private void updateVideoStatus(PcVideoRenderer.Status status) {
        if (destroyed.get() || videoInfoView == null) return;
        videoInfoView.setText(status.summary());
        videoInfoView.setTextColor(status.error.isEmpty() ? GREEN : AMBER);
    }

    private String diagnostic(JSONObject state, String screen) {
        StringBuilder result = new StringBuilder("COSMIC X PC ").append(packageVersion())
                .append("\nMotor x86/x64 propio\nImagen: ").append(imageView.getText())
                .append("\nVídeo: ").append(videoInfoView.getText()).append("\nAndroid ")
                .append(Build.VERSION.RELEASE)
                .append(" · SDK ").append(Build.VERSION.SDK_INT)
                .append("\nDispositivo: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append("\nEstado: ").append(state.optString("status"))
                .append("\nArranque: ").append(state.optString("boot_kind"))
                .append("\nModo CPU: ").append(state.optString("cpu_mode", "—"))
                .append("\nRAM virtual: ").append(memorySizeLabel(state.optLong("ram_bytes", -1)))
                .append("\nRAM usada por invitado: ")
                .append(memorySizeLabel(state.optLong("ram_allocated_bytes", -1)))
                .append(" / límite ").append(memorySizeLabel(state.optLong("ram_budget_bytes", -1)))
                .append("\nDirectorio de RAM: ")
                .append(memorySizeLabel(state.optLong("ram_metadata_bytes", -1)))
                .append(" · páginas asignadas: ").append(state.optLong("ram_allocated_pages", 0))
                .append("\nPSS de la app: ")
                .append(memorySizeLabel(state.optLong("process_pss_bytes", -1)))
                .append(" · antigüedad de muestra: ")
                .append(state.optLong("process_pss_sample_age_ms", 0)).append(" ms")
                .append("\nEl límite del invitado no es un límite de memoria total del proceso.")
                .append(frontendMessage.isEmpty() ? "" : "\nInterfaz: " + frontendMessage)
                .append(memoryPressurePaused ? "\nPausa por aviso de presión de memoria de Android." : "")
                .append("\nPasos CPU: ").append(state.optLong("instructions"))
                .append("\nCS:RIP ").append(String.format(Locale.US, "%04X:%s",
                        state.optInt("cs") & 0xffff, instructionPointer(state)))
                .append(" · FLAGS ").append(String.format(Locale.US, "%08X", state.optInt("flags")));
        JSONArray registers = state.optJSONArray("regs_hex");
        String[] names = {"RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
                "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"};
        if (registers != null) {
            for (int index = 0; index < Math.min(names.length, registers.length()); index++) {
                if (index % 2 == 0) result.append('\n');
                result.append(names[index]).append('=')
                        .append(registers.optString(index, "—").toUpperCase(Locale.US)).append(' ');
            }
        } else {
            // Compatibility with the original 32-bit snapshot format, preserving unsigned values.
            registers = state.optJSONArray("regs");
            String[] oldNames = {"EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"};
            if (registers != null) {
                for (int index = 0; index < Math.min(oldNames.length, registers.length()); index++) {
                    if (index % 4 == 0) result.append('\n');
                    result.append(oldNames[index]).append('=')
                            .append(String.format(Locale.US, "%08X", registers.optLong(index) & 0xffffffffL))
                            .append(' ');
                }
            }
        }
        try {
            result.append("\n\nEstado del motor y medición Android JSON:\n").append(state.toString(2));
        } catch (JSONException ignored) {
            result.append("\n\nEstado del motor y medición Android JSON:\n").append(state);
        }
        if ("text".equals(state.optString("video_kind"))) {
            result.append("\n\nTexto real del invitado 80x25:\n").append(screen == null ? "" : screen);
        }
        return result.toString();
    }

    private static String instructionPointer(JSONObject state) {
        String hex = state.optString("rip_hex", "");
        if (!hex.isEmpty()) return hex.toUpperCase(Locale.US);
        return String.format(Locale.US, "%016X", state.optLong("ip") & 0xffffffffL);
    }

    private void fail(String description, Throwable error, boolean operationFinished) {
        runRequested.set(false);
        postUi(() -> {
            if (operationFinished) busy = false;
            if (error instanceof LinkageError) machineAvailable = false;
            nativeStatus = "fault";
            String report = description + " (" + error.getClass().getSimpleName() + ").";
            frontendMessage = report;
            messageView.setText(report);
            messageView.setTextColor(AMBER);
            lastDiagnostic = "COSMIC X PC\n" + report + "\n\n" + lastDiagnostic;
            refreshControls();
        });
    }

    private void refreshControls() {
        boolean running = runRequested.get() && foreground.get();
        String label;
        if (busy) label = getString(R.string.working);
        else if ("fault".equals(nativeStatus)) label = getString(R.string.incomplete_instruction);
        else if (!hasBootImage()) label = getString(R.string.select_boot);
        else if (memoryPressurePaused) label = getString(R.string.memory_paused);
        else if (running) label = getString(R.string.running);
        else if ("waiting_key".equals(nativeStatus)) label = getString(R.string.waiting_key);
        else if ("halted".equals(nativeStatus)) label = getString(R.string.halted);
        else if ("running".equals(nativeStatus)) label = getString(R.string.paused);
        else label = getString(R.string.ready);
        statusView.setText(label);
        statusView.setTextColor("fault".equals(nativeStatus) ? AMBER : GREEN);
        boolean usable = machineAvailable && !busy;
        enabled(demoButton, usable);
        enabled(longModeDemoButton, usable);
        enabled(graphicsDemoButton, usable);
        enabled(openButton, usable);
        enabled(reopenButton, usable);
        enabled(runButton, usable && !running && canAdvanceCpu());
        enabled(pauseButton, machineAvailable && (running || busy));
        enabled(stepButton, usable && !running && canAdvanceCpu());
        enabled(resetButton, usable);
        enabled(sendButton, usable);
        enabled(backspaceButton, usable);
        enabled(escapeButton, usable);
        keyboard.setEnabled(usable);
        enabled(quantumRunButton, usable); enabled(quantumZeroButton, usable); enabled(quantumBellButton, usable);
        enabled(quantumProbeButton, usable); enabled(quantumSaveButton, usable); enabled(quantumOpenButton, usable);
        enabled(quantumDemoButton, usable); enabled(quantum64DemoButton, usable);
        enabled(quantumPhaseDemoButton, usable);
        quantumProgram.setEnabled(usable); quantumFirst.setEnabled(usable); quantumSecond.setEnabled(usable);
        if (running) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    private boolean hasBootImage() {
        return !"empty".equals(nativeStatus) && !bootKind.isEmpty() && !"none".equals(bootKind);
    }

    private boolean canAdvanceCpu() {
        return hasBootImage() && !"fault".equals(nativeStatus)
                && !"halted".equals(nativeStatus) && !"waiting_key".equals(nativeStatus);
    }

    private void updateReopenButton() {
        SharedPreferences prefs = preferences();
        boolean present = prefs.contains(LAST_URI);
        reopenButton.setVisibility(present ? View.VISIBLE : View.GONE);
        if (present) {
            reopenButton.setContentDescription(getString(R.string.reopen_image) + ": "
                    + prefs.getString(LAST_NAME, getString(R.string.local_image)));
        }
    }

    private void copyDiagnostic() {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        if (clipboard != null) {
            PcVideoRenderer.Status video = videoView.getStatus();
            String presentation = "\n\nPresentación actual:\n" + video.summary()
                    + "\nÁrea visible: " + video.screenWidth + "×" + video.screenHeight
                    + "\nGPU: " + video.renderer
                    + "\nLímite de textura: " + video.textureLimit
                    + "\nTexturas GL asignadas: " + video.textureBytes + " bytes"
                    + "\nCuadros presentados: " + video.frames + " · revisión: " + video.revision;
            clipboard.setPrimaryClip(ClipData.newPlainText("Diagnóstico COSMIC X PC", lastDiagnostic + presentation));
            if (Build.VERSION.SDK_INT < 33) showMessage(getString(R.string.diagnostic_copied));
        }
    }

    private boolean executeWorker(Runnable operation) {
        if (destroyed.get()) return false;
        try {
            machineThread.execute(() -> {
                if (!destroyed.get()) operation.run();
            });
            return true;
        } catch (RejectedExecutionException ignored) {
            return false;
        }
    }

    private void postUi(Runnable action) {
        if (destroyed.get()) return;
        main.post(() -> {
            if (!destroyed.get()) action.run();
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (videoView != null) videoView.onResume();
        foreground.set(true);
        if (quantumSurface != null) { quantumSurface.onResume(); quantumSurface.requestRender(); }
        // Native execution resumes only following an explicit user action.
        refreshControls();
        inspectInWorker();
        main.removeCallbacks(idleMemoryRefresh);
        main.postDelayed(idleMemoryRefresh, MEMORY_SAMPLE_INTERVAL_MS);
    }

    @Override
    protected void onPause() {
        runGeneration.incrementAndGet();
        foreground.set(false);
        runRequested.set(false);
        if (quantumSurface != null) quantumSurface.onPause();
        main.removeCallbacks(idleMemoryRefresh);
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        inspectInWorker();
        if (videoView != null) videoView.onPause();
        super.onPause();
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        // Android 14+ no longer sends RUNNING_CRITICAL. This is an extra response for older
        // devices, not a guarantee that Android will warn before terminating this process.
        if (level == ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL
                && foreground.get() && machineAvailable && !destroyed.get()) {
            memoryPressurePaused = true;
            pauseMachine();
            messageView.setText(R.string.memory_pressure_paused);
            messageView.setTextColor(AMBER);
        }
    }

    @Override
    protected void onDestroy() {
        runGeneration.incrementAndGet();
        foreground.set(false);
        runRequested.set(false);
        destroyed.set(true);
        main.removeCallbacksAndMessages(null);
        if (quantumSurface != null) {
            quantumSurface.onPause();
            // Detach joins the GL thread before its independently owned handle closes.
            quantumViewport.removeView(quantumSurface);
            quantumSurface = null;
        }
        NativeCore graphics = graphicsOwner.getAndSet(null);
        if (graphics != null) graphics.close();
        // Do not interrupt JNI. Destruction follows any in-flight native call on its own executor.
        try {
            machineThread.execute(() -> {
                if (machine != 0) {
                    try {
                        NativeMachine.destroy(machine);
                    } finally {
                        machine = 0;
                    }
                }
            });
        } catch (RejectedExecutionException ignored) {
            // Only possible after this Activity has already completed its shutdown.
        }
        machineThread.shutdown();
        super.onDestroy();
    }

    private SharedPreferences preferences() {
        return getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    private void showMessage(String message) {
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }

    private String sizeLabel(long bytes) {
        if (bytes >= 1024L * 1024 * 1024) return String.format(Locale.getDefault(), "%.2f GiB", bytes / 1073741824.0);
        return String.format(Locale.getDefault(), "%.1f MiB", bytes / 1048576.0);
    }

    private String memorySizeLabel(long bytes) {
        if (bytes < 0) return "—";
        if (bytes < 1024L * 1024) {
            return String.format(Locale.getDefault(), "%.1f KiB", bytes / 1024.0);
        }
        return String.format(Locale.getDefault(), "%.1f MiB", bytes / 1048576.0);
    }

    private TextView text(String value, float size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setFontFeatureSettings("kern");
        return view;
    }

    private TextView section(String value) {
        TextView view = text(value, 11, MUTED);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setLetterSpacing(0.10f);
        return view;
    }

    private LinearLayout card(LinearLayout parent, int marginTop) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(14), dp(14), dp(14), dp(14));
        card.setBackground(rounded(PANEL, BORDER, 14));
        LinearLayout.LayoutParams parameters = new LinearLayout.LayoutParams(-1, -2);
        parameters.topMargin = dp(marginTop);
        parent.addView(card, parameters);
        return card;
    }

    private LinearLayout row(LinearLayout parent) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        parent.addView(row, new LinearLayout.LayoutParams(-1, -2));
        return row;
    }

    private Button button(String label, boolean primary) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextSize(14);
        button.setTextColor(primary ? BACKGROUND : WHITE);
        button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setMinHeight(dp(48));
        button.setMinimumHeight(dp(48));
        button.setPadding(dp(8), 0, dp(8), 0);
        GradientDrawable shape = rounded(primary ? GREEN : PANEL, primary ? GREEN : BORDER, 10);
        button.setBackground(new RippleDrawable(ColorStateList.valueOf(
                primary ? 0x22070c0a : 0x3387f0a4), shape, null));
        button.setBackgroundTintList(null);
        return button;
    }

    private void pair(LinearLayout row, View first, View second, int height) {
        LinearLayout.LayoutParams firstParams = new LinearLayout.LayoutParams(0, dp(height), 1);
        firstParams.rightMargin = dp(8);
        row.addView(first, firstParams);
        row.addView(second, new LinearLayout.LayoutParams(0, dp(height), 1));
    }

    private void enabled(Button button, boolean value) {
        button.setEnabled(value);
        button.setAlpha(value ? 1f : 0.40f);
    }

    private GradientDrawable rounded(int fill, int stroke, int radius) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill);
        drawable.setCornerRadius(dp(radius));
        drawable.setStroke(dp(1), stroke);
        return drawable;
    }

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private interface NativeOperation {
        String call();
    }

    private static final class UnseekableImageException extends IOException { }

}
