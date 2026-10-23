package com.mundox.worldlink;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;

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
 * User-started, time-bounded foreground sensor acquisition for Galaxy Watch4.
 * It DOES NOT rely on an Activity being visible. A partial wake lock keeps
 * processing alive while the watch display sleeps; Android may still interrupt
 * sensing under device policy, which the phone reports as missing telemetry.
 */
public final class WatchSensorService extends Service
        implements SensorEventListener, MessageClient.OnMessageReceivedListener {
    public static final String ACTION_START="com.mundox.worldlink.action.START";
    public static final String ACTION_STOP="com.mundox.worldlink.action.STOP";
    public static final String SAMPLE_PATH="/mundox/worldlink/v1/telemetry";
    public static final String HAPTIC_PATH="/mundox/worldlink/v1/haptic";
    public static final String LIGHT_REQUEST_PATH="/mundox/worldlink/v1/light-request";
    public static final long MAX_DURATION_MS=8*60*1000L;
    private static final long SEND_EVERY_MS=200L;
    private static final int NOTIFICATION_ID=74;
    private static final String NOTIFICATION_CHANNEL="worldlink-sensing";
    private static volatile State lastState=State.empty();
    private static volatile WatchSensorService active;

    /** Immutable reading; UI can inspect without registering any sensors. */
    public static final class State {
        public final boolean running,hasAccelerometer,hasGyro,hasMagnet,hasLight,hasHeart;
        public final float ax,ay,az,gx,gy,gz,mx,my,mz,lux,bpm;
        public final int connected;
        public final long sent,elapsedMs;
        public final String detail;
        State(boolean running,boolean ha,boolean hg,boolean hm,boolean hl,boolean hh,
              float ax,float ay,float az,float gx,float gy,float gz,
              float mx,float my,float mz,float lux,float bpm,int connected,
              long sent,long elapsedMs,String detail){
            this.running=running;hasAccelerometer=ha;hasGyro=hg;hasMagnet=hm;
            hasLight=hl;hasHeart=hh;this.ax=ax;this.ay=ay;this.az=az;
            this.gx=gx;this.gy=gy;this.gz=gz;this.mx=mx;this.my=my;this.mz=mz;
            this.lux=lux;this.bpm=bpm;this.connected=connected;
            this.sent=sent;this.elapsedMs=elapsedMs;this.detail=detail;
        }
        static State empty() {
            return new State(false,false,false,false,false,false,
                 0,0,0,0,0,0,0,0,0,Float.NaN,Float.NaN,0,0,0,
                 "Activa una sesión para leer el reloj");
        }
    }
    public static State snapshot() { return lastState; }
    public static boolean isActive() { return active!=null && active.running; }

    private final Handler handler=new Handler(Looper.getMainLooper());
    private final List<String> connectedNodes=new ArrayList<>();
    private final float[] a=new float[3],g=new float[3],m=new float[3];
    private SensorManager sensors;
    private PowerManager.WakeLock wakeLock;
    private long startAt,seq,lastDiscovery;
    private String sid;
    private boolean running,hasA,hasG,hasM,hasL,hasH;
    private float lux=Float.NaN,bpm=Float.NaN;
    private String stateMessage="Preparando sensores";

    @Override public void onCreate() {
        super.onCreate();
        sensors=(SensorManager)getSystemService(Context.SENSOR_SERVICE);
    }

    @Override public int onStartCommand(Intent intent,int flags,int startId) {
        if(intent!=null && ACTION_STOP.equals(intent.getAction())) {
            stopSession("Detenido manualmente");
            return START_NOT_STICKY;
        }
        if(!running) {
            try { beginSession(); }
            catch (Exception e) {
                stateMessage="No fue posible mantener los sensores: "+e.getClass().getSimpleName();
                publishState(false);
                stopSelf();
            }
        }
        return START_NOT_STICKY;
    }

    private void beginSession() {
        createNotificationChannel();
        Notification notice=notification();
        if(Build.VERSION.SDK_INT>=29)
            startForeground(NOTIFICATION_ID,notice,ServiceInfo.FOREGROUND_SERVICE_TYPE_HEALTH);
        else startForeground(NOTIFICATION_ID,notice);

        running=true;active=this;
        sid=UUID.randomUUID().toString();
        startAt=SystemClock.elapsedRealtime();
        lastDiscovery=0;
        seq=0;connectedNodes.clear();
        hasA=false;hasG=false;hasM=false;hasL=false;hasH=false;
        lux=Float.NaN;bpm=Float.NaN;
        stateMessage="Grabación física activa incluso con pantalla apagada";

        PowerManager pm=(PowerManager)getSystemService(POWER_SERVICE);
        if(pm!=null) {
            wakeLock=pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"WorldLink:SensorSession");
            wakeLock.setReferenceCounted(false);
            // This lock is capped even if service shutdown is unexpectedly interrupted.
            wakeLock.acquire(MAX_DURATION_MS+12000L);
        }

        attach(Sensor.TYPE_ACCELEROMETER);
        attach(Sensor.TYPE_GYROSCOPE);
        attach(Sensor.TYPE_MAGNETIC_FIELD);
        attach(Sensor.TYPE_LIGHT);
        // Body sensor is OPTIONAL. Some Wear OS policies do not expose it
        // with the display off unless extra background access is granted.
        if(checkSelfPermission(Manifest.permission.BODY_SENSORS)==PackageManager.PERMISSION_GRANTED)
            attach(Sensor.TYPE_HEART_RATE);

        Wearable.getMessageClient(this).addListener(this);
        publishState(true);
        handler.post(sendLoop);
    }

    private void attach(int type) {
        if(sensors==null) return;
        Sensor sensor=sensors.getDefaultSensor(type);
        if(sensor==null)return;
        try {
            sensors.registerListener(this,sensor,50000);
        } catch (SecurityException ignored) {
            // Preserve the other sensors when heart-rate/background policy blocks one.
        }
    }

    private final Runnable sendLoop=new Runnable() {
        @Override public void run() {
            if(!running)return;
            long elapsed=SystemClock.elapsedRealtime()-startAt;
            if(elapsed>=MAX_DURATION_MS) {
                stopSession("8 minutos completados · sesión detenida");
                return;
            }
            if(SystemClock.elapsedRealtime()-lastDiscovery>=4000L) findNodes();
            transmit();
            publishState(true);
            handler.postDelayed(this,SEND_EVERY_MS);
        }
    };

    private void findNodes() {
        lastDiscovery=SystemClock.elapsedRealtime();
        Wearable.getNodeClient(this).getConnectedNodes()
            .addOnSuccessListener(nodes->{
                if(!running)return;
                connectedNodes.clear();
                for(Node node:nodes)connectedNodes.add(node.getId());
                publishState(true);
            })
            .addOnFailureListener(e->{
                if(running)stateMessage="No fue posible consultar los nodos Wear OS";
            });
    }

    private void transmit() {
        if(!hasA || !hasG || connectedNodes.isEmpty())return;
        try {
            JSONObject j=new JSONObject();
            j.put("v",2);
            j.put("sid",sid);
            j.put("seq",++seq);
            j.put("t",System.currentTimeMillis());
            j.put("mono",SystemClock.elapsedRealtime());
            j.put("mask",1|2|(hasM?4:0)|(hasH?8:0)|(hasL?16:0));
            j.put("ax",a[0]);j.put("ay",a[1]);j.put("az",a[2]);
            j.put("gx",g[0]);j.put("gy",g[1]);j.put("gz",g[2]);
            j.put("mx",m[0]);j.put("my",m[1]);j.put("mz",m[2]);
            j.put("lux",hasL?lux:0);
            j.put("hr",hasH?bpm:0);
            byte[] encoded=j.toString().getBytes(StandardCharsets.UTF_8);
            for(String node:connectedNodes) {
                Wearable.getMessageClient(this).sendMessage(node,SAMPLE_PATH,encoded)
                    .addOnFailureListener(e->{
                        if(running)stateMessage="Sin confirmación de envío; comprobar conexión";
                    });
            }
        } catch(Exception e) {
            stateMessage="Error al codificar señales: "+e.getClass().getSimpleName();
        }
    }

    private void publishState(boolean currentlyRunning) {
        long elapsed=startAt==0?0:SystemClock.elapsedRealtime()-startAt;
        lastState=new State(currentlyRunning,hasA,hasG,hasM,hasL,hasH,
            a[0],a[1],a[2],g[0],g[1],g[2],m[0],m[1],m[2],
            lux,bpm,connectedNodes.size(),seq,elapsed,stateMessage);
    }

    @Override public void onSensorChanged(SensorEvent event) {
        if(!running)return;
        float[] v=event.values;
        switch(event.sensor.getType()) {
            case Sensor.TYPE_ACCELEROMETER:
                if(valid3(v)){System.arraycopy(v,0,a,0,3);hasA=true;}break;
            case Sensor.TYPE_GYROSCOPE:
                if(valid3(v)){System.arraycopy(v,0,g,0,3);hasG=true;}break;
            case Sensor.TYPE_MAGNETIC_FIELD:
                if(valid3(v)){System.arraycopy(v,0,m,0,3);hasM=true;}break;
            case Sensor.TYPE_LIGHT:
                if(v.length>0 && Float.isFinite(v[0]) && v[0]>=0){
                    lux=v[0];hasL=true;
                }break;
            case Sensor.TYPE_HEART_RATE:
                if(v.length>0 && Float.isFinite(v[0]) && v[0]>0){
                    bpm=v[0];hasH=true;
                }break;
            default:break;
        }
    }
    private static boolean valid3(float[] v) {
        return v.length>=3 && Float.isFinite(v[0]) && Float.isFinite(v[1]) && Float.isFinite(v[2]);
    }
    @Override public void onAccuracyChanged(Sensor sensor,int accuracy){}

    @Override public void onMessageReceived(MessageEvent event) {
        if(!running || !HAPTIC_PATH.equals(event.getPath()))return;
        Vibrator vib=(Vibrator)getSystemService(Context.VIBRATOR_SERVICE);
        if(vib!=null && vib.hasVibrator())
            vib.vibrate(VibrationEffect.createOneShot(90,VibrationEffect.DEFAULT_AMPLITUDE));
    }

    private void createNotificationChannel() {
        if(Build.VERSION.SDK_INT>=26) {
            NotificationManager manager=(NotificationManager)getSystemService(NOTIFICATION_SERVICE);
            if(manager!=null)manager.createNotificationChannel(new NotificationChannel(
                NOTIFICATION_CHANNEL,"WorldLink · sensores",
                NotificationManager.IMPORTANCE_LOW));
        }
    }

    private Notification notification() {
        Intent open=new Intent(this,WatchActivity.class);
        PendingIntent activity=PendingIntent.getActivity(this,0,open,
                PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Intent stop=new Intent(this,WatchSensorService.class).setAction(ACTION_STOP);
        PendingIntent stopIntent=PendingIntent.getService(this,1,stop,
                PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder n;
        if(Build.VERSION.SDK_INT>=26)n=new Notification.Builder(this,NOTIFICATION_CHANNEL);
        else n=new Notification.Builder(this);
        return n.setContentTitle("WorldLink · experimento activo")
                .setContentText("Lectura del reloj incluso con pantalla apagada · máx. 8 min")
                .setSmallIcon(android.R.drawable.ic_menu_compass)
                .setContentIntent(activity)
                .setOngoing(true)
                .addAction(android.R.drawable.ic_media_pause,"DETENER",stopIntent)
                .build();
    }

    private void stopSession(String why) {
        if(running) {
            stateMessage=why;
            running=false;
            handler.removeCallbacks(sendLoop);
            if(sensors!=null)sensors.unregisterListener(this);
            Wearable.getMessageClient(this).removeListener(this);
            if(wakeLock!=null && wakeLock.isHeld())wakeLock.release();
            connectedNodes.clear();
            publishState(false);
        }
        active=null;
        stopForeground(true);
        stopSelf();
    }
    @Override public void onDestroy() {
        if(running) {
            stateMessage="Servicio interrumpido · reanuda manualmente";
            running=false;
            handler.removeCallbacks(sendLoop);
            if(sensors!=null)sensors.unregisterListener(this);
            Wearable.getMessageClient(this).removeListener(this);
            publishState(false);
        }
        if(wakeLock!=null && wakeLock.isHeld())wakeLock.release();
        active=null;
        super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}