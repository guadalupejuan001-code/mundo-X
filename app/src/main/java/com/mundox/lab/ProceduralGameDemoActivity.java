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

public class ProceduralGameDemoActivity extends Activity {
    private GLSurfaceView gl;
    private TextView hud;
    private DemoRenderer renderer;

    private int movePointer = -1;
    private int lookPointer = -1;
    private float moveStartX, moveStartY;
    private float lookLastX, lookLastY;

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
        hud.setBackgroundColor(0x70000000);
        hud.setPadding(dp(10), dp(7), dp(10), dp(7));
        hud.setText("PROCEDURAL OPEN WORLD\ninitializing...");

        FrameLayout.LayoutParams hp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        hp.gravity = Gravity.TOP | Gravity.START;
        hp.setMargins(dp(10), dp(10), dp(10), dp(10));
        root.addView(hud, hp);

        TextView moveLabel = controlLabel("MOVE\nleft side");
        FrameLayout.LayoutParams mlp = new FrameLayout.LayoutParams(dp(118), dp(72));
        mlp.gravity = Gravity.BOTTOM | Gravity.START;
        mlp.setMargins(dp(22), 0, 0, dp(22));
        root.addView(moveLabel, mlp);

        TextView lookLabel = controlLabel("LOOK\nright side");
        FrameLayout.LayoutParams llp = new FrameLayout.LayoutParams(dp(118), dp(72));
        llp.gravity = Gravity.BOTTOM | Gravity.END;
        llp.setMargins(0, 0, dp(22), dp(22));
        root.addView(lookLabel, llp);

