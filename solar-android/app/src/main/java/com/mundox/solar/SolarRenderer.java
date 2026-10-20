package com.mundox.solar;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import com.mundox.graphics.RenderResolution;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Locale;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import static android.opengl.GLES30.*;

/**
 * COSMIC X Solar: a native extension of graphics-android's QuantumRenderer.
 * The analytic eye observes a solar world instead of the original object grid.
 * The existing CompactCore remains authoritative and is only queried through
 * QuantumSolarBridge. Its expectations control illustrative trace sampling;
 * positions, sizes, periods and magnetic constants belong to SolarData.
 *
 * Mutations and requestCapture run on the GLSurfaceView's GL thread. Published
 * statistics and selection/date accessors are safe to read on the UI thread.
 */
public final class SolarRenderer implements GLSurfaceView.Renderer {
    public interface Listener { void status(String status); void selected(int body); }
    public interface CaptureListener { void captured(Capture image); }
    public static final class Capture {
        public final int width,height;
        /** RGBA8, OpenGL bottom row first. */
        public final byte[] rgba;
        public Capture(int width,int height,byte[] rgba){this.width=width;this.height=height;this.rgba=rgba;}
    }

    private static final int MAX_BODIES=24,MAX_OVERLAY_VERTICES=42000;
    private static final double TAU=Math.PI*2,AU_UNITS=100.0;
    private static final float FOV=47f,TAN_HALF_FOV=(float)Math.tan(Math.toRadians(FOV/2));
    private final Context context;
    private final QuantumSolarBridge bridge;
    private final Listener listener;
    private final String sceneVertex,sceneFragment,overlayVertex,overlayFragment;
    private final HashMap<String,Integer> sceneLocations=new HashMap<>();
    private final HashMap<String,Integer> overlayLocations=new HashMap<>();
    private final double[][] positions=new double[MAX_BODIES][3];
    private final double[][] physical=new double[MAX_BODIES][3];
    private final double[] radii=new double[MAX_BODIES];
    private final float[] packedBodies=new float[MAX_BODIES*4];
    private final float[] appearance=new float[MAX_BODIES*4],colors=new float[MAX_BODIES*4];
    private final float[] atmosphere=new float[MAX_BODIES*4],atmosphereColors=new float[MAX_BODIES*3];
    private final float[] axes=new float[MAX_BODIES*3];
    private final double[][] poleBasis=new double[MAX_BODIES][9];
    private final int[] layers=new int[MAX_BODIES];
    private final float[] quantum=new float[8],ether=new float[4];
    private final double[][] orbitCache=new double[MAX_BODIES][];
    private double orbitCacheDate=Double.NaN;
    private final double[] cameraEye={0,0,0},cameraTarget={0,0,0},panOffset={0,0,0};
    private final float[] forward=new float[3],right=new float[3],up=new float[3];
    private final float[] view=new float[16],projection=new float[16],vp=new float[16];
    private final FloatBuffer lines=direct(MAX_OVERLAY_VERTICES*7*4).asFloatBuffer();
    private final FloatBuffer points=direct(4096*7*4).asFloatBuffer();
    private int sceneProgram,overlayProgram,eyeVao,overlayVao,overlayVbo;
    private int mapTexture,bodyMaskTexture,colorTexture,depthBuffer,framebuffer;
    private int bodyMaskTilesX,bodyMaskTilesY;
    private SolarVisualEther.Plan visualPlan;
    private volatile int viewWidth=1,viewHeight=1,width=1,height=1;
    private int quality=960,acceptedQuality=960,mapWidth=2048,mapHeight=1024;
    private boolean requestedUhd,requestedUltra;
    private volatile boolean activeUhd,activeUltra;
    private volatile String resolutionMessage;
    private volatile RenderResolution.Fit display=RenderResolution.fit(1,1,new RenderResolution.Size(1,1));
    private volatile boolean entryMode,atmosphereEnabled=true;
    private double entryLatitude,entryLongitude,entryHeading,entryLookPitch;
    private volatile double entryAltitude;
    private boolean localPhysics;
    private SolarFlightPhysics.State flightState=new SolarFlightPhysics.State(35,0);
    private volatile double[] publishedEntry;
    private double sceneScale=1;
    private boolean framebufferDirty=true,failed=false,initializedCamera=false;
    private boolean freeFlight=false,fields=false,wind=true,orbits=true,realScale=false;
    private boolean isOverview=false,resetDistance=true;
    private volatile boolean paused=false;
    private volatile int selected=3;
    private volatile double jd=2461332.5;
    private double speed=.2,distance=7,desiredDistance=7;
    private double yaw=.9,pitch=.16,clockSeconds=0;
    private float near=.001f,far=2000f,aspect=1f;
    private long previousNanos,fpsNanos,statusNanos,framesAtFps,uploadedRevision=Long.MIN_VALUE;
    private CaptureListener capture;
    public volatile String failure,device="";
    public volatile boolean hasFrame;
    public volatile float fps;
    public volatile long frameCount;

    public SolarRenderer(Context context,QuantumSolarBridge bridge,Listener listener){
        if(context==null||bridge==null||listener==null)throw new IllegalArgumentException("Falta el contexto, núcleo o receptor visual");
        if(SolarData.BODIES.length>MAX_BODIES)throw new IllegalArgumentException("La escena admite hasta 24 cuerpos");
        this.context=context.getApplicationContext();this.bridge=bridge;this.listener=listener;
        sceneVertex=asset(context,"shaders/solar.vert");sceneFragment=asset(context,"shaders/solar.frag");
        overlayVertex=asset(context,"shaders/overlay.vert");overlayFragment=asset(context,"shaders/overlay.frag");
        jd=Math.max(SolarData.MIN_JD,Math.min(SolarData.MAX_JD,System.currentTimeMillis()/86400000.0+2440587.5));
        for(int i=0;i<MAX_BODIES;i++)layers[i]=-1;
        for(SolarData.Body body:SolarData.BODIES){
            double[] pole;
            if(body.id<=8)pole=SolarData.spinAxis(body.id);
            else{
                // Moon spin poles use their own parent-relative orbital normal.
                // This is a declared synchronous-orientation approximation, not
                // an IAU prime-meridian/libration solution or the ecliptic pole.
                double[] a=SolarData.orbitPosition(body.id,0,jd),b=SolarData.orbitPosition(body.id,.0001,jd);
                double[] parent=SolarData.position(body.parent,jd);
                for(int k=0;k<3;k++){a[k]-=parent[k];b[k]-=parent[k];}
                pole=cross(a,b);normalize(pole);
            }
            double[] reference=Math.abs(pole[2])>.98?new double[]{0,1,0}:new double[]{0,0,1};
            double[] bx=cross(pole,reference);normalize(bx);double[] bz=cross(bx,pole);
            for(int k=0;k<3;k++){axes[body.id*3+k]=(float)pole[k];poleBasis[body.id][k]=bx[k];poleBasis[body.id][3+k]=pole[k];poleBasis[body.id][6+k]=bz[k];}
        }
        syncWorld();chooseFocusAngle();
    }

    private static boolean finite(double value){return !Double.isNaN(value)&&!Double.isInfinite(value);}
    private static ByteBuffer direct(int bytes){return ByteBuffer.allocateDirect(bytes).order(ByteOrder.nativeOrder());}
    private static String asset(Context context,String path){
        try(InputStream in=context.getAssets().open(path);ByteArrayOutputStream out=new ByteArrayOutputStream()){
            byte[] b=new byte[8192];int n;while((n=in.read(b))!=-1)out.write(b,0,n);return out.toString("UTF-8");
        }catch(Exception e){throw new IllegalStateException("No se pudo abrir "+path,e);}
    }
    private static int shader(int type,String source){
        int shader=glCreateShader(type);glShaderSource(shader,source);glCompileShader(shader);
        int[] success={0};glGetShaderiv(shader,GL_COMPILE_STATUS,success,0);
        if(success[0]==0){String message=glGetShaderInfoLog(shader);glDeleteShader(shader);throw new IllegalStateException("Shader solar: "+message);}
        return shader;
    }
    private static int program(String vertex,String fragment){
        int v=0,f=0,p=0;
        try{
            v=shader(GL_VERTEX_SHADER,vertex);f=shader(GL_FRAGMENT_SHADER,fragment);
            p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
            int[] success={0};glGetProgramiv(p,GL_LINK_STATUS,success,0);
            if(success[0]==0)throw new IllegalStateException("Programa solar: "+glGetProgramInfoLog(p));
            return p;
        }catch(RuntimeException e){if(p!=0)glDeleteProgram(p);throw e;}
        finally{if(v!=0)glDeleteShader(v);if(f!=0)glDeleteShader(f);}
    }
    private static int newTexture(){int[] n={0};glGenTextures(1,n,0);return n[0];}
    private int sceneLoc(String name){Integer l=sceneLocations.get(name);if(l==null){l=glGetUniformLocation(sceneProgram,name);sceneLocations.put(name,l);}return l;}
    private int overlayLoc(String name){Integer l=overlayLocations.get(name);if(l==null){l=glGetUniformLocation(overlayProgram,name);overlayLocations.put(name,l);}return l;}
    private static void check(String where){int error=glGetError();if(error!=GL_NO_ERROR)throw new IllegalStateException(where+" GLES 0x"+Integer.toHexString(error));}

