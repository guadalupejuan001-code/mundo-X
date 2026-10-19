package com.mundox.lab;

import android.app.Activity;
import android.graphics.Color;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import android.os.Bundle;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.io.File;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.util.ArrayList;
import java.util.Locale;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Navigable city experiment using ordinary rasterized 3D geometry.
 * City layout and facade variation are generated deterministically at runtime.
 * This intentionally replaces the earlier full-screen ray-marched city.
 */
public class ProceduralGameDemoActivity extends Activity {
    private GLSurfaceView gl;
    private TextView hud;
    private CityRenderer renderer;

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
        renderer = new CityRenderer();
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
        hud.setText("CITY QUALITY TEST\ninitializing...");

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
        t.setTextColor(0xFFE2E2E2);
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
                    renderer.addLook(dx * 3.8f, -dy * 3.0f);
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

    private final class CityRenderer implements GLSurfaceView.Renderer {
        private static final float BLOCK = 28f;
        private static final int CITY_RADIUS = 7;

        private int width = 1, height = 1;
        private int renderW = 1, renderH = 1;
        private float renderScale = 0.82f;

        private int fbo, colorTex, depthRbo;
        private int cubeVbo, instanceVbo, groundVbo;
        private int buildingProgram, groundProgram, skyProgram, birdProgram;
        private int instanceCount = 0;

        private int bVP, bCam, bFog;
        private int gVP, gCam, gTime, gCenter, gSize;
        private int sTime, sRes, sAngles;
        private int birdVP, birdCam, birdTime;

        volatile float moveX = 0f;
        volatile float moveY = 0f;

        private float camX = 14f, camY = 1.72f, camZ = 0f;
        private float yaw = 0f, pitch = -0.04f;

        private final float[] projection = new float[16];
        private final float[] view = new float[16];
        private final float[] vp = new float[16];

        private long startNs, lastNs, hudNs, scaleDecisionNs;
        private float fpsSmooth = 0f;

        private int cityBlockX = Integer.MIN_VALUE;
        private int cityBlockZ = Integer.MIN_VALUE;
        private final ArrayList<Collider> colliders = new ArrayList<>();

        synchronized void addLook(float dyaw, float dpitch) {
            yaw += dyaw;
            pitch = Math.max(-0.76f, Math.min(0.76f, pitch + dpitch));
        }

        private final String SKY_VS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "out vec2 vUv;\n" +
                "const vec2 V[3]=vec2[3](vec2(-1.0,-1.0),vec2(3.0,-1.0),vec2(-1.0,3.0));\n" +
                "void main(){vec2 p=V[gl_VertexID];vUv=p*.5+.5;gl_Position=vec4(p,0.0,1.0);}\n";

        private final String SKY_FS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "uniform vec2 uRes; uniform float uTime; uniform vec2 uAngles;\n" +
                "in vec2 vUv; out vec4 fragColor;\n" +
                "float h(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}\n" +
                "float n(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(h(i),h(i+vec2(1,0)),f.x),mix(h(i+vec2(0,1)),h(i+1.0),f.x),f.y);}\n" +
                "void main(){\n" +
                " vec2 uv=(gl_FragCoord.xy-.5*uRes)/uRes.y;\n" +
                " float horizon=clamp(uv.y+.22+uAngles.y*.28,0.0,1.0);\n" +
                " vec3 col=mix(vec3(.010,.014,.026),vec3(.070,.125,.205),horizon);\n" +
                " float cloud=n(uv*2.2+vec2(uTime*.008,0.0))*n(uv*5.7-uTime*.004);\n" +
                " col+=vec3(.10,.12,.15)*smoothstep(.52,.83,cloud)*smoothstep(-.05,.55,uv.y);\n" +
                " vec2 moonP=uv-vec2(.46-uAngles.x*.08,.27-uAngles.y*.15);\n" +
                " float moon=1.0-smoothstep(.050,.058,length(moonP));\n" +
                " col+=vec3(1.0,.86,.64)*moon*1.85;\n" +
                " float stars=step(.9978,h(floor((uv+uAngles*.05)*vec2(690.0,390.0))));\n" +
                " col+=stars*vec3(.70,.82,1.0)*smoothstep(.02,.28,uv.y);\n" +
                " fragColor=vec4(col,1.0);\n" +
                "}\n";

