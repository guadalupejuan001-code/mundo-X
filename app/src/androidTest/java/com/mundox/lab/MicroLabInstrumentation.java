package com.mundox.lab;

import android.app.Instrumentation;
import android.content.ComponentName;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.View;
import android.widget.ScrollView;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

/** Exercises the native bridge, rendered controls, reproducibility and lifecycle. */
public final class MicroLabInstrumentation extends Instrumentation {
    private MicroLabActivity activity;
    private int passed,failed;
    private final List<String> notes=new ArrayList<>();
    @Override public void onCreate(Bundle args){super.onCreate(args);start();}
    @Override public void onStart(){new Thread(this::checks,"micro-checks").start();}
    private interface Check{void run()throws Exception;}
    private interface Condition{boolean ready(JSONObject s)throws Exception;}
    private void check(String name,Check body){try{body.run();passed++;notes.add("PASS "+name);}catch(Throwable e){failed++;notes.add("FAIL "+name+": "+e);}}
    private void require(boolean ok,String message){if(!ok)throw new AssertionError(message);}
    private void checks(){
        try{
            check("jni_buffer_bounds_and_session_lifetime",()->{
                long h=NativeCore.microCreate(99);
                try{
                    ByteBuffer data=ByteBuffer.allocateDirect(16384*36).order(ByteOrder.nativeOrder());
                    require(NativeCore.microFrame(h,1,data)==4096,"tracer count");
                    for(int i=0;i<4096;i++)for(int k=0;k<3;k++){float p=data.getFloat(i*36+k*4);require(Float.isFinite(p)&&Math.abs(p)<=1,"particle bounds");}
                    boolean rejected=false;try{NativeCore.microFrame(h,0,ByteBuffer.allocate(1024));}catch(IllegalStateException e){rejected=true;}
                    require(rejected,"heap buffer must be rejected");
                    rejected=false;try{NativeCore.microFrame(h,0,ByteBuffer.allocateDirect(4));}catch(IllegalStateException e){rejected=true;}
                    require(rejected,"short buffer must be rejected");
                }finally{NativeCore.microDestroy(h);}
                boolean rejected=false;try{NativeCore.microSnapshot(h);}catch(IllegalStateException e){rejected=true;}require(rejected,"closed handle must be rejected");
            });
            Intent intent=new Intent(Intent.ACTION_MAIN).setComponent(new ComponentName(getTargetContext(),"com.mundox.lab.MicroLabActivity"));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);activity=(MicroLabActivity)startActivitySync(intent);
            await(s->s.optLong("ticks")>5);
            check("render_and_pause",()->{
                click("micro_play");SystemClock.sleep(350);long ticks=state().getLong("ticks");SystemClock.sleep(250);
                require(state().getLong("ticks")==ticks,"pause must freeze model");require(activity.lastError.isEmpty(),activity.lastError);screenshot("micro-particles.png");
            });
            check("single_step",()->{long ticks=state().getLong("ticks");click("micro_step");await(s->s.optLong("ticks")==ticks+1);});
            check("particle_capacity",()->{click("micro_count");await(s->s.optInt("particles")==8192);click("micro_count");await(s->s.optInt("particles")==16384);});
            check("quantum_gates_and_bell_entropy",()->{
                click("micro_mode_2");await(s->s.optInt("mode")==2);click("micro_preset_0");await(s->q(s,0,"p1")==0);
                click("micro_gate_0");await(s->Math.abs(q(s,0,"x")-1)<1e-8);
                click("micro_gate_1");await(s->Math.abs(q(s,0,"y")-Math.sqrt(.5))<1e-8);
                click("micro_preset_0");await(s->q(s,0,"p1")==0);click("micro_gate_0");await(s->Math.abs(q(s,0,"x")-1)<1e-8);
                click("micro_gate_4");await(s->Math.abs(q(s,0,"entropy")-1)<1e-8);
                scrollTo("micro_state");screenshot("micro-quantum.png");
                click("micro_measure");await(s->s.optInt("measurement",-1)>=0);int outcome=state().getInt("measurement");
                require(outcome==0||outcome==3,"Bell correlation lost");require(q(state(),0,"entropy")==0,"joint collapse");
            });
            check("colony_growth_food_and_seeding",()->{
                click("micro_mode_1");await(s->s.optInt("mode")==1);
                int before=state().getInt("cells");command(9,1,0,0,0);await(s->s.optInt("cells")==before+12);
                double nutrient=state().getDouble("nutrient");command(9,0,0,0,0);await(s->s.optDouble("nutrient")>nutrient);
                // Fixed native steps avoid tying the biological assertion to emulator speed.
                long endTicks=state().getLong("ticks")+480;
                for(int i=0;i<480;i++)command(11,0,0,0,0);
                await(s->s.optLong("ticks")==endTicks);require(state().getLong("divisions")>0,"colony did not divide");require(state().getInt("cells")<=2048,"cell capacity");
                scrollTop();screenshot("micro-colony.png");
            });
            check("coupling_switch_and_seed_reset",()->{
                click("micro_coupling");await(s->!s.optBoolean("coupled"));
                command(10,123,0,0,0);await(s->s.optInt("seed")==123&&s.optLong("ticks")==0);
                require(state().getInt("cells")==48,"reset population");
            });
            check("background_resume_and_gl_recreation",()->{
                runOnMainSync(()->{callActivityOnPause(activity);callActivityOnResume(activity);});
                click("micro_step");await(s->s.optLong("ticks")==1);require(activity.lastError.isEmpty(),activity.lastError);
            });
        }catch(Throwable e){failed++;notes.add("FAIL setup: "+e);}
        finally{
            try{
                File dir=getTargetContext().getExternalFilesDir(null);JSONObject result=new JSONObject();result.put("passed",passed);result.put("failed",failed);result.put("notes",new org.json.JSONArray(notes));
                if(activity!=null)result.put("snapshot",state());try(PrintWriter out=new PrintWriter(new File(dir,"micro-results.json"))){out.print(result.toString(2));}
            }catch(Exception e){failed++;notes.add("FAIL evidence: "+e);}
            if(activity!=null)runOnMainSync(()->activity.finish());
            Bundle result=new Bundle();result.putInt("tests_passed",passed);result.putInt("tests_failed",failed);result.putString("stream",String.join("\n",notes));finish(failed==0?-1:0,result);
        }
    }
    private double q(JSONObject s,int index,String key)throws Exception{return s.getJSONArray("qubits").getJSONObject(index).getDouble(key);}
    private JSONObject state()throws Exception{return new JSONObject(activity.latestSnapshot);}
    private void await(Condition condition)throws Exception{
        long end=SystemClock.uptimeMillis()+20000;
        while(SystemClock.uptimeMillis()<end){require(activity.lastError.isEmpty(),activity.lastError);if(condition.ready(state()))return;SystemClock.sleep(50);}
        throw new AssertionError("state timeout: "+activity.latestSnapshot);
    }
    private void command(int op,int a,int b,float x,float y){runOnMainSync(()->activity.labCommand(op,a,b,x,y));}
    private View find(String tag){View v=activity.getWindow().getDecorView().findViewWithTag(tag);require(v!=null,"missing "+tag);return v;}
    private void click(String tag){runOnMainSync(()->{View v=find(tag);v.requestRectangleOnScreen(new Rect(0,0,v.getWidth(),v.getHeight()),true);require(v.performClick(),"click "+tag);});waitForIdleSync();}
    private void scrollTo(String tag){runOnMainSync(()->{View v=find(tag);v.requestRectangleOnScreen(new Rect(0,0,v.getWidth(),v.getHeight()),true);});waitForIdleSync();SystemClock.sleep(400);}
    private void scrollTop(){runOnMainSync(()->((ScrollView)find("micro_controls")).scrollTo(0,0));waitForIdleSync();SystemClock.sleep(400);}
    private void screenshot(String name)throws Exception{
        waitForIdleSync();SystemClock.sleep(500);Bitmap bitmap=getUiAutomation().takeScreenshot();require(bitmap!=null,"screenshot unavailable");
        try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getExternalFilesDir(null),name))){require(bitmap.compress(Bitmap.CompressFormat.PNG,100,out),"screenshot write");}finally{bitmap.recycle();}
    }
}
