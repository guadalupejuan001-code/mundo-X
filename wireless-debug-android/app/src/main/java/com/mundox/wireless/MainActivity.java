package com.mundox.wireless;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.text.InputType;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipFile;

/**
 * Standalone Samsung → Galaxy Watch4 Classic wireless debugging application.
 * The user explicitly authorizes pairing, connecting, and installing an APK.
 * It cannot silently enable Wireless Debugging on the watch.
 */
public final class MainActivity extends Activity {
    private static final int PICK_APK = 314;
    private static final long MAX_APK_SIZE = 150L * 1024L * 1024L;
    private static final int BG = 0xff101915, PANEL = 0xff1e2d25, PAPER = 0xfff1f4ed;
    private static final int GREEN = 0xffd7ed76, MUTED = 0xffb6c6b6;
    private final ExecutorService background = Executors.newSingleThreadExecutor();
    private EditText ip, pairingPort, connectionPort, pairingCode;
    private TextView status, selectedApk;
    private Button pairButton, connectButton, installBuiltinButton, pickButton, installFileButton, disconnectButton;
    private AdbIdentity adb;
    private File staged;
    private String device = "";
    private boolean busy, destroyed;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        buildInterface();
    }

    private void buildInterface() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18),dp(20),dp(18),dp(22));
        root.setBackgroundColor(BG);
        if (Build.VERSION.SDK_INT >= 30) {
            root.setOnApplyWindowInsetsListener((view,insets) -> {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
                view.setPadding(dp(18)+bars.left, dp(20)+bars.top,
                        dp(18)+bars.right, dp(22)+bars.bottom);
                return insets;
            });
        }

        TextView title = text("MUNDO X",26,GREEN,true);
        root.addView(title);
        root.addView(text("DEPURACIÓN INALÁMBRICA",16,PAPER,true));
        TextView explain = text("Instala apps en tu Galaxy Watch4 Classic directamente desde tu Samsung. Sin computadora ni cable.",13,MUTED,false);
        explain.setPadding(0,dp(4),0,dp(12));
        root.addView(explain);

        LinearLayout pair = panel(root,"1 · EMPAREJAR", "En el reloj: Ajustes → Opciones de desarrollador → Depuración inalámbrica → Vincular dispositivo con código.");
        SharedPreferences prefs = getSharedPreferences("watch-endpoint",MODE_PRIVATE);
        ip = entry(pair,"IP del reloj (ej. 192.168.1.50)",prefs.getString("ip",""),false);
        pairingPort = entry(pair,"Puerto de VINCULACIÓN (ventana del código)","",true);
        pairingCode = entry(pair,"Código de 6 dígitos","",true);
        pairingCode.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_VARIATION_PASSWORD);
        pairButton = action(pair,"Vincular con código",() -> onPair());

        LinearLayout connect = panel(root,"2 · CONECTAR","Cierra la ventana del código en el reloj. Mira el OTRO puerto que aparece en la pantalla principal de Depuración inalámbrica.");
        connectionPort = entry(connect,"Puerto de CONEXIÓN (diferente al anterior)",prefs.getString("connectPort",""),true);
        connectButton = action(connect,"Conectar con el reloj",() -> onConnect());
        disconnectButton = action(connect,"Desconectar",() -> execute("Desconectando...",() -> {
            if (adb != null) adb.disconnect();
            device = "";
            publish("Desconectado. Apaga Depuración inalámbrica en el reloj al terminar.");
        }));

        LinearLayout install = panel(root,"3 · INSTALAR APK","Puedes instalar Mundo X Enlace (el canalizador del reloj) o cualquier APK individual compatible con Wear OS.");
        installBuiltinButton = action(install,"Instalar Mundo X Enlace en el reloj",() -> prepareBuiltIn());
        pickButton = action(install,"Elegir otra APK del teléfono",() -> selectFile());
        selectedApk = text("Ninguna APK externa seleccionada.",12,MUTED,false);
        install.addView(selectedApk);
        installFileButton = action(install,"Instalar APK seleccionada",() -> {
            if (staged == null) { publish("Selecciona primero una APK."); return; }
            confirmInstall(staged,"APK seleccionada");
        });

        LinearLayout feedback = panel(root,"ESTADO","La app conserva la identidad de vinculación en almacenamiento privado. Los comandos van directamente al reloj.");
        status = text("Sin conexión",13,GREEN,false);
        feedback.addView(status);
        TextView footer = text("REQUISITOS · Reloj y teléfono en la misma red Wi-Fi. "
                + "No basta solo con Bluetooth. Al finalizar, desactiva Depuración inalámbrica en el reloj.",11,MUTED,false);
        footer.setPadding(0,dp(8),0,dp(16));
        root.addView(footer);

        scroll.addView(root);
        setContentView(scroll);
        setBusy(false);
    }

    private void onPair() {
        final String host, code;
        final int port;
        try {
            host = validateHost(ip.getText().toString());
            port = parsePort(pairingPort.getText().toString());
            code = pairingCode.getText().toString().trim();
            if (!code.matches("[0-9]{6}")) throw new IllegalArgumentException("Introduce los seis dígitos del reloj");
        } catch (Exception e) { publish(e.getMessage()); return; }
        saveHost(host);
        execute("Vinculando con " + host + ":" + port + "...", () -> {
            AdbIdentity client = identity();
            boolean paired = client.pair(host,port,code);
            publish(paired ? "Vinculación aceptada. Ahora escribe el OTRO puerto y pulsa Conectar."
                    : "El reloj rechazó el código. Verifica IP, puerto y código.");
        });
    }

    private void onConnect() {
        final String host;
        final int port;
        try {
            host = validateHost(ip.getText().toString());
            port = parsePort(connectionPort.getText().toString());
        } catch (Exception e) { publish(e.getMessage()); return; }
        saveHost(host);
        getSharedPreferences("watch-endpoint",MODE_PRIVATE).edit()
                .putString("connectPort",String.valueOf(port)).apply();
        execute("Conectando con el reloj...",() -> {
            AdbIdentity client = identity();
            if (client.isConnected()) client.disconnect();
            boolean connected = client.connect(host,port);
            if (!connected && !client.isConnected()) {
                throw new IllegalStateException("El reloj no autorizó la conexión ADB");
            }
            String model = AdbTransfer.shell(client,"getprop ro.product.model").trim();
            if (model.isEmpty()) model = "dispositivo Android";
            device = model;
            publish("Conectado por ADB con: " + model + "\n"
                    + host + ":" + port + "\n"
                    + "Ya puedes instalar la APK del reloj.");
        });
    }

    private void prepareBuiltIn() {
        execute("Preparando Mundo X Enlace...",() -> {
            try (InputStream input = getAssets().open("worldlink-watch.apk")) {
                File file = new File(getCacheDir(),"worldlink-watch-installer.apk");
                saveApk(input,file);
                publish("APK del Watch4 preparada: " + niceSize(file.length()));
                runOnUiThread(() -> confirmInstall(file,"Mundo X Enlace"));
            } catch (java.io.FileNotFoundException e) {
                throw new IllegalStateException("Esta compilación no contiene la APK del reloj");
            }
        });
    }

    private void selectFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{
                "application/vnd.android.package-archive","application/octet-stream","application/zip"
        });
        startActivityForResult(intent,PICK_APK);
    }

    @Override protected void onActivityResult(int code,int result,Intent data) {
        super.onActivityResult(code,result,data);
        if (code != PICK_APK || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        execute("Leyendo APK seleccionada...",() -> {
            try (InputStream input = getContentResolver().openInputStream(uri)) {
                if (input == null) throw new IllegalStateException("Android no puede abrir ese archivo");
                File file = new File(getCacheDir(),"selected-watch-installer.apk");
                saveApk(input,file);
                staged = file;
                runOnUiThread(() -> selectedApk.setText("Seleccionada: " + niceSize(file.length())));
                publish("APK válida; conecta el reloj e instala cuando quieras.");
            }
        });
    }

    private void confirmInstall(File file,String name) {
        if (destroyed || isFinishing()) return;
        if (adb == null || !adb.isConnected()) {
            publish("Conecta primero al Watch4 mediante el botón Conectar.");
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle("Instalar en el reloj")
                .setMessage(name + "\n" + niceSize(file.length()) + "\nDestino: " + device
                        + "\n\n¿Quieres transferir e instalar esta APK por ADB inalámbrico?")
                .setNegativeButton("Cancelar",null)
                .setPositiveButton("Instalar",(d,which) -> execute("Transfiriendo al reloj...",() -> {
                    if (adb == null || !adb.isConnected()) throw new IllegalStateException("Se perdió ADB");
                    String reply = AdbTransfer.install(adb,file,(sent,total) -> {
                        int percent = (int)((sent * 100L)/Math.max(1L,total));
                        if (percent == 100 || percent % 10 == 0) {
                            publish("Copiando al reloj: " + percent + "%");
                        }
                    });
                    publish("Instalación completada en " + device + ".\n" + reply);
                }))
                .show();
    }

    private synchronized AdbIdentity identity() throws Exception {
        if (adb == null) adb = new AdbIdentity(getApplicationContext());
        return adb;
    }

    private interface Job { void run() throws Exception; }
    private void execute(String message, Job job) {
        if (busy) { publish("Termina la operación actual antes de iniciar otra."); return; }
        setBusy(true);
        publish(message);
        background.execute(() -> {
            try { job.run(); }
            catch (Exception e) {
                publish("Error: " + (e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage())
                        + "\nRevisa Wi-Fi, los dos puertos y el código.");
            } finally {
                runOnUiThread(() -> { if (!destroyed) setBusy(false); });
            }
        });
    }

    private void publish(String message) {
        runOnUiThread(() -> {
            if (!destroyed && status != null) status.setText(message);
        });
    }

    private void setBusy(boolean state) {
        busy = state;
        for (Button b : new Button[]{pairButton,connectButton,installBuiltinButton,pickButton,installFileButton,disconnectButton}) {
            if (b != null) b.setEnabled(!state);
        }
    }

    private String validateHost(String raw) {
        String host = raw.trim();
        if (!host.matches("[0-9]{1,3}(\\.[0-9]{1,3}){3}")) {
            throw new IllegalArgumentException("Escribe la dirección IPv4 que muestra el reloj");
        }
        String[] fields = host.split("\\.");
        for (String field : fields) {
            if (Integer.parseInt(field)>255) throw new IllegalArgumentException("Dirección IP incorrecta");
        }
        return host;
    }
    private int parsePort(String value) {
        try {
            int port = Integer.parseInt(value.trim());
            if (port > 0 && port <= 65535) return port;
        } catch (NumberFormatException ignored) { }
        throw new IllegalArgumentException("Puerto inválido (1 a 65535)");
    }
    private void saveHost(String host) {
        getSharedPreferences("watch-endpoint",MODE_PRIVATE).edit().putString("ip",host).apply();
    }
    private static String niceSize(long bytes) {
        return String.format(java.util.Locale.US,"%.2f MB",bytes / (1024.0*1024.0));
    }

    private static void saveApk(InputStream source, File dest) throws Exception {
        byte[] buffer = new byte[32768];
        long total = 0;
        try (FileOutputStream output = new FileOutputStream(dest)) {
            int n;
            while ((n = source.read(buffer)) != -1) {
                total += n;
                if (total > MAX_APK_SIZE) throw new IllegalArgumentException("APK demasiado grande (>150 MB)");
                output.write(buffer,0,n);
            }
        }
        if (total < 128) throw new IllegalArgumentException("APK incompleta");
        try (ZipFile archive = new ZipFile(dest)) {
            if (archive.getEntry("AndroidManifest.xml") == null) {
                throw new IllegalArgumentException("El archivo no es una APK Android válida");
            }
        }
    }

    private LinearLayout panel(LinearLayout root,String name,String detail) {
        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setPadding(dp(14),dp(14),dp(14),dp(14));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(PANEL); bg.setCornerRadius(dp(15)); bg.setStroke(dp(1),0xff344a3c);
        container.setBackground(bg);
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(-1,-2);
        layout.bottomMargin = dp(12);
        root.addView(container,layout);
        container.addView(text(name,14,GREEN,true));
        TextView desc = text(detail,12,MUTED,false);
        desc.setPadding(0,dp(5),0,dp(10));
        container.addView(desc);
        return container;
    }

    private EditText entry(LinearLayout container,String hint,String initial,boolean numeric) {
        EditText field = new EditText(this);
        field.setSingleLine(true);
        field.setText(initial);
        field.setHint(hint);
        field.setTextSize(15);
        field.setTextColor(PAPER);
        field.setHintTextColor(0xff9dad9e);
        if (numeric) field.setInputType(InputType.TYPE_CLASS_NUMBER);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1,dp(52));
        p.bottomMargin = dp(3);
        container.addView(field,p);
        return field;
    }

    private Button action(LinearLayout container,String name,Runnable task) {
        Button button = new Button(this);
        button.setText(name); button.setAllCaps(false);
        button.setTextColor(PAPER);
        button.setTextSize(13);
        button.setOnClickListener(v -> task.run());
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1,dp(53));
        p.topMargin = dp(5);
        container.addView(button,p);
        return button;
    }
    private TextView text(String value,int size,int color,boolean bold) {
        TextView t = new TextView(this);
        t.setText(value); t.setTextSize(size); t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        return t;
    }
    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override protected void onDestroy() {
        destroyed = true;
        background.execute(() -> {
            try { if (adb != null) adb.disconnect(); }
            catch (Exception ignored) {}
        });
        background.shutdown();
        super.onDestroy();
    }
}
