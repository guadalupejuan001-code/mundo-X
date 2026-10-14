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
 * Asset-free real-time graphics experiment.
 * The scene is reconstructed procedurally in a single fragment shader.
 * No image textures, meshes, videos, or pre-rendered scene assets are bundled.
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
        hud.setTextSize(11);
        hud.setTypeface(android.graphics.Typeface.MONOSPACE);
        hud.setBackgroundColor(0x66000000);
        hud.setPadding(dp(10), dp(7), dp(10), dp(7));
        hud.setText("PROCEDURAL GAME GRAPHICS TEST\ninitializing...");

        FrameLayout.LayoutParams hp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        hp.gravity = Gravity.TOP | Gravity.START;
        hp.setMargins(dp(10), dp(10), dp(10), dp(10));
        root.addView(hud, hp);

        TextView hint = new TextView(this);
        hint.setTextColor(0xFFDDDDDD);
        hint.setTextSize(10);
        hint.setTypeface(android.graphics.Typeface.MONOSPACE);
        hint.setBackgroundColor(0x55000000);
        hint.setPadding(dp(8), dp(5), dp(8), dp(5));
        hint.setText("TOUCH / DRAG = CAMERA • procedural only • 1 draw call");

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
                renderer.touching = true;
                return true;
            }
            if (e.getAction() == MotionEvent.ACTION_UP ||
                    e.getAction() == MotionEvent.ACTION_CANCEL) {
                renderer.touching = false;
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
            double mib = f.length() / (1024.0 * 1024.0);
            return String.format(Locale.US, "%.2f MiB", mib);
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
        private int program;
        private int vao;
        private int uTime, uRes, uTouch;
        private int width = 1, height = 1;
        private long startNs, lastNs, hudNs;
        private float fpsSmooth = 0f;
        volatile float touchX = 0.5f;
        volatile float touchY = 0.5f;
        volatile boolean touching = false;

        private final String VS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "const vec2 V[3]=vec2[3](vec2(-1.0,-1.0),vec2(3.0,-1.0),vec2(-1.0,3.0));\n" +
                "void main(){gl_Position=vec4(V[gl_VertexID],0.0,1.0);}\n";

        private final String FS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "uniform vec2 uRes; uniform float uTime; uniform vec2 uTouch;\n" +
                "out vec4 fragColor;\n" +
                "float hash21(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}\n" +
                "float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),mix(hash21(i+vec2(0,1)),hash21(i+1.0),f.x),f.y);}\n" +
                "float sdBox(vec2 p,vec2 b){vec2 d=abs(p)-b;return length(max(d,0.0))+min(max(d.x,d.y),0.0);}\n" +
                "vec3 pal(float t){return 0.52+0.48*cos(6.28318*(vec3(0.05,0.18,0.32)+t*vec3(1.0,0.73,0.47)));}\n" +
                "vec3 cityLayer(vec2 uv,float depth,float seed){\n" +
                "  vec2 p=uv; p.x*=1.0+depth*.55; p.y+=depth*.14;\n" +
                "  float cell=floor((p.x+2.5)*9.0); float rnd=hash21(vec2(cell,seed));\n" +
                "  float cx=(cell/9.0)-2.5/9.0; float bw=.035+.035*rnd; float h=.14+.55*hash21(vec2(cell+7.0,seed));\n" +
                "  float d=sdBox(vec2(p.x-(cell+.5)/9.0+2.5/9.0,p.y+h*.5-.27),vec2(bw,h*.5));\n" +
                "  float building=1.0-smoothstep(0.0,.004,d);\n" +
                "  vec3 base=mix(vec3(.008,.012,.025),vec3(.025,.035,.065),rnd);\n" +
                "  float wx=step(.72,fract((p.x+cell*.013)*90.0)); float wy=step(.72,fract((p.y+h)*95.0));\n" +
                "  float windows=building*wx*wy*step(.38,hash21(floor(p*vec2(90.0,95.0))+seed));\n" +
                "  vec3 neon=pal(rnd+uTime*.015)*windows*2.4;\n" +
                "  return building*base+neon;\n" +
                "}\n" +
                "void main(){\n" +
                " vec2 uv=(gl_FragCoord.xy-.5*uRes.xy)/uRes.y;\n" +
                " float yaw=(uTouch.x-.5)*.22; uv.x+=yaw;\n" +
                " float t=uTime;\n" +
                " vec3 col=vec3(.004,.007,.018);\n" +
                " float sky=clamp(uv.y+.15,0.0,1.0); col+=vec3(.015,.025,.07)*(1.0-sky);\n" +
                " float cloud=noise(uv*3.1+vec2(t*.025,0.0)); col+=vec3(.025,.035,.07)*smoothstep(.58,.9,cloud)*(1.0-smoothstep(-.1,.65,uv.y));\n" +
                " col+=cityLayer(uv,1.0,2.0)*.42; col+=cityLayer(uv,0.58,9.0)*.72; col+=cityLayer(uv,0.12,18.0);\n" +
                " float horizon=-.30; float road=1.0-smoothstep(.0,.012,uv.y-horizon);\n" +
                " vec2 rp=vec2(uv.x/(max(.08,-uv.y-horizon+.09)),uv.y);\n" +
                " float lane=exp(-abs(abs(rp.x)-.34)*34.0)*road;\n" +
                " vec3 laneC=mix(vec3(.1,.18,.35),vec3(.05,.45,1.3),.5+.5*sin(t*.5));\n" +
                " col=mix(col,vec3(.008,.011,.018)+laneC*lane*.65,road*.74);\n" +
                " float refl=noise(vec2(uv.x*18.0,uv.y*55.0-t*.25));\n" +
                " col+=road*pal(uv.x*.32+t*.025)*pow(max(0.0,refl-.58),2.0)*1.5;\n" +
                " for(int i=0;i<6;i++){float fi=float(i);float x=fract(fi*.173+t*(.025+.008*fi))*2.4-1.2;float y=-.05+.08*sin(fi*4.7);float d=length(uv-vec2(x,y));col+=pal(fi*.17+t*.02)*.008/(d*d+.001);}\n" +
                " float vign=1.0-smoothstep(.42,1.05,length(uv*vec2(.75,1.0)));col*=.58+.42*vign;\n" +
                " col=1.0-exp(-col*1.55); col=pow(col,vec3(.82));\n" +
                " fragColor=vec4(col,1.0);\n" +
                "}\n";

        @Override
        public void onSurfaceCreated(GL10 gl10, EGLConfig config) {
            program = link(VS, FS);
            int[] vaos = new int[1];
            GLES30.glGenVertexArrays(1, vaos, 0);
            vao = vaos[0];
            GLES30.glBindVertexArray(vao);
            uTime = GLES30.glGetUniformLocation(program, "uTime");
            uRes = GLES30.glGetUniformLocation(program, "uRes");
            uTouch = GLES30.glGetUniformLocation(program, "uTouch");
            GLES30.glDisable(GLES30.GL_DEPTH_TEST);
            startNs = System.nanoTime();
            lastNs = startNs;
            hudNs = startNs;
        }

        @Override
        public void onSurfaceChanged(GL10 gl10, int w, int h) {
            width = Math.max(1, w);
            height = Math.max(1, h);
            GLES30.glViewport(0, 0, width, height);
        }

        @Override
        public void onDrawFrame(GL10 gl10) {
            long now = System.nanoTime();
            float dt = (now-lastNs)/1_000_000_000f;
            lastNs = now;
            if (dt > 0f && dt < 1f) {
                float fps=1f/dt;
                fpsSmooth=fpsSmooth==0f?fps:fpsSmooth*.92f+fps*.08f;
            }

            GLES30.glUseProgram(program);
            GLES30.glBindVertexArray(vao);
            GLES30.glUniform1f(uTime,(now-startNs)/1_000_000_000f);
            GLES30.glUniform2f(uRes,width,height);
            GLES30.glUniform2f(uTouch,touchX,touchY);
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES,0,3);

            if(now-hudNs>350_000_000L){
                hudNs=now;
                final float fps=fpsSmooth;
                final int w=width,h=height;
                runOnUiThread(() -> hud.setText(String.format(Locale.US,
                        "REAL-TIME PROCEDURAL GRAPHICS\n"+
                        "APK installed: %s\n"+
                        "FPS: %.1f   frame: %.2f ms\n"+
                        "resolution: %dx%d\n"+
                        "draw calls/frame: 1\n"+
                        "image textures: 0\n"+
                        "mesh assets: 0\n"+
                        "scene source: GLSL procedural\n"+
                        "target: APK < 5 MiB",
                        apkSizeText(),fps,fps>0?1000f/fps:0f,w,h)));
            }
        }

        private int shader(int type,String src){
            int s=GLES30.glCreateShader(type);
            GLES30.glShaderSource(s,src);
            GLES30.glCompileShader(s);
            int[] ok=new int[1];
            GLES30.glGetShaderiv(s,GLES30.GL_COMPILE_STATUS,ok,0);
            if(ok[0]==0) throw new RuntimeException(GLES30.glGetShaderInfoLog(s));
            return s;
        }

        private int link(String vs,String fs){
            int v=shader(GLES30.GL_VERTEX_SHADER,vs);
            int f=shader(GLES30.GL_FRAGMENT_SHADER,fs);
            int p=GLES30.glCreateProgram();
            GLES30.glAttachShader(p,v); GLES30.glAttachShader(p,f);
            GLES30.glLinkProgram(p);
            int[] ok=new int[1]; GLES30.glGetProgramiv(p,GLES30.GL_LINK_STATUS,ok,0);
            GLES30.glDeleteShader(v); GLES30.glDeleteShader(f);
            if(ok[0]==0) throw new RuntimeException(GLES30.glGetProgramInfoLog(p));
            return p;
        }
    }
}
