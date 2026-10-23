package com.mundox.worldlink;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.google.android.gms.wearable.MessageClient;
import com.google.android.gms.wearable.MessageEvent;
import com.google.android.gms.wearable.Node;
import com.google.android.gms.wearable.Wearable;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.UUID;

/**
 * Real sensor channel on Galaxy Watch4 Classic. UI and sensors operate while visible.
 * The watch can request a flashlight pulse only if explicitly armed on the phone.
 */
public final class WatchActivity extends Activity
        implements SensorEventListener, MessageClient.OnMessageReceivedListener {
    private static final String SAMPLE="/mundox/worldlink/v1/telemetry";
    private static final String HAPTIC="/mundox/worldlink/v1/haptic";
    private static final String LIGHT_REQUEST="/mundox/worldlink/v1/light-request";
    private static final String SOUND_REQUEST="/mundox/worldlink/v1/sound-command";
    private static final int BODY_REQUEST=702;
    private static final long SAMPLE_INTERVAL_MS=300L, MAX_DURATION_MS=180000L;
    private static final int BG=0xff0c1511, PANEL=0xff223328, WHITE=0xfff1f6ed;
    private static final int LIME=0xffcff77c, MUTED=0xffb2bfb0;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final float[] accel=new float[3],gyro=new float[3],mag=new float[3];
    private final List<String> nodes=new ArrayList<>();
    private SensorManager sensors;
    private TextView status, link, magneticValue, lightValue, movementValue, heartValue;
    private Button startButton, pulseButton, gestureButton;
    private boolean gestureEnabled,gestureLatched;
    private long lastGestureAt;
    private String sessionId, info="Toca INICIAR";
    private boolean running,heartEnabled,hasA,hasG,hasM,hasL,hasH;
    private float lux, bpm;
    private long sequence, began, lastQuery;
    private boolean listenerAdded;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        sensors=(SensorManager)getSystemService(Context.SENSOR_SERVICE);
        createUI();
    }

    private void createUI() {
        ScrollView scroll=new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root=new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(dp(23),dp(8),dp(23),dp(14));
        root.setBackgroundColor(BG);
        TextView brand=label("MUNDO X   ·   WORLDLINK",10,LIME,true);
        brand.setGravity(Gravity.CENTER);
        brand.setLetterSpacing(.05f);root.addView(brand);
        TextView title=label("CONTROL FÍSICO",17,WHITE,true);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0,dp(5),0,dp(8));root.addView(title);

        LinearLayout heading=panel(root);
        status=label("LISTO PARA INTERACTUAR",14,LIME,true);
        status.setGravity(Gravity.CENTER);heading.addView(status);
        link=label("Activa CONTROL REMOTO en el Samsung",11,MUTED,false);
        link.setGravity(Gravity.CENTER);link.setPadding(0,dp(4),0,0);heading.addView(link);

        LinearLayout interaction=panel(root);
        interaction.addView(label("ACTUAR SOBRE EL ENTORNO",11,LIME,true));
        interaction.addView(label("El Samsung emitirá ondas de presión física. "
            +"Activa CONTROL REMOTO en la app del móvil. "
            +"No necesitas iniciar los sensores para usar estos controles.",
            10,MUTED,false));
        control(interaction,"ONDA · 220 Hz",()->sendSound("tone:220"),true);
        control(interaction,"ONDA · 440 Hz",()->sendSound("tone:440"),false);
        control(interaction,"BARRER FRECUENCIAS",()->sendSound("sweep"),false);
        control(interaction,"MIL CORE 1000 → ONDA",()->sendSound("quantum"),true);
        control(interaction,"PARAR EMISIÓN",()->sendSound("stop"),false);
        pulseButton=control(interaction,"ENVIAR PULSO LUMINOSO",this::requestLight,false);
        gestureButton=control(interaction,"ACTIVAR GIRO DE MUÑECA",this::toggleGesture,false);
        interaction.addView(label("Gira la muñeca con decisión: el reloj envía el giro al núcleo MIL 1000. "
                +"El gesto funciona mientras esta pantalla permanezca abierta.",
                10,MUTED,false));

        LinearLayout signals=panel(root);
        signals.addView(label("SENSORES · OPCIONALES",10,LIME,true));
        magneticValue=datum(signals,"CAMPO","— µT");
        lightValue=datum(signals,"LUZ","— lux");
        movementValue=datum(signals,"GIRO","— rad/s");
        heartValue=datum(signals,"PULSO","— lpm");

        startButton=control(root,"INICIAR LECTURA",()->{
            if(running)stop();
            else start();
        },true);
         control(root,"ACTIVAR PULSO (OPCIONAL)",this::allowHeart,false);
        TextView note=label("ONDA SONORA = PRESIÓN REAL EN EL AIRE.\n"
                +"No significa cambiar átomos arbitrariamente.\n"
                +"Frecuencias limitadas · duración breve.",10,MUTED,false);
        note.setGravity(Gravity.CENTER);
        note.setPadding(0,dp(10),0,0);
        root.addView(note);
        scroll.addView(root);
        setContentView(scroll);
    }

    private TextView datum(LinearLayout parent,String title,String value) {
        LinearLayout row=new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0,dp(3),0,dp(3));
        TextView label=label(title,10,MUTED,true);
        row.addView(label,new LinearLayout.LayoutParams(0,-2,1));
        TextView result=label(value,12,WHITE,true);
        result.setGravity(Gravity.END);
        row.addView(result,new LinearLayout.LayoutParams(0,-2,2));
        parent.addView(row);
        return result;
    }

    @Override protected void onResume() {
        super.onResume();
        if(!listenerAdded) {
            listenerAdded=true;
            Wearable.getMessageClient(this).addListener(this);
        }
        queryNodes();
    }
    @Override protected void onPause() {
        gestureEnabled=false;
        if(gestureButton!=null)gestureButton.setText("ACTIVAR GIRO DE MUÑECA");
        stop();
        if(listenerAdded) {
            Wearable.getMessageClient(this).removeListener(this);
            listenerAdded=false;
        }
        super.onPause();
    }
    @Override protected void onDestroy() {
        stop();
        super.onDestroy();
    }

    private void start() {
        if(running)return;
        running=true;
        hasA=false;hasG=false;hasM=false;hasL=false;hasH=false;
        sessionId=UUID.randomUUID().toString();
        sequence=0;
        began=SystemClock.elapsedRealtime();
        lastQuery=0;
        nodes.clear();
        attach(Sensor.TYPE_ACCELEROMETER);
        attach(Sensor.TYPE_GYROSCOPE);
        attach(Sensor.TYPE_MAGNETIC_FIELD);
        attach(Sensor.TYPE_LIGHT);
        if(heartEnabled && checkSelfPermission(Manifest.permission.BODY_SENSORS)
                == PackageManager.PERMISSION_GRANTED)attach(Sensor.TYPE_HEART_RATE);
        startButton.setText("DETENER LECTURA");
        // Actuators do not depend on sensor acquisition.
        status.setText("LEYENDO...");
        status.setTextColor(LIME);
        info="Buscando el Samsung";
        sendLoop.run();
    }
    private void attach(int type) {
        if(sensors==null)return;
        Sensor sensor=sensors.getDefaultSensor(type);
        if(sensor!=null)sensors.registerListener(this,sensor,100000);
    }
    private void stop() {
        boolean wasRunning=running;
        running=false;
        handler.removeCallbacks(sendLoop);
        if(sensors!=null)sensors.unregisterListener(this);
        if(gestureEnabled)attach(Sensor.TYPE_GYROSCOPE);
        if(startButton!=null)startButton.setText("INICIAR LECTURA");
        // Keep remote controls usable when sensor acquisition stops.
        if(status!=null)status.setText(wasRunning?"DETENIDO":"EN ESPERA");
        if(link!=null && wasRunning)link.setText("Lecturas detenidas");
    }
    private void allowHeart() {
        if(checkSelfPermission(Manifest.permission.BODY_SENSORS)!=PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.BODY_SENSORS},BODY_REQUEST);
            return;
        }
        heartEnabled=true;
        if(running)attach(Sensor.TYPE_HEART_RATE);
        info="Pulso autorizado";
        if(link!=null)link.setText(info);
    }

    @Override public void onRequestPermissionsResult(int request,String[] names,int[] granted) {
        super.onRequestPermissionsResult(request,names,granted);
        if(request==BODY_REQUEST && granted.length>0 &&
                granted[0]==PackageManager.PERMISSION_GRANTED)allowHeart();
        else if(request==BODY_REQUEST && link!=null)link.setText("Pulso no autorizado; otros sensores disponibles");
    }

    private final Runnable sendLoop=new Runnable() {
        @Override public void run() {
            if(!running)return;
            long now=SystemClock.elapsedRealtime();
            if(now-began>=MAX_DURATION_MS) {
                stop();
                link.setText("3 minutos finalizados · puedes reiniciar");
                return;
            }
            if(now-lastQuery>5000) queryNodes();
            sendSample();
            status.setText(nodes.isEmpty()?"BUSCANDO TELÉFONO":"ENLACE ACTIVO");
            link.setText("Destinos: "+nodes.size()+"   ·   Muestras: "+sequence);
            handler.postDelayed(this,SAMPLE_INTERVAL_MS);
        }
    };

    private void queryNodes() {
        lastQuery=SystemClock.elapsedRealtime();
        Wearable.getNodeClient(this).getConnectedNodes()
            .addOnSuccessListener(available->{
                nodes.clear();
                for(Node node:available)nodes.add(node.getId());
            })
            .addOnFailureListener(e->{if(link!=null)link.setText("Sin enlace: "+e.getClass().getSimpleName());});
    }

    private void sendSample() {
        if(!hasA || !hasG || nodes.isEmpty())return;
        try {
            JSONObject j=new JSONObject();
            int mask=1|2|(hasM?4:0)|(hasH?8:0)|(hasL?16:0);
            j.put("v",1);
            j.put("sid",sessionId);
            j.put("seq",++sequence);
            j.put("mask",mask);
            j.put("t",System.currentTimeMillis());
            j.put("ax",accel[0]);j.put("ay",accel[1]);j.put("az",accel[2]);
            j.put("gx",gyro[0]);j.put("gy",gyro[1]);j.put("gz",gyro[2]);
            j.put("mx",mag[0]);j.put("my",mag[1]);j.put("mz",mag[2]);
            j.put("lux",hasL?lux:0);
            j.put("hr",hasH?bpm:0);
            byte[] raw=j.toString().getBytes(StandardCharsets.UTF_8);
            for(String id:nodes)
                Wearable.getMessageClient(this).sendMessage(id,SAMPLE,raw);
        } catch(Exception e) {
            link.setText("Error de lectura: "+e.getClass().getSimpleName());
        }
    }
    private void toggleGesture() {
        gestureEnabled=!gestureEnabled;
        gestureLatched=false;
        lastGestureAt=0;
        if(gestureEnabled) {
            if(!running)attach(Sensor.TYPE_GYROSCOPE);
            link.setText("Gesto listo: giro de muñeca → onda física en el Samsung");
            gestureButton.setText("DETENER CONTROL POR GESTO");
        } else {
            if(!running && sensors!=null)sensors.unregisterListener(this);
            link.setText("Control por gesto desactivado");
            gestureButton.setText("ACTIVAR GIRO DE MUÑECA");
        }
    }

    private void requestLight() {
        sendRemote(LIGHT_REQUEST,new byte[]{1},"Pulso luminoso solicitado");
    }

    private void sendSound(String command) {
        sendRemote(SOUND_REQUEST,command.getBytes(StandardCharsets.UTF_8),
            "Orden física: "+command+" · activa CONTROL REMOTO en móvil");
    }

    private void sendRemote(String route,byte[] payload,String message) {
        Wearable.getNodeClient(this).getConnectedNodes()
            .addOnSuccessListener(found->{
                if(found.isEmpty()){
                    link.setText("No se detectó la app Samsung: revisa Wear OS");
                    return;
                }
                for(Node node:found) {
                    Wearable.getMessageClient(this).sendMessage(node.getId(),route,payload)
                        .addOnFailureListener(e->link.setText("Enlace falló: "+e.getClass().getSimpleName()));
                }
                link.setText(message);
            })
            .addOnFailureListener(e->link.setText("No se pudo conectar con el móvil"));
    }

    @Override public void onSensorChanged(SensorEvent event) {
        float[] v=event.values;
        switch(event.sensor.getType()) {
            case Sensor.TYPE_ACCELEROMETER:
                if(v.length>=3 && valid(v[0],v[1],v[2])) {
                    System.arraycopy(v,0,accel,0,3);hasA=true;
                }
                break;
            case Sensor.TYPE_GYROSCOPE:
                if(v.length>=3 && valid(v[0],v[1],v[2])) {
                    System.arraycopy(v,0,gyro,0,3);hasG=true;
                    float magnitude=(float)Math.sqrt(gyro[0]*gyro[0]+gyro[1]*gyro[1]+gyro[2]*gyro[2]);
                    movementValue.setText(String.format(Locale.US,"%.2f rad/s",magnitude));
                    // Opt-in physical control, not a screen decoration.
                    if(gestureEnabled) {
                        if(magnitude<1.25f)gestureLatched=false;
                        long now=SystemClock.elapsedRealtime();
                        if(!gestureLatched && magnitude>=4.0f && now-lastGestureAt>=2600L) {
                            gestureLatched=true;lastGestureAt=now;
                            float strongest=gyro[0];
                            if(Math.abs(gyro[1])>Math.abs(strongest))strongest=gyro[1];
                            if(Math.abs(gyro[2])>Math.abs(strongest))strongest=gyro[2];
                            // Send the measured wrist angular rate through the actual
                            // quantum-core route; do not fabricate a fixed 220/440-Hz tone.
                            int hundredths=Math.min(1800,Math.max(0,Math.round(magnitude*100f)));
                            sendSound("quantum:"+hundredths);
                        }
                    }
                }
                break;
            case Sensor.TYPE_MAGNETIC_FIELD:
                if(v.length>=3 && valid(v[0],v[1],v[2])) {
                    System.arraycopy(v,0,mag,0,3);hasM=true;
                    magneticValue.setText(String.format(Locale.US,"%.1f µT",Math.sqrt(mag[0]*mag[0]+mag[1]*mag[1]+mag[2]*mag[2])));
                }
                break;
            case Sensor.TYPE_LIGHT:
                if(v.length>0 && Float.isFinite(v[0]) && v[0]>=0) {
                    lux=v[0];hasL=true;
                    lightValue.setText(String.format(Locale.US,"%.1f lux",lux));
                }
                break;
            case Sensor.TYPE_HEART_RATE:
                if(v.length>0 && Float.isFinite(v[0]) && v[0]>0) {
                    bpm=v[0];hasH=true;
                    heartValue.setText(String.format(Locale.US,"%.0f lpm",bpm));
                }
                break;
            default:break;
        }
    }
    private static boolean valid(float a,float b,float c){
        return Float.isFinite(a)&&Float.isFinite(b)&&Float.isFinite(c);
    }
    @Override public void onAccuracyChanged(Sensor sensor,int accuracy) {}

    @Override public void onMessageReceived(MessageEvent event) {
        if(!HAPTIC.equals(event.getPath()))return;
        if(!running)return;
        Vibrator vibration=(Vibrator)getSystemService(Context.VIBRATOR_SERVICE);
        if(vibration!=null && vibration.hasVibrator())
            vibration.vibrate(VibrationEffect.createOneShot(100,VibrationEffect.DEFAULT_AMPLITUDE));
    }

    private LinearLayout panel(LinearLayout root) {
        LinearLayout p=new LinearLayout(this);
        p.setOrientation(LinearLayout.VERTICAL);
        p.setPadding(dp(12),dp(10),dp(12),dp(10));
        GradientDrawable bg=new GradientDrawable();
        bg.setColor(PANEL);bg.setCornerRadius(dp(14));
        bg.setStroke(dp(1),0xff38533e);
        p.setBackground(bg);
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);
        lp.bottomMargin=dp(8);
        root.addView(p,lp);
        return p;
    }
    private Button control(LinearLayout root,String label,Runnable onClick,boolean prominent) {
        Button b=new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(11);
        b.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        b.setTextColor(prominent?BG:WHITE);
        GradientDrawable bg=new GradientDrawable();
        bg.setColor(prominent?LIME:0xff2c4433);
        bg.setCornerRadius(dp(30));
        b.setBackground(bg);
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,dp(47));
        p.bottomMargin=dp(7);
        root.addView(b,p);
        b.setOnClickListener(v->onClick.run());
        return b;
    }
    private TextView label(String str,int size,int color,boolean strong){
        TextView t=new TextView(this);
        t.setText(str);t.setTextSize(size);t.setTextColor(color);
        if(strong)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        return t;
    }
    private int dp(float v){
        return Math.round(v*getResources().getDisplayMetrics().density);
    }
}