        private final String BUILDING_VS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "layout(location=0) in vec3 aPos;\n" +
                "layout(location=1) in vec3 iCenter;\n" +
                "layout(location=2) in vec3 iScale;\n" +
                "layout(location=3) in float iStyle;\n" +
                "uniform mat4 uVP;\n" +
                "out vec3 vWorld; out vec3 vLocal; out vec3 vNormal; out float vStyle;\n" +
                "void main(){\n" +
                " vec3 local=aPos*iScale; vec3 world=iCenter+local;\n" +
                " int face=gl_VertexID/6;\n" +
                " vec3 normal=face==0?vec3(1,0,0):face==1?vec3(-1,0,0):face==2?vec3(0,1,0):face==3?vec3(0,-1,0):face==4?vec3(0,0,1):vec3(0,0,-1);\n" +
                " vWorld=world;vLocal=aPos;vNormal=normal;vStyle=iStyle;gl_Position=uVP*vec4(world,1.0);\n" +
                "}\n";

        private final String BUILDING_FS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "uniform vec3 uCam; uniform float uFog;\n" +
                "in vec3 vWorld; in vec3 vLocal; in vec3 vNormal; in float vStyle; out vec4 fragColor;\n" +
                "float h(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}\n" +
                "vec3 palette(float x){return .52+.48*cos(6.28318*(vec3(.02,.16,.31)+x*vec3(1.0,.76,.48)));}\n" +
                "void main(){\n" +
                " vec3 n=normalize(vNormal);vec3 viewDir=normalize(uCam-vWorld);\n" +
                " vec3 lightDir=normalize(vec3(-.42,.78,-.34));\n" +
                " float ndl=max(.08,dot(n,lightDir));\n" +
                " vec3 base=mix(vec3(.045,.055,.070),vec3(.11,.13,.16),vStyle);\n" +
                " float side=step(abs(n.y),.55);\n" +
                " vec2 wp=abs(n.x)>.5?vWorld.zy:vWorld.xy;\n" +
                " vec2 cell=floor(wp*vec2(.52,.29));vec2 f=fract(wp*vec2(.52,.29));\n" +
                " float frame=step(.08,f.x)*step(f.x,.86)*step(.09,f.y)*step(f.y,.80);\n" +
                " float on=step(.43,h(cell+floor(vStyle*97.0)));\n" +
                " vec3 warm=vec3(1.0,.53,.19);vec3 cool=vec3(.24,.65,1.0);\n" +
                " vec3 win=mix(warm,cool,step(.53,h(cell+vStyle*19.0)));\n" +
                " float floors=step(.90,fract(wp.y*.055));\n" +
                " float columns=step(.93,fract(wp.x*.035));\n" +
                " vec3 col=base*(.34+.66*ndl);\n" +
                " col+=side*frame*on*win*(1.45+.55*h(cell+7.0));\n" +
                " col+=side*(floors+columns)*vec3(.025,.035,.050);\n" +
                " float spec=pow(max(0.0,dot(reflect(-lightDir,n),viewDir)),32.0);\n" +
                " float fres=pow(1.0-max(0.0,dot(n,viewDir)),3.0);\n" +
                " col+=vec3(.20,.28,.40)*spec*.28+vec3(.08,.14,.23)*fres*.35;\n" +
                " float signBand=side*step(.34,vLocal.y)*step(vLocal.y,.44)*step(.55,h(vec2(vStyle*31.0,4.0)));\n" +
                " col+=signBand*palette(vStyle)*1.35;\n" +
                " float d=length(vWorld-uCam);float fog=1.0-exp(-d*uFog);\n" +
                " col=mix(col,vec3(.045,.070,.105),fog);\n" +
                " fragColor=vec4(col,1.0);\n" +
                "}\n";

        private final String GROUND_VS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "layout(location=0) in vec2 aPos; uniform mat4 uVP; uniform vec2 uCenter; uniform float uSize;\n" +
                "out vec3 vWorld;\n" +
                "void main(){vec3 w=vec3(uCenter.x+aPos.x*uSize,0.0,uCenter.y+aPos.y*uSize);vWorld=w;gl_Position=uVP*vec4(w,1.0);}\n";