    @Override public void onSurfaceCreated(GL10 unused,EGLConfig config){
        // EGL recreation invalidates GL names, never the logical solar world/core.
        sceneProgram=overlayProgram=eyeVao=overlayVao=overlayVbo=0;
        mapTexture=bodyMaskTexture=colorTexture=depthBuffer=framebuffer=0;
        bodyMaskTilesX=bodyMaskTilesY=0;visualPlan=null;
        activeUhd=false;activeUltra=false;
        sceneLocations.clear();overlayLocations.clear();hasFrame=false;failed=false;failure=null;
        previousNanos=fpsNanos=statusNanos=0;uploadedRevision=Long.MIN_VALUE;
        try{
            sceneProgram=program(sceneVertex,sceneFragment);overlayProgram=program(overlayVertex,overlayFragment);
            int[] names={0};glGenVertexArrays(1,names,0);eyeVao=names[0];
            glGenVertexArrays(1,names,0);overlayVao=names[0];
            glGenBuffers(1,names,0);overlayVbo=names[0];glBindVertexArray(overlayVao);
            glBindBuffer(GL_ARRAY_BUFFER,overlayVbo);
            glBufferData(GL_ARRAY_BUFFER,MAX_OVERLAY_VERTICES*7*4,null,GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,false,7*4,0);
            glEnableVertexAttribArray(1);glVertexAttribPointer(1,4,GL_FLOAT,false,7*4,3*4);
            glBindVertexArray(0);
            loadMaps();
            bodyMaskTexture=newTexture();
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,bodyMaskTexture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glActiveTexture(GL_TEXTURE0);
            glDisable(GL_DITHER);glDisable(GL_CULL_FACE);glClearColor(0,0,0,1);
            framebufferDirty=true;device=glGetString(GL_RENDERER);check("Inicio del observador solar");
        }catch(Throwable e){error(e);}
    }
    @Override public void onSurfaceChanged(GL10 unused,int w,int h){
        viewWidth=Math.max(1,w);viewHeight=Math.max(1,h);framebufferDirty=true;
        if(!initializedCamera||isOverview)resetDistance=true;
    }

