package com.mundox.lab;

import android.app.Activity;
import android.graphics.Color;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.io.File;
import java.util.Locale;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Tiny procedural graphics experiment.
 * The scene is reconstructed by GLSL at runtime: no bitmap textures,
 * model files, videos, or pre-rendered scene assets are bundled.
 */
public class ProceduralGameDemoActivity extends Activity {
    private GLSurfaceView gl;
    private TextView hud;
    private DemoRenderer renderer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);

        gl = new GLSurfaceView(this);
        gl.setEGLContextClientVersion(3);
        gl.setPreserveEGLContextOnPause(true);
        renderer = new DemoRenderer();
        gl.setRenderer(renderer);
        gl.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        root.addView(gl, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        hud = new TextView(this);
        hud.setTextColor(Color.WHITE);
        hud.setTextSize(10);
        hud.setTypeface(android.graphics.Typeface.MONOSPACE);
        hud.setBackgroundColor(0x72000000);
        hud.setPadding(dp(10), dp(7), dp(10), dp(7));
        hud.setText("PROCEDURAL GAME GRAPHICS TEST\ninitializing...");

        FrameLayout.LayoutParams hp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        hp.gravity = Gravity.TOP | Gravity.START;
        hp.setMargins(dp(10), dp(10), dp(10), dp(10));
        root.addView(hud, hp);

        TextView hint = new TextView(this);
        hint.setTextColor(0xFFE8E8E8);
        hint.setTextSize(10);
        hint.setTypeface(android.graphics.Typeface.MONOSPACE);
        hint.setBackgroundColor(0x66000000);
        hint.setPadding(dp(8), dp(5), dp(8), dp(5));
        hint.setText("DRAG = CAMERA • adaptive internal resolution • no external assets");

        FrameLayout.LayoutParams pp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        pp.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
        pp.setMargins(dp(8), dp(8), dp(8), dp(12));
        root.addView(hint, pp);

        gl.setOnTouchListener((v, e) -> {
            if (e.getAction() == MotionEvent.ACTION_DOWN ||
                    e.getAction() == MotionEvent.ACTION_MOVE) {
                renderer.touchX = e.getX() / Math.max(1f, gl.getWidth());
                renderer.touchY = e.getY() / Math.max(1f, gl.getHeight());
                return true;
            }
            return true;
        });

        setContentView(root);
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private String apkSizeText() {
        try {
            File f = new File(getApplicationInfo().sourceDir);
            return String.format(Locale.US, "%.2f MiB", f.length() / (1024.0 * 1024.0));
        } catch (Throwable t) {
            return "unknown";
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (gl != null) gl.onResume();
    }

    @Override
    protected void onPause() {
        if (gl != null) gl.onPause();
        super.onPause();
    }

    private final class DemoRenderer implements GLSurfaceView.Renderer {
        private int sceneProgram, postProgram, vao;
        private int sceneTime, sceneRes, sceneTouch;
        private int postTex, postTexel;

        private int fbo, colorTex;
        private int width = 1, height = 1, renderW = 1, renderH = 1;
        private float renderScale = 0.72f;

        private long startNs, lastNs, hudNs, scaleDecisionNs;
        private float fpsSmooth = 0f;
        volatile float touchX = 0.5f;
        volatile float touchY = 0.5f;

        private final String VS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "out vec2 vUv;\n" +
                "const vec2 V[3]=vec2[3](vec2(-1.0,-1.0),vec2(3.0,-1.0),vec2(-1.0,3.0));\n" +
                "void main(){vec2 p=V[gl_VertexID];vUv=p*.5+.5;gl_Position=vec4(p,0.0,1.0);}\n";

        private final String SCENE_FS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "uniform vec2 uRes; uniform float uTime; uniform vec2 uTouch;\n" +
                "in vec2 vUv; out vec4 fragColor;\n" +
                "float h1(float n){return fract(sin(n*127.1)*43758.5453123);}\n" +
                "float h2(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}\n" +
                "vec3 neon(float x){return .56+.44*cos(6.28318*(vec3(.03,.18,.34)+x*vec3(1.0,.71,.43)));}\n" +
                "float rect(vec2 p,vec2 c,vec2 s){vec2 q=abs(p-c)-s;return 1.0-step(0.0,max(q.x,q.y));}\n" +
                "vec3 skyline(vec2 uv,float z,float seed,vec3 tint){\n" +
                " float cols=mix(9.0,22.0,z); float gx=(uv.x+1.65+seed*.01)*cols; float id=floor(gx); float fx=fract(gx)-.5;\n" +
                " float r=h1(id+seed); float hh=mix(.13,.60,h1(id*1.73+seed*4.1))*(.62+.38*z);\n" +
                " float base=-.13+(.12*(1.0-z)); float bw=mix(.31,.44,h1(id+seed*9.7));\n" +
                " float inside=step(abs(fx),bw)*step(base,uv.y)*step(uv.y,base+hh);\n" +
                " vec3 b=tint*(.32+.55*r)*inside;\n" +
                " float edge=(1.0-step(abs(fx),bw-.025))*inside; b+=edge*vec3(.025,.055,.10);\n" +
                " vec2 wp=vec2((fx+bw)/(bw*2.0), (uv.y-base)/max(hh,.01));\n" +
                " vec2 wg=fract(wp*vec2(5.0,14.0));\n" +
                " float pane=step(.18,wg.x)*step(wg.x,.78)*step(.20,wg.y)*step(wg.y,.70);\n" +
                " float lit=step(.58,h2(vec2(floor(wp.x*5.0)+id*7.0,floor(wp.y*14.0)+seed)));\n" +
                " vec3 wc=mix(vec3(1.0,.44,.10),neon(r+seed*.03),.58);\n" +
                " b+=inside*pane*lit*wc*(1.3+1.1*z);\n" +
                " float roof=step(base+hh-.007,uv.y)*step(uv.y,base+hh+.010)*step(abs(fx),bw*.62);\n" +
                " b+=roof*neon(r+uTime*.01)*.48;\n" +
                " return b;\n" +
                "}\n" +
                "void main(){\n" +
                " vec2 uv=(gl_FragCoord.xy-.5*uRes)/uRes.y;\n" +
                " float yaw=(uTouch.x-.5)*.34; float pitch=(uTouch.y-.5)*.11; uv+=vec2(yaw,pitch);\n" +
                " float t=uTime;\n" +
                " vec3 col=mix(vec3(.008,.012,.028),vec3(.025,.055,.105),clamp(uv.y+.35,0.0,1.0));\n" +
                " float stars=step(.996,h2(floor((uv+vec2(t*.002,0.0))*vec2(620.0,360.0))))*step(.02,uv.y);\n" +
                " col+=stars*vec3(.65,.78,1.0);\n" +
                " vec2 moonP=uv-vec2(.48,.31); float moon=1.0-smoothstep(.055,.061,length(moonP));\n" +
                " col+=moon*vec3(1.0,.86,.61)*1.4;\n" +
                " col+=skyline(uv,0.22,3.0,vec3(.045,.065,.105))*.48;\n" +
                " col+=skyline(uv,0.52,19.0,vec3(.045,.080,.135))*.75;\n" +
                " col+=skyline(uv,0.92,41.0,vec3(.035,.070,.120))*1.08;\n" +
                " float horizon=-.18; float ground=step(uv.y,horizon);\n" +
                " vec3 road=vec3(.006,.009,.014);\n" +
                " float d=max(.018,horizon-uv.y+.035); float px=uv.x/d;\n" +
                " float lane=1.0-smoothstep(.014,.032,abs(abs(px)-1.18));\n" +
                " float center=1.0-smoothstep(.012,.026,abs(px));\n" +
                " float dash=step(.50,fract((1.0/d)*.11+t*.55));\n" +
                " road+=vec3(.025,.16,.34)*lane*.85+vec3(.72,.42,.08)*center*dash*.40;\n" +
                " float gx=floor((uv.x+1.6)*18.0); float signPick=step(.72,h1(gx+77.0));\n" +
                " float sx=(gx+.5)/18.0-1.6; float sign=rect(uv,vec2(sx,-.015+h1(gx)*.22),vec2(.018,.035+.028*h1(gx+8.0)))*signPick;\n" +
                " col+=sign*neon(h1(gx+11.0))*2.2;\n" +
                " float wet=h2(vec2(floor(uv.x*120.0),floor(uv.y*210.0+t*5.0)));\n" +
                " vec3 refl=neon(uv.x*.25+t*.018)*pow(max(0.0,wet-.68),2.0)*2.2;\n" +
                " road+=refl*.42;\n" +
                " col=mix(col,road,ground*.94);\n" +
                " float fog=clamp((-.02-uv.y)*.36,0.0,.18)+.035; col=mix(col,vec3(.055,.075,.095),fog);\n" +
                " float vig=1.0-smoothstep(.48,1.08,length(uv*vec2(.68,1.0))); col*=.66+.34*vig;\n" +
                " col=1.0-exp(-col*1.45); col=pow(col,vec3(.86));\n" +
                " fragColor=vec4(col,1.0);\n" +
                "}\n";

        private final String POST_FS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "uniform sampler2D uTex; uniform vec2 uTexel; in vec2 vUv; out vec4 fragColor;\n" +
                "void main(){\n" +
                " vec3 c=texture(uTex,vUv).rgb;\n" +
                " vec3 n=texture(uTex,vUv+vec2(0.0,uTexel.y)).rgb;\n" +
                " vec3 s=texture(uTex,vUv-vec2(0.0,uTexel.y)).rgb;\n" +
                " vec3 e=texture(uTex,vUv+vec2(uTexel.x,0.0)).rgb;\n" +
                " vec3 w=texture(uTex,vUv-vec2(uTexel.x,0.0)).rgb;\n" +
                " vec3 sharp=c*1.38-(n+s+e+w)*.095;\n" +
                " float l=dot(sharp,vec3(.2126,.7152,.0722));\n" +
                " sharp=mix(vec3(l),sharp,1.12);\n" +
                " fragColor=vec4(clamp(sharp,0.0,1.0),1.0);\n" +
                "}\n";

        @Override
        public void onSurfaceCreated(GL10 unused, EGLConfig config) {
            sceneProgram = link(VS, SCENE_FS);
            postProgram = link(VS, POST_FS);

            int[] vaos = new int[1];
            GLES30.glGenVertexArrays(1, vaos, 0);
            vao = vaos[0];
            GLES30.glBindVertexArray(vao);

            sceneTime = GLES30.glGetUniformLocation(sceneProgram, "uTime");
            sceneRes = GLES30.glGetUniformLocation(sceneProgram, "uRes");
            sceneTouch = GLES30.glGetUniformLocation(sceneProgram, "uTouch");

            postTex = GLES30.glGetUniformLocation(postProgram, "uTex");
            postTexel = GLES30.glGetUniformLocation(postProgram, "uTexel");

            GLES30.glDisable(GLES30.GL_DEPTH_TEST);

            startNs = System.nanoTime();
            lastNs = startNs;
            hudNs = startNs;
            scaleDecisionNs = startNs;
        }

        @Override
        public void onSurfaceChanged(GL10 unused, int w, int h) {
            width = Math.max(1, w);
            height = Math.max(1, h);
            rebuildTarget();
        }

        private void rebuildTarget() {
            int rw = Math.max(320, Math.round(width * renderScale));
            int rh = Math.max(180, Math.round(height * renderScale));
            if (rw == renderW && rh == renderH && fbo != 0) return;
            renderW = rw;
            renderH = rh;

            if (colorTex != 0) GLES30.glDeleteTextures(1, new int[]{colorTex}, 0);
            if (fbo != 0) GLES30.glDeleteFramebuffers(1, new int[]{fbo}, 0);

            int[] ids = new int[1];
            GLES30.glGenTextures(1, ids, 0);
            colorTex = ids[0];
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, colorTex);
            GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA,
                    renderW, renderH, 0, GLES30.GL_RGBA,
                    GLES30.GL_UNSIGNED_BYTE, null);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE);

            GLES30.glGenFramebuffers(1, ids, 0);
            fbo = ids[0];
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fbo);
            GLES30.glFramebufferTexture2D(GLES30.GL_FRAMEBUFFER,
                    GLES30.GL_COLOR_ATTACHMENT0, GLES30.GL_TEXTURE_2D, colorTex, 0);
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0);
        }

        @Override
        public void onDrawFrame(GL10 unused) {
            long now = System.nanoTime();
            float dt = (now - lastNs) / 1_000_000_000f;
            lastNs = now;
            if (dt > 0f && dt < 1f) {
                float fps = 1f / dt;
                fpsSmooth = fpsSmooth == 0f ? fps : fpsSmooth * .92f + fps * .08f;
            }

            if (now - scaleDecisionNs > 2_000_000_000L && fpsSmooth > 0f) {
                scaleDecisionNs = now;
                float old = renderScale;
                if (fpsSmooth < 43f) renderScale = Math.max(.54f, renderScale - .06f);
                else if (fpsSmooth > 57f) renderScale = Math.min(.82f, renderScale + .03f);
                if (Math.abs(old - renderScale) > .001f) rebuildTarget();
            }

            float time = (now - startNs) / 1_000_000_000f;

            // Pass 1: expensive procedural scene at adaptive internal resolution.
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fbo);
            GLES30.glViewport(0, 0, renderW, renderH);
            GLES30.glUseProgram(sceneProgram);
            GLES30.glBindVertexArray(vao);
            GLES30.glUniform1f(sceneTime, time);
            GLES30.glUniform2f(sceneRes, renderW, renderH);
            GLES30.glUniform2f(sceneTouch, touchX, touchY);
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3);

            // Pass 2: inexpensive full-resolution upscale + sharpening.
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0);
            GLES30.glViewport(0, 0, width, height);
            GLES30.glUseProgram(postProgram);
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0);
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, colorTex);
            GLES30.glUniform1i(postTex, 0);
            GLES30.glUniform2f(postTexel, 1f / renderW, 1f / renderH);
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3);

            if (now - hudNs > 350_000_000L) {
                hudNs = now;
                final float fps = fpsSmooth;
                final int w = width, h = height, rw = renderW, rh = renderH;
                final float scale = renderScale;
                runOnUiThread(() -> hud.setText(String.format(Locale.US,
                        "REAL-TIME PROCEDURAL GRAPHICS\n" +
                        "APK installed: %s\n" +
                        "FPS: %.1f   frame: %.2f ms\n" +
                        "display: %dx%d\n" +
                        "internal: %dx%d  scale: %.0f%%\n" +
                        "draw calls/frame: 2\n" +
                        "image textures: 0  mesh assets: 0\n" +
                        "scene: GLSL procedural + sharpen upscale\n" +
                        "target: APK < 5 MiB",
                        apkSizeText(), fps, fps > 0 ? 1000f / fps : 0f,
                        w, h, rw, rh, scale * 100f)));
            }
        }

        private int shader(int type, String src) {
            int s = GLES30.glCreateShader(type);
            GLES30.glShaderSource(s, src);
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

        private int link(String vs, String fs) {
            int v = shader(GLES30.GL_VERTEX_SHADER, vs);
            int f = shader(GLES30.GL_FRAGMENT_SHADER, fs);
            int p = GLES30.glCreateProgram();
            GLES30.glAttachShader(p, v);
            GLES30.glAttachShader(p, f);
            GLES30.glLinkProgram(p);
            int[] ok = new int[1];
            GLES30.glGetProgramiv(p, GLES30.GL_LINK_STATUS, ok, 0);
            GLES30.glDeleteShader(v);
            GLES30.glDeleteShader(f);
            if (ok[0] == 0) {
                String log = GLES30.glGetProgramInfoLog(p);
                GLES30.glDeleteProgram(p);
                throw new RuntimeException(log);
            }
            return p;
        }
    }
}