        private final String GROUND_FS =
                "#version 300 es\n" +
                "precision highp float;\n" +
                "uniform vec3 uCam; uniform float uTime; in vec3 vWorld; out vec4 fragColor;\n" +
                "float h(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}\n" +
                "vec3 palette(float x){return .5+.5*cos(6.28318*(vec3(.02,.17,.34)+x*vec3(1.0,.72,.46)));}\n" +
                "void main(){\n" +
                " vec2 q=mod(vWorld.xz+14.0,28.0)-14.0;\n" +
                " float road=max(step(9.8,abs(q.x)),step(9.8,abs(q.y)));\n" +
                " float sidewalk=max(step(8.6,abs(q.x)),step(8.6,abs(q.y)))-road;\n" +
                " vec3 col=mix(vec3(.042,.047,.050),vec3(.008,.011,.015),road);\n" +
                " col=mix(col,vec3(.125,.13,.125),sidewalk*.82);\n" +
                " float laneX=(1.0-step(.045,abs(fract(vWorld.x*.055)-.5)))*step(9.8,abs(q.y));\n" +
                " float laneZ=(1.0-step(.045,abs(fract(vWorld.z*.055)-.5)))*step(9.8,abs(q.x));\n" +
                " col+=vec3(.82,.58,.14)*(laneX+laneZ)*.40;\n" +
                " float crossX=step(.72,fract(vWorld.x*.44))*step(abs(q.y),9.8)*step(11.0,abs(q.x));\n" +
                " float crossZ=step(.72,fract(vWorld.z*.44))*step(abs(q.x),9.8)*step(11.0,abs(q.y));\n" +
                " col+=vec3(.70)*(crossX+crossZ)*.14;\n" +
                " float wet=road*smoothstep(.57,.92,h(floor(vWorld.xz*1.8)));\n" +
                " float viewGrazing=pow(1.0-clamp((uCam.y)/max(2.0,length(vWorld-uCam)),0.0,1.0),3.0);\n" +
                " vec3 reflection=mix(vec3(.035,.080,.15),palette(vWorld.x*.018+vWorld.z*.011+uTime*.006),.48);\n" +
                " col+=reflection*wet*(.20+.46*viewGrazing);\n" +
                " float carV=mod(vWorld.z-uTime*8.0+floor(vWorld.x/28.0)*13.0,56.0);\n" +
                " float carH=mod(vWorld.x+uTime*7.0+floor(vWorld.z/28.0)*17.0,56.0);\n" +
                " float carLight=road*(smoothstep(2.3,0.0,abs(carV-7.0))*step(9.8,abs(q.x))+smoothstep(2.3,0.0,abs(carH-13.0))*step(9.8,abs(q.y)));\n" +
                " col+=vec3(.18,.46,1.0)*carLight*.36;\n" +
                " float fog=1.0-exp(-length(vWorld-uCam)*.0075);col=mix(col,vec3(.038,.058,.083),fog);\n" +
                " fragColor=vec4(col,1.0);\n" +
                "}\n";

        private final String BIRD_VS =
                "#version 300 es\n" +
                "precision highp float; uniform mat4 uVP; uniform vec3 uCam; uniform float uTime;\n" +
                "out float vAlpha;\n" +
                "void main(){float id=float(gl_VertexID);float a=id*1.618+uTime*.17;float r=18.0+mod(id*7.7,34.0);\n" +
                " vec3 p=uCam+vec3(cos(a)*r,11.0+mod(id*3.1,12.0)+sin(a*2.1)*1.5,sin(a)*r);\n" +
                " gl_Position=uVP*vec4(p,1.0);gl_PointSize=5.0+mod(id,4.0);vAlpha=.72;}\n";

        private final String BIRD_FS =
                "#version 300 es\n" +
                "precision highp float; in float vAlpha; out vec4 fragColor;\n" +
                "void main(){vec2 p=gl_PointCoord*2.0-1.0;float wing=abs(p.y+.45*abs(p.x));if(wing>.30||abs(p.x)>.92)discard;fragColor=vec4(.015,.020,.026,vAlpha);}\n";