        gl.setOnTouchListener((v, e) -> handleTouch(e));
        setContentView(root);
    }

    private TextView controlLabel(String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setGravity(Gravity.CENTER);
        t.setTextColor(0xFFDDDDDD);
        t.setTextSize(11);
        t.setTypeface(android.graphics.Typeface.MONOSPACE);
        t.setBackgroundColor(0x44000000);
        return t;
    }

    private boolean handleTouch(MotionEvent e) {
        final int action = e.getActionMasked();
        final int actionIndex = e.getActionIndex();
        final int id = e.getPointerId(actionIndex);
        final float half = gl.getWidth() * 0.5f;

        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            float x = e.getX(actionIndex);
            float y = e.getY(actionIndex);
            if (x < half && movePointer == -1) {
                movePointer = id;
                moveStartX = x;
                moveStartY = y;
            } else if (lookPointer == -1) {
                lookPointer = id;
                lookLastX = x;
                lookLastY = y;
            }
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < e.getPointerCount(); i++) {
                int pid = e.getPointerId(i);
                float x = e.getX(i);
                float y = e.getY(i);

                if (pid == movePointer) {
                    float radius = Math.max(90f, gl.getWidth() * 0.12f);
                    renderer.moveX = clamp((x - moveStartX) / radius, -1f, 1f);
                    renderer.moveY = clamp((moveStartY - y) / radius, -1f, 1f);
                } else if (pid == lookPointer) {
                    float dx = (x - lookLastX) / Math.max(1f, gl.getWidth());
                    float dy = (y - lookLastY) / Math.max(1f, gl.getHeight());
                    renderer.addLook(dx * 3.6f, -dy * 2.8f);
                    lookLastX = x;
                    lookLastY = y;
                }
            }
        } else if (action == MotionEvent.ACTION_UP ||
                action == MotionEvent.ACTION_POINTER_UP ||
                action == MotionEvent.ACTION_CANCEL) {
            if (id == movePointer || action == MotionEvent.ACTION_CANCEL) {
                movePointer = -1;
                renderer.moveX = 0f;
                renderer.moveY = 0f;
            }
            if (id == lookPointer || action == MotionEvent.ACTION_CANCEL) {
                lookPointer = -1;
            }
        }
        return true;
    }

    private float clamp(float v, float lo, float hi) {
        return Math.max(lo, Math.min(hi, v));
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
        private int sceneTime, sceneRes, sceneCam, sceneAngles;
        private int postTex, postTexel;

        private int fbo, colorTex;
        private int width = 1, height = 1, renderW = 1, renderH = 1;
        private float renderScale = 0.68f;

        private long startNs, lastNs, hudNs, scaleDecisionNs;
        private float fpsSmooth = 0f;

        volatile float moveX = 0f;
        volatile float moveY = 0f;
        private float camX = 0f, camY = 1.62f, camZ = 0f;
        private float yaw = 0f, pitch = -0.03f;

        synchronized void addLook(float dyaw, float dpitch) {
            yaw += dyaw;
            pitch = Math.max(-0.68f, Math.min(0.68f, pitch + dpitch));
        }

        private final String VS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "out vec2 vUv;\n" +
                "const vec2 V[3]=vec2[3](vec2(-1.0,-1.0),vec2(3.0,-1.0),vec2(-1.0,3.0));\n" +
                "void main(){vec2 p=V[gl_VertexID];vUv=p*.5+.5;gl_Position=vec4(p,0.0,1.0);}\n";

        private final String SCENE_FS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "uniform vec2 uRes; uniform float uTime; uniform vec3 uCam; uniform vec2 uAngles;\n" +
                "in vec2 vUv; out vec4 fragColor;\n" +
                "float h1(float n){return fract(sin(n*127.1)*43758.5453123);}\n" +
                "float h2(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}\n" +
                "vec3 pal(float x){return .5+.5*cos(6.28318*(vec3(.02,.18,.34)+x*vec3(1.0,.73,.47)));}\n" +
                "float boxSDF(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.0))+min(max(q.x,max(q.y,q.z)),0.0);}\n" +
                "float cityMap(vec3 p,out float bid,out vec2 cell){\n" +
                "  float block=7.0; cell=floor((p.xz+3.5)/block);\n" +
                "  vec2 q=mod(p.xz+3.5,block)-3.5;\n" +
                "  float rnd=h2(cell);\n" +
                "  float h=3.5+15.0*h2(cell+vec2(7.3,19.1));\n" +
                "  vec2 fp=vec2(2.0+0.35*h2(cell+3.0),2.0+0.35*h2(cell+9.0));\n" +
                "  vec3 bp=vec3(q.x,p.y-h*.5,q.y);\n" +
                "  float mainB=boxSDF(bp,vec3(fp.x,h*.5,fp.y));\n" +
                "  float crownH=.8+2.1*h2(cell+21.0);\n" +
                "  float crown=boxSDF(vec3(q.x,p.y-h-crownH*.5,q.y),vec3(fp*.52,crownH*.5));\n" +
                "  float b=min(mainB,crown);\n" +
                "  bid=rnd;\n" +
                "  return min(b,p.y);\n" +
                "}\n" +
                "vec3 normalAt(vec3 p){float id;vec2 c;float e=.004;float d=cityMap(p,id,c);return normalize(vec3(cityMap(p+vec3(e,0,0),id,c)-d,cityMap(p+vec3(0,e,0),id,c)-d,cityMap(p+vec3(0,0,e),id,c)-d));}\n" +
                "vec3 material(vec3 p,vec3 n,vec3 rd,float bid,vec2 cell){\n" +
                "  if(n.y>.75){\n" +
                "    float gx=abs(fract((p.x+3.5)/7.0)-.5); float gz=abs(fract((p.z+3.5)/7.0)-.5);\n" +
                "    float road=max(step(.29,gx),step(.29,gz));\n" +
                "    vec3 g=mix(vec3(.018,.023,.030),vec3(.006,.009,.014),road);\n" +
                "    float stripe=(1.0-step(.018,abs(fract(p.x*.14)-.5)))*road;\n" +
                "    g+=vec3(.72,.45,.08)*stripe*.55;\n" +
                "    return g;\n" +
                "  }\n" +
                "  vec3 base=mix(vec3(.028,.043,.070),vec3(.070,.095,.130),bid);\n" +
                "  vec2 wp=abs(n.x)>.5?p.zy:p.xy;\n" +
                "  vec2 g=fract(wp*vec2(1.05,.48));\n" +
                "  float win=step(.16,g.x)*step(g.x,.78)*step(.16,g.y)*step(g.y,.70);\n" +
                "  float lit=step(.55,h2(floor(wp*vec2(1.05,.48))+cell*9.0));\n" +
                "  vec3 wc=mix(vec3(1.0,.45,.12),pal(bid+.12),.62);\n" +
                "  vec3 sun=normalize(vec3(-.45,.72,-.35));\n" +
                "  float dif=max(.16,dot(n,sun));\n" +
                "  return base*dif+wc*win*lit*2.35+vec3(.03,.05,.08)*pow(max(0.0,1.0-dot(n,-rd)),2.0);\n" +
                "}\n" +
                "void main(){\n" +
                " vec2 uv=(gl_FragCoord.xy-.5*uRes)/uRes.y;\n" +
                " float cy=cos(uAngles.x),sy=sin(uAngles.x),cp=cos(uAngles.y),sp=sin(uAngles.y);\n" +
                " vec3 fw=normalize(vec3(sy*cp,sp,cy*cp));\n" +
                " vec3 rt=normalize(vec3(cy,0.0,-sy));\n" +
                " vec3 up=normalize(cross(rt,fw));\n" +
                " vec3 rd=normalize(fw+uv.x*1.45*rt+uv.y*1.10*up);\n" +
                " vec3 ro=uCam; float dist=0.0,hit=0.0,bid=0.0;vec2 cell=vec2(0);vec3 p=ro;\n" +
                " for(int i=0;i<42;i++){p=ro+rd*dist;float d=cityMap(p,bid,cell);if(d<.012){hit=1.0;break;}dist+=max(.025,d*.68);if(dist>92.0)break;}\n" +
                " vec3 sky=mix(vec3(.005,.010,.026),vec3(.055,.090,.15),clamp(rd.y*.75+.42,0.0,1.0));\n" +
                " float moon=pow(max(0.0,dot(rd,normalize(vec3(.40,.34,.85)))),650.0);sky+=vec3(1.0,.84,.62)*moon*6.0;\n" +
                " float stars=step(.9985,h2(floor((rd.xy+rd.z)*vec2(620.0,360.0))));sky+=stars*vec3(.65,.78,1.0);\n" +
                " vec3 col=sky;\n" +
                " if(hit>.5){vec3 n=normalAt(p);col=material(p,n,rd,bid,cell);\n" +
                "   float fog=1.0-exp(-dist*.017);col=mix(col,sky,fog);\n" +
                "   if(n.y>.75){float wet=h2(floor(p.xz*5.0));col+=pal(p.x*.035+uTime*.008)*pow(wet,10.0)*.09;}\n" +
                " }\n" +
                " float vig=1.0-smoothstep(.56,1.30,length(uv*vec2(.66,1.0)));col*=.72+.28*vig;\n" +
                " col=1.0-exp(-col*1.58);col=pow(col,vec3(.86));fragColor=vec4(col,1.0);\n" +
                "}\n";

        private final String POST_FS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "uniform sampler2D uTex; uniform vec2 uTexel; in vec2 vUv; out vec4 fragColor;\n" +
                "void main(){vec3 c=texture(uTex,vUv).rgb;vec3 n=texture(uTex,vUv+vec2(0,uTexel.y)).rgb;vec3 s=texture(uTex,vUv-vec2(0,uTexel.y)).rgb;vec3 e=texture(uTex,vUv+vec2(uTexel.x,0)).rgb;vec3 w=texture(uTex,vUv-vec2(uTexel.x,0)).rgb;vec3 q=c*1.34-(n+s+e+w)*.085;fragColor=vec4(clamp(q,0.0,1.0),1.0);}\n";

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
            sceneCam = GLES30.glGetUniformLocation(sceneProgram, "uCam");
            sceneAngles = GLES30.glGetUniformLocation(sceneProgram, "uAngles");
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
                    renderW, renderH, 0, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, null);
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
            if (dt > 0f && dt < .12f) {
                float fps = 1f / dt;
                fpsSmooth = fpsSmooth == 0f ? fps : fpsSmooth * .92f + fps * .08f;

                float speed = 6.0f;
                float sy = (float)Math.sin(yaw), cy = (float)Math.cos(yaw);
                float forwardX = sy, forwardZ = cy;
                float rightX = cy, rightZ = -sy;
                camX += (forwardX * moveY + rightX * moveX) * speed * dt;
                camZ += (forwardZ * moveY + rightZ * moveX) * speed * dt;
            }

            if (now - scaleDecisionNs > 2_000_000_000L && fpsSmooth > 0f) {
                scaleDecisionNs = now;
                float old = renderScale;
                if (fpsSmooth < 46f) renderScale = Math.max(.50f, renderScale - .06f);
                else if (fpsSmooth > 58f) renderScale = Math.min(.80f, renderScale + .03f);
                if (Math.abs(old - renderScale) > .001f) rebuildTarget();
            }

            float time = (now - startNs) / 1_000_000_000f;

            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fbo);
            GLES30.glViewport(0, 0, renderW, renderH);
            GLES30.glUseProgram(sceneProgram);
            GLES30.glBindVertexArray(vao);
            GLES30.glUniform1f(sceneTime, time);
            GLES30.glUniform2f(sceneRes, renderW, renderH);
            GLES30.glUniform3f(sceneCam, camX, camY, camZ);
            GLES30.glUniform2f(sceneAngles, yaw, pitch);
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3);

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
                final int rw = renderW, rh = renderH;
                final float x = camX, z = camZ, yv = yaw;
                final float scale = renderScale;
                runOnUiThread(() -> hud.setText(String.format(Locale.US,
                        "PROCEDURAL OPEN WORLD\n" +
                        "APK: %s\n" +
                        "FPS: %.1f   frame: %.2f ms\n" +
                        "internal: %dx%d (%.0f%%)\n" +
                        "position: %.1f, %.1f   yaw: %.0f deg\n" +
                        "textures: 0   mesh assets: 0\n" +
                        "LEFT=move   RIGHT=look",
                        apkSizeText(), fps, fps > 0 ? 1000f / fps : 0f,
                        rw, rh, scale * 100f, x, z, Math.toDegrees(yv))));
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
