package com.cosmicx.pc;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.webkit.CookieManager;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.URLUtil;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.FileNotFoundException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

/**
 * Mundo X Personal Desktop: practical everyday Android workspace.
 *
 * Real browser, real local notes, SAF document access, and direct launchers
 * for the project's own quantum computer and x86 emulator. Android handles
 * networking, storage and hardware; no false claim that web/notes run on
 * quantum amplitudes or that this app replaces the Android operating system.
 */
public final class MundoXDesktopActivity extends Activity {
    private static final int OPEN_FILE = 610;
    private static final int EXPORT_NOTES = 611;
    private static final int IMPORT_NOTES = 612;
    private static final int SAVE_DOWNLOAD = 613;
    private static final int WEB_FILE_CHOOSER = 614;
    private static final int TAB_HOME = 0, TAB_BROWSER = 1, TAB_NOTES = 2, TAB_FILES = 3;
    private static final int MAX_NOTE_CHARS = 200_000;
    private static final int MAX_FILE_PREVIEW = 256 * 1024;
    private static final long MAX_DOWNLOAD = 100L * 1024 * 1024;
    private static final String NOTES_FILE = "mundo_x_notes.txt";
    private static final String PREFS = "mundo_x_desktop";
    private static final String RECENT_URI = "recent_document_uri";
    private static final String RECENT_NAME = "recent_document_name";
    private static final int BG = Color.rgb(7, 12, 20);
    private static final int SURFACE = Color.rgb(15, 25, 38);
    private static final int ELEVATED = Color.rgb(21, 35, 51);
    private static final int BORDER = Color.rgb(50, 73, 89);
    private static final int CYAN = Color.rgb(113, 237, 212);
    private static final int WHITE = Color.rgb(234, 246, 253);
    private static final int MUTED = Color.rgb(151, 177, 195);

    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService filesWorker = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "Mundo-X-Desktop-Files");
        thread.setPriority(Thread.NORM_PRIORITY - 1);
        return thread;
    });
    private final View[] pages = new View[4];
    private final Button[] tabs = new Button[4];
    private int selectedTab = TAB_HOME;
    private boolean closing;
    private FrameLayout content;
    private TextView subtitle, browserProgress, browserTitle, filesInfo, filesPreview, notesStatus;
    private EditText address, notesEditor;
    private Button recentFileButton;
    private WebView browser;
    private WebChromeClient.FileChooserParams pendingChooserParams;
    private android.webkit.ValueCallback<Uri[]> pendingWebFiles;
    private PendingDownload pendingDownload;
    private String currentFileName = "";
    private Uri currentFileUri;
    private final Runnable saveNotes = () -> {
        if (notesEditor != null) writeNotesAsync(notesEditor.getText().toString());
    };

    private static final class PendingDownload {
        final String url;
        final String mime;
        final String cookie;
        final String agent;
        PendingDownload(String url, String mime, String cookie, String agent) {
            this.url = url; this.mime = mime; this.cookie = cookie; this.agent = agent;
        }
    }

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        renderShell();
        showTab(saved == null ? TAB_HOME : saved.getInt("tab", TAB_HOME));
    }

    @Override protected void onSaveInstanceState(Bundle bundle) {
        super.onSaveInstanceState(bundle);
        bundle.putInt("tab", selectedTab);
    }

    private void renderShell() {
        LinearLayout shell = new LinearLayout(this);
        shell.setOrientation(LinearLayout.VERTICAL);
        shell.setBackgroundColor(BG);
        shell.setFitsSystemWindows(true);
        setContentView(shell);

        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.VERTICAL);
        top.setPadding(dp(19), dp(16), dp(19), dp(12));
        top.setBackgroundColor(SURFACE);
        TextView mark = text("MUNDO X   /   DESKTOP", 21, WHITE);
        mark.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        mark.setLetterSpacing(0.08f);
        top.addView(mark);
        subtitle = text("Computadora personal  ·  entorno local", 11, MUTED);
        subtitle.setPadding(0, dp(4), 0, 0);
        top.addView(subtitle);
        shell.addView(top);

        content = new FrameLayout(this);
        shell.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));

        LinearLayout nav = new LinearLayout(this);
        nav.setPadding(dp(8), dp(5), dp(8), dp(7));
        nav.setBackgroundColor(SURFACE);
        nav.setOrientation(LinearLayout.HORIZONTAL);
        String[] titles = {"Inicio", "Web", "Notas", "Archivos"};
        for (int i = 0; i < titles.length; ++i) {
            final int tab = i;
            Button button = button(titles[i], false);
            button.setTextSize(12);
            tabs[i] = button;
            LinearLayout.LayoutParams param = new LinearLayout.LayoutParams(0, dp(49), 1);
            param.leftMargin = dp(3);
            param.rightMargin = dp(3);
            nav.addView(button, param);
            button.setOnClickListener(v -> showTab(tab));
        }
        shell.addView(nav);
    }

    private void showTab(int tab) {
        if (tab < TAB_HOME || tab > TAB_FILES) tab = TAB_HOME;
        selectedTab = tab;
        if (pages[tab] == null) {
            switch (tab) {
                case TAB_HOME: pages[tab] = makeHome(); break;
                case TAB_BROWSER: pages[tab] = makeBrowser(); break;
                case TAB_NOTES: pages[tab] = makeNotes(); break;
                default: pages[tab] = makeFiles(); break;
            }
        }
        content.removeAllViews();
        View next = pages[tab];
        if (next.getParent() instanceof ViewGroup) ((ViewGroup)next.getParent()).removeView(next);
        content.addView(next, new FrameLayout.LayoutParams(-1, -1));
        for (int i = 0; i < tabs.length; ++i) {
            tabs[i].setTextColor(i == tab ? BG : WHITE);
            tabs[i].setBackground(shape(i == tab ? CYAN : ELEVATED, BORDER, 12));
        }
        String[] section = {"Inicio y aplicaciones", "Navegador Web integrado",
                "Notas privadas en tu dispositivo", "Documentos del dispositivo"};
        subtitle.setText(section[tab]);
    }

    private View makeHome() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout page = stack();
        page.setPadding(dp(16), dp(12), dp(16), dp(20));
        scroll.addView(page);

        FrameLayout hero = new FrameLayout(this);
        hero.setBackground(shape(ELEVATED, BORDER, 19));
        LinearLayout.LayoutParams heroLayout = new LinearLayout.LayoutParams(-1, dp(188));
        heroLayout.topMargin = dp(8);
        page.addView(hero, heroLayout);
        OrbitView orbit = new OrbitView(this);
        orbit.setContentDescription("Motivo visual de órbitas; no es una simulación cuántica");
        hero.addView(orbit, new FrameLayout.LayoutParams(-1, -1));
        LinearLayout heroText = stack();
        heroText.setPadding(dp(19), dp(23), dp(15), dp(18));
        hero.addView(heroText);
        heroText.addView(text("COSMIC X ENGINE", 12, CYAN));
        TextView focus = text("Tu máquina.\nTus reglas.", 26, WHITE);
        focus.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        focus.setLineSpacing(dp(3), 1.0f);
        focus.setPadding(0, dp(10), 0, dp(8));
        heroText.addView(focus);
        heroText.addView(text("Tu espacio para navegar, crear y calcular.", 12, MUTED));

        title(page, "APLICACIONES");
        tile(page, "Quantum Workstation", "XI persistente · XASM · MIL 1000 · programas", () ->
                startActivity(new Intent(this, QuantumDesktopActivity.class)));
        tile(page, "Navegador Web", "Buscar, abrir sitios y guardar archivos", () -> showTab(TAB_BROWSER));
        tile(page, "Notas", "Editor privado con autoguardado y exportación", () -> showTab(TAB_NOTES));
        tile(page, "Archivos", "Abrir documentos reales de Android", () -> showTab(TAB_FILES));
        tile(page, "COSMIC X PC", "Tu emulador x86/x64 con arranque y vídeo propios", () ->
                startActivity(new Intent(this, MainActivity.class)));

        TextView scope = text("Las aplicaciones cotidianas utilizan los servicios de Android. Tu núcleo cuántico por software se ejecuta desde Quantum Workstation.", 11, MUTED);
        scope.setPadding(dp(5), dp(18), dp(5), dp(10));
        page.addView(scope);
        return scroll;
    }

    private View makeBrowser() {
        LinearLayout page = stack();
        LinearLayout toolbar = new LinearLayout(this);
        toolbar.setPadding(dp(9), dp(8), dp(9), dp(5));
        toolbar.setOrientation(LinearLayout.HORIZONTAL);
        page.addView(toolbar);

        Button back = button("‹", false), forward = button("›", false);
        Button go = button("Ir", true);
        back.setContentDescription("Atrás");
        forward.setContentDescription("Adelante");
        for (Button b : new Button[]{back, forward}) {
            toolbar.addView(b, new LinearLayout.LayoutParams(dp(39), dp(45)));
        }
        address = new EditText(this);
        address.setTextColor(WHITE);
        address.setHintTextColor(MUTED);
        address.setHint("Busca o escribe una URL");
        address.setSingleLine(true);
        address.setTextSize(13);
        address.setPadding(dp(8), 0, dp(8), 0);
        address.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_VARIATION_URI);
        address.setBackground(shape(ELEVATED, BORDER, 8));
        LinearLayout.LayoutParams ap = new LinearLayout.LayoutParams(0, dp(45), 1);
        ap.leftMargin = dp(5);
        ap.rightMargin = dp(5);
        toolbar.addView(address, ap);
        toolbar.addView(go, new LinearLayout.LayoutParams(dp(47), dp(45)));

        browserProgress = text("Navegador disponible", 10, MUTED);
        browserProgress.setPadding(dp(14), dp(3), dp(10), dp(5));
        page.addView(browserProgress);
        ProgressBar loadBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        loadBar.setMax(100);
        loadBar.setProgress(0);
        page.addView(loadBar, new LinearLayout.LayoutParams(-1, dp(2)));

        browser = new WebView(this);
        browser.setBackgroundColor(BG);
        browser.setContentDescription("Navegador Mundo X");
        WebSettings settings = browser.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setSupportMultipleWindows(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        if (Build.VERSION.SDK_INT >= 26) settings.setSafeBrowsingEnabled(true);
        browser.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                if (!request.isForMainFrame()) return false;
                Uri uri = request.getUrl();
                String scheme = uri.getScheme();
                if (scheme == null) return true;
                if (scheme.equalsIgnoreCase("https") || scheme.equalsIgnoreCase("http")) return false;
                if (scheme.equalsIgnoreCase("mailto") || scheme.equalsIgnoreCase("tel")) {
                    try {
                        startActivity(new Intent(Intent.ACTION_VIEW, uri));
                    } catch (ActivityNotFoundException error) {
                        message("No hay una aplicación compatible");
                    }
                }
                return true;
            }

            @Override public void onPageFinished(WebView view, String url) {
                if (!closing && address != null && isWebUrl(url)) address.setText(url);
            }
        });
        browser.setWebChromeClient(new WebChromeClient() {
            @Override public void onProgressChanged(WebView view, int progress) {
                loadBar.setProgress(progress);
                if (!closing && browserProgress != null) {
                    browserProgress.setText(progress == 100 ? "Página cargada" : "Cargando · " + progress + "%");
                }
            }

            @Override public void onReceivedTitle(WebView view, String title) {
                if (!closing && browserTitle != null) browserTitle.setText(title);
            }

            @Override public boolean onShowFileChooser(WebView webView,
                    android.webkit.ValueCallback<Uri[]> callback, FileChooserParams params) {
                if (pendingWebFiles != null) pendingWebFiles.onReceiveValue(null);
                pendingWebFiles = callback;
                pendingChooserParams = params;
                try {
                    Intent intent = params.createIntent();
                    startActivityForResult(intent, WEB_FILE_CHOOSER);
                    return true;
                } catch (Exception error) {
                    pendingWebFiles = null;
                    pendingChooserParams = null;
                    callback.onReceiveValue(null);
                    message("No se pudo abrir el selector de archivos");
                    return false;
                }
            }
        });
        browser.setDownloadListener((url, userAgent, disposition, mime, length) -> {
            if (!isWebUrl(url)) {
                message("Solo se permiten descargas HTTP o HTTPS");
                return;
            }
            String filename = URLUtil.guessFileName(url, disposition, mime);
            filename = safeFilename(filename);
            PendingDownload candidate = new PendingDownload(url, mime,
                    CookieManager.getInstance().getCookie(url), userAgent);
            pendingDownload = candidate;
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.setType(mime == null || mime.isEmpty() ? "application/octet-stream" : mime);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.putExtra(Intent.EXTRA_TITLE, filename);
            try {
                startActivityForResult(intent, SAVE_DOWNLOAD);
            } catch (ActivityNotFoundException error) {
                pendingDownload = null;
                message("No se encontró un selector para guardar");
            }
        });
        page.addView(browser, new LinearLayout.LayoutParams(-1, 0, 1));

        browserTitle = text("Web dentro de Mundo X", 11, MUTED);
        browserTitle.setSingleLine();
        browserTitle.setPadding(dp(14), dp(7), dp(14), dp(9));
        page.addView(browserTitle);

        go.setOnClickListener(v -> navigate(address.getText().toString()));
        address.setOnEditorActionListener((v, actionId, event) -> {
            navigate(address.getText().toString());
            return true;
        });
        back.setOnClickListener(v -> { if (browser.canGoBack()) browser.goBack(); });
        forward.setOnClickListener(v -> { if (browser.canGoForward()) browser.goForward(); });

        browser.loadUrl("https://duckduckgo.com/");
        return page;
    }

    private void navigate(String input) {
        if (browser == null) return;
        String addressText = input.trim();
        if (addressText.isEmpty()) return;
        String url;
        if (addressText.matches("(?i)^https?://\\S+$")) {
            url = addressText;
        } else if (!addressText.contains(" ") && !addressText.contains(":")
                && addressText.contains(".")) {
            url = "https://" + addressText;
        } else {
            try {
                url = "https://duckduckgo.com/?q=" +
                        URLEncoder.encode(addressText, "UTF-8");
            } catch (Exception error) {
                message("No se pudo interpretar la búsqueda");
                return;
            }
        }
        if (!isWebUrl(url)) {
            message("Dirección no permitida");
            return;
        }
        browser.loadUrl(url);
    }

    private static boolean isWebUrl(String text) {
        try {
            Uri uri = Uri.parse(text);
            String scheme = uri.getScheme();
            return ("https".equalsIgnoreCase(scheme) || "http".equalsIgnoreCase(scheme))
                    && uri.getHost() != null && !uri.getHost().isEmpty();
        } catch (Exception error) {
            return false;
        }
    }

    private View makeNotes() {
        LinearLayout page = stack();
        page.setPadding(dp(14), dp(12), dp(14), dp(14));
        TextView heading = text("Bloc de notas", 25, WHITE);
        heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        page.addView(heading);
        TextView help = text("Tus notas se guardan automáticamente en tu teléfono. No necesitan Internet.", 12, MUTED);
        help.setPadding(0, dp(6), 0, dp(11));
        page.addView(help);

        notesEditor = new EditText(this);
        notesEditor.setTextColor(WHITE);
        notesEditor.setHintTextColor(MUTED);
        notesEditor.setTextSize(15);
        notesEditor.setTypeface(Typeface.MONOSPACE);
        notesEditor.setGravity(Gravity.TOP | Gravity.START);
        notesEditor.setHint("Escribe aquí tus ideas, proyectos y tareas…");
        notesEditor.setSingleLine(false);
        notesEditor.setMinLines(8);
        notesEditor.setBackground(shape(ELEVATED, BORDER, 11));
        notesEditor.setPadding(dp(13), dp(12), dp(13), dp(12));
        notesEditor.setFilters(new InputFilter[]{new InputFilter.LengthFilter(MAX_NOTE_CHARS)});
        page.addView(notesEditor, new LinearLayout.LayoutParams(-1, 0, 1));
        try {
            notesEditor.setText(readPrivateNotes());
        } catch (Exception error) {
            message("No se pudieron recuperar las notas locales");
        }
        notesStatus = text("Autoguardado local", 11, CYAN);
        notesStatus.setPadding(dp(3), dp(8), dp(3), dp(8));
        page.addView(notesStatus);
        notesEditor.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) { }
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                notesStatus.setText("Cambios pendientes…");
                main.removeCallbacks(saveNotes);
                main.postDelayed(saveNotes, 650);
            }
            @Override public void afterTextChanged(Editable editable) { }
        });

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        page.addView(actions);
        Button export = button("Exportar .txt", true);
        Button imports = button("Importar .txt", false);
        actions.addView(export, new LinearLayout.LayoutParams(0, dp(50), 1));
        LinearLayout.LayoutParams right = new LinearLayout.LayoutParams(0, dp(50), 1);
        right.leftMargin = dp(8);
        actions.addView(imports, right);
        export.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("text/plain");
            intent.putExtra(Intent.EXTRA_TITLE, "notas-mundo-x.txt");
            startActivityForResult(intent, EXPORT_NOTES);
        });
        imports.setOnClickListener(v -> {
            new AlertDialog.Builder(this)
                    .setTitle("Importar notas")
                    .setMessage("El texto seleccionado reemplazará el bloc actual. Puedes exportarlo antes.")
                    .setPositiveButton("Elegir archivo", (dialog, which) -> selectDocument(IMPORT_NOTES))
                    .setNegativeButton("Cancelar", null).show();
        });
        return page;
    }

    private String readPrivateNotes() throws Exception {
        try (InputStream input = openFileInput(NOTES_FILE)) {
            return new String(readBounded(input, MAX_NOTE_CHARS * 4), StandardCharsets.UTF_8);
        } catch (FileNotFoundException missing) {
            return "";
        }
    }

    private void writeNotesAsync(String contents) {
        if (closing) return;
        try {
            filesWorker.execute(() -> {
                try (OutputStream output = openFileOutput(NOTES_FILE, Context.MODE_PRIVATE)) {
                    output.write(contents.getBytes(StandardCharsets.UTF_8));
                    main.post(() -> {
                        if (!closing && notesStatus != null &&
                                notesEditor.getText().toString().equals(contents))
                            notesStatus.setText("Guardado en este dispositivo");
                    });
                } catch (Exception error) {
                    main.post(() -> {
                        if (!closing && notesStatus != null)
                            notesStatus.setText("Error al guardar: exporta tu texto");
                    });
                }
            });
        } catch (RejectedExecutionException ignored) { }
    }

    private View makeFiles() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout page = stack();
        page.setPadding(dp(15), dp(14), dp(15), dp(20));
        scroll.addView(page);
        TextView heading = text("Documentos", 25, WHITE);
        heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        page.addView(heading);
        TextView help = text("Abre archivos del teléfono o de proveedores conectados al selector de Android. Para formatos binarios se usará una app compatible.", 12, MUTED);
        help.setPadding(0, dp(8), 0, dp(15));
        page.addView(help);

        Button picker = button("Abrir documento", true);
        page.addView(picker, new LinearLayout.LayoutParams(-1, dp(52)));
        picker.setOnClickListener(v -> selectDocument(OPEN_FILE));

        Button recent = button("Abrir documento reciente", false);
        recentFileButton = recent;
        LinearLayout.LayoutParams param = new LinearLayout.LayoutParams(-1, dp(48));
        param.topMargin = dp(9);
        page.addView(recent, param);
        SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        String last = prefs.getString(RECENT_URI, null);
        String lastName = prefs.getString(RECENT_NAME, "");
        recent.setText(last == null ? "Sin archivo reciente" : "Reciente: " + lastName);
        recent.setEnabled(last != null);
        recent.setOnClickListener(v -> {
            String stringUri = prefs.getString(RECENT_URI, null);
            if (stringUri != null) preview(Uri.parse(stringUri));
        });

        title(page, "VISTA DE ARCHIVOS");
        filesInfo = text("Selecciona un archivo para consultar su información.", 12, CYAN);
        filesInfo.setPadding(0, dp(5), 0, dp(12));
        page.addView(filesInfo);

        filesPreview = text("Los archivos de texto pueden verse aquí sin abrir otra aplicación.", 13, WHITE);
        filesPreview.setTextIsSelectable(true);
        filesPreview.setTypeface(Typeface.MONOSPACE);
        filesPreview.setPadding(dp(12), dp(12), dp(12), dp(12));
        filesPreview.setBackground(shape(ELEVATED, BORDER, 11));
        page.addView(filesPreview);

        Button external = button("Abrir con otra aplicación", false);
        page.addView(external, spaced(52));
        external.setOnClickListener(v -> {
            if (currentFileUri != null) openExternally(currentFileUri);
        });

        page.addView(text("Mundo X puede abrir archivos existentes, pero todavía no reemplaza al gestor completo de archivos de Android.", 11, MUTED));
        return scroll;
    }

    private void selectDocument(int request) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(request == IMPORT_NOTES ? "text/plain" : "*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, request);
    }

    private void preview(Uri uri) {
        currentFileUri = uri;
        String name = getDisplayName(uri);
        currentFileName = name;
        String mime = getContentResolver().getType(uri);
        String lower = name.toLowerCase(java.util.Locale.ROOT);
        boolean isText = (mime != null && mime.startsWith("text/")) ||
                lower.endsWith(".md") || lower.endsWith(".txt") ||
                lower.endsWith(".json") || lower.endsWith(".log") ||
                lower.endsWith(".xi") || lower.endsWith(".x") ||
                lower.endsWith(".csv") || lower.endsWith(".xml");
        if (filesInfo != null) filesInfo.setText(name + "\n" + (mime == null ? "Tipo desconocido" : mime));
        if (filesPreview != null) filesPreview.setText(isText ? "Leyendo documento…" :
                "Vista previa de este formato no incluida. Usa «Abrir con otra aplicación».");
        if (!isText) return;
        try {
            filesWorker.execute(() -> {
                try (InputStream stream = getContentResolver().openInputStream(uri)) {
                    if (stream == null) throw new FileNotFoundException("No se pudo abrir");
                    String result = new String(readBounded(stream, MAX_FILE_PREVIEW), StandardCharsets.UTF_8);
                    main.post(() -> {
                        if (!closing && uri.equals(currentFileUri) && filesPreview != null)
                            filesPreview.setText(result);
                    });
                } catch (Exception e) {
                    main.post(() -> {
                        if (!closing && uri.equals(currentFileUri) && filesPreview != null)
                            filesPreview.setText("No se pudo previsualizar el archivo: " + e.getMessage());
                    });
                }
            });
        } catch (RejectedExecutionException ignored) { }
    }

    private void openExternally(Uri uri) {
        String mime = getContentResolver().getType(uri);
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(uri, mime == null ? "*/*" : mime);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivity(Intent.createChooser(intent, "Abrir documento"));
        } catch (ActivityNotFoundException error) {
            message("No hay una aplicación para abrir ese formato");
        }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == WEB_FILE_CHOOSER) {
            android.webkit.ValueCallback<Uri[]> callback = pendingWebFiles;
            pendingWebFiles = null;
            pendingChooserParams = null;
            if (callback != null) {
                callback.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(result, data));
            }
            return;
        }
        if (request == SAVE_DOWNLOAD) {
            PendingDownload download = pendingDownload;
            pendingDownload = null;
            if (download != null && result == RESULT_OK && data != null && data.getData() != null)
                downloadTo(download, data.getData());
            return;
        }
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        if (request == OPEN_FILE) {
            try {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                        .putString(RECENT_URI, uri.toString())
                        .putString(RECENT_NAME, getDisplayName(uri)).apply();
            } catch (SecurityException ignored) {
                // Still usable until Android revokes the transient grant.
            }
            showTab(TAB_FILES);
            if (recentFileButton != null) {
                recentFileButton.setText("Reciente: " + getDisplayName(uri));
                recentFileButton.setEnabled(true);
            }
            preview(uri);
        } else if (request == EXPORT_NOTES) {
            String note = notesEditor == null ? "" : notesEditor.getText().toString();
            try {
                filesWorker.execute(() -> {
                    try (OutputStream output = getContentResolver().openOutputStream(uri, "wt")) {
                        if (output == null) throw new FileNotFoundException("No se pudo escribir");
                        output.write(note.getBytes(StandardCharsets.UTF_8));
                        output.flush();
                        messageAsync("Notas exportadas");
                    } catch (Exception error) {
                        messageAsync("Error al exportar notas");
                    }
                });
            } catch (RejectedExecutionException ignored) { }
        } else if (request == IMPORT_NOTES) {
            try {
                filesWorker.execute(() -> {
                    try (InputStream stream = getContentResolver().openInputStream(uri)) {
                        if (stream == null) throw new FileNotFoundException("No se pudo abrir");
                        String note = new String(readBounded(stream, MAX_NOTE_CHARS * 4),
                                StandardCharsets.UTF_8);
                        if (note.length() > MAX_NOTE_CHARS)
                            throw new IllegalArgumentException("Notas demasiado extensas");
                        main.post(() -> {
                            if (!closing && notesEditor != null) {
                                notesEditor.setText(note);
                                message("Notas importadas");
                            }
                        });
                    } catch (Exception error) { messageAsync("No se pudieron importar las notas"); }
                });
            } catch (RejectedExecutionException ignored) { }
        }
    }

    private void downloadTo(PendingDownload download, Uri target) {
        try {
            filesWorker.execute(() -> {
                HttpURLConnection connection = null;
                try {
                    URL current = new URL(download.url);
                    String originalHost = current.getHost();
                    for (int redirects = 0; redirects <= 5; redirects++) {
                        if (!isWebUrl(current.toString())) throw new IllegalArgumentException("Destino no HTTP(S)");
                        connection = (HttpURLConnection) current.openConnection();
                        connection.setConnectTimeout(15000);
                        connection.setReadTimeout(30000);
                        connection.setInstanceFollowRedirects(false);
                        if (download.agent != null && !download.agent.isEmpty())
                            connection.setRequestProperty("User-Agent", download.agent);
                        if (download.cookie != null && !download.cookie.isEmpty() &&
                                originalHost.equalsIgnoreCase(current.getHost()))
                            connection.setRequestProperty("Cookie", download.cookie);
                        int status = connection.getResponseCode();
                        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                            String location = connection.getHeaderField("Location");
                            if (location == null || redirects == 5) throw new IllegalStateException("Demasiadas redirecciones");
                            URL next = new URL(current, location);
                            connection.disconnect();
                            connection = null;
                            current = next;
                            continue;
                        }
                        if (status < 200 || status >= 300) throw new IllegalStateException("HTTP " + status);
                        long size = connection.getContentLengthLong();
                        if (size > MAX_DOWNLOAD) throw new IllegalStateException("Archivo mayor de 100 MiB");
                        try (InputStream from = connection.getInputStream();
                             OutputStream to = getContentResolver().openOutputStream(target, "wt")) {
                            if (to == null) throw new FileNotFoundException("Destino no disponible");
                            byte[] buffer = new byte[8192];
                            long bytes = 0;
                            int count;
                            while ((count = from.read(buffer)) != -1) {
                                bytes += count;
                                if (bytes > MAX_DOWNLOAD) throw new IllegalStateException("Descarga mayor de 100 MiB");
                                to.write(buffer, 0, count);
                            }
                            to.flush();
                            messageAsync("Descarga guardada: " + bytes + " bytes");
                        }
                        return;
                    }
                } catch (Exception error) {
                    messageAsync("No se completó la descarga: " + error.getMessage());
                } finally {
                    if (connection != null) connection.disconnect();
                }
            });
        } catch (RejectedExecutionException ignored) { }
    }

    private static String safeFilename(String filename) {
        if (filename == null || filename.trim().isEmpty()) return "descarga";
        filename = filename.replaceAll("[^a-zA-Z0-9._ -]", "_");
        if (filename.length() > 120) filename = filename.substring(0, 120);
        return filename.isEmpty() ? "descarga" : filename;
    }

    private static byte[] readBounded(InputStream stream, int max) throws Exception {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] data = new byte[8192];
        int n;
        while ((n = stream.read(data)) != -1) {
            if (n > max - output.size()) throw new IllegalStateException("Archivo supera el límite de lectura");
            output.write(data, 0, n);
        }
        return output.toByteArray();
    }

    private String getDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String name = cursor.getString(0);
                if (name != null && !name.isEmpty()) return name;
            }
        } catch (Exception ignored) { }
        return uri.getLastPathSegment() == null ? "Documento" : uri.getLastPathSegment();
    }

    private void messageAsync(String text) {
        main.post(() -> message(text));
    }

    private void message(String msg) {
        if (!closing) Toast.makeText(this, msg, Toast.LENGTH_LONG).show();
    }

    private void title(LinearLayout parent, String value) {
        TextView heading = text(value, 12, CYAN);
        heading.setLetterSpacing(0.11f);
        heading.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        heading.setPadding(dp(5), dp(21), 0, dp(6));
        parent.addView(heading);
    }

    private void tile(LinearLayout parent, String name, String description, Runnable action) {
        LinearLayout layout = stack();
        layout.setPadding(dp(17), dp(14), dp(14), dp(14));
        layout.setBackground(shape(SURFACE, BORDER, 12));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.topMargin = dp(10);
        parent.addView(layout, params);
        TextView title = text(name + "   ›", 17, WHITE);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        layout.addView(title);
        TextView detail = text(description, 11, MUTED);
        detail.setPadding(0, dp(5), 0, 0);
        layout.addView(detail);
        layout.setContentDescription(name + ". " + description);
        layout.setClickable(true);
        layout.setFocusable(true);
        layout.setOnClickListener(v -> action.run());
    }

    private LinearLayout stack() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextColor(color);
        view.setTextSize(size);
        view.setLineSpacing(dp(2), 1.0f);
        return view;
    }

    private Button button(String label, boolean primary) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextSize(12);
        button.setTextColor(primary ? BG : WHITE);
        button.setBackground(shape(primary ? CYAN : ELEVATED, BORDER, 10));
        return button;
    }

    private GradientDrawable shape(int fill, int stroke, int radius) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(fill);
        drawable.setCornerRadius(dp(radius));
        drawable.setStroke(dp(1), stroke);
        return drawable;
    }

    private LinearLayout.LayoutParams spaced(int height) {
        LinearLayout.LayoutParams param = new LinearLayout.LayoutParams(-1, dp(height));
        param.topMargin = dp(13);
        return param;
    }

    private int dp(int points) {
        return (int) (getResources().getDisplayMetrics().density * points + 0.5f);
    }

    /** Lightweight static home motif. It makes no claims about quantum rendering. */
    private static final class OrbitView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        OrbitView(Context ctx) { super(ctx); }
        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float width = getWidth(), height = getHeight();
            float cx = width * 0.79f, cy = height * 0.52f, radius = Math.min(width, height) * .25f;
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(Math.max(1f, width / 370f));
            for (int i = 0; i < 4; i++) {
                p.setColor(Color.argb(75 - 13 * i, 113, 237, 212));
                float r = radius * (1.0f + i * .48f);
                canvas.drawOval(new RectF(cx - r, cy - r * .65f, cx + r, cy + r * .65f), p);
            }
            p.setStyle(Paint.Style.FILL);
            p.setColor(Color.rgb(113, 237, 212));
            canvas.drawCircle(cx, cy, radius * .31f, p);
            p.setColor(Color.argb(95, 113, 237, 212));
            canvas.drawCircle(cx, cy, radius * .51f, p);
        }
    }

    @Override protected void onPause() {
        main.removeCallbacks(saveNotes);
        if (notesEditor != null) writeNotesAsync(notesEditor.getText().toString());
        if (browser != null) browser.onPause();
        super.onPause();
    }

    @Override protected void onResume() {
        super.onResume();
        if (browser != null) browser.onResume();
    }

    @Override public void onBackPressed() {
        if (selectedTab == TAB_BROWSER && browser != null && browser.canGoBack()) {
            browser.goBack();
        } else if (selectedTab != TAB_HOME) {
            showTab(TAB_HOME);
        } else {
            super.onBackPressed();
        }
    }

    @Override protected void onDestroy() {
        if (notesEditor != null) {
            main.removeCallbacks(saveNotes);
            writeNotesAsync(notesEditor.getText().toString());
        }
        closing = true;
        main.removeCallbacksAndMessages(null);
        if (pendingWebFiles != null) {
            pendingWebFiles.onReceiveValue(null);
            pendingWebFiles = null;
        }
        if (browser != null) {
            browser.stopLoading();
            if (browser.getParent() instanceof ViewGroup)
                ((ViewGroup)browser.getParent()).removeView(browser);
            browser.destroy();
            browser = null;
        }
        filesWorker.shutdown();
        super.onDestroy();
    }
}
