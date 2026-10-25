package com.mundox.ghzwatch;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Handler;
import android.os.SystemClock;
import com.mundox.maplink.FieldTelemetry;
import org.json.JSONArray;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

final class WatchFieldSensors implements SensorEventListener {
    interface Sink { void frame(byte[] bytes,String summary); }
    private final SensorManager manager;
    private final Handler handler;
    private final Sink sink;
    private final String session;
    private final float[][] values=new float[2][];
    private final long[] timestamp=new long[2];
    private final int[] accuracy={-1,-1};
    private boolean running;
    private long sequence;
    private final Runnable publish=new Runnable(){@Override public void run(){
        if(!running)return;emit();handler.postDelayed(this,1000);
    }};
    WatchFieldSensors(Context context,Handler h,String s,Sink callback){
        manager=(SensorManager)context.getSystemService(Context.SENSOR_SERVICE);handler=h;session=s;sink=callback;
    }
    boolean isRunning(){return running;}
    void start(){
        if(running)return;
        values[0]=values[1]=null;timestamp[0]=timestamp[1]=0;accuracy[0]=accuracy[1]=-1;
        running=true;
        if(manager!=null)for(int type:new int[]{Sensor.TYPE_MAGNETIC_FIELD,Sensor.TYPE_GRAVITY}){
            Sensor sensor=manager.getDefaultSensor(type);
            if(sensor!=null)manager.registerListener(this,sensor,100000,handler);
        }
        handler.post(publish);
    }
    void stop(){
        if(!running)return;running=false;handler.removeCallbacks(publish);
        if(manager!=null)manager.unregisterListener(this);
        values[0]=values[1]=null;emit();
    }
    @Override public void onSensorChanged(SensorEvent event){
        if(!running||event.values.length<3)return;
        int slot=event.sensor.getType()==Sensor.TYPE_MAGNETIC_FIELD?0:event.sensor.getType()==Sensor.TYPE_GRAVITY?1:-1;
        if(slot<0)return;
        float limit=slot==0?200000:100;
        for(int i=0;i<3;i++)if(!Float.isFinite(event.values[i])||Math.abs(event.values[i])>limit){values[slot]=null;return;}
        values[slot]=new float[]{event.values[0],event.values[1],event.values[2]};
        timestamp[slot]=event.timestamp;accuracy[slot]=event.accuracy;
    }
    @Override public void onAccuracyChanged(Sensor sensor,int value){
        if(sensor.getType()==Sensor.TYPE_MAGNETIC_FIELD)accuracy[0]=value;
        if(sensor.getType()==Sensor.TYPE_GRAVITY)accuracy[1]=value;
    }
    private void emit(){
        try{
            boolean emulator=Build.FINGERPRINT.startsWith("generic")||Build.MODEL.contains("Emulator")||Build.MODEL.contains("sdk_gphone")||Build.PRODUCT.contains("sdk");
            JSONObject j=new JSONObject().put("v",1).put("session",session).put("seq",++sequence)
                .put("mode",running?"measuring":"stopped").put("model",Build.MODEL.substring(0,Math.min(80,Build.MODEL.length())))
                .put("emulator",emulator);
            StringBuilder summary=new StringBuilder(emulator?"EMULADOR\n":"SENSORES · RELOJ\n");
            for(int slot=0;slot<2;slot++){
                String key=slot==0?"magnetic":"gravity";
                long age=(SystemClock.elapsedRealtimeNanos()-timestamp[slot])/1000000;
                boolean valid=running&&values[slot]!=null&&age>=0&&age<=3000;
                JSONArray vector=new JSONArray();
                if(valid)for(float v:values[slot])vector.put((double)v);
                j.put(key,valid?vector:JSONObject.NULL).put(key+"AgeMs",valid?age:0).put(key+"Accuracy",valid?accuracy[slot]:-1);
                summary.append(slot==0?"B: ":"g: ");
                if(valid){double magnitude=Math.sqrt(values[slot][0]*values[slot][0]+values[slot][1]*values[slot][1]+values[slot][2]*values[slot][2]);
                    summary.append(String.format(Locale.ROOT,"%.2f %s",magnitude,slot==0?"µT":"m/s²"));
                    if(accuracy[slot]<=0)summary.append(" · sin fiabilidad");
                }else summary.append(running?"sin muestra reciente":"detenido");
                summary.append('\n');
            }
            byte[] bytes=j.toString().getBytes(StandardCharsets.UTF_8);FieldTelemetry.parse(bytes);sink.frame(bytes,summary.toString());
        }catch(org.json.JSONException error){sink.frame(null,"No se pudieron validar las lecturas.");}
    }
}
