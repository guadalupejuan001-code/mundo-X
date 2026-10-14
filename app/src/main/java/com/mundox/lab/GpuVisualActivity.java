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
import android.widget.LinearLayout;
import android.widget.TextView;

import java.nio.IntBuffer;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GpuVisualActivity extends Activity {
    private TextView stats;
    private GLSurfaceView glView;
    private SceneRenderer renderer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);

        stats = new TextView(this);
        stats.setTextColor(Color.WHITE);
        stats.setTextSize(11);
        stats.setTypeface(android.graphics.Typeface.MONOSPACE);
        stats.setPadding(12, 8, 12, 8);
        stats.setText("GPU path starting...\nCompact Core -> uniforms -> 1 GPU draw call");
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
                float tx = e.getX() / Math.max(1f, e.getSource() == 0 ? glView.getWidth() : glView.getWidth());
                float ty = e.getY() / Math.max(1f, glView.getHeight());
                tx = clamp01(tx);
                ty = clamp01(ty);
                renderer.touchX = tx;
                renderer.touchY = ty;
                renderer.feedback = Math.min(4f, renderer.feedback + 0.9f);

                int qx = Math.min(7, Math.max(0, (int) (tx * 8f)));
                int qy = Math.min(3, Math.max(0, (int) (ty * 4f)));
                renderer.pendingTouchQubit.set(qy * 8 + qx);
                return true;
            }
            return true;
        });

        root.addView(glView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);

        Button normal = new Button(this);
        normal.setText("6000");
        normal.setOnClickListener(v -> renderer.setCount(6000));
        controls.addView(normal, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button stress = new Button(this);
        stress.setText("STRESS 24000");
        stress.setOnClickListener(v -> renderer.setCount(24000));
        controls.addView(stress, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button extreme = new Button(this);
        extreme.setText("96000");
        extreme.setOnClickListener(v -> renderer.setCount(96000));
        controls.addView(extreme, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button reset = new Button(this);
        reset.setText("RESET");
        reset.setOnClickListener(v -> renderer.requestReset());
        controls.addView(reset, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        root.addView(controls);
        setContentView(root);
    }

    private float clamp01(float x) {
        return Math.max(0f, Math.min(1f, x));
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

    private final class SceneRenderer implements GLSurfaceView.Renderer {
        private int program;
        private int vao;

        private int uTime;
        private int uTouch;
        private int uFeedback;
        private int uQ;
        private int uAspect;

        private volatile int count = 6000;
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
                "precision highp float;\n" +
                "precision highp int;\n" +
                "uniform float uTime;\n" +
                "uniform vec2 uTouch;\n" +
                "uniform float uFeedback;\n" +
                "uniform float uQ[8];\n" +
                "uniform float uAspect;\n" +
                "out vec4 vColor;\n" +
                "flat out int vShape;\n" +
                "uint h32(uint x){\n" +
                "  x ^= x >> 16u; x *= 0x7feb352du;\n" +
                "  x ^= x >> 15u; x *= 0x846ca68bu;\n" +
                "  x ^= x >> 16u; return x;\n" +
                "}\n" +
                "float rnd(uint x){ return float(h32(x) & 0x00ffffffu) / 16777215.0; }\n" +
                "vec3 hsv2rgb(vec3 c){\n" +
                "  vec3 p=abs(fract(c.xxx+vec3(0.0,2.0/3.0,1.0/3.0))*6.0-3.0);\n" +
                "  return c.z*mix(vec3(1.0),clamp(p-1.0,0.0,1.0),c.y);\n" +
                "}\n" +
                "void main(){\n" +
                "  uint id=uint(gl_VertexID);\n" +
                "  float a=rnd(id*11u+1u);\n" +
                "  float b=rnd(id*11u+2u);\n" +
                "  float p=rnd(id*11u+5u)*6.2831853 + uQ[3]*6.2831853 + uQ[7]*3.1415926;\n" +
                "  float freq=(0.35+rnd(id*11u+3u)*2.8)*(0.75+uQ[0]*5.0+uQ[6]*2.0);\n" +
                "  float freq2=(0.35+rnd(id*11u+4u)*2.8)*(0.9+uQ[1]*2.0);\n" +
                "  vec2 base=fract(vec2(a,b)+0.05*vec2(sin(uTime*freq+p),cos(uTime*freq2+p*0.73)));\n" +
                "  vec2 d=base-uTouch;\n" +
                "  float radial=length(d);\n" +
                "  float wave=sin(radial*(26.0+24.0*uQ[6])-uTime*(3.0+5.0*uQ[0])+uFeedback*0.24+p);\n" +
                "  float depthGain=0.65+uQ[1]*2.5+uQ[5]*0.7;\n" +
                "  float z=0.20+min(0.80,depthGain*0.80*(0.5+0.5*sin(p+uTime*0.7+wave)));\n" +
                "  float lens=0.72+0.30*z;\n" +
                "  float warp=1.0+uQ[2]*6.0+uQ[6]*4.0;\n" +
                "  vec2 pos=(base-0.5)*lens+0.5+d*wave*0.025*uFeedback*warp;\n" +
                "  vec2 ndc=pos*2.0-1.0;\n" +
                "  ndc.x/=max(1.0,uAspect*0.58);\n" +
                "  gl_Position=vec4(ndc,0.0,1.0);\n" +
                "  gl_PointSize=2.0+8.0*z*(0.8+uQ[5]*0.8);\n" +
                "  float hue=fract(a*0.71+b*0.37+uTime*(0.018+uQ[0]*0.10)+wave*(0.02+uQ[1]*0.10)+uQ[7]+uQ[4]*0.25);\n" +
                "  vColor=vec4(hsv2rgb(vec3(hue,0.78,1.0)),0.22+0.72*clamp(z,0.0,1.0));\n" +
                "  vShape=int(h32(id*11u+6u)%3u);\n" +
                "}\n";

        private final String fragmentShader =
                "#version 300 es\n" +
                "precision mediump float;\n" +
                "in vec4 vColor;\n" +
                "flat in int vShape;\n" +
                "out vec4 fragColor;\n" +
                "void main(){\n" +
                "  vec2 p=gl_PointCoord*2.0-1.0;\n" +
                "  if(vShape==0){ if(dot(p,p)>1.0) discard; }\n" +
                "  else if(vShape==2){ if(abs(p.x)>0.28 && abs(p.y)>0.28) discard; }\n" +
                "  fragColor=vColor;\n" +
                "}\n";

        void setCount(int newCount) {
            count = newCount;
            previousFrameNs = 0;
            fpsSmoothed = 0f;
            frameMsSmoothed = 0f;
        }

        void requestReset() {
            resetRequested = true;
            feedback = 0f;
            touchX = 0.5f;
            touchY = 0.5f;
            pendingTouchQubit.set(-1);
            startNs = System.nanoTime();
            previousFrameNs = 0;
            fpsSmoothed = 0f;
            frameMsSmoothed = 0f;
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
            float aspect = width / (float) Math.max(1, height);
            GLES30.glUseProgram(program);
            GLES30.glUniform1f(uAspect, aspect);
        }

        @Override
        public void onDrawFrame(GL10 unused) {
            long now = System.nanoTime();
            if (previousFrameNs != 0) {
                float ms = (now - previousFrameNs) / 1_000_000f;
                float fps = ms > 0f ? 1000f / ms : 0f;
                frameMsSmoothed = frameMsSmoothed == 0f ? ms :
                        frameMsSmoothed * 0.90f + ms * 0.10f;
                fpsSmoothed = fpsSmoothed == 0f ? fps :
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
            if (q != null && q.length >= 8) {
                GLES30.glUniform1fv(uQ, 8, q, 0);
            }
            GLES30.glDrawArrays(GLES30.GL_POINTS, 0, count);

            if (now - statsNs >= 250_000_000L) {
                statsNs = now;
                final float showFps = fpsSmoothed;
                final float showMs = frameMsSmoothed;
                final int showCount = count;
                final float[] showQ = q == null ? null : q.clone();
                final long pssKb = Debug.getPss();
                final float showFeedback = feedback;
                final float tx = touchX;
                final float ty = touchY;

                runOnUiThread(() -> {
                    long coreBytes = showQ != null && showQ.length > 8
                            ? Math.round(showQ[8]) : 544L;
                    long logicalBytes = coreBytes + 64L;
                    float step = showQ != null && showQ.length > 9 ? showQ[9] : 0f;

                    StringBuilder out = new StringBuilder();
                    out.append(String.format(Locale.US,
                            "mode=COMPACT->GPU objects=%d fps=%.1f frame=%.2f ms\n",
                            showCount, showFps, showMs));
                    out.append(String.format(Locale.US,
                            "draw_calls/frame=1 logical_state=%d B process_PSS=%d kB\n",
                            logicalBytes, pssKb));
                    out.append(String.format(Locale.US,
                            "CompactCore=32q core_bytes=%d step=%.0f\n",
                            coreBytes, step));
                    if (showQ != null && showQ.length >= 8) {
                        out.append(String.format(Locale.US,
                                "X=%.3f Z=%.3f Y=%.3f phase=%.3f corr=%.3f hash=%.3f\n",
                                showQ[0], showQ[1], showQ[2], showQ[3],
                                showQ[6], showQ[7]));
                    }
                    out.append(String.format(Locale.US,
                            "CPU sends state/time/touch -> GPU generates all points\nfeedback=%.3f touch=(%.2f, %.2f)",
                            showFeedback, tx, ty));
                    stats.setText(out.toString());
                });
            }
        }

        private int compileShader(int type, String source) {
            int shader = GLES30.glCreateShader(type);
            GLES30.glShaderSource(shader, source);
            GLES30.glCompileShader(shader);
            int[] ok = new int[1];
            GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, ok, 0);
            if (ok[0] == 0) {
                String log = GLES30.glGetShaderInfoLog(shader);
                GLES30.glDeleteShader(shader);
                throw new RuntimeException("Shader compile failed: " + log);
            }
            return shader;
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
                throw new RuntimeException("Program link failed: " + log);
            }
            return p;
        }
    }
}