        @Override
        public void onSurfaceCreated(GL10 unused, EGLConfig config) {
            skyProgram = link(SKY_VS, SKY_FS);
            buildingProgram = link(BUILDING_VS, BUILDING_FS);
            groundProgram = link(GROUND_VS, GROUND_FS);
            birdProgram = link(BIRD_VS, BIRD_FS);

            bVP = GLES30.glGetUniformLocation(buildingProgram, "uVP");
            bCam = GLES30.glGetUniformLocation(buildingProgram, "uCam");
            bFog = GLES30.glGetUniformLocation(buildingProgram, "uFog");

            gVP = GLES30.glGetUniformLocation(groundProgram, "uVP");
            gCam = GLES30.glGetUniformLocation(groundProgram, "uCam");
            gTime = GLES30.glGetUniformLocation(groundProgram, "uTime");
            gCenter = GLES30.glGetUniformLocation(groundProgram, "uCenter");
            gSize = GLES30.glGetUniformLocation(groundProgram, "uSize");

            sTime = GLES30.glGetUniformLocation(skyProgram, "uTime");
            sRes = GLES30.glGetUniformLocation(skyProgram, "uRes");
            sAngles = GLES30.glGetUniformLocation(skyProgram, "uAngles");

            birdVP = GLES30.glGetUniformLocation(birdProgram, "uVP");
            birdCam = GLES30.glGetUniformLocation(birdProgram, "uCam");
            birdTime = GLES30.glGetUniformLocation(birdProgram, "uTime");

            int[] ids = new int[3];
            GLES30.glGenBuffers(3, ids, 0);
            cubeVbo = ids[0];
            instanceVbo = ids[1];
            groundVbo = ids[2];

            uploadCube();
            uploadGround();

            startNs = System.nanoTime();
            lastNs = startNs;
            hudNs = startNs;
            scaleDecisionNs = startNs;

            GLES30.glEnable(GLES30.GL_BLEND);
            GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA, GLES30.GL_ONE_MINUS_SRC_ALPHA);
        }

        @Override
        public void onSurfaceChanged(GL10 unused, int w, int h) {
            width = Math.max(1, w);
            height = Math.max(1, h);
            rebuildTarget();
            rebuildCity(true);
        }

