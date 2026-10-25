package com.mundox.ghzwatch;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailability;
import com.google.android.gms.wearable.CapabilityClient;
import com.google.android.gms.wearable.MessageClient;
import com.google.android.gms.wearable.MessageEvent;
import com.google.android.gms.wearable.Node;
import com.google.android.gms.wearable.Wearable;
import com.mundox.maplink.MapProtocol;
import com.mundox.maplink.FieldTelemetry;
import org.json.JSONObject;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public final class MapRemoteActivity extends Activity implements MessageClient.OnMessageReceivedListener {
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final String session=UUID.randomUUID().toString();
    private String node,pendingCommand;
    private long sequence,pending;
    private boolean active,ready,listenerAttached,discovering;
    private int page;
    private TextView status,sensorStatus;
    private Button sensorToggle;
    private WatchFieldSensors fieldSensors;
    private Button connect,mode;
    private final List<Button> controls=new ArrayList<>();
    private final String[][] commands={{"LEFT","RIGHT","UP","DOWN","ZOOM_OUT","ZOOM_IN"},
        {"PREVIOUS","NEXT","PLAY","RESET"},{"Z_DOWN","Z_UP","FIELD_DOWN","FIELD_UP","RESET"},
        {"LAB_MAGNETIC","LAB_GRAVITY","LAB_BOTH","LAB_PLAY","LAB_RESET"},
        {"MAGNETIC_DOWN","MAGNETIC_UP","GRAVITY_DOWN","GRAVITY_UP"}};
    private final String[][] labels={{"GIRO ←","GIRO →","INCLINAR ↑","INCLINAR ↓","ZOOM −","ZOOM +"},
        {"TIEMPO −","TIEMPO +","REPRODUCIR / PAUSAR","REINICIAR VISTA"},
        {"ALTURA Z −","ALTURA Z +","DIPOLO −","DIPOLO +","REINICIAR VISTA"},
        {"CAMPO MAGNÉTICO","GRAVEDAD","AMBOS CAMPOS","MOVER / PAUSAR OBJETOS","REINICIAR OBJETOS"},
        {"B VIRTUAL −","B VIRTUAL +","GRAVEDAD −","GRAVEDAD +"}};
    private LinearLayout buttons;
    private final Runnable heartbeat=new Runnable(){@Override public void run(){
        if(!active)return;
        if(ready&&pending==0)send("HELLO");
        handler.postDelayed(this,5000);
    }};
    private final Runnable timeout=()->{
        if(pending==0)return;
        pending=0;ready=false;
        status.setText("Sin confirmación. Abre el mapa en el teléfono y vuelve a conectar.");
        updateEnabled();
    };
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);
        ScrollView scroll=new ScrollView(this);scroll.setBackgroundColor(Color.BLACK);scroll.setFillViewport(true);
        scroll.setOnGenericMotionListener((v,e)->{
            if(e.getAction()==MotionEvent.ACTION_SCROLL){scroll.smoothScrollBy(0,Math.round(-e.getAxisValue(MotionEvent.AXIS_SCROLL)*dp(38)));return true;}return false;
        });
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);root.setPadding(dp(28),dp(28),dp(28),dp(50));scroll.addView(root);
        text(root,"MUNDO X",16,Color.WHITE,true);
        text(root,"MANDO · MAPA 4D",11,Color.rgb(255,78,90),true);
        status=text(root,"Abre Mundo X · Mapa en tu A16.",12,Color.rgb(205,205,216),false);
        status.setId(R.id.map_status);
        connect=button(root,"CONECTAR TELÉFONO",true);connect.setId(R.id.map_connect);connect.setOnClickListener(v->discover());
        mode=button(root,"VISTA · CAMBIAR",false);
        mode.setOnClickListener(v->{page=(page+1)%commands.length;renderControls();});
        buttons=new LinearLayout(this);buttons.setOrientation(LinearLayout.VERTICAL);root.addView(buttons);
        renderControls();
        text(root,"Modifica el mapa virtual.\\nDipolo ± sólo edita la demo.\\nMantén ambas apps abiertas.",11,Color.rgb(156,156,172),false);
        sensorToggle=button(root,"MEDIR SENSORES",false);sensorToggle.setId(R.id.field_sensors);
        sensorStatus=text(root,"B y gravedad del sensor. No emite un campo externo.",11,Color.rgb(205,205,216),false);sensorStatus.setId(R.id.field_readings);
        fieldSensors=new WatchFieldSensors(this,handler,session,(bytes,summary)->{
            sensorStatus.setText(summary);
            if(bytes!=null&&active&&ready&&node!=null)Wearable.getMessageClient(this).sendMessage(node,FieldTelemetry.PATH,bytes);
        });
        sensorToggle.setOnClickListener(v->{if(!active)return;if(fieldSensors.isRunning())fieldSensors.stop();else fieldSensors.start();
            sensorToggle.setText(fieldSensors.isRunning()?"DETENER SENSORES":"MEDIR SENSORES");});
        Button probe=button(root,"VIBRACIÓN + SENSORES",false);
        probe.setOnClickListener(v->startActivity(new android.content.Intent(this,HardwareActivity.class)));
        setContentView(scroll);
    }
    @Override protected void onStart(){
        super.onStart();active=true;ready=false;pending=0;updateEnabled();
        if(GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(this)!=ConnectionResult.SUCCESS){
            status.setText("Actualiza Google Play services para conectar.");return;
        }
        Wearable.getMessageClient(this).addListener(this).addOnSuccessListener(v->{
            if(active){listenerAttached=true;discover();}else Wearable.getMessageClient(this).removeListener(this);
        }).addOnFailureListener(e->status.setText("No se pudo activar el enlace."));
        handler.postDelayed(heartbeat,5000);
    }
    @Override protected void onStop(){
        fieldSensors.stop();sensorToggle.setText("MEDIR SENSORES");
        active=false;ready=false;pending=0;discovering=false;handler.removeCallbacksAndMessages(null);
        if(listenerAttached)Wearable.getMessageClient(this).removeListener(this);
        listenerAttached=false;super.onStop();
    }
    private void discover(){
        if(!active||pending!=0||discovering)return;
        if(!listenerAttached){status.setText("El enlace todavía no está disponible.");return;}
        discovering=true;ready=false;node=null;status.setText("Buscando la app del teléfono…");connect.setEnabled(false);updateEnabled();
        Wearable.getCapabilityClient(this).getCapability(MapProtocol.CAPABILITY,CapabilityClient.FILTER_REACHABLE)
            .addOnSuccessListener(capability->{
                if(!active)return;
                discovering=false;
                List<Node> nodes=new ArrayList<>(capability.getNodes()),nearby=new ArrayList<>();
                for(Node n:nodes)if(n.isNearby())nearby.add(n);
                List<Node> candidates=nearby.isEmpty()?nodes:nearby;
                if(candidates.size()!=1){
                    status.setText(candidates.isEmpty()?"No encontré Mundo X · Mapa. Instala el APK del teléfono y abre su mapa.":"Hay varios teléfonos. Deja conectado sólo el que usarás.");
                    updateEnabled();return;
                }
                node=candidates.get(0).getId();send("HELLO");
            }).addOnFailureListener(e->{if(active){discovering=false;status.setText("No se pudo buscar el teléfono. Revisa la conexión de Galaxy Wearable.");updateEnabled();}});
    }
    private void send(String command){
        if(!active||node==null||pending!=0)return;
        if(!"HELLO".equals(command)&&!ready)return;
        final long id=++sequence;
        final byte[] bytes;
        try{bytes=MapProtocol.request(session,id,command);}catch(Exception e){return;}
        pending=id;pendingCommand=command;updateEnabled();
        if(!"HELLO".equals(command))status.setText("Esperando que el mapa aplique la orden…");
        handler.removeCallbacks(timeout);handler.postDelayed(timeout,4000);
        Wearable.getMessageClient(this).sendMessage(node,MapProtocol.REQUEST,bytes)
            .addOnFailureListener(e->{if(active&&pending==id){handler.removeCallbacks(timeout);pending=0;ready=false;
                status.setText("No se pudo enviar. Vuelve a conectar.");updateEnabled();}});
    }
    @Override public void onMessageReceived(MessageEvent event){
        if(!MapProtocol.ACK.equals(event.getPath()))return;
        final String source=event.getSourceNodeId();final byte[] bytes=event.getData();
        runOnUiThread(()->{
            if(!active||node==null||!node.equals(source)||pending==0)return;
            try{
                JSONObject response=MapProtocol.parseAck(bytes,session,pending);
                handler.removeCallbacks(timeout);pending=0;
                boolean ok=response.getBoolean("ok");ready=ok||(!"HELLO".equals(pendingCommand)&&ready);
                status.setText((ok?"APLICADO · ":"NO APLICADO · ")+response.getString("message"));
                updateEnabled();
            }catch(Exception ignored){}
        });
    }
    private void renderControls(){
        if(buttons==null)return;
        buttons.removeAllViews();controls.clear();
        mode.setText(new String[]{"VISTA · CAMBIAR","TIEMPO · CAMBIAR","CAMPO · CAMBIAR","OBJETOS · CAMBIAR","FUERZAS · CAMBIAR"}[page]);
        for(int i=0;i<commands[page].length;i++){
            final String cmd=commands[page][i];
            Button b=button(buttons,labels[page][i],false);b.setOnClickListener(v->send(cmd));controls.add(b);
        }
        updateEnabled();
    }
    private void updateEnabled(){
        if(connect==null)return;
        connect.setEnabled(active&&pending==0&&!discovering);
        mode.setEnabled(pending==0);
        for(Button b:controls){b.setEnabled(active&&ready&&pending==0);b.setAlpha(b.isEnabled()?1:.35f);}
    }
    private TextView text(LinearLayout parent,String content,int size,int color,boolean bold){
        TextView v=new TextView(this);v.setText(content.replace("\\n","\n"));v.setTextSize(size);v.setTextColor(color);v.setGravity(Gravity.CENTER);
        if(bold)v.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.topMargin=dp(6);p.bottomMargin=dp(5);parent.addView(v,p);return v;
    }
    private Button button(LinearLayout parent,String label,boolean primary){
        Button b=new Button(this);b.setText(label);b.setTextSize(11);b.setTextColor(Color.WHITE);b.setAllCaps(false);b.setPadding(dp(3),0,dp(3),0);
        GradientDrawable background=new GradientDrawable();background.setColor(primary?Color.rgb(149,30,45):Color.rgb(31,31,39));background.setCornerRadius(dp(22));b.setBackground(background);
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,dp(44));p.topMargin=dp(6);parent.addView(b,p);return b;
    }
    boolean sensorsRunningForTest(){return fieldSensors.isRunning();}
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
}

