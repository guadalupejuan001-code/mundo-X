package com.cosmicx.pc;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

/**
 * A usable quantum workstation, not an x86 test screen.
 *
 * Editor and terminal run on the same real QuantumRuntime session. Programs
 * are editable .xi files; .mqx files preserve program, InfiniteCore, Inverse
 * Ether Memory and the RNG state together. Android's document picker handles
 * persistent files without requiring storage permissions or cloud services.
 */
public final class QuantumDesktopActivity extends Activity {
    private static final int OPEN_PROGRAM = 530;
    private static final int SAVE_PROGRAM = 531;
    private static final int OPEN_SESSION = 532;
    private static final int SAVE_SESSION = 533;
    private static final int MAX_PROGRAM_BYTES = 1024 * 1024;
    private static final int MAX_SESSION_BYTES = 32 * 1024 * 1024;
    private static final int MAX_CONSOLE_CHARS = 100_000;
    private static final String SESSION_FORMAT = "MUNDOX-QUANTUM-WORKSTATION-1";

    private static final int BACKGROUND = Color.rgb(8, 13, 20);
    private static final int PANEL = Color.rgb(17, 27, 39);
    private static final int BORDER = Color.rgb(48, 70, 86);
    private static final int ACCENT = Color.rgb(107, 225, 201);
    private static final int TEXT = Color.rgb(233, 245, 250);
    private static final int MUTED = Color.rgb(159, 181, 196);

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "Mundo-X-Quantum-Workstation");
        thread.setPriority(Thread.NORM_PRIORITY - 1);
        return thread;
    });

    private volatile long session = 0;
    private volatile boolean closed = false;
    private boolean busy = false;
    private EditText editor;
    private EditText terminal;
    private TextView console;
    private TextView state;
    private Button runButton, sendButton, inspectButton, resetButton;
    private Button openProgramButton, saveProgramButton, newProgramButton;
    private Button openSessionButton, saveSessionButton, modeButton;
    private boolean xasmMode;
    private String xiDraft = "# Programa XI persistente\n# Memoria lógica sin medir qubits\nmem_store 0 88\nmem_read 0\nmem_stats\n";
    private String xasmDraft = "# XASM: circuito nuevo por ejecución\nbackend reduced\nqubits 1000\nh 0\nt 0\nshow\nbytes\n";
    private final StringBuilder transcript = new StringBuilder();

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().setStatusBarColor(BACKGROUND);
        getWindow().setNavigationBarColor(BACKGROUND);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        buildInterface();
        if (saved != null) {
            String program = saved.getString("program");
            xasmMode = saved.getBoolean("xasmMode", false);
            if (program != null) {
                if (xasmMode) xasmDraft = program;
                else xiDraft = program;
                editor.setText(program);
            }
            updateModeButtons();
        }
        work(() -> {
            session = QuantumWorkstation.create();
            if (session == 0) throw new IllegalStateException("No se pudo crear la sesión cuántica");
            log("Sesión local iniciada.\n" + QuantumWorkstation.status(session));
        });
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        // The saved program is intentionally bounded to avoid Binder's limit.
        String program = editor.getText().toString();
        if (program.length() <= 32_000) out.putString("program", program);
        out.putBoolean("xasmMode", xasmMode);
    }

    private void buildInterface() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BACKGROUND);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(20), dp(16), dp(32));
        scroll.addView(root);
        setContentView(scroll);

        TextView brand = label("MUNDO X  /  COSMIC X ENGINE", 11, ACCENT);
        brand.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        root.addView(brand);
        TextView heading = label("Quantum Workstation", 27, TEXT);
        heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        heading.setPadding(0, dp(7), 0, dp(5));
        root.addView(heading);
        root.addView(label("Ejecuta programas XI directamente en Ether/InfiniteCore e Inverse Ether Memory, sin arrancar un PC x86.", 12, MUTED));

        state = label("Iniciando núcleo…", 12, ACCENT);
        state.setContentDescription("quantum-workstation-status");
        state.setPadding(0, dp(16), 0, dp(6));
        root.addView(state);

        LinearLayout programCard = card(root);
        programCard.addView(heading("PROGRAMAS  ·  XI / XASM"));
        modeButton = control("Modo XI persistente · cambiar", false);
        programCard.addView(modeButton, spaced(44));
        modeButton.setOnClickListener(v -> changeMode());
        editor = new EditText(this);
        editor.setContentDescription("quantum-workstation-program");
        editor.setText(xiDraft);
        editor.setTextColor(TEXT);
        editor.setHintTextColor(MUTED);
        editor.setHint("Escribe un programa XI…");
        editor.setTextSize(14);
        editor.setTypeface(Typeface.MONOSPACE);
        editor.setGravity(Gravity.TOP | Gravity.START);
        editor.setSingleLine(false);
        editor.setMinLines(7);
        editor.setBackground(panelBackground(BACKGROUND));
        editor.setPadding(dp(12), dp(12), dp(12), dp(12));
        programCard.addView(editor, new LinearLayout.LayoutParams(-1, -2));

        runButton = control("Ejecutar programa", true);
        programCard.addView(runButton, spaced(50));
        runButton.setOnClickListener(v -> {
            final String source = editor.getText().toString();
            if (xasmMode) {
                work(() -> log("Programa XASM ejecutado (circuito independiente).\n"
                        + QuantumWorkstation.executeXasm(source)));
            } else {
                execute(source, "Programa XI");
            }
        });

        LinearLayout programFiles = row(programCard);
        openProgramButton = control("Abrir .xi", false);
        saveProgramButton = control("Guardar .xi", false);
        pair(programFiles, openProgramButton, saveProgramButton);
        openProgramButton.setOnClickListener(v -> openDocument(OPEN_PROGRAM, "*/*"));
        saveProgramButton.setOnClickListener(v -> createDocument(SAVE_PROGRAM, "text/plain",
                xasmMode ? "programa.x" : "programa.xi"));
        newProgramButton = control("Nuevo programa", false);
        programCard.addView(newProgramButton, spaced(44));
        newProgramButton.setOnClickListener(v -> new AlertDialog.Builder(this)
                .setTitle("Nuevo programa")
                .setMessage("¿Vaciar el editor? El estado del núcleo no se borrará.")
                .setPositiveButton("Vaciar editor", (d, w) -> editor.setText(""))
                .setNegativeButton("Cancelar", null).show());

        LinearLayout terminalCard = card(root);
        terminalCard.addView(heading("TERMINAL XI  ·  sesión persistente"));
        terminalCard.addView(label("La terminal siempre usa XI: el circuito XASM se ejecuta aparte y no modifica esta sesión.", 12, MUTED));
        terminal = new EditText(this);
        terminal.setContentDescription("quantum-workstation-terminal");
        terminal.setTextColor(TEXT);
        terminal.setHintTextColor(MUTED);
        terminal.setTextSize(14);
        terminal.setTypeface(Typeface.MONOSPACE);
        terminal.setHint("Ejemplo: mem_read 0");
        terminal.setMinLines(2);
        terminal.setSingleLine(false);
        terminal.setBackground(panelBackground(BACKGROUND));
        terminal.setPadding(dp(12), dp(12), dp(12), dp(12));
        terminalCard.addView(terminal, spaced(-2));
        sendButton = control("Enviar a Ether", true);
        terminalCard.addView(sendButton, spaced(48));
        sendButton.setOnClickListener(v -> {
            final String command = terminal.getText().toString();
            if (!command.trim().isEmpty()) execute(command, "Terminal");
        });

        LinearLayout consoleCard = card(root);
        consoleCard.addView(heading("SALIDA  ·  consola"));
        console = label("Los resultados aparecerán aquí.", 12, TEXT);
        console.setTypeface(Typeface.MONOSPACE);
        console.setContentDescription("quantum-workstation-output");
        console.setTextIsSelectable(true);
        console.setPadding(0, dp(8), 0, dp(8));
        consoleCard.addView(console);

        LinearLayout memoryCard = card(root);
        memoryCard.addView(heading("MÁQUINA Y ARCHIVOS"));
        memoryCard.addView(label("Guarda toda tu sesión (programa, núcleo y memoria) en un archivo .mqx. Podrás reabrirla tras cerrar la app.", 12, MUTED));
        LinearLayout sessionRow = row(memoryCard);
        saveSessionButton = control("Guardar sesión", false);
        openSessionButton = control("Abrir sesión", false);
        pair(sessionRow, saveSessionButton, openSessionButton);
        saveSessionButton.setOnClickListener(v -> createDocument(SAVE_SESSION, "application/json", "mundo-x.mqx"));
        openSessionButton.setOnClickListener(v -> openDocument(OPEN_SESSION, "*/*"));

        LinearLayout coreRow = row(memoryCard);
        inspectButton = control("Ver estado", false);
        resetButton = control("Reiniciar núcleo", false);
        pair(coreRow, inspectButton, resetButton);
        inspectButton.setOnClickListener(v -> work(() -> log("Estado de máquina:\n" + QuantumWorkstation.status(requireSession()))));
        resetButton.setOnClickListener(v -> new AlertDialog.Builder(this)
                .setTitle("Reiniciar núcleo")
                .setMessage("Esto borra el estado cuántico y la memoria lógica de la sesión actual. Puedes guardarla antes.")
                .setPositiveButton("Reiniciar", (dialog, choice) -> work(() -> {
                    QuantumWorkstation.reset(requireSession());
                    log("Núcleo reiniciado. El archivo del editor se conserva.");
                }))
                .setNegativeButton("Cancelar", null).show());

        Button pc = control("Abrir COSMIC X PC (x86/x64)", false);
        memoryCard.addView(pc, spaced(46));
        pc.setOnClickListener(v -> {
            startActivity(new Intent(this, MainActivity.class));
            finish();
        });
        root.addView(label("Motor cuántico de software local. Esta interfaz no representa una QPU física ni arranca Windows.", 11, MUTED));
    }

    private void changeMode() {
        if (xasmMode) xasmDraft = editor.getText().toString();
        else xiDraft = editor.getText().toString();
        xasmMode = !xasmMode;
        editor.setText(xasmMode ? xasmDraft : xiDraft);
        updateModeButtons();
    }

    private void updateModeButtons() {
        modeButton.setText(xasmMode ? "Modo XASM universal · cambiar" : "Modo XI persistente · cambiar");
        runButton.setText(xasmMode ? "Ejecutar circuito XASM" : "Ejecutar programa XI");
    }

    private String documentName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return cursor.getString(0);
        } catch (RuntimeException ignored) {
            // A provider may omit display-name metadata; manual mode still works.
        }
        return "";
    }

    private void execute(String source, String origin) {
        work(() -> {
            String result = QuantumWorkstation.execute(requireSession(), source);
            log(origin + " ejecutado.\n" + (result.isEmpty() ? "(Sin salida)" : result));
        });
    }

    private long requireSession() {
        long id = session;
        if (id == 0) throw new IllegalStateException("La sesión aún no está disponible");
        return id;
    }

    private void openDocument(int request, String type) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.setType(type);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        startActivityForResult(intent, request);
    }

    private void createDocument(int request, String type, String name) {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.setType(type);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.putExtra(Intent.EXTRA_TITLE, name);
        startActivityForResult(intent, request);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        if (request == OPEN_PROGRAM) {
            work(() -> {
                String contents = readText(uri, MAX_PROGRAM_BYTES);
                String name = documentName(uri).toLowerCase(Locale.ROOT);
                ui.post(() -> {
                    if (closed) return;
                    if (name.endsWith(".x") && !xasmMode) changeMode();
                    if (name.endsWith(".xi") && xasmMode) changeMode();
                    editor.setText(contents);
                });
                log("Programa abierto. Pulsa Ejecutar para usarlo.");
            });
        } else if (request == SAVE_PROGRAM) {
            final String contents = editor.getText().toString();
            work(() -> {
                writeText(uri, contents, MAX_PROGRAM_BYTES);
                log("Programa guardado.");
            });
        } else if (request == SAVE_SESSION) {
            final String source = editor.getText().toString();
            final boolean savedXasmMode = xasmMode;
            final String savedXi = savedXasmMode ? xiDraft : source;
            final String savedXasm = savedXasmMode ? source : xasmDraft;
            work(() -> {
                String[] snapshot = QuantumWorkstation.snapshot(requireSession());
                JSONObject dataJson = new JSONObject();
                dataJson.put("format", SESSION_FORMAT);
                dataJson.put("program", source);
                dataJson.put("xi_program", savedXi);
                dataJson.put("xasm_program", savedXasm);
                dataJson.put("mode", savedXasmMode ? "xasm" : "xi");
                dataJson.put("core", snapshot[0]);
                dataJson.put("memory", snapshot[1]);
                writeText(uri, dataJson.toString(), MAX_SESSION_BYTES);
                log("Sesión .mqx guardada: programas, núcleo XI y memoria.");
            });
        } else if (request == OPEN_SESSION) {
            work(() -> {
                JSONObject dataJson = new JSONObject(readText(uri, MAX_SESSION_BYTES));
                if (!SESSION_FORMAT.equals(dataJson.getString("format")))
                    throw new IllegalArgumentException("Formato .mqx incompatible");
                // Invalid input must not replace any part of the live runtime.
                QuantumWorkstation.restore(requireSession(),
                        dataJson.getString("core"), dataJson.getString("memory"));
                final String source = dataJson.getString("program");
                final String xi = dataJson.optString("xi_program", source);
                final String xasm = dataJson.optString("xasm_program", "");
                final boolean loadXasmMode = "xasm".equals(dataJson.optString("mode", "xi"));
                ui.post(() -> {
                    if (closed) return;
                    xiDraft = xi;
                    xasmDraft = xasm;
                    xasmMode = loadXasmMode;
                    editor.setText(xasmMode ? xasmDraft : xiDraft);
                    updateModeButtons();
                });
                log("Sesión .mqx recuperada. Núcleo XI y memoria restaurados.");
            });
        }
    }

    private String readText(Uri uri, int maxBytes) throws Exception {
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IllegalStateException("No se pudo abrir el documento");
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] block = new byte[8192];
            int size;
            while ((size = input.read(block)) != -1) {
                if (size > maxBytes - output.size())
                    throw new IllegalStateException("Archivo demasiado grande");
                output.write(block, 0, size);
            }
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    private void writeText(Uri uri, String contents, int maxBytes) throws Exception {
        byte[] bytes = contents.getBytes(StandardCharsets.UTF_8);
        if (bytes.length > maxBytes) throw new IllegalStateException("Archivo demasiado grande");
        try (OutputStream stream = getContentResolver().openOutputStream(uri, "wt")) {
            if (stream == null) throw new IllegalStateException("No se pudo escribir el archivo");
            stream.write(bytes);
            stream.flush();
        }
    }

    private interface Job {
        void run() throws Exception;
    }

    private void work(Job job) {
        if (busy || closed) return;
        busy = true;
        enableControls(false);
        state.setText("Procesando…");
        try {
            worker.execute(() -> {
                try {
                    job.run();
                } catch (Exception | LinkageError error) {
                    log("Error: " + error.getMessage()
                            + "\nSi falló la ejecución XI, los cambios del programa se descartan.");
                } finally {
                    ui.post(() -> {
                        if (!closed) {
                            busy = false;
                            enableControls(true);
                            state.setText(session == 0 ? "Núcleo no disponible" : "Ether activo · sesión local");
                        }
                    });
                }
            });
        } catch (RejectedExecutionException error) {
            busy = false;
            enableControls(false);
            state.setText("La sesión se está cerrando.");
        }
    }

    private void log(String message) {
        ui.post(() -> {
            if (closed) return;
            transcript.append(message).append("\n\n");
            if (transcript.length() > MAX_CONSOLE_CHARS) {
                transcript.delete(0, transcript.length() - MAX_CONSOLE_CHARS);
                transcript.insert(0, "[Consola antigua recortada]\n");
            }
            console.setText(transcript.toString());
        });
    }

    private void enableControls(boolean enabled) {
        runButton.setEnabled(enabled);
        modeButton.setEnabled(enabled);
        sendButton.setEnabled(enabled);
        inspectButton.setEnabled(enabled);
        resetButton.setEnabled(enabled);
        openProgramButton.setEnabled(enabled);
        saveProgramButton.setEnabled(enabled);
        newProgramButton.setEnabled(enabled);
        openSessionButton.setEnabled(enabled);
        saveSessionButton.setEnabled(enabled);
    }

    private TextView label(String text, int size, int color) {
        TextView label = new TextView(this);
        label.setText(text);
        label.setTextSize(size);
        label.setTextColor(color);
        label.setLineSpacing(dp(2), 1.05f);
        return label;
    }

    private TextView heading(String title) {
        TextView label = label(title, 14, ACCENT);
        label.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        label.setPadding(0, 0, 0, dp(12));
        return label;
    }

    private GradientDrawable panelBackground(int color) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(color);
        background.setCornerRadius(dp(12));
        background.setStroke(dp(1), BORDER);
        return background;
    }

    private LinearLayout card(LinearLayout parent) {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(14), dp(15), dp(14), dp(15));
        panel.setBackground(panelBackground(PANEL));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.topMargin = dp(18);
        parent.addView(panel, params);
        return panel;
    }

    private Button control(String text, boolean primary) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextColor(primary ? BACKGROUND : TEXT);
        button.setTextSize(12);
        button.setAllCaps(false);
        button.setBackground(panelBackground(primary ? ACCENT : PANEL));
        return button;
    }

    private LinearLayout row(LinearLayout parent) {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        parent.addView(layout, spaced(48));
        return layout;
    }

    private void pair(LinearLayout parent, Button first, Button second) {
        LinearLayout.LayoutParams left = new LinearLayout.LayoutParams(0, -1, 1);
        left.rightMargin = dp(8);
        parent.addView(first, left);
        parent.addView(second, new LinearLayout.LayoutParams(0, -1, 1));
    }

    private LinearLayout.LayoutParams spaced(int heightDp) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1,
                heightDp < 0 ? -2 : dp(heightDp));
        params.topMargin = dp(9);
        return params;
    }

    private int dp(int points) {
        return (int) (getResources().getDisplayMetrics().density * points + 0.5f);
    }

    @Override
    protected void onDestroy() {
        closed = true;
        try {
            worker.execute(() -> {
                if (session != 0) {
                    QuantumWorkstation.close(session);
                    session = 0;
                }
            });
        } catch (RejectedExecutionException ignored) {
            // The worker is already stopped.
        }
        worker.shutdown();
        super.onDestroy();
    }
}
