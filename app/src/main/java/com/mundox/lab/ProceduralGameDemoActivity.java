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
                "vec3 pal(float x){return .5+.5*cos(6.28318*(vec3(.02,.18,.34)+x*vec3(1.0,.73,.47)));}\n" +
                "float boxSDF(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.0))+min(max(q.x,max(q.y,q.z)),0.0);}\n" +
                "float cityMap(vec3 p,out float bid,out vec2 cell){\n" +
                "  float block=5.0; cell=floor((p.xz+2.5)/block);\n" +
                "  vec2 q=mod(p.xz+2.5,block)-2.5;\n" +
                "  float rnd=h2(cell);\n" +
                "  float roadX=1.0-step(2.0,abs(q.x)); float roadZ=1.0-step(2.0,abs(q.y));\n" +
                "  float onRoad=max(roadX,roadZ);\n" +
                "  float h=2.5+10.0*h2(cell+vec2(7.3,19.1));\n" +
                "  vec2 footprint=vec2(1.45+0.25*h2(cell+3.0),1.45+0.25*h2(cell+9.0));\n" +
                "  vec3 bp=vec3(q.x,p.y-h*.5,q.y);\n" +
                "  float b=boxSDF(bp,vec3(footprint.x,h*.5,footprint.y));\n" +
                "  bid=rnd;\n" +
                "  float ground=p.y;\n" +
                "  return min(mix(b,1000.0,onRoad),ground);\n" +
                "}\n" +
                "vec3 normalAt(vec3 p){float id;vec2 cell;float e=.002;float d=cityMap(p,id,cell);return normalize(vec3(cityMap(p+vec3(e,0,0),id,cell)-d,cityMap(p+vec3(0,e,0),id,cell)-d,cityMap(p+vec3(0,0,e),id,cell)-d));}\n" +
                "vec3 shade(vec3 p,vec3 n,vec3 rd,float bid,vec2 cell){\n" +
                "  vec3 base=mix(vec3(.018,.028,.045),vec3(.055,.075,.105),bid);\n" +
                "  float side=abs(n.y)<.5?1.0:0.0;\n" +
                "  vec2 wp=abs(n.x)>.5?p.zy:p.xy;\n" +
                "  vec2 g=fract(wp*vec2(1.35,.55));\n" +
                "  float win=side*step(.18,g.x)*step(g.x,.78)*step(.17,g.y)*step(g.y,.72);\n" +
                "  float lit=step(.50,h2(floor(wp*vec2(1.35,.55))+cell*7.0));\n" +
                "  vec3 wc=mix(vec3(1.0,.42,.10),pal(bid+.15),.55);\n" +
                "  vec3 sun=normalize(vec3(-.35,.72,-.42));\n" +
                "  float dif=max(.12,dot(n,sun));\n" +
                "  float rim=pow(max(0.0,1.0-dot(n,-rd)),2.0);\n" +
                "  vec3 c=base*dif+wc*win*lit*2.1+vec3(.06,.09,.15)*rim;\n" +
                "  if(n.y>.8){float lane=max(step(.92,fract(p.x*.22)),step(.92,fract(p.z*.22)));c+=vec3(.08,.13,.18)*lane;}\n" +
                "  return c;\n" +
                "}\n" +
                "void main(){\n" +
                " vec2 uv=(gl_FragCoord.xy-.5*uRes)/uRes.y;\n" +
                " float t=uTime;\n" +
                " float yaw=(uTouch.x-.5)*1.2 + sin(t*.10)*.10;\n" +
                " float pitch=(.5-uTouch.y)*.40-.05;\n" +
                " vec3 ro=vec3(sin(t*.08)*2.0,1.45,t*1.65);\n" +
                " vec3 fw=normalize(vec3(sin(yaw),pitch,cos(yaw)));\n" +
                " vec3 rt=normalize(cross(vec3(0,1,0),fw));\n" +
                " vec3 up=cross(fw,rt);\n" +
                " vec3 rd=normalize(fw+uv.x*rt+uv.y*up);\n" +
                " float dist=0.0; float hit=0.0; float bid=0.0; vec2 cell=vec2(0); vec3 p=ro;\n" +
                " for(int i=0;i<28;i++){p=ro+rd*dist;float d=cityMap(p,bid,cell);if(d<.012){hit=1.0;break;}dist+=max(.035,d*.72);if(dist>65.0)break;}\n" +
                " vec3 sky=mix(vec3(.006,.010,.028),vec3(.045,.075,.13),clamp(rd.y*.8+.45,0.0,1.0));\n" +
                " float moon=pow(max(0.0,dot(rd,normalize(vec3(.45,.40,.78)))),500.0); sky+=vec3(1.0,.84,.58)*moon*5.0;\n" +
                " vec3 col=sky;\n" +
                " if(hit>0.5){vec3 n=normalAt(p);col=shade(p,n,rd,bid,cell);\n" +
                "   float fog=1.0-exp(-dist*.020);col=mix(col,sky,fog);\n" +
                "   if(n.y>.8){float wet=.35+.65*h2(floor(p.xz*7.0));col+=pal(p.x*.05+t*.01)*pow(wet,8.0)*.12;}\n" +
                " }\n" +
                " float glow=0.0;for(int i=0;i<4;i++){float fi=float(i);vec3 lp=vec3(-6.0+fi*4.0,1.7,ro.z+7.0+fi*9.0);float a=max(0.0,dot(normalize(lp-ro),rd));glow+=pow(a,700.0);}col+=vec3(.1,.45,1.0)*glow*2.0;\n" +
                " float vig=1.0-smoothstep(.50,1.18,length(uv*vec2(.72,1.0)));col*=.68+.32*vig;\n" +
                " col=1.0-exp(-col*1.55);col=pow(col,vec3(.86));\n" +
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
