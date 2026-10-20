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
                SolarRenderer r=a.renderer();r.setPaused(true);r.setQuality(360);
            });
            int[] bodies={3,3,6,0,3,5,9,7};
            for(int index=0;index<bodies.length;index++){
                final int k=index;
                gl(a,()->{
                    SolarRenderer r=a.renderer();QuantumSolarBridge bridge=a.bridge();
                    r.focus(bodies[k]);r.setRealScale(k==6);r.setJulianDate(2461331.0+k*13.125);r.setAnimationTimeForTest(7.25f);
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
