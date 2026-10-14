package com.mundox.lab;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Debug;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.HttpURLConnection;
import java.net.Socket;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class InternetSyncActivity extends Activity {
    private final ExecutorService io = Executors.newSingleThreadExecutor();

    private EditText host;
    private EditText port;
    private EditText room;
    private EditText name;
    private TextView output;

    private volatile Socket socket;
    private volatile BufferedWriter writer;
    private volatile boolean connected = false;
    private volatile long sentBytes = 0;
    private volatile long receivedBytes = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ScrollView scroll = new ScrollView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(24, 24, 24, 40);
        root.setBackgroundColor(Color.rgb(6, 8, 12));
        scroll.addView(root);

        root.addView(text("MUNDO X INTERNET", 26, true));
        root.addView(text(
                "Experimental state sync: send changes/state, not the rendered world.",
                13, false));

        host = field("Relay host or IP", "");
        port = field("Port", "7777");
        room = field("Room", "mundo-x");
        name = field("Device name", "android");

        root.addView(host);
        root.addView(port);
        root.addView(room);
        root.addView(name);

        Button testInternet = button("TEST INTERNET");
        testInternet.setOnClickListener(v -> testInternet());
        root.addView(testInternet);

        Button connect = button("CONNECT TO RELAY");
        connect.setOnClickListener(v -> connectRelay());
        root.addView(connect);

        Button sendState = button("SEND COMPACT STATE");
        sendState.setOnClickListener(v -> sendCompactState());
        root.addView(sendState);

        Button sendTouch = button("SEND TOUCH 17");
        sendTouch.setOnClickListener(v -> sendLine("EVENT touch qubit=17"));
        root.addView(sendTouch);

        Button disconnect = button("DISCONNECT");
        disconnect.setOnClickListener(v -> disconnect());
        root.addView(disconnect);

        output = text("Ready.\n", 12, false);
        output.setTypeface(android.graphics.Typeface.MONOSPACE);
        output.setTextIsSelectable(true);
        root.addView(output, fullWidth());

        setContentView(scroll);
    }

    private EditText field(String hint, String value) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setText(value);
        e.setSingleLine(true);
        e.setTextColor(Color.WHITE);
        e.setHintTextColor(Color.GRAY);
        e.setBackgroundColor(Color.rgb(16, 19, 25));
        e.setPadding(14, 8, 14, 8);
        LinearLayout.LayoutParams p = fullWidth();
        p.setMargins(0, 8, 0, 0);
        e.setLayoutParams(p);
        return e;
    }

    private void testInternet() {
        append("\n[TEST] Internet connectivity...\n");
        io.submit(() -> {
            HttpURLConnection c = null;
            try {
                URL url = new URL("https://www.google.com/generate_204");
                c = (HttpURLConnection) url.openConnection();
                c.setConnectTimeout(4000);
                c.setReadTimeout(4000);
                c.setInstanceFollowRedirects(false);
                c.setRequestMethod("GET");
                int code = c.getResponseCode();
                appendUi("internet=PASS http=" + code + "\n");
            } catch (Throwable t) {
                appendUi("internet=FAIL " + t.getClass().getSimpleName() + ": " +
                        String.valueOf(t.getMessage()) + "\n");
            } finally {
                if (c != null) c.disconnect();
            }
        });
    }

    private void connectRelay() {
        final String h = host.getText().toString().trim();
        final String roomName = room.getText().toString().trim();
        final String deviceName = name.getText().toString().trim();
        final int p;

        if (h.isEmpty()) {
            append("\nEnter a relay host/IP first.\n");
            return;
        }

        try {
            p = Integer.parseInt(port.getText().toString().trim());
        } catch (Exception e) {
            append("\nInvalid port.\n");
            return;
        }

        disconnect();
        append("\n[CONNECTING] " + h + ":" + p + " room=" + roomName + "\n");

        io.submit(() -> {
            try {
                Socket s = new Socket(h, p);
                s.setTcpNoDelay(true);
                BufferedWriter w = new BufferedWriter(new OutputStreamWriter(
                        s.getOutputStream(), StandardCharsets.UTF_8));
                BufferedReader r = new BufferedReader(new InputStreamReader(
                        s.getInputStream(), StandardCharsets.UTF_8));

                socket = s;
                writer = w;
                connected = true;

                String join = "JOIN " + sanitize(roomName) + " " + sanitize(deviceName);
                writeRaw(join);

                appendUi("connected=YES\n");

                String line;
                while (connected && (line = r.readLine()) != null) {
                    receivedBytes += line.getBytes(StandardCharsets.UTF_8).length + 1;
                    appendUi("RX " + line + "\n" + counters());
                }
            } catch (Throwable t) {
                if (connected || socket == null) {
                    appendUi("connection=FAIL " + t.getClass().getSimpleName() + ": " +
                            String.valueOf(t.getMessage()) + "\n");
                }
            } finally {
                closeSocket();
                appendUi("connected=NO\n");
            }
        });
    }

    private void sendCompactState() {
        float[] q;
        try {
            q = NativeCore.quantumVisualStep(-1, false);
        } catch (Throwable t) {
            append("\nCompact Core error: " + t + "\n");
            return;
        }

        if (q == null || q.length < 10) {
            append("\nCompact state unavailable.\n");
            return;
        }

        String line = String.format(Locale.US,
                "STATE step=%.0f x=%.4f z=%.4f y=%.4f phase=%.4f diagx=%.4f diagz=%.4f corr=%.4f hash=%.4f bytes=%.0f",
                q[9], q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8]);
        sendLine(line);
    }

    private void sendLine(String line) {
        if (!connected || writer == null) {
            append("\nNot connected.\n");
            return;
        }
        io.submit(() -> {
            try {
                writeRaw(line);
                appendUi("TX " + line + "\n" + counters());
            } catch (Throwable t) {
                appendUi("send=FAIL " + t + "\n");
            }
        });
    }

    private void writeRaw(String line) throws Exception {
        BufferedWriter w = writer;
        if (w == null) throw new IllegalStateException("writer unavailable");
        w.write(line);
        w.write("\n");
        w.flush();
        sentBytes += line.getBytes(StandardCharsets.UTF_8).length + 1;
    }

    private String counters() {
        return "traffic tx=" + sentBytes + " B rx=" + receivedBytes +
                " B processPSS=" + Debug.getPss() + " kB\n";
    }

    private String sanitize(String s) {
        if (s == null) return "x";
        return s.replaceAll("[^A-Za-z0-9_.-]", "_");
    }

    private void disconnect() {
        connected = false;
        closeSocket();
    }

    private synchronized void closeSocket() {
        try {
            if (socket != null) socket.close();
        } catch (Throwable ignored) {}
        socket = null;
        writer = null;
    }

    private void append(String s) {
        output.append(s);
    }

    private void appendUi(String s) {
        runOnUiThread(() -> append(s));
    }

    private Button button(String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        LinearLayout.LayoutParams p = fullWidth();
        p.setMargins(0, 10, 0, 0);
        b.setLayoutParams(p);
        return b;
    }

    private TextView text(String value, int sp, boolean bold) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextColor(Color.WHITE);
        t.setTextSize(sp);
        if (bold) t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        t.setPadding(0, 6, 0, 6);
        return t;
    }

    private LinearLayout.LayoutParams fullWidth() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    @Override
    protected void onDestroy() {
        disconnect();
        io.shutdownNow();
        super.onDestroy();
    }
}
