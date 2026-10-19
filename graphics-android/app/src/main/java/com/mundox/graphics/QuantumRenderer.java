package com.mundox.graphics;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.Serializable;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import static android.opengl.GLES30.*;

/** The GLES observer borrows the authoritative core prepared by the application. */
public final class QuantumRenderer implements GLSurfaceView.Renderer {
    public static final int MAX_OBJECTS=1_048_576;
    public interface Listener { void status(String text); }
    public interface CaptureListener { void captured(Capture image); }
    public static final class Capture {
        public final int width,height;
        public final byte[] rgba;
        public Capture(int w,int h,byte[] p){
            if(w<1||h<1||p==null||(long)w*h*4!=p.length)throw new IllegalArgumentException("Captura inválida");
            width=w;height=h;rgba=p;
        }
        public int idAt(int x,int y){
            if(x<0||x>=width||y<0||y>=height)throw new IllegalArgumentException("Píxel fuera de la captura");
            int p=((height-1-y)*width+x)*4;return (rgba[p]&255)+((rgba[p+1]&255)<<8)+((rgba[p+2]&255)<<16)-1;
        }
        public int changed(Capture b){
            if(width!=b.width||height!=b.height)throw new IllegalArgumentException("Different resolutions");
            int count=0;for(int i=0;i<rgba.length;i+=4)if(rgba[i]!=b.rgba[i]||rgba[i+1]!=b.rgba[i+1]||rgba[i+2]!=b.rgba[i+2])count++;
            return count;
        }
        public int nonBackground(){int n=0;for(int i=0;i<rgba.length;i+=4)if((rgba[i]&255)!=17||(rgba[i+1]&255)!=22||(rgba[i+2]&255)!=19)n++;return n;}
    }
    /** Logical world only: no GL names, native pointers, Activity or listeners. */
    public static final class Snapshot implements Serializable {
        private static final long serialVersionUID=1L;
        public final int objectCount,selectedId,resolutionLimit;
        public final int projectVersion;
        public final long seed;
        public final boolean overview,coupling;
        public final float yaw,pitch,zoom,time,quantumStrength;
        // Optional session metadata: old serialized sessions default this to 0.
        public final float enabledQuantumStrength;
        public final float cameraNear,cameraFar,cameraFov;
        public final float[] overrideEye,overrideTarget;
        public final String[] observables;
        public final int[] editIds,editValues;
        private Snapshot(QuantumRenderer r){
            objectCount=r.count;selectedId=r.selected;resolutionLimit=r.limit;
            projectVersion=1;seed=r.seed;
            overview=r.overview;coupling=r.strength!=0;
            yaw=r.yaw;pitch=r.pitch;zoom=r.zoom;time=r.timeValue;quantumStrength=r.strength;
            enabledQuantumStrength=r.enabledStrength;
            cameraNear=r.overrideNear;cameraFar=r.overrideFar;cameraFov=r.cameraFov;
            overrideEye=r.overrideEye==null?null:r.overrideEye.clone();
            overrideTarget=r.overrideTarget==null?null:r.overrideTarget.clone();
            observables=r.observables.clone();editIds=new int[r.edits.size()];editValues=new int[editIds.length];
            int index=0;for(Map.Entry<Integer,Integer> e:r.edits.entrySet()){
                editIds[index]=e.getKey();editValues[index++]=e.getValue();
            }
        }
        Snapshot(int count,int selected,int resolution,long seed,boolean overview,float yaw,float pitch,
                 float zoom,float time,float strength,float[] eye,float[] target,float near,float far,float fov,
                 String[] observables,int[] ids,int[] values){
            this.objectCount=count;this.selectedId=selected;this.resolutionLimit=resolution;
            this.projectVersion=1;this.seed=seed;this.overview=overview;this.coupling=strength!=0;
            this.yaw=yaw;this.pitch=pitch;this.zoom=zoom;this.time=time;this.quantumStrength=strength;
            this.enabledQuantumStrength=strength!=0?strength:.25f;
            this.overrideEye=eye==null?null:eye.clone();this.overrideTarget=target==null?null:target.clone();
            this.cameraNear=near;this.cameraFar=far;this.cameraFov=fov;
            this.observables=observables.clone();this.editIds=ids.clone();this.editValues=values.clone();
        }
    }
    private final NativeCore core;
    private final Listener listener;
    private final String vertexSource,fragmentSource;
    private String[] observables;
    private float[] coefficients;
    private long uploadedCore=-1;
    private long uploadedEpoch=-1;
    private final TreeMap<Integer,Integer> edits=new TreeMap<>();
    private final HashMap<String,Integer> locations=new HashMap<>();
    private int program,vao,editTexture,quantumTexture,colorTexture,depth,fbo;
    private int viewWidth=1,viewHeight=1,width=1,height=1,limit=640;
    private int count=1_000_000,selected=-1;
    private long seed=2026;
    private boolean framebufferDirty=true,editsDirty=true,failed=false;
    private boolean overview=true;
    private float yaw=.67f,pitch=.65f,zoom=1f,timeValue=0f,strength=.25f;
    private float enabledStrength=.25f,overrideNear=.1f,overrideFar=10000,cameraFov=48;
    private float[] overrideEye,overrideTarget;
    private float[] eye=new float[3],forward=new float[3],right=new float[3],up=new float[3],vp=new float[16];
    private float near,far,aspect;
    private CaptureListener capture;
    private int captureDebug;
    private int pickX=-1,pickY=-1;
    private long drawNumber;
    public volatile boolean hasFrame;
    public volatile String failure;
    public volatile String device="";

