package com.mundox.solar;

import android.app.Instrumentation;
import android.content.Intent;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Debug;
import android.os.SystemClock;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.json.JSONArray;
import org.json.JSONObject;

/** Uses only pre-existing shipping APIs so exactly the same test can exercise
 * a baseline APK. Fixed time, full fresh draws, glFinish before every sample.
 * Timed frames include Java preparation, GPU scene/overlays and final blit;
 * capture/PNG and independent core invariants are measured outside that span. */
public final class SolarPerformanceChecks {
    private interface Work{void run()throws Exception;}
    private static void gl(SolarActivity a,Work work)throws Exception{
        CountDownLatch done=new CountDownLatch(1);AtomicReference<Throwable> error=new AtomicReference<>();
        a.surface().queueEvent(()->{try{work.run();}catch(Throwable t){error.set(t);}finally{done.countDown();}});
        if(!done.await(120,TimeUnit.SECONDS))throw new AssertionError("GL work exceeded 120 seconds");
        if(error.get()!=null)throw new AssertionError("GL work failed",error.get());
    }
    private static String hash(byte[] bytes)throws Exception{
        byte[] d=MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder s=new StringBuilder();for(byte b:d)s.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return s.toString();
    }
    private static void write(File f,byte[] data)throws Exception{try(FileOutputStream out=new FileOutputStream(f)){out.write(data);}}
    private static Object field(SolarRenderer r,String name)throws Exception{Field f=SolarRenderer.class.getDeclaredField(name);f.setAccessible(true);return f.get(r);}
    private static double[] cross(double[] a,double[] b){return new double[]{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
    private static void normalize(double[] v){double n=Math.sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(int i=0;i<3;i++)v[i]/=n;}
    private static void fixedMoonPoles(SolarRenderer r)throws Exception{
        // Shipping constructors derive these approximate moon poles from the
        // launch date. Fix that fixture input in both APKs, without changing
        // either production implementation or its public API.
        float[] axes=(float[])field(r,"axes");double[][] basis=(double[][])field(r,"poleBasis");
        for(SolarData.Body body:SolarData.BODIES)if(body.id>8){
            double[] a=SolarData.orbitPosition(body.id,0,2461331.0),b=SolarData.orbitPosition(body.id,.0001,2461331.0),parent=SolarData.position(body.parent,2461331.0);
            for(int i=0;i<3;i++){a[i]-=parent[i];b[i]-=parent[i];}
            double[] pole=cross(a,b);normalize(pole);
            double[] reference=Math.abs(pole[2])>.98?new double[]{0,1,0}:new double[]{0,0,1};
            double[] bx=cross(pole,reference);normalize(bx);double[] bz=cross(bx,pole);
            for(int i=0;i<3;i++){axes[body.id*3+i]=(float)pole[i];basis[body.id][i]=bx[i];basis[body.id][3+i]=pole[i];basis[body.id][6+i]=bz[i];}
        }
    }
    private static void bits(Object value,StringBuilder out){
        if(value==null){out.append("null;");return;}
        if(value.getClass().isArray()){out.append('[');for(int i=0;i<Array.getLength(value);i++)bits(Array.get(value,i),out);out.append(']');}
        else if(value instanceof Float)out.append(Integer.toHexString(Float.floatToIntBits((Float)value))).append(';');
        else if(value instanceof Double)out.append(Long.toHexString(Double.doubleToLongBits((Double)value))).append(';');
        else out.append(value).append(';');
    }
    private static JSONObject inputs(SolarRenderer r)throws Exception{
        JSONObject inputs=new JSONObject();
        for(String name:new String[]{"packedBodies","appearance","colors","axes","poleBasis","atmosphere","atmosphereColors","layers","quantum","ether","forward","right","up","vp","orbitCache","cameraEye","jd","clockSeconds","entryAltitude","near","far","aspect"}){
            StringBuilder data=new StringBuilder();bits(field(r,name),data);inputs.put(name,hash(data.toString().getBytes(StandardCharsets.UTF_8)));
        }
        return inputs;
    }
    public static void run(Instrumentation ins){
        JSONObject result=new JSONObject();Bundle end=new Bundle();int status=-1;
        File dir=new File(ins.getTargetContext().getFilesDir(),"solar-performance");dir.mkdirs();
        try{
            ins.getTargetContext().getSharedPreferences("solar",0).edit().clear().commit();
            Intent launch=new Intent(ins.getTargetContext(),SolarActivity.class);launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);
            SolarActivity a=(SolarActivity)ins.startActivitySync(launch);
            long deadline=SystemClock.uptimeMillis()+120000;
            while(!a.renderer().hasFrame&&a.renderer().failure==null&&SystemClock.uptimeMillis()<deadline)SystemClock.sleep(100);
            if(!a.renderer().hasFrame)throw new AssertionError("Solar first frame: "+a.renderer().failure);
            ins.runOnMainSync(()->a.surface().setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY));
            JSONArray cases=new JSONArray();result.put("cases",cases);
            gl(a,()->{
                result.put("renderer",GLES30.glGetString(GLES30.GL_RENDERER));result.put("gles",GLES30.glGetString(GLES30.GL_VERSION));
                result.put("scope","Android runtime; GPU/virtualization recorded separately; not physical Samsung FPS");
                SolarRenderer r=a.renderer();r.setPaused(true);r.setQuality(360);fixedMoonPoles(r);
                result.put("moonPoleFixtureJulianDate",2461331.0);
            });
            int[] bodies={3,3,6,0,3,5,9,7};
            for(int index=0;index<bodies.length;index++){
                final int k=index;
                gl(a,()->{
                    SolarRenderer r=a.renderer();QuantumSolarBridge bridge=a.bridge();
                    // Focus derives its initial angle from the current date.
                    // Install the fixed date first, independent of launch time.
                    r.setJulianDate(2461331.0+k*13.125);r.setRealScale(k==6);r.focus(bodies[k]);r.setAnimationTimeForTest(7.25f);
                    r.setFields(k==1);r.setWind(true);r.setOrbits(true);r.setAtmosphereEnabled(true);bridge.setPreset(k%4);
                    if(k==4||k==5)r.enterAtmosphere(bodies[k]);
                    // Every scene includes a distinct view; no image cache hits.
                    r.rotate(13*k,-3*k);r.onDrawFrame(null);GLES30.glFinish();
                    JSONObject c=new JSONObject();c.put("case",k);c.put("body",bodies[k]);c.put("entry",k==4||k==5);
                    c.put("preset",bridge.preset());c.put("nativeRevision",bridge.nativeRevision());c.put("nativeEpoch",bridge.nativeEpoch());
                    c.put("activeSites",bridge.activeSites());c.put("stateBytes",bridge.stateBytes());
                    JSONArray times=new JSONArray();long version=bridge.nativeRevision(),epoch=bridge.nativeEpoch();
                    float[] reference=bridge.verifyReadOnly(),global=bridge.verifyEtherReadOnly();
                    for(int rep=0;rep<5;rep++){
                        GLES30.glFinish();long begin=System.nanoTime();r.onDrawFrame(null);GLES30.glFinish();times.put((System.nanoTime()-begin)/1e6);
                    }
                    if(r.failure!=null)throw new AssertionError(r.failure);
                    if(version!=bridge.nativeRevision()||epoch!=bridge.nativeEpoch()||!Arrays.equals(reference,bridge.verifyReadOnly())||!Arrays.equals(global,bridge.verifyEtherReadOnly()))throw new AssertionError("Rendering changed core state");
                    c.put("samplesMs",times);c.put("width",r.renderedWidth());c.put("height",r.renderedHeight());
                    AtomicReference<SolarRenderer.Capture> capture=new AtomicReference<>();r.requestCapture(capture::set);r.onDrawFrame(null);GLES30.glFinish();
                    SolarRenderer.Capture frame=capture.get();if(frame==null)throw new AssertionError("Capture missing");
                    c.put("rgbaSha256",hash(frame.rgba));write(new File(dir,"case-"+k+".rgba"),frame.rgba);
                    c.put("inputSha256",inputs(r));
                    capture.set(null);r.requestCapture(capture::set);r.onDrawFrame(null);GLES30.glFinish();
                    if(capture.get()==null||!Arrays.equals(frame.rgba,capture.get().rgba))throw new AssertionError("Repeated fixed scene changed");
                    c.put("repeatCaptureExact",true);
                    c.put("nativeHeapBytes",Debug.getNativeHeapAllocatedSize());c.put("javaUsedBytes",Runtime.getRuntime().totalMemory()-Runtime.getRuntime().freeMemory());
                    cases.put(c);
                });
            }
            gl(a,()->{
                for(int i=0;i<128;i++)try(QuantumSolarBridge bridge=new QuantumSolarBridge(i%4)){
                    bridge.verifyReadOnly();bridge.verifyEtherReadOnly();
                }
                result.put("sessionLifetimes",128);result.put("frames",a.renderer().frameCount);
            });
            result.put("status","passed");end.putString("stream","Solar performance: 8 scenes, 40 completed frames, 128 core lifetimes passed\n");
        }catch(Throwable e){status=0;try{result.put("status","failed");result.put("error",android.util.Log.getStackTraceString(e));}catch(Exception ignored){}end.putString("stream",e.toString());}
        try{write(new File(dir,"results.json"),result.toString(2).getBytes(StandardCharsets.UTF_8));}
        catch(Exception e){status=0;end.putString("writeError",e.toString());}
        ins.finish(status,end);
    }
}
