package com.mundox.ghzwatch;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.SystemClock;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.mundox.maplink.MapProtocol;
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
}