        private void uploadCube() {
            float[] cube = makeCube();
            FloatBuffer fb = floats(cube);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, cubeVbo);
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, cube.length * 4, fb, GLES30.GL_STATIC_DRAW);
        }

        private void uploadGround() {
            float[] g = {-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
            FloatBuffer fb = floats(g);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, groundVbo);
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, g.length * 4, fb, GLES30.GL_STATIC_DRAW);
        }

        private float[] makeCube() {
            float[][] c = {
                    {-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},
                    {-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}
            };
            int[][] faces = {
                    {1,5,6,2}, {4,0,3,7}, {3,2,6,7},
                    {4,5,1,0}, {5,4,7,6}, {0,1,2,3}
            };
            float[] out = new float[36*3];
            int k=0;
            for (int[] f : faces) {
                int[] tri = {f[0],f[1],f[2], f[0],f[2],f[3]};
                for (int idx : tri) {
                    out[k++]=c[idx][0]; out[k++]=c[idx][1]; out[k++]=c[idx][2];
                }
            }
            return out;
        }

        private FloatBuffer floats(float[] a) {
            FloatBuffer b = ByteBuffer.allocateDirect(a.length*4)
                    .order(ByteOrder.nativeOrder()).asFloatBuffer();
            b.put(a).position(0);
            return b;
        }

        private void rebuildTarget() {
            int rw = Math.max(480, Math.round(width * renderScale));
            int rh = Math.max(270, Math.round(height * renderScale));
            if (rw == renderW && rh == renderH && fbo != 0) return;
            renderW = rw;
            renderH = rh;

            int[] one = new int[1];
            if (colorTex != 0) {
                one[0]=colorTex; GLES30.glDeleteTextures(1, one, 0);
            }
            if (depthRbo != 0) {
                one[0]=depthRbo; GLES30.glDeleteRenderbuffers(1, one, 0);
            }
            if (fbo != 0) {
                one[0]=fbo; GLES30.glDeleteFramebuffers(1, one, 0);
            }

            GLES30.glGenTextures(1, one, 0);
            colorTex=one[0];
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, colorTex);
            GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D,0,GLES30.GL_RGBA,
                    renderW,renderH,0,GLES30.GL_RGBA,GLES30.GL_UNSIGNED_BYTE,null);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D,GLES30.GL_TEXTURE_MIN_FILTER,GLES30.GL_LINEAR);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D,GLES30.GL_TEXTURE_MAG_FILTER,GLES30.GL_LINEAR);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D,GLES30.GL_TEXTURE_WRAP_S,GLES30.GL_CLAMP_TO_EDGE);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D,GLES30.GL_TEXTURE_WRAP_T,GLES30.GL_CLAMP_TO_EDGE);

            GLES30.glGenRenderbuffers(1, one, 0);
            depthRbo=one[0];
            GLES30.glBindRenderbuffer(GLES30.GL_RENDERBUFFER, depthRbo);
            GLES30.glRenderbufferStorage(GLES30.GL_RENDERBUFFER,GLES30.GL_DEPTH_COMPONENT16,renderW,renderH);

            GLES30.glGenFramebuffers(1, one, 0);
            fbo=one[0];
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER,fbo);
            GLES30.glFramebufferTexture2D(GLES30.GL_FRAMEBUFFER,GLES30.GL_COLOR_ATTACHMENT0,
                    GLES30.GL_TEXTURE_2D,colorTex,0);
            GLES30.glFramebufferRenderbuffer(GLES30.GL_FRAMEBUFFER,GLES30.GL_DEPTH_ATTACHMENT,
                    GLES30.GL_RENDERBUFFER,depthRbo);
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER,0);
        }

        private float rand(int x, int z, int salt) {
            int n = x * 374761393 + z * 668265263 + salt * 1442695041;
            n = (n ^ (n >>> 13)) * 1274126177;
            n ^= (n >>> 16);
            return (n & 0x7fffffff) / 2147483647f;
        }

        private void rebuildCity(boolean force) {
            int bx = (int)Math.floor((camX + BLOCK*.5f) / BLOCK);
            int bz = (int)Math.floor((camZ + BLOCK*.5f) / BLOCK);
            if (!force && bx == cityBlockX && bz == cityBlockZ) return;
            cityBlockX = bx;
            cityBlockZ = bz;

            final int maxInstances = (CITY_RADIUS*2+1)*(CITY_RADIUS*2+1)*14;
            float[] data = new float[maxInstances*7];
            int count = 0;
            colliders.clear();

            for (int z=bz-CITY_RADIUS; z<=bz+CITY_RADIUS; z++) {
                for (int x=bx-CITY_RADIUS; x<=bx+CITY_RADIUS; x++) {
                    float style = rand(x,z,1);
                    float cx = x*BLOCK;
                    float cz = z*BLOCK;

                    if (style < .33f) {
                        for (int iz=0; iz<2; iz++) {
                            for (int ix=0; ix<2; ix++) {
                                float ox = ix==0 ? -5.0f : 5.0f;
                                float oz = iz==0 ? -5.0f : 5.0f;
                                float w = 6.6f + rand(x*2+ix,z*2+iz,3)*1.8f;
                                float d = 6.6f + rand(x*2+ix,z*2+iz,4)*1.8f;
                                float h = 12f + rand(x*2+ix,z*2+iz,5)*28f;
                                float st = rand(x*2+ix,z*2+iz,6);
                                count = addBox(data,count,cx+ox,h*.5f,cz+oz,w,h,d,st);
                                count = addBox(data,count,cx+ox,h+1.2f,cz+oz,w*.72f,2.4f,d*.72f,st+.17f);
                                colliders.add(new Collider(cx+ox,cz+oz,w*.5f,d*.5f));
                            }
                        }
                    } else {
                        float w = 13f + rand(x,z,7)*4.0f;
                        float d = 13f + rand(x,z,8)*4.0f;
                        float h = 28f + rand(x,z,9)*58f;
                        float podiumH = 5.0f + rand(x,z,10)*3.0f;
                        float st = rand(x,z,11);

                        count = addBox(data,count,cx,podiumH*.5f,cz,w*1.10f,podiumH,d*1.10f,st);
                        count = addBox(data,count,cx,podiumH+(h-podiumH)*.5f,cz,w,h-podiumH,d,st+.11f);

                        float setbackH = 6f + rand(x,z,12)*10f;
                        count = addBox(data,count,cx,h+setbackH*.5f,cz,w*.66f,setbackH,d*.66f,st+.23f);

                        if (rand(x,z,13) > .56f) {
                            count = addBox(data,count,cx,h+setbackH+2.2f,cz,w*.24f,4.4f,d*.24f,st+.31f);
                        }
                        colliders.add(new Collider(cx,cz,w*.57f,d*.57f));
                    }
                }
            }

            instanceCount = count;
            FloatBuffer fb = ByteBuffer.allocateDirect(count*7*4)
                    .order(ByteOrder.nativeOrder()).asFloatBuffer();
            fb.put(data,0,count*7).position(0);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, instanceVbo);
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER,count*7*4,fb,GLES30.GL_DYNAMIC_DRAW);
        }

        private int addBox(float[] data,int count,float x,float y,float z,float sx,float sy,float sz,float style) {
            int k=count*7;
            if (k+6>=data.length) return count;
            data[k]=x;data[k+1]=y;data[k+2]=z;
            data[k+3]=sx;data[k+4]=sy;data[k+5]=sz;
            data[k+6]=style-(float)Math.floor(style);
            return count+1;
        }

        private boolean blocked(float x,float z) {
            final float r=.48f;
            for (Collider c : colliders) {
                if (Math.abs(x-c.x)<c.hx+r && Math.abs(z-c.z)<c.hz+r) return true;
            }
            return false;
        }

        @Override
        public void onDrawFrame(GL10 unused) {
            long now=System.nanoTime();
            float dt=(now-lastNs)/1_000_000_000f;
            lastNs=now;

            if (dt>0f && dt<.12f) {
                float fps=1f/dt;
                fpsSmooth=fpsSmooth==0f?fps:fpsSmooth*.92f+fps*.08f;

                float mag=Math.min(1f,(float)Math.sqrt(moveX*moveX+moveY*moveY));
                float speed=7.2f*(.25f+.75f*mag);
                float sy=(float)Math.sin(yaw), cy=(float)Math.cos(yaw);
                float nx=camX+(sy*moveY+cy*moveX)*speed*dt;
                float nz=camZ+(cy*moveY-sy*moveX)*speed*dt;

                if (!blocked(nx,camZ)) camX=nx;
                if (!blocked(camX,nz)) camZ=nz;
            }

            rebuildCity(false);

            if (now-scaleDecisionNs>2_200_000_000L && fpsSmooth>0f) {
                scaleDecisionNs=now;
                float old=renderScale;
                if (fpsSmooth<44f) renderScale=Math.max(.64f,renderScale-.04f);
                else if (fpsSmooth>58f) renderScale=Math.min(.94f,renderScale+.025f);
                if (Math.abs(old-renderScale)>.001f) rebuildTarget();
            }

            float aspect=renderW/(float)Math.max(1,renderH);
            Matrix.perspectiveM(projection,0,72f,aspect,.08f,520f);

            float cp=(float)Math.cos(pitch), sp=(float)Math.sin(pitch);
            float sy=(float)Math.sin(yaw), cy=(float)Math.cos(yaw);
            float fx=sy*cp, fy=sp, fz=cy*cp;
            Matrix.setLookAtM(view,0,camX,camY,camZ,camX+fx,camY+fy,camZ+fz,0f,1f,0f);
            Matrix.multiplyMM(vp,0,projection,0,view,0);

            float time=(now-startNs)/1_000_000_000f;

            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER,fbo);
            GLES30.glViewport(0,0,renderW,renderH);
            GLES30.glClearColor(.008f,.012f,.020f,1f);
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT|GLES30.GL_DEPTH_BUFFER_BIT);

            // Sky
            GLES30.glDisable(GLES30.GL_DEPTH_TEST);
            GLES30.glUseProgram(skyProgram);
            GLES30.glUniform1f(sTime,time);
            GLES30.glUniform2f(sRes,renderW,renderH);
            GLES30.glUniform2f(sAngles,yaw,pitch);
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES,0,3);

            // Ground / roads / sidewalks
            GLES30.glEnable(GLES30.GL_DEPTH_TEST);
            GLES30.glUseProgram(groundProgram);
            GLES30.glUniformMatrix4fv(gVP,1,false,vp,0);
            GLES30.glUniform3f(gCam,camX,camY,camZ);
            GLES30.glUniform1f(gTime,time);
            GLES30.glUniform2f(gCenter,camX,camZ);
            GLES30.glUniform1f(gSize,BLOCK*(CITY_RADIUS+1.6f));
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER,groundVbo);
            GLES30.glEnableVertexAttribArray(0);
            GLES30.glVertexAttribPointer(0,2,GLES30.GL_FLOAT,false,2*4,0);
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES,0,6);

            // Instanced city geometry
            GLES30.glUseProgram(buildingProgram);
            GLES30.glUniformMatrix4fv(bVP,1,false,vp,0);
            GLES30.glUniform3f(bCam,camX,camY,camZ);
            GLES30.glUniform1f(bFog,.0056f);

            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER,cubeVbo);
            GLES30.glEnableVertexAttribArray(0);
            GLES30.glVertexAttribPointer(0,3,GLES30.GL_FLOAT,false,3*4,0);
            GLES30.glVertexAttribDivisor(0,0);

            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER,instanceVbo);
            int stride=7*4;
            GLES30.glEnableVertexAttribArray(1);
            GLES30.glVertexAttribPointer(1,3,GLES30.GL_FLOAT,false,stride,0);
            GLES30.glVertexAttribDivisor(1,1);
            GLES30.glEnableVertexAttribArray(2);
            GLES30.glVertexAttribPointer(2,3,GLES30.GL_FLOAT,false,stride,3*4);
            GLES30.glVertexAttribDivisor(2,1);
            GLES30.glEnableVertexAttribArray(3);
            GLES30.glVertexAttribPointer(3,1,GLES30.GL_FLOAT,false,stride,6*4);
            GLES30.glVertexAttribDivisor(3,1);

            GLES30.glDrawArraysInstanced(GLES30.GL_TRIANGLES,0,36,instanceCount);

            // Birds
            GLES30.glUseProgram(birdProgram);
            GLES30.glUniformMatrix4fv(birdVP,1,false,vp,0);
            GLES30.glUniform3f(birdCam,camX,camY,camZ);
            GLES30.glUniform1f(birdTime,time);
            GLES30.glDisable(GLES30.GL_DEPTH_TEST);
            GLES30.glDrawArrays(GLES30.GL_POINTS,0,52);

            // Present the completed scene directly, with linear upscaling.
            // This transfer does not depend on the instanced draw's vertex state.
            GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER,fbo);
            GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER,0);
            GLES30.glBlitFramebuffer(0,0,renderW,renderH,0,0,width,height,
                    GLES30.GL_COLOR_BUFFER_BIT,GLES30.GL_LINEAR);
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER,0);

            if (now-hudNs>350_000_000L) {
                hudNs=now;
                final float fps=fpsSmooth;
                final int rw=renderW,rh=renderH,ic=instanceCount;
                final float x=camX,z=camZ,sc=renderScale,yv=yaw;
                runOnUiThread(() -> hud.setText(String.format(Locale.US,
                        "CITY QUALITY TEST\n"+
                        "APK: %s\n"+
                        "FPS: %.1f   frame: %.2f ms\n"+
                        "internal: %dx%d (%.0f%%)\n"+
                        "position: %.1f, %.1f  yaw: %.0f°\n"+
                        "city geometry instances: %d\n"+
                        "rasterized 3D + wet roads + birds\n"+
                        "LEFT=move   RIGHT=look",
                        apkSizeText(),fps,fps>0?1000f/fps:0f,
                        rw,rh,sc*100f,x,z,Math.toDegrees(yv),ic)));
            }
        }

        private int shader(int type,String src) {
            int s=GLES30.glCreateShader(type);
            GLES30.glShaderSource(s,src);
            GLES30.glCompileShader(s);
            int[] ok=new int[1];
            GLES30.glGetShaderiv(s,GLES30.GL_COMPILE_STATUS,ok,0);
            if(ok[0]==0) {
                String log=GLES30.glGetShaderInfoLog(s);
                GLES30.glDeleteShader(s);
                throw new RuntimeException(log);
            }
            return s;
        }

        private int link(String vs,String fs) {
            int v=shader(GLES30.GL_VERTEX_SHADER,vs);
            int f=shader(GLES30.GL_FRAGMENT_SHADER,fs);
            int p=GLES30.glCreateProgram();
            GLES30.glAttachShader(p,v);
            GLES30.glAttachShader(p,f);
            GLES30.glLinkProgram(p);
            int[] ok=new int[1];
            GLES30.glGetProgramiv(p,GLES30.GL_LINK_STATUS,ok,0);
            GLES30.glDeleteShader(v);
            GLES30.glDeleteShader(f);
            if(ok[0]==0) {
                String log=GLES30.glGetProgramInfoLog(p);
                GLES30.glDeleteProgram(p);
                throw new RuntimeException(log);
            }
            return p;
        }

        private final class Collider {
            final float x,z,hx,hz;
            Collider(float x,float z,float hx,float hz) {
                this.x=x;this.z=z;this.hx=hx;this.hz=hz;
            }
        }
    }
}
