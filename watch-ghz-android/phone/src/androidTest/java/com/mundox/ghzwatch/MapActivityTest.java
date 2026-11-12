package com.mundox.ghzwatch;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.SystemClock;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.mundox.maplink.MapProtocol;
import com.mundox.maplink.FieldTelemetry;
import org.json.JSONArray;
import java.util.concurrent.atomic.AtomicBoolean;
import org.json.JSONObject;
import org.json.JSONTokener;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public final class MapActivityTest {
    private final String session="12345678-1234-1234-1234-123456789abc";
    private Instrumentation instrumentation(){return InstrumentationRegistry.getInstrumentation();}
    private JSONObject evaluate(MapActivity activity,String code) throws Exception {
        CountDownLatch latch=new CountDownLatch(1);AtomicReference<String> value=new AtomicReference<>();
        instrumentation().runOnMainSync(()->activity.webForTest().evaluateJavascript(code,result->{value.set(result);latch.countDown();}));
        assertTrue("JavaScript timed out",latch.await(10,TimeUnit.SECONDS));
        Object decoded=new JSONTokener(value.get()).nextValue();
        return decoded instanceof String?new JSONObject((String)decoded):new JSONObject();
    }
    private JSONObject state(MapActivity activity)throws Exception {
        return evaluate(activity,"JSON.stringify(window.MundoXRemote ? window.MundoXRemote.apply('HELLO') : {})");
    }
    private JSONObject send(MapActivity activity,long sequence,String command)throws Exception {
        CountDownLatch latch=new CountDownLatch(1);AtomicReference<byte[]> value=new AtomicReference<>();
        byte[] request=MapProtocol.request(session,sequence,command);
        instrumentation().runOnMainSync(()->activity.accept("instrumented-watch",request,result->{value.set(result);latch.countDown();}));
        assertTrue("Map acknowledgement timed out",latch.await(10,TimeUnit.SECONDS));
        return MapProtocol.parseAck(value.get(),session,sequence);
    }
    @Test public void commandsReachRealWebViewAndAreNotReplayed()throws Exception {
        Intent intent=new Intent(instrumentation().getTargetContext(),MapActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        MapActivity activity=(MapActivity)instrumentation().startActivitySync(intent);
        try{
            long until=SystemClock.elapsedRealtime()+25000;
            JSONObject initial;
            do { initial=state(activity);if(initial.optBoolean("ok"))break;Thread.sleep(100); }while(SystemClock.elapsedRealtime()<until);
            assertTrue("Bundled 3D map failed to load",initial.optBoolean("ok"));
            JSONObject layout=evaluate(activity,"JSON.stringify({width:innerWidth,edges:[...document.querySelectorAll('.physical-panel,.lab-panel,.lab-panel button')].map(el=>({left:el.getBoundingClientRect().left,right:el.getBoundingClientRect().right}))})");
            for(int index=0;index<layout.getJSONArray("edges").length();index++){
                JSONObject edge=layout.getJSONArray("edges").getJSONObject(index);
                assertTrue("Field control clipped on left",edge.getDouble("left")>=0);
                assertTrue("Field control clipped on right",edge.getDouble("right")<=layout.getDouble("width")+1);
            }

            assertTrue(send(activity,1,"HELLO").getBoolean("ok"));
            assertTrue(send(activity,2,"NEXT").getBoolean("ok"));
            assertEquals(2,state(activity).getJSONObject("state").getInt("capture"));
            assertTrue(send(activity,2,"NEXT").getBoolean("ok"));
            assertEquals("Duplicate packet advanced time twice",2,state(activity).getJSONObject("state").getInt("capture"));
            assertFalse(send(activity,1,"NEXT").getBoolean("ok"));
            assertTrue(send(activity,3,"FIELD_UP").getBoolean("ok"));
            JSONObject changed=state(activity).getJSONObject("state");
            assertEquals(1.1,changed.getDouble("dipole"),.000001);
            assertEquals(2,changed.getInt("capture"));
            assertTrue(send(activity,4,"RIGHT").getBoolean("ok"));
            assertEquals(-.50,state(activity).getJSONObject("state").getDouble("yaw"),.000001);
            assertTrue(send(activity,5,"Z_UP").getBoolean("ok"));
            assertEquals(.26,state(activity).getJSONObject("state").getDouble("slice"),.000001);
            assertTrue(send(activity,6,"RESET").getBoolean("ok"));
            assertEquals(1,state(activity).getJSONObject("state").getInt("capture"));
            assertEquals(1.1,state(activity).getJSONObject("state").getDouble("dipole"),.000001);
            assertTrue(send(activity,7,"LAB_GRAVITY").getBoolean("ok"));
            assertEquals("gravity",state(activity).getJSONObject("state").getJSONObject("lab").getString("mode"));
            assertTrue(send(activity,8,"GRAVITY_UP").getBoolean("ok"));
            assertEquals(1.1,state(activity).getJSONObject("state").getJSONObject("lab").getDouble("gravity"),.000001);
            JSONObject before=state(activity).getJSONObject("state").getJSONObject("lab");
            assertTrue(send(activity,9,"LAB_PLAY").getBoolean("ok"));
            Thread.sleep(700);
            assertTrue(send(activity,10,"LAB_PLAY").getBoolean("ok"));
            JSONObject after=state(activity).getJSONObject("state").getJSONObject("lab");
            assertTrue("Virtual bodies did not move",after.getDouble("time")>0);
            assertNotEquals(before.getJSONArray("particles").toString(),after.getJSONArray("particles").toString());
            assertFalse(after.getBoolean("running"));
            assertTrue(send(activity,11,"LAB_MAGNETIC").getBoolean("ok"));
            assertTrue(send(activity,12,"MAGNETIC_DOWN").getBoolean("ok"));
            assertEquals(.9,state(activity).getJSONObject("state").getJSONObject("lab").getDouble("magnetic"),.000001);
            assertTrue(send(activity,13,"LAB_BOTH").getBoolean("ok"));
            assertTrue(send(activity,14,"LAB_RESET").getBoolean("ok"));
            assertEquals(0,state(activity).getJSONObject("state").getJSONObject("lab").getDouble("time"),.000001);
            assertEquals(1.1,state(activity).getJSONObject("state").getDouble("dipole"),.000001);

        }finally{instrumentation().runOnMainSync(activity::finish);}
    }
    @Test public void malformedRequestsAndWrongAcknowledgementsAreRejected()throws Exception {
        for(String bad:new String[]{"{}", "{\"v\":1,\"session\":\""+session+"\",\"seq\":1,\"cmd\":\"INJECT\"}",
            "{\"v\":1,\"session\":\""+session+"\",\"seq\":1.5,\"cmd\":\"NEXT\"}"}){
            try{MapProtocol.parse(bad.getBytes(java.nio.charset.StandardCharsets.UTF_8));fail("Invalid request accepted");}
            catch(org.json.JSONException expected){}
        }
        MapProtocol.Request request=MapProtocol.parse(MapProtocol.request(session,1,"HELLO"));
        try{MapProtocol.parseAck(MapProtocol.ack(request,true,"ok"),session,2);fail("Wrong sequence accepted");}
        catch(org.json.JSONException expected){}
    }
    private byte[] telemetry(long sequence,String mode)throws Exception {
        JSONObject packet=new JSONObject().put("v",1).put("session",session).put("seq",sequence).put("mode",mode)
            .put("model","instrumented sensor fixture").put("emulator",true)
            .put("magnetic",mode.equals("measuring")?new JSONArray(new double[]{30,40,0}):JSONObject.NULL)
            .put("gravity",mode.equals("measuring")?new JSONArray(new double[]{0,0,9.81}):JSONObject.NULL)
            .put("magneticAgeMs",0).put("gravityAgeMs",0).put("magneticAccuracy",3).put("gravityAccuracy",3);
        return packet.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8);
    }
    private boolean ingest(MapActivity activity,String node,byte[] packet){
        AtomicBoolean result=new AtomicBoolean();
        instrumentation().runOnMainSync(()->result.set(activity.acceptTelemetry(node,packet)));return result.get();
    }
    @Test public void sensorPacketsRequireLivePeerAndExpireInsteadOfInventingMeasurements()throws Exception {
        MapActivity activity=(MapActivity)instrumentation().startActivitySync(new Intent(instrumentation().getTargetContext(),MapActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        try{
            long until=SystemClock.elapsedRealtime()+25000;
            while(!state(activity).optBoolean("ok")&&SystemClock.elapsedRealtime()<until)Thread.sleep(100);
            assertFalse(ingest(activity,"instrumented-watch",telemetry(1,"measuring")));
            assertTrue(send(activity,1,"HELLO").getBoolean("ok"));
            assertFalse(ingest(activity,"other-watch",telemetry(1,"measuring")));
            assertTrue(ingest(activity,"instrumented-watch",telemetry(1,"measuring")));
            JSONObject reading=evaluate(activity,"JSON.stringify({b:document.getElementById('magnetic-live').textContent,status:document.getElementById('physical-status').textContent})");
            assertTrue(reading.getString("b").startsWith("50"));assertEquals("SENSORES DE EMULADOR",reading.getString("status"));
            assertFalse(ingest(activity,"instrumented-watch",telemetry(1,"measuring")));
            Thread.sleep(4100);
            reading=evaluate(activity,"JSON.stringify({b:document.getElementById('magnetic-live').textContent,status:document.getElementById('physical-status').textContent})");
            assertEquals("—",reading.getString("b"));assertEquals("Sin lecturas recientes",reading.getString("status"));
            assertTrue(ingest(activity,"instrumented-watch",telemetry(2,"measuring")));
            assertTrue(ingest(activity,"instrumented-watch",telemetry(3,"stopped")));
            assertEquals("Medición detenida",evaluate(activity,"JSON.stringify({status:document.getElementById('physical-status').textContent})").getString("status"));
        }finally{instrumentation().runOnMainSync(activity::finish);}
    }
    @Test public void malformedTelemetryCannotMasqueradeAsASensorReading()throws Exception {
        JSONObject valid=FieldTelemetry.parse(telemetry(1,"measuring"));
        assertTrue(valid.getBoolean("emulator"));
        for(int index=0;index<5;index++){
            JSONObject bad=new JSONObject(valid.toString());
            if(index==0)bad.put("magnetic",new JSONArray(new double[]{1,2}));
            if(index==1)bad.put("gravity",new JSONArray(new String[]{"1","2","3"}));
            if(index==2)bad.put("gravityAgeMs",3001);
            if(index==3)bad.put("mode","generated");
            if(index==4){bad.remove("magnetic");bad.put("unexpected",true);}
            try{FieldTelemetry.parse(bad.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));fail("Malformed telemetry accepted");}
            catch(org.json.JSONException expected){}
        }
    }

}

