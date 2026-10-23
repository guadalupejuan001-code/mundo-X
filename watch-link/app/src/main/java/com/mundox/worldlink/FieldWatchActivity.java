package com.mundox.worldlink;

import android.app.Activity;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.google.android.gms.wearable.Node;
import com.google.android.gms.wearable.Wearable;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.UUID;

/** Dedicated experiment mode: no light, vibration, sound or heart sensor during capture. */
public final class FieldWatchActivity extends Activity implements SensorEventListener {
    private static final String ROUTE="/mundox/field/v1/sample";
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final List<String> nodes=new ArrayList<>();
    private final double[] q=new double[4];
    private SensorManager manager;
    private Sensor magnetic,rotation;
    private TextView status,reading;
    private Button button;
    private boolean running;
    private String sid;
    private long qt,lastSend,began,sequence,skipped;
    private int pending;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        manager=(SensorManager)getSystemService(SENSOR_SERVICE);
        if(manager!=null){magnetic=manager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED);
            rotation=manager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR);}
        ScrollView scroll=new ScrollView(this);
        LinearLayout root=new LinearLayout(this);root.setOrientation(1);root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(26,28,26,32);root.setBackgroundColor(0xff101a17);
        add(root,"WORLDLINK · CAMPO",14,0xffcbf47e);
        add(root,"PREDICCIÓN FÍSICA",19,0xfff2f6ee);
        status=add(root,"Comprueba el enlace con tu Samsung",12,0xffc5d1c7);
        reading=add(root,"— µT",20,0xffcbf47e);
        button=new Button(this);button.setText("INICIAR SENSORES");root.addView(button);
        button.setOnClickListener(v->{if(running)stop("Lecturas detenidas");else start();});
        add(root,"1. Inicia aquí.\n2. En el móvil, inicia la prueba.\n3. Gira lentamente la muñeca en varios ejes, sin cambiar de lugar.\nMantén ambas pantallas abiertas.",12,0xffc5d1c7);
        add(root,"50 s de experimento · sin vibraciones\nCampo real + orientación inercial",11,0xff9eafa2);
        scroll.addView(root);setContentView(scroll);
        if(magnetic==null || rotation==null){button.setEnabled(false);status.setText(
                "Sensor requerido no disponible: "+(magnetic==null?"campo sin calibrar ":"")+(rotation==null?"orientación inercial":""));}
    }
    private TextView add(LinearLayout root,String text,int size,int color){
        TextView t=new TextView(this);t.setText(text);t.setTextSize(size);t.setTextColor(color);t.setGravity(Gravity.CENTER);
        t.setPadding(0,8,0,8);root.addView(t);return t;
    }
    private void start() {
        if(running || magnetic==null || rotation==null)return;
        sid=UUID.randomUUID().toString();qt=0;lastSend=0;sequence=0;skipped=0;pending=0;
        running=true;began=SystemClock.elapsedRealtime();
        boolean r=manager.registerListener(this,rotation,20_000);
        boolean m=manager.registerListener(this,magnetic,50_000);
        if(!r||!m){stop("El sistema no permitió registrar ambos sensores");return;}
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        button.setText("DETENER");status.setText("Buscando Samsung…");tick.run();
    }
    private void stop(String message) {
        running=false;handler.removeCallbacks(tick);if(manager!=null)manager.unregisterListener(this);
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        button.setText("INICIAR SENSORES");status.setText(message);
    }
    private final Runnable tick=new Runnable(){@Override public void run(){
        if(!running)return;
        if(SystemClock.elapsedRealtime()-began>180_000){stop("Sesión de sensores terminada (3 min)");return;}
        Wearable.getNodeClient(FieldWatchActivity.this).getConnectedNodes().addOnSuccessListener(found->{
            if(!running)return;nodes.clear();for(Node n:found)nodes.add(n.getId());
            status.setText(nodes.isEmpty()?"Sin teléfono enlazado":String.format(Locale.US,
                    "%d lecturas · %d omitidas\nInicia la prueba en el móvil",sequence,skipped));
        }).addOnFailureListener(e->{if(running)status.setText("No se pudo consultar el enlace");});
        handler.postDelayed(this,2000);
    }};
    @Override public void onSensorChanged(SensorEvent e) {
        if(!running)return;
        if(e.sensor.getType()==Sensor.TYPE_GAME_ROTATION_VECTOR) {
            if(e.values.length<3)return;
            double n=0;for(int i=0;i<3;i++){q[i]=e.values[i];n+=q[i]*q[i];}
            q[3]=e.values.length>=4?e.values[3]:Math.sqrt(Math.max(0,1-n));qt=e.timestamp;
            return;
        }
        if(e.sensor.getType()!=Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED || e.values.length<3)return;
        if(e.timestamp-lastSend<100_000_000L)return;
        if(qt<=0 || qt>e.timestamp || e.timestamp-qt>50_000_000L || nodes.isEmpty() || pending>=4){skipped++;return;}
        for(int i=0;i<3;i++)if(!Float.isFinite(e.values[i])){skipped++;return;}
        try {
            JSONObject j=new JSONObject();j.put("v",1);j.put("sid",sid);j.put("seq",++sequence);
            j.put("mt",e.timestamp);j.put("qt",qt);j.put("mx",e.values[0]);j.put("my",e.values[1]);j.put("mz",e.values[2]);
            j.put("qx",q[0]);j.put("qy",q[1]);j.put("qz",q[2]);j.put("qw",q[3]);
            j.put("accuracy",e.accuracy);j.put("orientation","game_rotation_vector");
            j.put("field","magnetic_field_uncalibrated");j.put("units","uT");
            byte[] bytes=j.toString().getBytes(StandardCharsets.UTF_8);lastSend=e.timestamp;
            final String sentSid=sid;
            // One phone per run prevents multiple receivers from silently sharing an experiment.
            pending++;
            Wearable.getMessageClient(this).sendMessage(nodes.get(0),ROUTE,bytes)
                .addOnCompleteListener(task->{if(sentSid.equals(sid)){pending=Math.max(0,pending-1);
                    if(!task.isSuccessful()){skipped++;status.setText("Envío falló; revisa el enlace");}}});
            double norm=Math.sqrt(e.values[0]*e.values[0]+e.values[1]*e.values[1]+e.values[2]*e.values[2]);
            reading.setText(String.format(Locale.US,"%.1f µT",norm));
        } catch(Exception ex){skipped++;}
    }
    @Override public void onAccuracyChanged(Sensor sensor,int accuracy){}
    @Override protected void onPause(){stop("Pausado · inicia una sesión nueva");super.onPause();}
}
