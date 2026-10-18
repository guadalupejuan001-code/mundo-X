package com.mundox.lab;

import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.util.concurrent.atomic.AtomicBoolean;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** One bounded native vertex buffer, one point draw, and a static reference grid. */
final class MicroLabRenderer implements GLSurfaceView.Renderer {
    interface Observer { void state(String json, float computeMs, float fps); void error(String message); }
    private static final int STRIDE = 9 * 4, CAPACITY = 16384;
    private final long handle;
    private final AtomicBoolean closed;
    private final Observer observer;
    private final ByteBuffer vertices = ByteBuffer.allocateDirect(CAPACITY * STRIDE).order(ByteOrder.nativeOrder());
    private final float[] view = new float[16], projection = new float[16], vp = new float[16], inverse = new float[16];
    private int program, buffer, gridBuffer, gridCount, width = 1, height = 1, mode;
    private float yaw = -1.15f, pitch = .8f, distance = 3.8f, fps;
    private long lastTime, lastReport, fpsStart;
    private int frames;
    private double accumulator;
    volatile boolean running = true;
    private boolean failed;

    MicroLabRenderer(long handle, AtomicBoolean closed, Observer observer) {
        this.handle = handle; this.closed = closed; this.observer = observer;
    }
    void play(boolean value) { running = value; lastTime = 0; accumulator = 0; }
    void command(int op, int a, int b, float x, float y) {
        if (closed.get() || failed) return;
        try {
            String json = NativeCore.microCommand(handle, op, a, b, x, y);
            if (op == 0) { mode = a; pitch = mode == 1 ? 1.25f : .8f; }
            if (op == 8 || op == 10) { accumulator = 0; lastTime = 0; }
            observer.state(json, 0, fps); lastReport = 0;
        } catch (RuntimeException e) { fail(e); }
    }
    void orbit(float dx, float dy) {
        yaw += dx * .008f; pitch = Math.max(.18f, Math.min(1.5f, pitch + dy * .006f));
    }
    void zoom(float scale) { distance = Math.max(2.4f, Math.min(7f, distance / scale)); }
    void touch(float x, float y, int brush) {
        if (mode == 2 || !Matrix.invertM(inverse, 0, vp, 0)) return;
        float[] a = {2*x/width-1, 1-2*y/height, -1, 1}, b = {a[0], a[1], 1, 1};
        float[] near = new float[4], far = new float[4];
        Matrix.multiplyMV(near, 0, inverse, 0, a, 0); Matrix.multiplyMV(far, 0, inverse, 0, b, 0);
        for (int i = 0; i < 3; i++) { near[i] /= near[3]; far[i] /= far[3]; }
        float dz = far[2] - near[2]; if (Math.abs(dz) < 1e-6f) return;
        float t = -near[2] / dz;
        float px = near[0] + t * (far[0]-near[0]), py = near[1] + t * (far[1]-near[1]);
        if (t >= 0 && Math.abs(px) <= 1 && Math.abs(py) <= 1) command(9, mode == 0 ? 2 : brush, 0, px, py);
    }
    @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        try {
            program = program(VERTEX, FRAGMENT);
            int[] ids = new int[2]; GLES30.glGenBuffers(2, ids, 0); buffer = ids[0]; gridBuffer = ids[1];
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, buffer);
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, CAPACITY * STRIDE, null, GLES30.GL_DYNAMIC_DRAW);
            FloatBuffer grid = ByteBuffer.allocateDirect(44 * STRIDE).order(ByteOrder.nativeOrder()).asFloatBuffer();
            for (int i = 0; i <= 10; i++) {
                float v = -1 + .2f * i;
                for (float[] p : new float[][]{{-1,v},{1,v},{v,-1},{v,1}})
                    grid.put(new float[]{p[0],p[1],-.06f,1,.10f,.19f,.22f,0,4});
            }
            grid.flip(); gridCount = 44;
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, gridBuffer);
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, grid.remaining()*4, grid, GLES30.GL_STATIC_DRAW);
            GLES30.glClearColor(.025f,.047f,.063f,1); GLES30.glEnable(GLES30.GL_BLEND);
            GLES30.glEnable(GLES30.GL_DEPTH_TEST); lastTime = 0; accumulator = 0;
        } catch (RuntimeException e) { fail(e); }
    }
    @Override public void onSurfaceChanged(GL10 gl, int w, int h) {
        width = Math.max(1,w); height = Math.max(1,h); GLES30.glViewport(0,0,width,height);
        Matrix.perspectiveM(projection,0,44,(float)width/height,.1f,30);
    }
    @Override public void onDrawFrame(GL10 gl) {
        if (closed.get() || failed) return;
        try {
            long now = System.nanoTime();
            if (running && lastTime != 0) accumulator += Math.min(.05, (now-lastTime)/1e9);
            lastTime = now;
            int steps = running ? Math.min(3,(int)(accumulator*60)) : 0;
            accumulator -= steps/60.0;
            long start = System.nanoTime();
            int count = NativeCore.microFrame(handle,steps,vertices);
            float compute = (System.nanoTime()-start)/1e6f;
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT | GLES30.GL_DEPTH_BUFFER_BIT);
            float flat = distance * (float)Math.cos(pitch);
            Matrix.setLookAtM(view,0,flat*(float)Math.cos(yaw),flat*(float)Math.sin(yaw),
                    distance*(float)Math.sin(pitch),0,0,0,0,0,1);
            Matrix.multiplyMM(vp,0,projection,0,view,0);
            GLES30.glUseProgram(program);
            GLES30.glUniformMatrix4fv(GLES30.glGetUniformLocation(program,"uVP"),1,false,vp,0);
            GLES30.glUniform1f(GLES30.glGetUniformLocation(program,"uHeight"),height);
            GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA,GLES30.GL_ONE_MINUS_SRC_ALPHA);
            GLES30.glDepthMask(true); attributes(gridBuffer); GLES30.glDrawArrays(GLES30.GL_LINES,0,gridCount);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER,buffer); vertices.position(0);
            GLES30.glBufferSubData(GLES30.GL_ARRAY_BUFFER,0,count*STRIDE,vertices);
            attributes(buffer); GLES30.glDepthMask(false);
            if (mode == 0) GLES30.glBlendFunc(GLES30.GL_SRC_ALPHA,GLES30.GL_ONE);
            GLES30.glDrawArrays(GLES30.GL_POINTS,0,count); GLES30.glDepthMask(true);
            int error = GLES30.glGetError(); if (error != GLES30.GL_NO_ERROR) throw new IllegalStateException("OpenGL " + error);
            if (fpsStart == 0) fpsStart = now;
            frames++;
            if (now-fpsStart >= 500000000L) { fps = frames*1e9f/(now-fpsStart); frames = 0; fpsStart = now; }
            if (now-lastReport >= 250000000L) { observer.state(NativeCore.microSnapshot(handle),compute,running ? fps : 0); lastReport = now; }
        } catch (RuntimeException e) { fail(e); }
    }
    private void fail(RuntimeException e) {
        if (!closed.get()) { failed = true; running = false; observer.error(e.getMessage()); }
    }
    private void attributes(int id) {
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER,id);
        int[] lengths = {3,1,3,1,1}, offsets = {0,12,16,28,32};
        for (int i=0;i<5;i++) { GLES30.glEnableVertexAttribArray(i); GLES30.glVertexAttribPointer(i,lengths[i],GLES30.GL_FLOAT,false,STRIDE,offsets[i]); }
    }
    private static int shader(int type,String source) {
        int s=GLES30.glCreateShader(type); GLES30.glShaderSource(s,source); GLES30.glCompileShader(s);
        int[] ok={0}; GLES30.glGetShaderiv(s,GLES30.GL_COMPILE_STATUS,ok,0);
        if(ok[0]==0) throw new IllegalStateException(GLES30.glGetShaderInfoLog(s)); return s;
    }
    private static int program(String vertex,String fragment) {
        int vs=shader(GLES30.GL_VERTEX_SHADER,vertex),fs=shader(GLES30.GL_FRAGMENT_SHADER,fragment);
        int p=GLES30.glCreateProgram(); GLES30.glAttachShader(p,vs); GLES30.glAttachShader(p,fs); GLES30.glLinkProgram(p);
        int[] ok={0}; GLES30.glGetProgramiv(p,GLES30.GL_LINK_STATUS,ok,0);
        GLES30.glDeleteShader(vs); GLES30.glDeleteShader(fs);
        if(ok[0]==0) throw new IllegalStateException(GLES30.glGetProgramInfoLog(p)); return p;
    }
    private static final String VERTEX = "#version 300 es\nprecision highp float;\n"
        + "layout(location=0) in vec3 aPosition; layout(location=1) in float aSize;"
        + "layout(location=2) in vec3 aColor; layout(location=3) in float aAngle; layout(location=4) in float aKind;"
        + "uniform mat4 uVP; uniform float uHeight; out vec3 vColor; out float vAngle; flat out int vKind;"
        + "void main(){ vec4 p=uVP*vec4(aPosition,1.); gl_Position=p; gl_PointSize=clamp(aSize*uHeight/p.w,2.,120.);"
        + "vec4 e=uVP*vec4(aPosition+vec3(cos(aAngle),sin(aAngle),0.)*.02,1.);"
        + "vec2 d=e.xy/e.w-p.xy/p.w; vAngle=atan(-d.y,d.x); vColor=aColor; vKind=int(aKind+.5); }";
    private static final String FRAGMENT = "#version 300 es\nprecision highp float;\n"
        + "in vec3 vColor; in float vAngle; flat in int vKind; out vec4 color;"
        + "void main(){ if(vKind==4){color=vec4(vColor,1.);return;} vec2 p=gl_PointCoord*2.-1.; float a;"
        + "if(vKind==1){float c=cos(vAngle),s=sin(vAngle); p=mat2(c,-s,s,c)*p;"
        + "float d=length(vec2(max(abs(p.x)-.38,0.),p.y))-.28; a=1.-smoothstep(-.06,.05,d);"
        + "color=vec4(vColor*(.72+.28*(1.-abs(p.y))),a);return;}"
        + "float r=length(p); if(r>1.)discard;"
        + "if(vKind==3){a=(1.-smoothstep(.55,1.,r))*.52;}"
        + "else if(vKind==2){a=1.-smoothstep(.8,1.,r);}else{a=exp(-3.5*r*r)*.75;}"
        + "color=vec4(vColor*(1.-.24*r),a);}";
}