    private Bitmap loadMap(String path){
        if(path==null||path.isEmpty())return null;
        try(InputStream in=context.getAssets().open(path)){
            BitmapFactory.Options options=new BitmapFactory.Options();options.inPreferredConfig=Bitmap.Config.ARGB_8888;
            Bitmap source=BitmapFactory.decodeStream(in,null,options);if(source==null)return null;
            Bitmap scaled=Bitmap.createScaledBitmap(source,mapWidth,mapHeight,true);if(scaled!=source)source.recycle();return scaled;
        }catch(Exception ignored){return null;}
    }
    private void loadMaps(){
        int[] maximum={0};glGetIntegerv(GL_MAX_TEXTURE_SIZE,maximum,0);
        mapWidth=maximum[0]>=2048?2048:1024;mapHeight=mapWidth/2;
        ArrayList<Bitmap> maps=new ArrayList<>();
        for(int i=0;i<MAX_BODIES;i++)layers[i]=-1;
        try{
            for(SolarData.Body body:SolarData.BODIES){
                Bitmap bitmap=loadMap(body.texture);
                if(bitmap==null&&body.id==3)bitmap=loadMap("textures/earth.png");
                if(bitmap==null&&body.id==3)bitmap=loadMap("textures/earth.jpg");
                if(bitmap==null&&body.id==9)bitmap=loadMap("textures/moon.jpg");
                if(bitmap==null&&body.id==5)bitmap=loadMap("textures/jupiter.jpg");
                if(bitmap!=null){layers[body.id]=maps.size();maps.add(bitmap);}
            }
            mapTexture=newTexture();glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,mapTexture);
            glTexStorage3D(GL_TEXTURE_2D_ARRAY,mapWidth==2048?12:11,GL_RGBA8,mapWidth,mapHeight,Math.max(1,maps.size()));
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            int[] pixels=new int[mapWidth*mapHeight];ByteBuffer bytes=direct(mapWidth*mapHeight*4);
            for(int layer=0;layer<maps.size();layer++){
                maps.get(layer).getPixels(pixels,0,mapWidth,0,0,mapWidth,mapHeight);bytes.clear();
                for(int p:pixels){bytes.put((byte)(p>>16));bytes.put((byte)(p>>8));bytes.put((byte)p);bytes.put((byte)255);}
                bytes.flip();glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,layer,mapWidth,mapHeight,1,GL_RGBA,GL_UNSIGNED_BYTE,bytes);
            }
            if(maps.isEmpty()){
                bytes.clear();for(int i=0;i<mapWidth*mapHeight;i++)bytes.putInt(-1);bytes.flip();
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,0,mapWidth,mapHeight,1,GL_RGBA,GL_UNSIGNED_BYTE,bytes);
            }
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);check("Mapas planetarios");
        }finally{for(Bitmap bitmap:maps)if(!bitmap.isRecycled())bitmap.recycle();}
    }
    private static void deleteTarget(int color,int depth,int target){
        if(target!=0)glDeleteFramebuffers(1,new int[]{target},0);
        if(depth!=0)glDeleteRenderbuffers(1,new int[]{depth},0);
        if(color!=0)glDeleteTextures(1,new int[]{color},0);
    }
    private static void allocationCheck(String stage){
        int error=glGetError();if(error==GL_NO_ERROR)return;
        for(int n=0;n<16&&glGetError()!=GL_NO_ERROR;n++){}
        throw new IllegalStateException(stage+": "+(error==GL_OUT_OF_MEMORY?"memoria gráfica insuficiente":"GLES 0x"+Integer.toHexString(error)));
    }
    /** Same exact UHD/FIT geometry as the standalone COSMIC X 4K observer. */
    private RenderResolution.Limits resolutionLimits(){
        int[] caps=new int[4];glGetIntegerv(GL_MAX_TEXTURE_SIZE,caps,0);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,caps,1);glGetIntegerv(GL_MAX_VIEWPORT_DIMS,caps,2);
        check("Límites de resolución");return new RenderResolution.Limits(caps[0],caps[1],caps[2],caps[3]);
    }
    /** Candidate attachments are checked before any current GL name is replaced. */
    private void installTarget(RenderResolution.Size size,int mode,int limit){
        RenderResolution.Fit fit=RenderResolution.fit(viewWidth,viewHeight,size);
        if(framebuffer==0||width!=size.width||height!=size.height){
            RenderResolution.Size existing=framebuffer==0?null:new RenderResolution.Size(width,height);
            RenderResolution.requireAllocation(size,existing);
            int color=0,depth=0,target=0;boolean accepted=false;
            try{
                color=newTexture();int[] name={0};glGenRenderbuffers(1,name,0);depth=name[0];
                glGenFramebuffers(1,name,0);target=name[0];allocationCheck("Crear framebuffer");
                if(color==0||depth==0||target==0)throw new IllegalStateException("No se pudo reservar el framebuffer");
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,color);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size.width,size.height,0,GL_RGBA,GL_UNSIGNED_BYTE,null);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                allocationCheck("Reservar color");
                glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,size.width,size.height);
                allocationCheck("Reservar profundidad");
                int[] actual=new int[2];glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,actual,0);
                glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,actual,1);allocationCheck("Dimensiones asignadas");
                if(actual[0]!=size.width||actual[1]!=size.height)throw new IllegalStateException("El controlador no asignó la resolución solicitada");
                glBindFramebuffer(GL_FRAMEBUFFER,target);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
                int status=glCheckFramebufferStatus(GL_FRAMEBUFFER);allocationCheck("Comprobar framebuffer");
                if(status!=GL_FRAMEBUFFER_COMPLETE)throw new IllegalStateException("Framebuffer incompleto (0x"+Integer.toHexString(status)+")");
                int oldColor=colorTexture,oldDepth=depthBuffer,oldTarget=framebuffer;
                colorTexture=color;depthBuffer=depth;framebuffer=target;width=size.width;height=size.height;
                accepted=true;deleteTarget(oldColor,oldDepth,oldTarget);
            }finally{
                if(!accepted)deleteTarget(color,depth,target);
                glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glBindRenderbuffer(GL_RENDERBUFFER,depthBuffer);
                glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,0);
            }
        }
        display=fit;aspect=(float)width/height;quality=acceptedQuality=limit;
        activeUhd=requestedUhd=mode==RenderResolution.UHD_4K;
        activeUltra=requestedUltra=mode==RenderResolution.UHD_8K;
        framebufferDirty=false;
    }
    private void sizeFramebuffer(){
        RenderResolution.Limits caps=null;
        RenderResolution.Size uhd=RenderResolution.uhd(viewWidth,viewHeight);
        RenderResolution.Size ultra=RenderResolution.uhd8k(viewWidth,viewHeight);
        boolean wantedUltra=requestedUltra,wantedUhd=requestedUhd&&!wantedUltra;
        int wantedQuality=quality;
        int wantedMode=wantedUltra?RenderResolution.UHD_8K:wantedUhd?RenderResolution.UHD_4K:RenderResolution.ADAPTIVE;
        RenderResolution.Size requested=wantedUltra?ultra:uhd;
        String request=wantedUltra?"UHD 8K "+ultra.width+"×"+ultra.height
                :wantedUhd?"UHD 4K "+uhd.width+"×"+uhd.height:wantedQuality+" px";
        try{
            caps=resolutionLimits();
            RenderResolution.Size size=wantedMode==RenderResolution.ADAPTIVE
                    ?RenderResolution.adaptive(viewWidth,viewHeight,wantedQuality,caps):requested;
            caps.require(size);installTarget(size,wantedMode,wantedQuality);resolutionMessage=null;
        }catch(RuntimeException|OutOfMemoryError rejected){
            String reason=rejected.getMessage();if(reason==null)reason=rejected.getClass().getSimpleName();
            if(framebuffer!=0){
                requestedUhd=activeUhd;requestedUltra=activeUltra;quality=acceptedQuality;framebufferDirty=false;
                display=RenderResolution.fit(viewWidth,viewHeight,new RenderResolution.Size(width,height));
                resolutionMessage="No se pudo activar "+request+": "+reason+". Se conserva "+width+"×"+height+".";return;
            }
            if(caps==null)throw rejected;
            int fallback=wantedQuality;
            while(true){
                try{
                    installTarget(RenderResolution.adaptive(viewWidth,viewHeight,fallback,caps),RenderResolution.ADAPTIVE,fallback);
                    resolutionMessage="No se pudo activar "+request+": "+reason+". Se usa modo adaptado "+width+"×"+height+".";return;
                }catch(RuntimeException|OutOfMemoryError unavailable){
                    if(fallback<=64)throw new IllegalStateException("No se pudo asignar un framebuffer mínimo",unavailable);
                    fallback=Math.max(64,Math.min(640,fallback/2));
                }
            }
        }
    }

    /** Readable mode compresses orbital radii and magnifies spheres explicitly. */
    private double displayRadius(SolarData.Body body){
        if(realScale)return body.radiusKm/SolarData.AU_KM*AU_UNITS;
        if(body.id==0)return 5.2;
        return Math.max(.17,.92*Math.pow(body.radiusKm/6371.0,.49));
    }
    private static double length(double[] p){return Math.sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);}
    private void mapPrimary(double[] au,double[] out){
        if(realScale){for(int k=0;k<3;k++)out[k]=au[k]*AU_UNITS;return;}
        double distanceAU=length(au),display=25.0*Math.log1p(distanceAU*2.0);
        double s=distanceAU<1e-14?0:display/distanceAU;
        for(int k=0;k<3;k++)out[k]=au[k]*s;
    }
    private double moonScale(SolarData.Body body){
        if(realScale)return AU_UNITS;
        SolarData.Body parent=SolarData.BODIES[body.parent];
        double parentRadius=radii[parent.id];
        double orbitalRadii=Math.max(1.0,body.semiMajorAU*SolarData.AU_KM/parent.radiusKm);
        double display=parentRadius*(2.1+Math.log1p(orbitalRadii)*1.1);
        return display/Math.max(1e-12,body.semiMajorAU);
    }
    private void syncWorld(){
        for(SolarData.Body body:SolarData.BODIES){
            double[] p=SolarData.position(body.id,jd);System.arraycopy(p,0,physical[body.id],0,3);
            radii[body.id]=displayRadius(body);
        }
        for(SolarData.Body body:SolarData.BODIES){
            if(body.parent<=0)mapPrimary(physical[body.id],positions[body.id]);
            else{
                double scale=moonScale(body);
                for(int k=0;k<3;k++)positions[body.id][k]=positions[body.parent][k]+(physical[body.id][k]-physical[body.parent][k])*scale;
            }
            int p=body.id*4;appearance[p]=spin(body);appearance[p+1]=(float)Math.toRadians(body.obliquityDeg);
            appearance[p+2]=(float)body.ringInner;appearance[p+3]=(float)body.ringOuter;
            for(int k=0;k<3;k++)colors[p+k]=body.color[k];colors[p+3]=1;
            SolarEnvironment environment=SolarEnvironment.environment(body.id);
            atmosphere[p]=(float)(environment.atmosphereTopKm/body.radiusKm);
            atmosphere[p+1]=(float)(environment.scaleHeightKm/body.radiusKm);
            atmosphere[p+2]=environment.opacity;atmosphere[p+3]=environment.type;
            for(int k=0;k<3;k++)atmosphereColors[body.id*3+k]=environment.color[k];
        }
    }
    private float spin(SolarData.Body body){
        double period=Math.abs(body.rotationHours)/24.0;
        return period>1e-10?(float)(((jd-SolarData.J2000)%period)/period*TAU):0f;
    }
    private void syncQuantum(){
        long revision=bridge.revision();if(revision==uploadedRevision)return;
        float[] coefficients=bridge.coefficients(),etherCoefficients=bridge.etherCoefficients();
        for(int i=0;i<8;i++)quantum[i]=i<coefficients.length&&finite(coefficients[i])?Math.max(-1,Math.min(1,coefficients[i])):0;
        for(int i=0;i<4;i++)ether[i]=i<etherCoefficients.length&&finite(etherCoefficients[i])?Math.max(-1,Math.min(1,etherCoefficients[i])):0;
        uploadedRevision=revision;
    }
    private double defaultDistance(){
        if(isOverview){
            // Fit a sphere enclosing all eight orbital paths into the narrower
            // camera cone. Account for perspective, FOV and portrait/landscape
            // aspect; a fixed distance/aspect multiplier cropped outer planets.
            double extent=radii[0];double[] mapped=new double[3];
            for(SolarData.Body b:SolarData.BODIES){
                double bound=radii[b.id]*Math.max(1.0,b.ringOuter);
                extent=Math.max(extent,length(positions[b.id])+bound);
                if(b.id<1||b.id>8)continue;
                for(int sample=0;sample<64;sample++){
                    mapPrimary(SolarData.orbitPosition(b.id,sample/64.0,jd),mapped);
                    extent=Math.max(extent,length(mapped)+bound);
                }
            }
            double tangent=TAN_HALF_FOV*Math.min(1.0,aspect);
            return extent*1.10*Math.sqrt(1.0+tangent*tangent)/tangent;
        }
        double r=radii[selected];double ring=SolarData.BODIES[selected].ringOuter;
        double framing=Math.max(4.8,3.25/aspect);
        if(ring>0)framing*=Math.min(2.0,ring*.8);
        if(fields)framing=Math.max(framing,10.0);
        return r*framing;
    }
    private void chooseFocusAngle(){
        if(selected==0){yaw=.6;pitch=.21;return;}
        double[] p=positions[selected];yaw=Math.atan2(-p[0],-p[2])+.52;pitch=.21;
    }
    private static void normal(float[] a){
        double l=Math.sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
        if(l<1e-15){a[0]=0;a[1]=0;a[2]=-1;return;}
        a[0]/=l;a[1]/=l;a[2]/=l;
    }
    private static void cross(float[] a,float[] b,float[] result){
        result[0]=a[1]*b[2]-a[2]*b[1];result[1]=a[2]*b[0]-a[0]*b[2];result[2]=a[0]*b[1]-a[1]*b[0];
    }
    private double[] bodyDirection(double x,double y,double z){
        double a=spin(SolarData.BODIES[selected]),c=Math.cos(a),s=Math.sin(a);
        double rx=c*x+s*z,rz=-s*x+c*z;double[] basis=poleBasis[selected];
        return new double[]{basis[0]*rx+basis[3]*y+basis[6]*rz,basis[1]*rx+basis[4]*y+basis[7]*rz,basis[2]*rx+basis[5]*y+basis[8]*rz};
    }
    private void entryCamera(){
        double latitude=Math.toRadians(entryLatitude),longitude=Math.toRadians(entryLongitude);
        double sl=Math.sin(latitude),cl=Math.cos(latitude),sn=Math.sin(longitude),cn=Math.cos(longitude);
        double[] radial=bodyDirection(cl*cn,sl,-cl*sn);
        double[] north=bodyDirection(-sl*cn,cl,sl*sn),east=bodyDirection(-sn,0,-cn);
        double bearing=Math.toRadians(entryHeading),look=Math.toRadians(entryLookPitch);
        double horizontal=Math.cos(look),vertical=Math.sin(look),ch=Math.cos(bearing),sh=Math.sin(bearing);
        double radius=radii[selected],height=radius*(1+entryAltitude/SolarData.BODIES[selected].radiusKm);
        for(int k=0;k<3;k++){
            cameraEye[k]=positions[selected][k]+radial[k]*height;
            forward[k]=(float)((north[k]*ch+east[k]*sh)*horizontal+radial[k]*vertical);
            right[k]=(float)(east[k]*ch-north[k]*sh);
            cameraTarget[k]=cameraEye[k]+forward[k]*radius;
        }
        normal(forward);normal(right);cross(right,forward,up);normal(up);
        distance=desiredDistance=height;sceneScale=1/Math.max(1e-15,radius);
        // Render a close approach in units of this body's radius, after the
        // double precision camera subtraction. No AU-sized float translation.
        near=(float)Math.max(1e-7,Math.min(2e-5,Math.abs(entryAltitude)/SolarData.BODIES[selected].radiusKm*.025));
        far=(float)Math.max(200,(length(cameraEye)+(realScale?8000:700))*sceneScale);
        publishEntry();
    }
    private void camera(double dt){
        if(entryMode){entryCamera();finishCamera();return;}
        sceneScale=1;
        if(resetDistance){distance=desiredDistance=defaultDistance();resetDistance=false;}
        double lerp=1.0-Math.exp(-Math.max(.001,dt)*10.0);
        if(paused||Math.abs(distance-desiredDistance)<Math.max(1e-12,desiredDistance*1e-7))distance=desiredDistance;
        else distance+=(desiredDistance-distance)*lerp;
        if(!freeFlight){
            for(int k=0;k<3;k++)cameraTarget[k]=(isOverview?0:positions[selected][k])+panOffset[k];
            cameraEye[0]=cameraTarget[0]+distance*Math.cos(pitch)*Math.sin(yaw);
            cameraEye[1]=cameraTarget[1]+distance*Math.sin(pitch);
            cameraEye[2]=cameraTarget[2]+distance*Math.cos(pitch)*Math.cos(yaw);
            for(int k=0;k<3;k++)forward[k]=(float)(cameraTarget[k]-cameraEye[k]);normal(forward);
        }else{
            forward[0]=(float)(-Math.cos(pitch)*Math.sin(yaw));forward[1]=(float)-Math.sin(pitch);forward[2]=(float)(-Math.cos(pitch)*Math.cos(yaw));
        }
        right[0]=-forward[2];right[1]=0;right[2]=forward[0];normal(right);cross(right,forward,up);normal(up);
        double focusRadius=radii[selected];
        near=(float)Math.max(1e-8,Math.min(distance*.003,focusRadius*.02));
        far=(float)Math.max(realScale?8000:700,length(cameraEye)+(realScale?8000:700));
        finishCamera();
    }
    private void finishCamera(){
        // Camera-relative matrices, preserving close detail even in real AU scale.
        Matrix.setLookAtM(view,0,0,0,0,forward[0],forward[1],forward[2],up[0],up[1],up[2]);
        Matrix.perspectiveM(projection,0,FOV,aspect,near,far);Matrix.multiplyMM(vp,0,projection,0,view,0);
        for(SolarData.Body body:SolarData.BODIES){int p=body.id*4;for(int k=0;k<3;k++)packedBodies[p+k]=(float)((positions[body.id][k]-cameraEye[k])*sceneScale);packedBodies[p+3]=(float)(radii[body.id]*sceneScale);}
        initializedCamera=true;
    }
    private void syncVisualEther(){
        visualPlan=SolarVisualEther.plan(width,height,SolarVisualEther.DEFAULT_TILE,TAN_HALF_FOV,aspect,
                forward,right,up,packedBodies,appearance,atmosphere,SolarData.BODIES.length);
        ByteBuffer raw=direct(visualPlan.masks.length*4);
        raw.asIntBuffer().put(visualPlan.masks);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,bodyMaskTexture);
        if(bodyMaskTilesX!=visualPlan.tilesX||bodyMaskTilesY!=visualPlan.tilesY){
            glTexImage2D(GL_TEXTURE_2D,0,GL_R32UI,visualPlan.tilesX,visualPlan.tilesY,0,
                    GL_RED_INTEGER,GL_UNSIGNED_INT,raw);
            bodyMaskTilesX=visualPlan.tilesX;bodyMaskTilesY=visualPlan.tilesY;
        }else{
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,visualPlan.tilesX,visualPlan.tilesY,
                    GL_RED_INTEGER,GL_UNSIGNED_INT,raw);
        }
        glActiveTexture(GL_TEXTURE0);check("Visual Ether");
    }
    private void drawScene(){
        glUseProgram(sceneProgram);glBindVertexArray(eyeVao);glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(true);
        glUniform2f(sceneLoc("uResolution"),width,height);
        glUniform3fv(sceneLoc("uForward"),1,forward,0);glUniform3fv(sceneLoc("uRight"),1,right,0);glUniform3fv(sceneLoc("uUp"),1,up,0);
        glUniform1f(sceneLoc("uAspect"),aspect);glUniform1f(sceneLoc("uTanHalfFov"),TAN_HALF_FOV);
        glUniform1f(sceneLoc("uNear"),near);glUniform1f(sceneLoc("uFar"),far);glUniform1f(sceneLoc("uTime"),(float)(clockSeconds%100000));
        glUniform1i(sceneLoc("uCount"),SolarData.BODIES.length);
        glUniform4fv(sceneLoc("uBodies[0]"),SolarData.BODIES.length,packedBodies,0);
        glUniform4fv(sceneLoc("uAppearance[0]"),SolarData.BODIES.length,appearance,0);
        glUniform4fv(sceneLoc("uColors[0]"),SolarData.BODIES.length,colors,0);
        glUniform3fv(sceneLoc("uAxes[0]"),SolarData.BODIES.length,axes,0);
        glUniform4fv(sceneLoc("uAtmosphere[0]"),SolarData.BODIES.length,atmosphere,0);
        glUniform3fv(sceneLoc("uAtmosphereColors[0]"),SolarData.BODIES.length,atmosphereColors,0);
        glUniform4f(sceneLoc("uEntry"),entryMode?selected:-1,(float)entryAltitude,(float)SolarData.BODIES[selected].radiusKm,entryMode?1:0);
        glUniform1i(sceneLoc("uAtmosphereEnabled"),atmosphereEnabled?1:0);
        glUniform1iv(sceneLoc("uLayers[0]"),SolarData.BODIES.length,layers,0);
        glUniform4fv(sceneLoc("uQ0"),1,quantum,0);glUniform4fv(sceneLoc("uQ1"),1,quantum,4);
        glUniform4fv(sceneLoc("uEther"),1,ether,0);
        glUniform1f(sceneLoc("uCoupling"),(fields||wind)?1f:0f);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D_ARRAY,mapTexture);glUniform1i(sceneLoc("uMaps"),0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,bodyMaskTexture);glUniform1i(sceneLoc("uBodyTiles"),1);
        glUniform2i(sceneLoc("uBodyTileGrid"),visualPlan.tilesX,visualPlan.tilesY);
        glActiveTexture(GL_TEXTURE0);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
    }

    private void vertex(FloatBuffer buffer,double x,double y,double z,float r,float g,float b,float a){
        if(buffer.remaining()<7)return;
        buffer.put((float)((x-cameraEye[0])*sceneScale)).put((float)((y-cameraEye[1])*sceneScale)).put((float)((z-cameraEye[2])*sceneScale)).put(r).put(g).put(b).put(a);
    }
    private void line(double[] a,double[] b,float r,float g,float blue,float alpha){
        if(lines.remaining()<14)return;
        vertex(lines,a[0],a[1],a[2],r,g,blue,alpha);vertex(lines,b[0],b[1],b[2],r,g,blue,alpha);
    }
    private double[] mappedOrbit(int body,double fraction){
        SolarData.Body b=SolarData.BODIES[body];double[] p=SolarData.orbitPosition(body,fraction,jd),out=new double[3];
        if(b.parent<=0)mapPrimary(p,out);
        else{double scale=moonScale(b);for(int k=0;k<3;k++)out[k]=positions[b.parent][k]+(p[k]-physical[b.parent][k])*scale;}
        return out;
    }
    private void orbitLines(){
        if(!orbits)return;
        // The orbit path changes only through slow element drift. Rebuild at most
        // each half simulated day; explicit date/scale changes invalidate it.
        // Moon paths store offsets and follow the parent's exact current centre.
        if(!finite(orbitCacheDate)||Math.abs(jd-orbitCacheDate)>.5){
            for(SolarData.Body b:SolarData.BODIES){
                if(b.id==0)continue;int segments=b.parent>0?120:220;
                double[] path=orbitCache[b.id];if(path==null||path.length!=(segments+1)*3)path=new double[(segments+1)*3];
                for(int k=0;k<=segments;k++){
                    double[] point=mappedOrbit(b.id,(double)k/segments);
                    for(int axis=0;axis<3;axis++)path[k*3+axis]=point[axis]-(b.parent>0?positions[b.parent][axis]:0);
                }
                orbitCache[b.id]=path;
            }
            orbitCacheDate=jd;
        }
        int parent=SolarData.BODIES[selected].parent;
        for(SolarData.Body b:SolarData.BODIES){
            if(b.id==0)continue;
            if(b.parent>0&&(isOverview||(b.parent!=selected&&b.parent!=parent)))continue;
            double[] path=orbitCache[b.id];int segments=path.length/3-1;
            double tx=b.parent>0?positions[b.parent][0]:0,ty=b.parent>0?positions[b.parent][1]:0,tz=b.parent>0?positions[b.parent][2]:0;
            float a=b.parent>0?.20f:(b.id==selected?.42f:.145f);
            float red=b.id==selected?.28f:.29f,green=b.id==selected?.68f:.43f,blue=b.id==selected?1f:.61f;
            for(int k=1;k<=segments;k++){
                int before=(k-1)*3,next=k*3;
                vertex(lines,path[before]+tx,path[before+1]+ty,path[before+2]+tz,red,green,blue,a);
                vertex(lines,path[next]+tx,path[next+1]+ty,path[next+2]+tz,red,green,blue,a);
            }
        }
    }
    private double[] solarDirection(int id){
        double[] d={positions[0][0]-positions[id][0],positions[0][1]-positions[id][1],positions[0][2]-positions[id][2]};
        double l=length(d);if(l<1e-15)return new double[]{1,0,0};for(int k=0;k<3;k++)d[k]/=l;return d;
    }
    private double[] magneticPoint(SolarData.Body b,double l,double theta,double phi,double[] sun){
        double rr=l*Math.sin(theta)*Math.sin(theta),s=Math.sin(theta),c=Math.cos(theta);
        double x=rr*s*Math.cos(phi),y=rr*c,z=rr*s*Math.sin(phi);
        double tilt=Math.toRadians(b.magneticTiltDeg),ct=Math.cos(tilt),st=Math.sin(tilt);
        double xx=x*ct-y*st,yy=x*st+y*ct;x=xx;y=yy;
        double spin=spin(b),cs=Math.cos(spin),ss=Math.sin(spin);xx=x*cs+z*ss;z=-x*ss+z*cs;x=xx;
        double[] basis=poleBasis[b.id];
        xx=basis[0]*x+basis[3]*y+basis[6]*z;yy=basis[1]*x+basis[4]*y+basis[7]*z;
        z=basis[2]*x+basis[5]*y+basis[8]*z;x=xx;y=yy;
        double along=x*sun[0]+y*sun[1]+z*sun[2];
        // Ganymede resides inside Jupiter's magnetosphere: keep its local dipole
        // without inventing an isolated solar-wind-compressed Jovian moon tail.
        double bend=b.parent>=5?0:(along>0?-along*.34:along*Math.min(1.9,-along*.19));
        double r=radii[b.id];
        return new double[]{positions[b.id][0]+(x+sun[0]*bend)*r,positions[b.id][1]+(y+sun[1]*bend)*r,positions[b.id][2]+(z+sun[2]*bend)*r};
    }
    private void dipoleLines(SolarData.Body b){
        double[] sun=solarDirection(b.id);double[] shells={1.6,2.35,3.45,5.1};
        for(int meridian=0;meridian<12;meridian++){
            double phi=meridian*TAU/12;
            for(int shell=0;shell<shells.length;shell++){
                double l=shells[shell],start=Math.asin(Math.sqrt(1.025/l));
                double[] last=magneticPoint(b,l,start,phi,sun);
                for(int step=1;step<=58;step++){
                    double theta=start+(Math.PI-2*start)*step/58;
                    double[] next=magneticPoint(b,l,theta,phi,sun);
                    float mix=(float)step/58;
                    float r=.14f+.37f*mix,g=.72f-.23f*mix,blue=1f;
                    line(last,next,r,g,blue,shell==0?.35f:.20f);last=next;
                }
            }
        }
    }
    private static double[] cross(double[] a,double[] b){return new double[]{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
    private static void normalize(double[] a){double l=length(a);if(l<1e-12)return;for(int i=0;i<3;i++)a[i]/=l;}
    private void inducedLines(SolarData.Body b){
        double[] sun=solarDirection(b.id),u=cross(sun,new double[]{0,1,0});normalize(u);double[] v=cross(sun,u);
        double r=radii[b.id];
        for(int meridian=0;meridian<14;meridian++)for(int shell=0;shell<3;shell++){
            double phi=meridian*TAU/14;double[] last=null;
            for(int step=0;step<=70;step++){
                double t=-2.9+step*6.5/70.0;double q=(1.15+shell*.36)*Math.sqrt(1+Math.max(0,-t)*.34);
                double axial=t-Math.exp(-t*t*.6)*1.2;
                double[] point=new double[3];
                for(int k=0;k<3;k++)point[k]=positions[b.id][k]+r*(sun[k]*axial+q*(u[k]*Math.cos(phi)+v[k]*Math.sin(phi)));
                if(last!=null)line(last,point,.43f,.71f,1f,.20f);last=point;
            }
        }
    }
    private void crustalLines(SolarData.Body b){
        // Local arches in the southern hemisphere; no invented global Mars dipole.
        double r=radii[b.id];
        for(int patch=0;patch<12;patch++){
            double phi=TAU*patch/12,latitude=-.35-(patch%3)*.16;
            double[] normal=bodyVector(b,new double[]{Math.cos(latitude)*Math.cos(phi),Math.sin(latitude),Math.cos(latitude)*Math.sin(phi)});
            double[] tangent=bodyVector(b,new double[]{-Math.sin(phi),0,Math.cos(phi)});
            for(int loop=0;loop<3;loop++){
                double[] last=null;double size=.10+loop*.07;
                for(int step=0;step<=22;step++){
                    double t=step*Math.PI/22;
                    double[] p=new double[3];for(int k=0;k<3;k++)p[k]=positions[b.id][k]+r*(normal[k]*(1.01+size*Math.sin(t))+tangent[k]*size*Math.cos(t));
                    if(last!=null)line(last,p,.97f,.44f,.22f,.45f);last=p;
                }
            }
        }
    }
    private double[] bodyVector(SolarData.Body body,double[] p){
        double a=spin(body),c=Math.cos(a),s=Math.sin(a),x=p[0]*c+p[2]*s,y=p[1],z=-p[0]*s+p[2]*c;
        double[] b=poleBasis[body.id];
        return new double[]{b[0]*x+b[3]*y+b[6]*z,b[1]*x+b[4]*y+b[7]*z,b[2]*x+b[5]*y+b[8]*z};
    }
    private void coronalLines(SolarData.Body b){
        double r=radii[b.id];
        for(int patch=0;patch<24;patch++){
            double phi=patch*2.3999632297,lat=Math.asin(-.9+1.8*(patch+.5)/24);
            double[] n={Math.cos(lat)*Math.cos(phi),Math.sin(lat),Math.cos(lat)*Math.sin(phi)};
            double[] tangent={-Math.sin(phi),0,Math.cos(phi)};
            for(int loop=0;loop<2;loop++){
                double[] last=null;double size=.22+loop*.18;
                for(int step=0;step<=24;step++){
                    double t=step*Math.PI/24;double[] p=new double[3];
                    for(int k=0;k<3;k++)p[k]=positions[b.id][k]+r*(n[k]*(1.01+size*Math.sin(t))+tangent[k]*size*.6*Math.cos(t));
                    if(last!=null)line(last,p,1f,.54f,.17f,.4f);last=p;
                }
            }
        }
    }
    private void fieldLines(){
        if(!fields||isOverview)return;SolarData.Body b=SolarData.BODIES[selected];
        if(b.fieldType==1)dipoleLines(b);else if(b.fieldType==2)inducedLines(b);
        else if(b.fieldType==3){inducedLines(b);crustalLines(b);}else if(b.fieldType==4)coronalLines(b);
    }
    private static double hash(int n){double d=Math.sin(n*127.1+311.7)*43758.5453123;return d-Math.floor(d);}
    private void tracerParticles(){
        if((!wind&&!fields)||isOverview)return;
        SolarData.Body b=SolarData.BODIES[selected];double r=radii[selected];
        double phase=quantum[0]*.13+quantum[1]*.17+quantum[3]*.29+quantum[4]*.37+quantum[6]*.41;
        if(wind&&selected==0){
            // The Sun emits radially outward; it is not an obstacle in incoming wind.
            for(int i=0;i<460;i++){
                double latitude=Math.asin(2*hash(i+27)-1),longitude=TAU*(hash(i+881)+phase*.09);
                double advance=hash(i+913)*7+clockSeconds*(.28+.035*quantum[5])+phase;
                advance=advance-Math.floor(advance/7)*7;
                double rr=r*(1.07+advance),cos=Math.cos(latitude);
                float alpha=(float)(.78*(1-advance/8));
                vertex(points,positions[0][0]+rr*cos*Math.cos(longitude),positions[0][1]+rr*Math.sin(latitude),positions[0][2]+rr*cos*Math.sin(longitude),1f,.60f,.27f,alpha);
            }
        }
        if(wind&&selected!=0&&b.parent<5){
            double[] sun=solarDirection(selected),u=cross(sun,new double[]{0,1,0});normalize(u);double[] v=cross(sun,u);
            for(int i=0;i<360;i++){
                double random=hash(i+71),azimuth=TAU*(hash(i+987)+phase*.15);
                double offset=.5+Math.sqrt(hash(i+436))*3.2*(1+.12*quantum[2]);
                double travel=hash(i+1143)*16-clockSeconds*(1.05+.15*quantum[5])+phase*2.5;
                travel=travel-Math.floor(travel/16)*16-8;
                // A schematic obstacle deflects traces at magnetized/induced bodies.
                double repel=(b.fieldType==1||b.fieldType==2||b.fieldType==3)?1.5:.35;
                double bend=repel*Math.exp(-Math.pow((travel-.4)/2.4,2));
                double lateral=offset+bend;
                double x=positions[selected][0]+r*(sun[0]*travel+lateral*(u[0]*Math.cos(azimuth)+v[0]*Math.sin(azimuth)));
                double y=positions[selected][1]+r*(sun[1]*travel+lateral*(u[1]*Math.cos(azimuth)+v[1]*Math.sin(azimuth)));
                double z=positions[selected][2]+r*(sun[2]*travel+lateral*(u[2]*Math.cos(azimuth)+v[2]*Math.sin(azimuth)));
                float red=(float)(.37+.17*(quantum[7]+1)/2),green=.72f,blue=.97f;
                float alpha=(float)(.26+.47*random)*(float)Math.min(1,(8-Math.abs(travel))*.5);
                vertex(points,x,y,z,red,green,blue,alpha);
            }
        }
        if(fields&&b.fieldType==1){
            double[] sun=solarDirection(selected);
            for(int i=0;i<132;i++){
                double l=1.7+hash(i+665)*3.4,phi=TAU*(hash(i+398)+phase*.22);
                double start=Math.asin(Math.sqrt(1.025/l));double phase01=hash(i+587)+clockSeconds*.105+phase;
                phase01-=Math.floor(phase01);double theta=start+(Math.PI-2*start)*phase01;
                double[] p=magneticPoint(b,l,theta,phi,sun);
                vertex(points,p[0],p[1],p[2],.30f,.82f,1f,.89f);
            }
        }
    }
    private void drawOverlay(FloatBuffer buffer,boolean particles){
        int count=buffer.position()/7;if(count==0)return;buffer.flip();
        glUseProgram(overlayProgram);glBindVertexArray(overlayVao);glBindBuffer(GL_ARRAY_BUFFER,overlayVbo);
        glBufferSubData(GL_ARRAY_BUFFER,0,count*7*4,buffer);
        glUniformMatrix4fv(overlayLoc("uVp"),1,false,vp,0);
        glUniform1f(overlayLoc("uPointSize"),particles?Math.max(3.2f,Math.min(6.5f,quality/170f)):1f);
        glUniform1i(overlayLoc("uPoints"),particles?1:0);
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,particles?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(false);glDepthFunc(GL_LEQUAL);glLineWidth(1f);
        glDrawArrays(particles?GL_POINTS:GL_LINES,0,count);glDepthMask(true);
    }

    @Override public void onDrawFrame(GL10 unused){
        if(failed||sceneProgram==0)return;
        try{
            long now=System.nanoTime();double dt=previousNanos==0?0:Math.min(.1,Math.max(0,(now-previousNanos)*1e-9));previousNanos=now;
            if(!paused)clockSeconds+=dt;
            if(!paused){double next=jd+dt*speed;if(next<SolarData.MIN_JD||next>SolarData.MAX_JD){jd=Math.max(SolarData.MIN_JD,Math.min(SolarData.MAX_JD,next));paused=true;}else jd=next;}
            if(entryMode&&localPhysics&&!paused&&dt>0){
                SolarFlightPhysics.step(selected,SolarEnvironment.environment(selected),flightState,0,dt);
                entryAltitude=flightState.altitudeKm;publishEntry();
            }
            if(framebufferDirty)sizeFramebuffer();syncWorld();syncQuantum();camera(dt);syncVisualEther();
            glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,width,height);drawScene();
            lines.clear();points.clear();if(!entryMode){orbitLines();fieldLines();tracerParticles();drawOverlay(lines,false);drawOverlay(points,true);}
            check("Observación solar");
            CaptureListener requested=capture;Capture result=null;
            if(requested!=null){
                int rowBytes=width*4,blockRows=Math.min(height,128);byte[] rgba=new byte[rowBytes*height];
                ByteBuffer pixels=direct(rowBytes*blockRows);
                for(int row=0;row<height;row+=blockRows){
                    int rows=Math.min(blockRows,height-row);pixels.clear();glReadPixels(0,row,width,rows,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check("Lectura solar");
                    pixels.rewind();pixels.get(rgba,row*rowBytes,rows*rowBytes);
                }
                result=new Capture(width,height,rgba);capture=null;
            }
            glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
            int bottom=viewHeight-display.top-display.height;
            glBlitFramebuffer(0,0,width,height,display.left,bottom,display.left+display.width,bottom+display.height,GL_COLOR_BUFFER_BIT,GL_LINEAR);glBindFramebuffer(GL_FRAMEBUFFER,0);
            check("Pantalla solar");hasFrame=true;frameCount++;
            if(fpsNanos==0){fpsNanos=now;framesAtFps=frameCount;}
            else if(now-fpsNanos>750000000L){fps=(float)((frameCount-framesAtFps)*1e9/(now-fpsNanos));fpsNanos=now;framesAtFps=frameCount;}
            if(now-statusNanos>1000000000L){statusNanos=now;listener.status(String.format(Locale.US,"%.0f FPS · %d × %d%s · núcleo r%d · éter visual %.1f%%%s",fps,width,height,isUhd8k()?" UHD 8K":isUhd4k()?" UHD 4K":"",uploadedRevision,visualPlan==null?100.0:visualPlan.referenceRatio()*100.0,resolutionMessage==null?"":"\n"+resolutionMessage));}
            if(requested!=null)requested.captured(result);
        }catch(Throwable e){error(e);}
    }
    private void error(Throwable e){failed=true;failure=e.toString();listener.status("No se pudo observar la escena: "+failure);}

    private void publishEntry(){
        publishedEntry=entryMode?new double[]{selected,entryAltitude,entryLatitude,entryLongitude,entryHeading,entryLookPitch}:null;
    }
    /** Entry coordinates are degrees: latitude, east longitude, heading and look pitch. */
    public void enterAtmosphere(int body){
        if(body<0||body>=SolarData.BODIES.length)throw new IllegalArgumentException("Cuerpo desconocido");
        selected=body;entryMode=true;isOverview=false;freeFlight=false;
        for(int k=0;k<3;k++)panOffset[k]=0;
        syncWorld();SolarEnvironment environment=SolarEnvironment.environment(body);
        entryAltitude=environment.entryAltitudeKm;flightState=new SolarFlightPhysics.State(entryAltitude,0);entryHeading=90;
        double depression=Math.toDegrees(Math.acos(Math.min(1.0,SolarData.BODIES[body].radiusKm/(SolarData.BODIES[body].radiusKm+Math.max(0,entryAltitude)))));
        entryLookPitch=-Math.min(78,Math.max(15,depression+7));
        double[] day=solarDirection(body),basis=poleBasis[body];
        double px=day[0]*basis[0]+day[1]*basis[1]+day[2]*basis[2];
        double py=day[0]*basis[3]+day[1]*basis[4]+day[2]*basis[5];
        double pz=day[0]*basis[6]+day[1]*basis[7]+day[2]*basis[8];
        double a=spin(SolarData.BODIES[body]);
        entryLatitude=Math.toDegrees(Math.asin(Math.max(-.9999,Math.min(.9999,py))));
        entryLongitude=-Math.toDegrees(Math.atan2(Math.sin(a)*px+Math.cos(a)*pz,Math.cos(a)*px-Math.sin(a)*pz));
        if(body==3){
            // Pick an illuminated continental approach; these are camera sites,
            // not terrain measurements or an IAU prime-meridian solution.
            double[][] sites={{26,25},{32,100},{25,-105},{-20,135},{-20,-55}};double best=-2;
            for(double[] site:sites){
                double lat=Math.toRadians(site[0]),lon=Math.toRadians(site[1]);
                double[] n=bodyDirection(Math.cos(lat)*Math.cos(lon),Math.sin(lat),-Math.cos(lat)*Math.sin(lon));
                double light=n[0]*day[0]+n[1]*day[1]+n[2]*day[2];
                if(light>best){best=light;entryLatitude=site[0];entryLongitude=site[1];}
            }
        }
        publishEntry();listener.selected(body);
    }
    public void exitAtmosphere(){if(entryMode)focus(selected);}
    public void setEntryAltitudeKm(double altitude){
        if(!finite(altitude))throw new IllegalArgumentException("Altitud no finita");
        if(!entryMode)enterAtmosphere(selected);
        entryAltitude=SolarEnvironment.environment(selected).clampAltitudeKm(altitude);
        flightState=new SolarFlightPhysics.State(entryAltitude,0);publishEntry();
    }
    public double altitudeKm(){return entryMode?entryAltitude:Double.NaN;}
    public boolean isEntryMode(){return entryMode;}
    public void setLocalPhysics(boolean enabled){
        localPhysics=enabled;
        flightState=new SolarFlightPhysics.State(entryMode?entryAltitude:Math.max(.5,entryAltitude),0);
    }
    public boolean localPhysicsEnabled(){return localPhysics;}
    public double radialVelocityKmS(){return entryMode?flightState.radialVelocityKmS:Double.NaN;}
    public double localGravityMs2(){return entryMode?SolarData.gravityMs2(selected,entryAltitude):Double.NaN;}
    public void radialImpulseKmS(double delta){
        if(entryMode&&localPhysics&&finite(delta))
            flightState.radialVelocityKmS=Math.max(-20,Math.min(20,flightState.radialVelocityKmS+delta));
    }
    /** Immutable publication: [body, km, latitude°, longitude°, heading°, lookPitch°]. */
    public double[] entryState(){double[] state=publishedEntry;return state==null?null:state.clone();}
    public void restoreEntryState(double[] state){
        if(state==null){exitAtmosphere();return;}
        if(state.length!=6)throw new IllegalArgumentException("La cámara de entrada requiere seis valores");
        for(double value:state)if(!finite(value))throw new IllegalArgumentException("Cámara de entrada no finita");
        int body=(int)state[0];if(body!=state[0]||body<0||body>=SolarData.BODIES.length)throw new IllegalArgumentException("Cuerpo de entrada inválido");
        selected=body;entryMode=true;isOverview=false;freeFlight=false;syncWorld();
        entryAltitude=SolarEnvironment.environment(body).clampAltitudeKm(state[1]);
        flightState=new SolarFlightPhysics.State(entryAltitude,0);
        entryLatitude=Math.max(-89.9,Math.min(89.9,state[2]));entryLongitude=state[3]%360;
        entryHeading=state[4]%360;entryLookPitch=Math.max(-85,Math.min(85,state[5]));
        for(int k=0;k<3;k++)panOffset[k]=0;publishEntry();listener.selected(body);
    }
    private void moveEntry(double northKm,double eastKm){
        double radius=SolarData.BODIES[selected].radiusKm+entryAltitude;
        double distance=Math.hypot(northKm,eastKm);
        if(!(radius>0)||!finite(distance)||distance<1e-12)return;
        // Exact great-circle destination from a local north/east displacement.
        // This avoids the first-order tangent drift of the previous update.
        double lat1=Math.toRadians(entryLatitude),lon1=Math.toRadians(entryLongitude);
        double bearing=Math.atan2(eastKm,northKm),angular=distance/radius;
        double sinLat1=Math.sin(lat1),cosLat1=Math.cos(lat1);
        double sinAngular=Math.sin(angular),cosAngular=Math.cos(angular);
        double lat2=Math.asin(Math.max(-1,Math.min(1,
                sinLat1*cosAngular+cosLat1*sinAngular*Math.cos(bearing))));
        double lon2=lon1+Math.atan2(Math.sin(bearing)*sinAngular*cosLat1,
                cosAngular-sinLat1*Math.sin(lat2));
        entryLatitude=Math.toDegrees(lat2);
        entryLongitude=Math.toDegrees(Math.atan2(Math.sin(lon2),Math.cos(lon2)));
        publishEntry();
    }
    public void focus(int body){
        if(body<0||body>=SolarData.BODIES.length)return;
        entryMode=false;publishedEntry=null;
        selected=body;isOverview=false;freeFlight=false;for(int k=0;k<3;k++)panOffset[k]=0;
        syncWorld();chooseFocusAngle();resetDistance=true;listener.selected(body);
    }
    public void overview(){
        entryMode=false;publishedEntry=null;
        isOverview=true;freeFlight=false;for(int k=0;k<3;k++)panOffset[k]=0;
        yaw=.24;pitch=1.0;resetDistance=true;listener.selected(-1);
    }
    public void rotate(float dx,float dy){
        if(!finite(dx)||!finite(dy))return;
        if(entryMode){entryHeading=(entryHeading+dx*.34)%360;entryLookPitch=Math.max(-85,Math.min(85,entryLookPitch-dy*.28));publishEntry();return;}
        yaw=(yaw+dx*.006)%TAU;pitch=Math.max(-1.52,Math.min(1.52,pitch+dy*.005));
    }
    public void zoom(float factor){
        if(!finite(factor)||factor<=0)return;
        if(entryMode){
            SolarEnvironment environment=SolarEnvironment.environment(selected);
            double base=Math.max(1,environment.scaleHeightKm*.1);
            setEntryAltitudeKm(environment.minAltitudeKm+(entryAltitude-environment.minAltitudeKm+base)/factor-base);return;
        }
        if(freeFlight){move((factor-1)*4,0);return;}
        double minimum=radii[selected]*1.065;
        desiredDistance=Math.max(minimum,Math.min(realScale?40000:2400,desiredDistance/factor));
    }
    public void pan(float dx,float dy){
        if(!finite(dx)||!finite(dy))return;
        if(entryMode){
            double scale=Math.max(.5,Math.abs(entryAltitude)+SolarEnvironment.environment(selected).scaleHeightKm)*2*TAN_HALF_FOV/Math.max(1,display.height);
            double h=Math.toRadians(entryHeading),ahead=dy*scale,side=-dx*scale;
            moveEntry(ahead*Math.cos(h)-side*Math.sin(h),ahead*Math.sin(h)+side*Math.cos(h));return;
        }
        double scale=distance*2*TAN_HALF_FOV/Math.max(1,viewHeight);
        for(int k=0;k<3;k++){
            double shift=(-right[k]*dx+up[k]*dy)*scale;
            if(freeFlight)cameraEye[k]+=shift;else panOffset[k]+=shift;
        }
    }
    public void move(float forwardAmount,float rightAmount){
        if(!finite(forwardAmount)||!finite(rightAmount))return;
        if(entryMode){
            SolarEnvironment environment=SolarEnvironment.environment(selected);
            double step=Math.max(.25,Math.min(150,Math.abs(entryAltitude)*.18+environment.scaleHeightKm*.1));
            double h=Math.toRadians(entryHeading);
            moveEntry((forwardAmount*Math.cos(h)-rightAmount*Math.sin(h))*step,(forwardAmount*Math.sin(h)+rightAmount*Math.cos(h))*step);return;
        }
        if(!freeFlight)setFreeFlight(true);
        double nearest=Double.POSITIVE_INFINITY;
        for(SolarData.Body b:SolarData.BODIES){double dx=cameraEye[0]-positions[b.id][0],dy=cameraEye[1]-positions[b.id][1],dz=cameraEye[2]-positions[b.id][2];nearest=Math.min(nearest,Math.max(radii[b.id]*.04,Math.sqrt(dx*dx+dy*dy+dz*dz)-radii[b.id]));}
        double travel=Math.max(radii[selected]*.025,Math.min(realScale?150:8,nearest*.18));
        for(int k=0;k<3;k++)cameraEye[k]+=(forward[k]*forwardAmount+right[k]*rightAmount)*travel;
        // Stop outside solid bodies so navigation cannot become trapped inside a sphere.
        for(SolarData.Body b:SolarData.BODIES){
            double dx=cameraEye[0]-positions[b.id][0],dy=cameraEye[1]-positions[b.id][1],dz=cameraEye[2]-positions[b.id][2];
            double l=Math.sqrt(dx*dx+dy*dy+dz*dz),minimum=radii[b.id]*1.035;
            if(l<minimum&&l>1e-14){cameraEye[0]=positions[b.id][0]+dx/l*minimum;cameraEye[1]=positions[b.id][1]+dy/l*minimum;cameraEye[2]=positions[b.id][2]+dz/l*minimum;}
        }
    }
    public void setFreeFlight(boolean enabled){
        if(entryMode)return;
        if(enabled==freeFlight)return;
        freeFlight=enabled;
        if(!enabled){for(int k=0;k<3;k++)panOffset[k]=0;resetDistance=true;}
    }
    public void setFields(boolean enabled){
        fields=enabled;if(enabled&&!entryMode&&!isOverview&&!freeFlight&&distance<radii[selected]*10)desiredDistance=radii[selected]*10;
    }
    public void setWind(boolean enabled){wind=enabled;}
    public void setOrbits(boolean enabled){orbits=enabled;}
    public void setRealScale(boolean enabled){
        if(realScale==enabled)return;
        realScale=enabled;freeFlight=false;for(int k=0;k<3;k++)panOffset[k]=0;
        syncWorld();orbitCacheDate=Double.NaN;resetDistance=true;
    }
    public void setQuality(int maxDimension){quality=Math.max(360,Math.min(1920,maxDimension));requestedUhd=requestedUltra=false;resolutionMessage=null;framebufferDirty=true;}
    public void setUhd4k(boolean enabled){requestedUhd=enabled; if(enabled)requestedUltra=false; resolutionMessage=null;framebufferDirty=true;if(isOverview)resetDistance=true;}
    public void setUhd8k(boolean enabled){requestedUltra=enabled; if(enabled)requestedUhd=false; resolutionMessage=null;framebufferDirty=true;if(isOverview)resetDistance=true;}
    public boolean isUhd4k(){return activeUhd&&framebuffer!=0&&((width==3840&&height==2160)||(width==2160&&height==3840));}
    public boolean isUhd8k(){return activeUltra&&framebuffer!=0&&((width==7680&&height==4320)||(width==4320&&height==7680));}
    public int renderedWidth(){return width;}
    public int renderedHeight(){return height;}
    public int surfaceWidth(){return viewWidth;}
    public int surfaceHeight(){return viewHeight;}
    public RenderResolution.Fit displayRect(){return display;}
    public String resolutionNotice(){return resolutionMessage;}
    public void setAtmosphereEnabled(boolean enabled){atmosphereEnabled=enabled;}
    public boolean atmosphereEnabled(){return atmosphereEnabled;}
    public void setPaused(boolean paused){this.paused=paused;}
    public void setSpeed(double daysPerSecond){if(finite(daysPerSecond))speed=Math.max(-36525,Math.min(36525,daysPerSecond));}
    public void setJulianDate(double date){if(finite(date)){jd=Math.max(SolarData.MIN_JD,Math.min(SolarData.MAX_JD,date));orbitCacheDate=Double.NaN;syncWorld();}}
    public int selectedBody(){return selected;}
    public double julianDate(){return jd;}
    public boolean isPaused(){return paused;}
    public boolean fieldsEnabled(){return fields;}
    public boolean windEnabled(){return wind;}
    public boolean realScaleEnabled(){return realScale;}
    public boolean orbitsEnabled(){return orbits;}
    public boolean freeFlightEnabled(){return freeFlight;}
    public boolean isOverview(){return isOverview;}
    public int qualityLimit(){return quality;}
    public int textureLayerForBody(int body){return body>=0&&body<MAX_BODIES?layers[body]:-1;}
    public int loadedMapCount(){int count=0;for(int layer:layers)if(layer>=0)count++;return count;}
    /** Lifecycle hook: a resumed frame must not integrate suspended wall time. */
    public void resetFrameClock(){previousNanos=0;}
    /** Deterministic fixture hook: call while paused, on the GL thread. */
    public void setAnimationTimeForTest(float seconds){if(finite(seconds))clockSeconds=Math.max(0,seconds);}
    public void pick(float screenX,float screenY){
        if(!initializedCamera||!finite(screenX)||!finite(screenY))return;
        RenderResolution.Fit fit=display;
        if(!fit.contains((int)Math.floor(screenX),(int)Math.floor(screenY)))return;
        // Entry gestures manipulate the local camera, not a giant sphere
        // projection that would pull the observer back into orbit on a tap.
        if(entryMode)return;
        float x=2*(screenX-fit.left)/fit.width-1,y=1-2*(screenY-fit.top)/fit.height;int best=-1;double score=Double.POSITIVE_INFINITY;
        for(SolarData.Body b:SolarData.BODIES){
            int o=b.id*4;double xx=packedBodies[o],yy=packedBodies[o+1],zz=packedBodies[o+2];
            double depth=xx*forward[0]+yy*forward[1]+zz*forward[2];if(depth<=near)continue;
            double px=(xx*right[0]+yy*right[1]+zz*right[2])/(depth*TAN_HALF_FOV*aspect);
            double py=(xx*up[0]+yy*up[1]+zz*up[2])/(depth*TAN_HALF_FOV);
            double dx=(px-x)*fit.width*.5,dy=(py-y)*fit.height*.5;
            double radiusPixels=Math.max(16,radii[b.id]/depth/TAN_HALF_FOV*fit.height*.5);
            double d=Math.sqrt(dx*dx+dy*dy);if(d<=radiusPixels&&d/Math.max(1,radiusPixels)<score){score=d/radiusPixels;best=b.id;}
        }
        if(best>=0)focus(best);
    }
    /** Request on the GL thread. The callback receives the next complete frame. */
    public void requestCapture(CaptureListener listener){
        if(listener==null)throw new IllegalArgumentException("Falta el receptor de la captura");
        if(capture!=null)throw new IllegalStateException("Ya existe una captura pendiente");capture=listener;
    }
    /** Delete GPU resources while the GL context is current; the core is borrowed. */
    public void release(){
        if(sceneProgram!=0)glDeleteProgram(sceneProgram);if(overlayProgram!=0)glDeleteProgram(overlayProgram);
        glDeleteTextures(2,new int[]{mapTexture,colorTexture},0);
        glDeleteRenderbuffers(1,new int[]{depthBuffer},0);glDeleteFramebuffers(1,new int[]{framebuffer},0);
        glDeleteBuffers(1,new int[]{overlayVbo},0);glDeleteVertexArrays(2,new int[]{eyeVao,overlayVao},0);
        sceneProgram=overlayProgram=0;hasFrame=false;capture=null;
        activeUhd=activeUltra=false;
    }
}
