package com.mundox.lab;

import android.app.Activity;
import android.graphics.Color;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Debug;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.Enumeration;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class SyncedGpuActivity extends Activity {
    private static final int PORT = 7777;

    private final ExecutorService net = Executors.newCachedThreadPool();

    private TextView stats;
    private EditText hostField;
    private GLSurfaceView glView;
    private SceneRenderer renderer;

    private volatile ServerSocket serverSocket;
    private volatile Socket socket;
    private volatile BufferedWriter writer;
    private volatile boolean connected = false;
    private volatile boolean hosting = false;
    private volatile long txBytes = 0;
    private volatile long rxBytes = 0;
    private volatile String peer = "-";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);

        LinearLayout netRow = new LinearLayout(this);
        netRow.setOrientation(LinearLayout.HORIZONTAL);

        hostField = new EditText(this);
        hostField.setHint("Host IP");
        hostField.setSingleLine(true);
        hostField.setTextColor(Color.WHITE);
        hostField.setHintTextColor(Color.GRAY);
        netRow.addView(hostField, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.6f));

        Button host = new Button(this);
        host.setText("HOST");
        host.setOnClickListener(v -> host());
        netRow.addView(host, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button join = new Button(this);
        join.setText("JOIN");
        join.setOnClickListener(v -> join());
        netRow.addView(join, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button stop = new Button(this);
        stop.setText("STOP");
        stop.setOnClickListener(v -> disconnect());
        netRow.addView(stop, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        root.addView(netRow);

        stats = new TextView(this);
        stats.setTextColor(Color.WHITE);
        stats.setTextSize(10);
        stats.setTypeface(android.graphics.Typeface.MONOSPACE);
        stats.setPadding(10, 4, 10, 4);
        stats.setText("Two-phone sync ready. Same Wi-Fi recommended.");
        root.addView(stats, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        glView = new GLSurfaceView(this);
        glView.setEGLContextClientVersion(3);
        glView.setPreserveEGLContextOnPause(true);
        renderer = new SceneRenderer();
        glView.setRenderer(renderer);
        glView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);

        glView.setOnTouchListener((v, e) -> {
            if (e.getAction() == MotionEvent.ACTION_DOWN ||
                    e.getAction() == MotionEvent.ACTION_MOVE) {
                float tx = clamp01(e.getX() / Math.max(1f, glView.getWidth()));
                float ty = clamp01(e.getY() / Math.max(1f, glView.getHeight()));

                renderer.touchX = tx;
                renderer.touchY = ty;
                renderer.feedback = Math.min(4f, renderer.feedback + 0.9f);

                int qx = Math.min(7, Math.max(0, (int) (tx * 8f)));
                int qy = Math.min(3, Math.max(0, (int) (ty * 4f)));
                int q = qy * 8 + qx;
                renderer.pendingTouchQubit.set(q);

                send(String.format(Locale.US, "T %.5f %.5f %d", tx, ty, q));
                return true;
            }
            return true;
        });

        root.addView(glView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);

        Button b6 = new Button(this);
        b6.setText("6000");
        b6.setOnClickListener(v -> setCountAndSend(6000));
        controls.addView(b6, weighted());

        Button b24 = new Button(this);
        b24.setText("24000");
        b24.setOnClickListener(v -> setCountAndSend(24000));
        controls.addView(b24, weighted());

        Button b96 = new Button(this);
        b96.setText("96000");
        b96.setOnClickListener(v -> setCountAndSend(96000));
        controls.addView(b96, weighted());

        Button reset = new Button(this);
        reset.setText("RESET");
        reset.setOnClickListener(v -> {
            renderer.requestReset();
            send("R");
        });
        controls.addView(reset, weighted());

        root.addView(controls);
        setContentView(root);

        String local = localIpv4();
        if (local != null) hostField.setHint("Host IP; this phone=" + local);
    }

    private LinearLayout.LayoutParams weighted() {
        return new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
    }

    private float clamp01(float x) {
        return Math.max(0f, Math.min(1f, x));
    }

    private void setCountAndSend(int n) {
        renderer.setCount(n);
        send("C " + n);
    }

    private void host() {
        disconnect();
        hosting = true;
        final String ip = localIpv4();
        stats.setText("HOSTING " + (ip == null ? "unknown-ip" : ip) + ":" + PORT +
                "\nWaiting for phone B...");

        net.submit(() -> {
            try {
                serverSocket = new ServerSocket(PORT);
                Socket s = serverSocket.accept();
                attach(s);
            } catch (Throwable t) {
                if (hosting) ui("HOST error: " + t.getClass().getSimpleName() +
                        " " + String.valueOf(t.getMessage()));
            }
        });
    }

    private void join() {
        disconnect();
        hosting = false;
        final String h = hostField.getText().toString().trim();
        if (h.isEmpty()) {
            stats.setText("Enter phone A IP first.");
            return;
        }

        stats.setText("JOINING " + h + ":" + PORT + "...");
        net.submit(() -> {
            try {
                Socket s = new Socket(h, PORT);
                attach(s);
            } catch (Throwable t) {
                ui("JOIN error: " + t.getClass().getSimpleName() +
                        " " + String.valueOf(t.getMessage()));
            }
        });
    }

    private void attach(Socket s) throws Exception {
        s.setTcpNoDelay(true);
        socket = s;
        writer = new BufferedWriter(new OutputStreamWriter(
                s.getOutputStream(), StandardCharsets.UTF_8));
        connected = true;
        peer = String.valueOf(s.getInetAddress().getHostAddress());
        ui("CONNECTED peer=" + peer + "\nTouch either phone.");

        BufferedReader reader = new BufferedReader(new InputStreamReader(
                s.getInputStream(), StandardCharsets.UTF_8));
        String line;
        while (connected && (line = reader.readLine()) != null) {
            rxBytes += line.getBytes(StandardCharsets.UTF_8).length + 1;
            applyRemote(line);
        }

        disconnectSocketOnly();
        ui("DISCONNECTED");
    }

    private void applyRemote(String line) {
        try {
            String[] p = line.trim().split("\\s+");
            if (p.length == 0) return;

            if ("T".equals(p[0]) && p.length >= 4) {
                float x = Float.parseFloat(p[1]);
                float y = Float.parseFloat(p[2]);
                int q = Integer.parseInt(p[3]);

                renderer.touchX = clamp01(x);
                renderer.touchY = clamp01(y);
                renderer.feedback = Math.min(4f, renderer.feedback + 0.9f);
                renderer.pendingTouchQubit.set(Math.max(0, Math.min(31, q)));
            } else if ("C".equals(p[0]) && p.length >= 2) {
                int n = Integer.parseInt(p[1]);
                renderer.setCount(Math.max(100, Math.min(200000, n)));
            } else if ("R".equals(p[0])) {
                renderer.requestReset();
            }
        } catch (Throwable ignored) {}
    }

    private void send(String line) {
        BufferedWriter w = writer;
        if (!connected || w == null) return;

        net.submit(() -> {
            try {
                synchronized (SyncedGpuActivity.this) {
                    BufferedWriter current = writer;
                    if (current == null) return;
                    current.write(line);
                    current.write("\n");
                    current.flush();
                    txBytes += line.getBytes(StandardCharsets.UTF_8).length + 1;
                }
            } catch (Throwable ignored) {}
        });
    }

    private void ui(String s) {
        runOnUiThread(() -> stats.setText(s));
    }

    private void disconnect() {
        hosting = false;
        connected = false;

        try {
            if (serverSocket != null) serverSocket.close();
        } catch (Throwable ignored) {}
        serverSocket = null;

        disconnectSocketOnly();
    }

    private synchronized void disconnectSocketOnly() {
        connected = false;
        try {
            if (socket != null) socket.close();
        } catch (Throwable ignored) {}
        socket = null;
        writer = null;
        peer = "-";
    }

    private String localIpv4() {
        try {
            Enumeration<NetworkInterface> ifaces = NetworkInterface.getNetworkInterfaces();
            for (NetworkInterface ni : Collections.list(ifaces)) {
                if (!ni.isUp() || ni.isLoopback()) continue;
                for (InetAddress a : Collections.list(ni.getInetAddresses())) {
                    if (a instanceof Inet4Address && !a.isLoopbackAddress()) {
                        String ip = a.getHostAddress();
                        if (ip != null && (ip.startsWith("192.168.") ||
                                ip.startsWith("10.") ||
                                ip.startsWith("172."))) {
                            return ip;
                        }
                    }
                }
            }
        } catch (Throwable ignored) {}
        return null;
    }

    @Override
    protected void onPause() {
        glView.onPause();
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (glView != null) glView.onResume();
    }

    @Override
    protected void onDestroy() {
        disconnect();
        net.shutdownNow();
        super.onDestroy();
    }

    private final class SceneRenderer implements GLSurfaceView.Renderer {
        private int program;
        private int vao;
        private int uTime;
        private int uTouch;
        private int uFeedback;
        private int uQ;
        private int uAspect;

        private volatile int count = 96000;
        volatile float touchX = 0.5f;
        volatile float touchY = 0.5f;
        volatile float feedback = 0f;
        final AtomicInteger pendingTouchQubit = new AtomicInteger(-1);
        private volatile boolean resetRequested = true;

        private long startNs;
        private long previousFrameNs;
        private long statsNs;
        private float fpsSmoothed;
        private float frameMsSmoothed;
        private float[] q = new float[10];

        private final String vertexShader =
                "#version 300 es\n" +
                "precision highp float; precision highp int;\n" +
                "uniform float uTime; uniform vec2 uTouch; uniform float uFeedback;\n" +
                "uniform float uQ[8]; uniform float uAspect;\n" +
                "out vec4 vColor; flat out int vShape;\n" +
                "uint h32(uint x){x^=x>>16u;x*=0x7feb352du;x^=x>>15u;x*=0x846ca68bu;x^=x>>16u;return x;}\n" +
                "float rnd(uint x){return float(h32(x)&0x00ffffffu)/16777215.0;}\n" +
                "vec3 hsv2rgb(vec3 c){vec3 p=abs(fract(c.xxx+vec3(0.0,2.0/3.0,1.0/3.0))*6.0-3.0);return c.z*mix(vec3(1.0),clamp(p-1.0,0.0,1.0),c.y);}\n" +
                "void main(){\n" +
                "uint id=uint(gl_VertexID);float a=rnd(id*11u+1u);float b=rnd(id*11u+2u);\n" +
                "float p=rnd(id*11u+5u)*6.2831853+uQ[3]*6.2831853+uQ[7]*3.1415926;\n" +
                "float f=(0.35+rnd(id*11u+3u)*2.8)*(0.75+uQ[0]*5.0+uQ[6]*2.0);\n" +
                "float f2=(0.35+rnd(id*11u+4u)*2.8)*(0.9+uQ[1]*2.0);\n" +
                "vec2 base=fract(vec2(a,b)+0.05*vec2(sin(uTime*f+p),cos(uTime*f2+p*0.73)));\n" +
                "vec2 d=base-uTouch;float radial=length(d);\n" +
                "float wave=sin(radial*(26.0+24.0*uQ[6])-uTime*(3.0+5.0*uQ[0])+uFeedback*0.24+p);\n" +
                "float dg=0.65+uQ[1]*2.5+uQ[5]*0.7;float z=0.20+min(0.80,dg*0.80*(0.5+0.5*sin(p+uTime*0.7+wave)));\n" +
                "float lens=0.72+0.30*z;float warp=1.0+uQ[2]*6.0+uQ[6]*4.0;\n" +
                "vec2 pos=(base-0.5)*lens+0.5+d*wave*0.025*uFeedback*warp;\n" +
                "vec2 ndc=pos*2.0-1.0;ndc.x/=max(1.0,uAspect*0.58);gl_Position=vec4(ndc,0.0,1.0);\n" +
                "gl_PointSize=2.0+8.0*z*(0.8+uQ[5]*0.8);\n" +
                "float hue=fract(a*0.71+b*0.37+uTime*(0.018+uQ[0]*0.10)+wave*(0.02+uQ[1]*0.10)+uQ[7]+uQ[4]*0.25);\n" +
                "vColor=vec4(hsv2rgb(vec3(hue,0.78,1.0)),0.22+0.72*clamp(z,0.0,1.0));vShape=int(h32(id*11u+6u)%3u);}\n";

        private final String fragmentShader =
                "#version 300 es\nprecision mediump float;in vec4 vColor;flat in int vShape;out vec4 fragColor;\n" +
                "void main(){vec2 p=gl_PointCoord*2.0-1.0;if(vShape==0&&dot(p,p)>1.0)discard;" +
                "if(vShape==2&&abs(p.x)>0.28&&abs(p.y)>0.28)discard;fragColor=vColor;}\n";

        void setCount(int n) {
            count = n;
        }

        void requestReset() {
            resetRequested = true;
            feedback = 0f;
            touchX = 0.5f;
            touchY = 0.5f;
            pendingTouchQubit.set(-1);
            startNs = System.nanoTime();
        }

        @Override
        public void onSurfaceCreated(GL10 unused, EGLConfig config) {
            program = linkProgram(vertexShader, fragmentShader);
            GLES30.glUseProgram(program);
            uTime = GLES30.glGetUniformLocation(program, "uTime");
            uTouch = GLES30.glGetUniformLocation(program, "uTouch");
            uFeedback = GLES30.glGetUniformLocation(program, "uFeedback");
            uQ = GLES30.glGetUniformLocation(program, "uQ");
            uAspect = GLES30.glGetUniformLocation(program, "uAspect");

            int[] vaos = new int[1];
            GLES30.glGenVertexArrays(1, vaos, 0);
            vao = vaos[0];
            GLES30.glBindVertexArray(vao);

            GLES30.glDisable(GLES30.GL_DEPTH_TEST);
            GLES30.glEnable(GLES30.GL_BLEND);
            GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA, GLES30.GL_ONE_MINUS_SRC_ALPHA);
            GLES30.glClearColor(0.01f, 0.02f, 0.05f, 1f);

            startNs = System.nanoTime();
            statsNs = startNs;
            q = NativeCore.quantumVisualStep(-1, true);
        }

        @Override
        public void onSurfaceChanged(GL10 unused, int width, int height) {
            GLES30.glViewport(0, 0, width, height);
            GLES30.glUseProgram(program);
            GLES30.glUniform1f(uAspect, width / (float)Math.max(1, height));
        }

        @Override
        public void onDrawFrame(GL10 unused) {
            long now = System.nanoTime();

            if (previousFrameNs != 0) {
                float ms = (now - previousFrameNs) / 1_000_000f;
                float fps = ms > 0 ? 1000f / ms : 0f;
                frameMsSmoothed = frameMsSmoothed == 0 ? ms :
                        frameMsSmoothed * 0.90f + ms * 0.10f;
                fpsSmoothed = fpsSmoothed == 0 ? fps :
                        fpsSmoothed * 0.90f + fps * 0.10f;
            }
            previousFrameNs = now;

            if (resetRequested) {
                q = NativeCore.quantumVisualStep(-1, true);
                resetRequested = false;
            } else {
                q = NativeCore.quantumVisualStep(
                        pendingTouchQubit.getAndSet(-1), false);
            }

            feedback *= 0.985f;
            float time = (now - startNs) / 1_000_000_000f;

            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT);
            GLES30.glUseProgram(program);
            GLES30.glBindVertexArray(vao);
            GLES30.glUniform1f(uTime, time);
            GLES30.glUniform2f(uTouch, touchX, 1f - touchY);
            GLES30.glUniform1f(uFeedback, feedback);
            if (q != null && q.length >= 8) GLES30.glUniform1fv(uQ, 8, q, 0);
            GLES30.glDrawArrays(GLES30.GL_POINTS, 0, count);

            if (now - statsNs >= 300_000_000L) {
                statsNs = now;
                final float fps = fpsSmoothed;
                final float ms = frameMsSmoothed;
                final int n = count;
                final long pss = Debug.getPss();
                final String role = connected ? (hosting ? "HOST" : "JOIN") : "OFF";
                final String p = peer;
                final long tx = txBytes;
                final long rx = rxBytes;
                final float fb = feedback;

                runOnUiThread(() -> stats.setText(String.format(Locale.US,
                        "sync=%s peer=%s objects=%d fps=%.1f frame=%.2fms\n" +
                        "tx=%dB rx=%dB logical_state=608B PSS=%dkB\n" +
                        "touch either phone -> send tiny event -> other world reacts\n" +
                        "feedback=%.3f",
                        role, p, n, fps, ms, tx, rx, pss, fb)));
            }
        }

        private int compileShader(int type, String source) {
            int s = GLES30.glCreateShader(type);
            GLES30.glShaderSource(s, source);
            GLES30.glCompileShader(s);
            int[] ok = new int[1];
            GLES30.glGetShaderiv(s, GLES30.GL_COMPILE_STATUS, ok, 0);
            if (ok[0] == 0) {
                String log = GLES30.glGetShaderInfoLog(s);
                GLES30.glDeleteShader(s);
                throw new RuntimeException(log);
            }
            return s;
        }

        private int linkProgram(String vsSource, String fsSource) {
            int vs = compileShader(GLES30.GL_VERTEX_SHADER, vsSource);
            int fs = compileShader(GLES30.GL_FRAGMENT_SHADER, fsSource);
            int p = GLES30.glCreateProgram();
            GLES30.glAttachShader(p, vs);
            GLES30.glAttachShader(p, fs);
            GLES30.glLinkProgram(p);
            int[] ok = new int[1];
            GLES30.glGetProgramiv(p, GLES30.GL_LINK_STATUS, ok, 0);
            GLES30.glDeleteShader(vs);
            GLES30.glDeleteShader(fs);
            if (ok[0] == 0) {
                String log = GLES30.glGetProgramInfoLog(p);
                GLES30.glDeleteProgram(p);
                throw new RuntimeException(log);
            }
            return p;
        }
    }
}