    public QuantumRenderer(Context context,NativeCore existingCore,Listener listener){
        this(context,existingCore,listener,null);
    }
    public QuantumRenderer(Context context,NativeCore existingCore,Listener listener,Snapshot state){
        if(existingCore==null||listener==null)throw new IllegalArgumentException("Falta el núcleo o el receptor del renderer");
        this.core=existingCore;this.listener=listener;
        if(state==null)observables=core.visualProgram();else restore(state);
        String common=asset(context,"shaders/common.glsl");
        vertexSource=asset(context,"shaders/eye.vert").replace("// @COMMON@",common);
        fragmentSource=asset(context,"shaders/eye.frag").replace("// @COMMON@",common);
    }
    private static boolean finite(float n){return !Float.isNaN(n)&&!Float.isInfinite(n);}
    private static void vector(float[] a){
        if(a==null||a.length!=3||!finite(a[0])||!finite(a[1])||!finite(a[2])
            ||Math.abs(a[0])>1_000_000||Math.abs(a[1])>1_000_000||Math.abs(a[2])>1_000_000)
            throw new IllegalArgumentException("Vector de cámara inválido");
    }
    private static void cameraPair(float[] position,float[] target){
        vector(position);vector(target);
        double dx=(double)position[0]-target[0],dy=(double)position[1]-target[1],dz=(double)position[2]-target[2];
        if(dx*dx+dy*dy+dz*dz<1e-8)throw new IllegalArgumentException("La cámara y su objetivo coinciden");
    }
    private void restore(Snapshot state){
        if(state.objectCount<1||state.objectCount>MAX_OBJECTS||state.selectedId < -1||state.selectedId>=state.objectCount
            ||state.resolutionLimit<64||state.resolutionLimit>4096||!finite(state.yaw)||!finite(state.pitch)
            ||state.pitch<.04f||state.pitch>1.52f||!finite(state.zoom)||state.zoom<.12f||state.zoom>6f
            ||!finite(state.time)||Math.abs(state.time)>1_000_000||!finite(state.quantumStrength)||Math.abs(state.quantumStrength)>.25f
            ||!finite(state.enabledQuantumStrength)||Math.abs(state.enabledQuantumStrength)>.25f
            ||state.coupling!=(state.quantumStrength!=0f)||state.observables==null||state.observables.length<1||state.observables.length>4096
            ||state.editIds==null||state.editValues==null||state.editIds.length!=state.editValues.length
            ||state.editIds.length>100_000||Math.abs(state.yaw)>1_000_000
            ||(state.projectVersion!=0&&(state.projectVersion!=1||state.seed<0||state.seed>0xffffffffL
               ||!finite(state.cameraNear)||!finite(state.cameraFar)||!finite(state.cameraFov)
               ||state.cameraNear<.001f||state.cameraNear>=state.cameraFar||state.cameraFar>1_000_000
               ||state.cameraFov<1||state.cameraFov>170)))
            throw new IllegalArgumentException("Estado visual guardado inválido");
        for(String pauli:state.observables){
            if(pauli==null)throw new IllegalArgumentException("Observable guardado nulo");
            int start=pauli.length()==core.qubits+1&&(pauli.charAt(0)=='+'||pauli.charAt(0)=='-')?1:0;
            if(pauli.length()-start!=core.qubits)throw new IllegalArgumentException("Longitud de observable guardado inválida");
            for(int q=start;q<pauli.length();q++)if("IXYZ".indexOf(pauli.charAt(q))<0)
                throw new IllegalArgumentException("Observable guardado inválido");
        }
        int previous=-1;
        for(int j=0;j<state.editIds.length;j++){
            int id=state.editIds[j],value=state.editValues[j];
            if(id<=previous||id>=MAX_OBJECTS||value<1||value>7)throw new IllegalArgumentException("Edición guardada inválida");
            edits.put(id,value);previous=id;
        }
        if(state.overrideEye!=null||state.overrideTarget!=null){
            cameraPair(state.overrideEye,state.overrideTarget);
            overrideEye=state.overrideEye.clone();overrideTarget=state.overrideTarget.clone();
        }
        count=state.objectCount;selected=state.selectedId;limit=state.resolutionLimit;overview=state.overview;
        yaw=state.yaw;pitch=state.pitch;zoom=state.zoom;timeValue=state.time;strength=state.quantumStrength;
        if(strength!=0)enabledStrength=strength;
        else if(state.enabledQuantumStrength!=0)enabledStrength=state.enabledQuantumStrength;
        if(state.projectVersion!=0){seed=state.seed;overrideNear=state.cameraNear;overrideFar=state.cameraFar;cameraFov=state.cameraFov;}
        observables=state.observables.clone();
    }
    private static String asset(Context c,String path){
        try(InputStream in=c.getAssets().open(path);ByteArrayOutputStream out=new ByteArrayOutputStream()){
            byte[] chunk=new byte[8192];int n;while((n=in.read(chunk))!=-1)out.write(chunk,0,n);return out.toString("UTF-8");
        }catch(Exception e){throw new IllegalStateException("No se pudo leer "+path,e);}
    }
    private static int texture(){int[] v=new int[1];glGenTextures(1,v,0);return v[0];}
    private int loc(String name){Integer v=locations.get(name);if(v==null){v=glGetUniformLocation(program,name);locations.put(name,v);}return v;}
    private void i(String name,int value){glUniform1i(loc(name),value);}
    private void f(String name,float value){glUniform1f(loc(name),value);}
    private void v(String name,float[] value){glUniform3f(loc(name),value[0],value[1],value[2]);}
    private static ByteBuffer direct(int bytes){return ByteBuffer.allocateDirect(bytes).order(ByteOrder.nativeOrder());}
    private static void check(String stage){int e=glGetError();if(e!=GL_NO_ERROR)throw new IllegalStateException(stage+" GL 0x"+Integer.toHexString(e));}
    private static int shader(int type,String source){
        int s=glCreateShader(type);glShaderSource(s,source);glCompileShader(s);int[] ok=new int[1];glGetShaderiv(s,GL_COMPILE_STATUS,ok,0);
        if(ok[0]==0){String message=glGetShaderInfoLog(s);glDeleteShader(s);throw new IllegalStateException("Shader: "+message);}return s;
    }
    @Override public void onSurfaceCreated(GL10 unused,EGLConfig config){
        // EGL context recreation invalidates every old GL name, never the world.
        program=vao=editTexture=quantumTexture=colorTexture=depth=fbo=0;
        hasFrame=false;failure=null;failed=false;locations.clear();
        int vert=0,frag=0;
        try{
            vert=shader(GL_VERTEX_SHADER,vertexSource);frag=shader(GL_FRAGMENT_SHADER,fragmentSource);
            program=glCreateProgram();glAttachShader(program,vert);glAttachShader(program,frag);glLinkProgram(program);
            int[] ok=new int[1];glGetProgramiv(program,GL_LINK_STATUS,ok,0);
            if(ok[0]==0)throw new IllegalStateException("Programa: "+glGetProgramInfoLog(program));
            int[] id=new int[1];glGenVertexArrays(1,id,0);vao=id[0];glBindVertexArray(vao);
            editTexture=texture();quantumTexture=texture();colorTexture=texture();
            glGenFramebuffers(1,id,0);fbo=id[0];glGenRenderbuffers(1,id,0);depth=id[0];
            glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
            framebufferDirty=true;editsDirty=true;uploadedCore=-1;uploadedEpoch=-1;failed=false;
            device=glGetString(GL_RENDERER);check("Inicio");
        }catch(Throwable e){if(program!=0){glDeleteProgram(program);program=0;}error(e);}
        finally{if(vert!=0)glDeleteShader(vert);if(frag!=0)glDeleteShader(frag);}
    }
    @Override public void onSurfaceChanged(GL10 unused,int w,int h){viewWidth=Math.max(1,w);viewHeight=Math.max(1,h);framebufferDirty=true;}
    private static void sampling(){
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    private void sizeFramebuffer(){
        int[] renderbufferMax=new int[1];glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,renderbufferMax,0);
        int supportedLimit=Math.min(limit,Math.min(maxTexture(),renderbufferMax[0]));
        if(supportedLimit<1)throw new IllegalStateException("No hay resolución GLES disponible");
        float scale=Math.min(1f,(float)supportedLimit/Math.max(viewWidth,viewHeight));
        width=Math.max(1,Math.round(viewWidth*scale));height=Math.max(1,Math.round(viewHeight*scale));
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,colorTexture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,null);sampling();
        glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,colorTexture,0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw new IllegalStateException("Framebuffer incompleto");
        framebufferDirty=false;check("Resolución");
    }
    private int maxTexture(){int[] n=new int[1];glGetIntegerv(GL_MAX_TEXTURE_SIZE,n,0);return n[0];}
    private void sync(){
        if(editsDirty){
            int max=maxTexture(),w=Math.min(Math.min(4096,max),Math.max(1,edits.size())),h=Math.max(1,(edits.size()+w-1)/w);
            if(h>max)throw new IllegalStateException("Las ediciones exceden la textura de este dispositivo");
            ByteBuffer bytes=direct(w*h*8);for(int j=0;j<w*h;j++){bytes.putInt(-1);bytes.putInt(-1);}bytes.rewind();
            for(Map.Entry<Integer,Integer> e:edits.entrySet()){bytes.putInt(e.getKey());bytes.putInt(e.getValue());}bytes.rewind();
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,editTexture);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RG32I,w,h,0,GL_RG_INTEGER,GL_INT,bytes);sampling();check("Ediciones");editsDirty=false;
        }
        long version,epoch;
        synchronized(core){
            version=core.version();epoch=core.epoch();
            if(version!=uploadedCore||epoch!=uploadedEpoch)coefficients=core.query(observables);
        }
        if(version!=uploadedCore||epoch!=uploadedEpoch){
            int max=maxTexture(),w=Math.min(Math.min(4096,max),coefficients.length),h=(coefficients.length+w-1)/w;
            if(h>max)throw new IllegalStateException("Los observables exceden la textura de este dispositivo");
            ByteBuffer bytes=direct(w*h*4);bytes.asFloatBuffer().put(coefficients);
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,quantumTexture);
            glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,w,h,0,GL_RED,GL_FLOAT,bytes);sampling();check("Observables");uploadedCore=version;uploadedEpoch=epoch;
        }
        glActiveTexture(GL_TEXTURE0);check("Estado");
    }
    private static float[] cross(float[] a,float[] b){return new float[]{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
    private static float[] normalize(float[] a){
        double n=Math.sqrt((double)a[0]*a[0]+(double)a[1]*a[1]+(double)a[2]*a[2]);
        if(!Double.isFinite(n)||n<1e-10)throw new IllegalArgumentException("Dirección de cámara inválida");
        return new float[]{(float)(a[0]/n),(float)(a[1]/n),(float)(a[2]/n)};
    }
    private void camera(){
        aspect=(float)width/height;float side=(float)Math.ceil(Math.sqrt(count)),extent=side*2.2f;
        float[] target=overview?new float[]{0,0,0}:new float[]{0,1,0};
        float distance=(overview?extent*Math.max(1.65f,1.4f/aspect):25f)*zoom;
        eye=new float[]{distance*(float)(Math.cos(pitch)*Math.sin(yaw)),target[1]+distance*(float)Math.sin(pitch),distance*(float)(Math.cos(pitch)*Math.cos(yaw))};
        near=overview?Math.max(.05f,extent*.01f):.1f;far=overview?Math.max(10000,distance+extent*4):5000;
        if(overrideEye!=null){eye=overrideEye;target=overrideTarget;near=overrideNear;far=overrideFar;}
        forward=normalize(new float[]{target[0]-eye[0],target[1]-eye[1],target[2]-eye[2]});
        right=normalize(cross(forward,Math.abs(forward[1])>.999f?new float[]{0,0,1}:new float[]{0,1,0}));up=cross(right,forward);
        float[] view=new float[16],projection=new float[16];
        Matrix.setLookAtM(view,0,eye[0],eye[1],eye[2],target[0],target[1],target[2],up[0],up[1],up[2]);
        Matrix.perspectiveM(projection,0,cameraFov,aspect,near,far);Matrix.multiplyMM(vp,0,projection,0,view,0);
    }
    private double draw(int debug){
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,width,height);glUseProgram(program);glBindVertexArray(vao);
        int side=(int)Math.ceil(Math.sqrt(count));
        i("uCount",count);i("uSide",side);i("uEditCount",edits.size());i("uSelected",selected);i("uDebug",debug);i("uEdits",0);i("uQuantum",1);i("uQuantumCount",coefficients.length);
        int seedBits=(int)seed;glUniform4ui(loc("uSeedBytes"),seedBits&255,(seedBits>>>8)&255,(seedBits>>>16)&255,(seedBits>>>24)&255);
        f("uTime",timeValue);f("uFogDistance",Math.max(50,side*2.2f));f("uQuantumStrength",strength);
        f("uNear",near);f("uFar",far);f("uAspect",aspect);f("uTanHalfFov",(float)Math.tan(Math.toRadians(cameraFov*.5)));
        v("uEye",eye);v("uCameraForward",forward);v("uCameraRight",right);v("uCameraUp",up);
        glUniform2f(loc("uResolution"),width,height);glUniformMatrix4fv(loc("uVp"),1,false,vp,0);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,editTexture);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,quantumTexture);
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(true);
        if(debug==1)glClearColor(0,0,0,1);else glClearColor(.067f,.086f,.075f,1);
        glFinish();long started=System.nanoTime();glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);glFinish();
        double elapsed=(System.nanoTime()-started)/1e6;check("Observación");return elapsed;
    }
    private Capture read(){
        ByteBuffer bytes=direct(width*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,bytes);byte[] data=new byte[bytes.capacity()];bytes.rewind();bytes.get(data);
        for(int j=0;j<data.length;j+=4)if((data[j]&255)==255&&(data[j+1]&255)==0&&(data[j+2]&255)==255)throw new IllegalStateException("Se agotó el recorrido de celdas");
        check("Lectura");return new Capture(width,height,data);
    }
    @Override public void onDrawFrame(GL10 unused){
        if(failed||program==0)return;
        try{
            if(framebufferDirty)sizeFramebuffer();sync();camera();
            if(pickX>=0){
                draw(1);int x=(int)Math.min(width-1,Math.max(0,(long)pickX*width/viewWidth));
                int y=(int)Math.min(height-1,Math.max(0,height-1-(long)pickY*height/viewHeight));
                ByteBuffer bytes=direct(4);glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bytes);
                selected=(bytes.get(0)&255)+((bytes.get(1)&255)<<8)+((bytes.get(2)&255)<<16)-1;pickX=-1;pickY=-1;
                check("Selección");
                if(selected>=count)throw new IllegalStateException("El recorrido devolvió un ID inválido");
            }
            CaptureListener requested=capture;int debug=requested==null?0:captureDebug;
            double elapsed=draw(debug);Capture pixels=requested==null?null:read();
            glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
            glBlitFramebuffer(0,0,width,height,0,0,viewWidth,viewHeight,GL_COLOR_BUFFER_BIT,GL_LINEAR);glBindFramebuffer(GL_FRAMEBUFFER,0);
            check("Pantalla");
            hasFrame=true;drawNumber++;
            if(requested!=null)capture=null;
            listener.status(String.format(Locale.US,"%,d objetos · %dx%d · %.1f ms\n%d qubits · %d observables · estado %d%s\n%s",count,width,height,elapsed,core.qubits,coefficients.length,uploadedCore,selected>=0?" · ID "+selected:"",device));
            if(requested!=null)requested.captured(pixels);
        }catch(Throwable e){error(e);}
    }
    private void error(Throwable e){failed=true;failure=e.toString();listener.status("No se pudo dibujar: "+failure);}
    public void gate(int op,int a,int b){core.apply(op,a,b);}
    public void reset(){
        synchronized(core){core.resetGraph();observables=core.visualProgram();}
        uploadedCore=-1;uploadedEpoch=-1;edits.clear();editsDirty=true;selected=-1;pickX=-1;pickY=-1;timeValue=0;
    }
    public void setTime(float seconds){
        if(!finite(seconds)||Math.abs(seconds)>1_000_000)throw new IllegalArgumentException("Tiempo fuera del rango del proyecto");
        timeValue=seconds;
    }
    public void toggleView(){overview=!overview;pitch=overview?.65f:.3f;zoom=1;overrideEye=null;overrideTarget=null;cameraFov=48;}
    public void streetView(){overview=false;pitch=.3f;zoom=1;overrideEye=null;overrideTarget=null;cameraFov=48;}
    public void orbit(float dx,float dy){
        if(!finite(dx)||!finite(dy))throw new IllegalArgumentException("Movimiento de cámara inválido");
        yaw=(yaw+dx*.006f)%(float)(Math.PI*2);pitch=Math.max(.04f,Math.min(1.52f,pitch+dy*.005f));overrideEye=null;overrideTarget=null;cameraFov=48;
    }
    public void zoom(float factor){
        if(!finite(factor)||factor<=0)throw new IllegalArgumentException("Zoom inválido");
        zoom=Math.max(.12f,Math.min(6,zoom/factor));overrideEye=null;overrideTarget=null;cameraFov=48;
    }
    public void resolution(int pixels){
        if(pixels<64||pixels>4096)throw new IllegalArgumentException("Resolución fuera de rango");
        limit=pixels;framebufferDirty=true;
    }
    public void objectCount(int n){
        if(n<1||n>MAX_OBJECTS)throw new IllegalArgumentException("Número de objetos");
        count=n;if(selected>=n)selected=-1;
        // Off-range IDs retain their edits and reappear intact when count grows.
    }
    public void worldSeed(long value){
        if(value<0||value>0xffffffffL)throw new IllegalArgumentException("Semilla fuera de rango");
        seed=value;
    }
    public void coupling(boolean enabled){strength=enabled?enabledStrength:0;}
    public void pick(int x,int y){selected=-1;pickX=Math.max(0,x);pickY=Math.max(0,y);}
    public void editSelected(int action){
        if(action==-1)action=7;
        if(action<0||action>7)throw new IllegalArgumentException("Edición inválida");
        if(selected<0||selected>=count)return;
        int value=action==1?Math.min(6,edits.getOrDefault(selected,0)+1):action;
        if(value!=0&&!edits.containsKey(selected)&&edits.size()>=100_000)
            throw new IllegalStateException("El proyecto alcanzó 100.000 ediciones");
        if(value==0)edits.remove(selected);else edits.put(selected,value);editsDirty=true;
    }
    public void requestCapture(boolean ids,CaptureListener listener){
        if(listener==null)throw new IllegalArgumentException("Falta el receptor de captura");
        if(capture!=null)throw new IllegalStateException("Ya hay una captura pendiente");
        capture=listener;captureDebug=ids?1:0;
    }
    public void testCamera(float[] position,float[] target){
        cameraPair(position,target);overrideEye=position.clone();overrideTarget=target.clone();overrideNear=.1f;overrideFar=10000;cameraFov=48;
    }
    /** Call on the GL thread, after preceding queued world changes. */
    public Snapshot snapshot(){return new Snapshot(this);}
    public int selectedId(){return selected;}
    public int objectCount(){return count;}
    public boolean overview(){return overview;}
    public int resolution(){return limit;}
}
