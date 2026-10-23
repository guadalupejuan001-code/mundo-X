package com.mundox.worldlink.phone;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.google.android.gms.wearable.MessageClient;
import com.google.android.gms.wearable.MessageEvent;
import com.google.android.gms.wearable.Wearable;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** 50-second controlled capture. All measurements originate on the paired watch. */
public final class FieldExperimentActivity extends Activity implements MessageClient.OnMessageReceivedListener {
    private static final String ROUTE="/mundox/field/v1/sample";
    private static final int EXPORT=881,BG=0xff101a17,GREEN=0xffcbf47e,WHITE=0xfff0f5ee,MUTED=0xffb1c2b5;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final ExecutorService storage=Executors.newSingleThreadExecutor();
    private TextView connection,progress,result,storageStatus;
    private Button begin,export;
    private FieldExperiment.Sample latest;
    private FieldExperiment experiment;
    private String latestNode="",experimentNode="";
    private long lastReceived;
    private boolean visible,saving,saveQueued;
    private File lastFile;
    private String exportText;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);lastFile=new File(getFilesDir(),"worldlink-field-last.csv");
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);
        LinearLayout root=new LinearLayout(this);root.setOrientation(1);root.setPadding(dp(20),dp(24),dp(20),dp(28));root.setBackgroundColor(BG);
        root.setOnApplyWindowInsetsListener((v,insets)->{
            if(android.os.Build.VERSION.SDK_INT>=30){android.graphics.Insets b=insets.getInsets(WindowInsets.Type.systemBars());v.setPadding(dp(20)+b.left,dp(24)+b.top,dp(20)+b.right,dp(28)+b.bottom);}return insets;
        });
        label(root,"WORLDLINK  /  CAMPO",12,GREEN,true);
        label(root,"Predecir antes\nde comprobar",31,WHITE,true);
        label(root,"Una prueba física de 50 segundos",15,MUTED,false);
        LinearLayout intro=card(root);
        label(intro,"1  PREPARA",12,GREEN,true);
        label(intro,"Abre WorldLink Campo en el reloj e inicia sus sensores. Mantén la muñeca en el mismo lugar y gírala despacio en varios ejes. Conserva ambas pantallas abiertas.",15,WHITE,false);
        label(intro,"Durante toda la sesión mantén iguales las condiciones del entorno. El movimiento cambia la lectura que esperamos del mismo campo.",13,MUTED,false);
        connection=label(intro,"Esperando campo + orientación del reloj…",13,GREEN,false);
        begin=button(intro,"INICIAR PRUEBA · 50 s",this::start,true);begin.setEnabled(false);
        button(intro,"DETENER Y GUARDAR SESIÓN PARCIAL",()->abort("Detenida por el usuario"),false);
        LinearLayout run=card(root);
        label(run,"2  APRENDE → CALIBRA → COMPRUEBA",12,GREEN,true);
        progress=label(run,"20 s de ajuste · 10 s para medir el error · 20 s de prueba con el modelo fijo.",16,WHITE,false);
        result=label(run,"La prueba final compara las predicciones con lecturas nuevas y dos referencias sencillas.",14,MUTED,false);
        LinearLayout files=card(root);label(files,"3  CONSERVA LA EVIDENCIA",12,GREEN,true);
        export=button(files,"EXPORTAR MEDICIONES CSV",this::export,false);
        storageStatus=label(files,lastFile.exists()?"Hay una sesión anterior disponible.":"Las lecturas se guardarán en este teléfono.",13,MUTED,false);
        export.setEnabled(lastFile.exists());
        label(files,"El archivo incluye tiempos, orientación, campo, fase del experimento y predicciones. Puedes compartirlo después de exportarlo.",13,MUTED,false);
        label(root,"Se estudia un campo magnético clásico. La app no genera entrelazamiento ni demuestra una lattice universal.",12,MUTED,false);
        scroll.addView(root);setContentView(scroll);getWindow().setStatusBarColor(BG);getWindow().setNavigationBarColor(BG);
    }
    @Override protected void onStart(){super.onStart();visible=true;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Wearable.getMessageClient(this).addListener(this).addOnFailureListener(e->connection.setText("Error de enlace: "+e.getMessage()));
        handler.post(tick);
    }
    @Override protected void onStop(){visible=false;abort("La pantalla se cerró. Se conservó la sesión parcial.");
        Wearable.getMessageClient(this).removeListener(this);handler.removeCallbacks(tick);
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);super.onStop();}
    @Override protected void onDestroy(){storage.shutdown();super.onDestroy();}
    private final Runnable tick=new Runnable(){@Override public void run(){
        if(!visible)return;
        long age=SystemClock.elapsedRealtime()-lastReceived;
        if(experiment!=null&&experiment.active()&&age>2000)abort("Se perdió la señal del reloj durante más de 2 s");
        if(age>2000){connection.setText("Sin lecturas recientes. Inicia los sensores del reloj.");begin.setEnabled(false);}
        handler.postDelayed(this,500);
    }};
    private void start(){
        if(latest==null||SystemClock.elapsedRealtime()-lastReceived>1000){connection.setText("Espera una lectura reciente del reloj");return;}
        if(saving){storageStatus.setText("Espera a que termine el guardado");return;}
        if(experiment!=null&&experiment.active())return;
        try{experiment=new FieldExperiment(latest.session,new NativeFieldRule());}
        catch(LinkageError error){connection.setText("No se pudo cargar la memoria Ether: "+error.getMessage());return;}
        experimentNode=latestNode;saveQueued=false;begin.setEnabled(false);export.setEnabled(false);
        progress.setText("Preparando primera muestra…");result.setText("El modelo se fijará al terminar los primeros 20 segundos.");
    }
    private void abort(String reason){
        if(experiment!=null&&experiment.active()){experiment.abort(reason);progress.setText(experiment.status());finish();}
    }
    @Override public void onMessageReceived(MessageEvent event){
        if(!ROUTE.equals(event.getPath()))return;
        byte[] bytes=event.getData();if(bytes==null||bytes.length>4096)return;
        String node=event.getSourceNodeId();byte[] data=bytes.clone();
        runOnUiThread(()->read(data,node));
    }
    private void read(byte[] data,String node){
        if(!visible)return;
        try {
            JSONObject j=new JSONObject(new String(data,StandardCharsets.UTF_8));
            if(j.getInt("v")!=1 || !"game_rotation_vector".equals(j.getString("orientation"))
                    || !"magnetic_field_uncalibrated".equals(j.getString("field")) || !"uT".equals(j.getString("units")))
                throw new IllegalArgumentException("Se requieren sensores independientes");
            FieldExperiment.Sample s=new FieldExperiment.Sample(j.getString("sid"),j.getLong("seq"),j.getLong("mt"),j.getLong("qt"),
                new double[]{j.getDouble("qx"),j.getDouble("qy"),j.getDouble("qz"),j.getDouble("qw")},
                new double[]{j.getDouble("mx"),j.getDouble("my"),j.getDouble("mz")},j.getInt("accuracy"));
            if(experiment!=null&&experiment.active()&&!experimentNode.equals(node)){abort("Cambió el dispositivo de origen");return;}
            if(latest!=null&&latestNode.equals(node)&&latest.session.equals(s.session)&&s.mt<=latest.mt){
                if(experiment!=null&&experiment.active())experiment.reject();return;
            }
            latest=s;latestNode=node;lastReceived=SystemClock.elapsedRealtime();
            connection.setText(String.format(Locale.US,"Reloj enlazado · %.1f / %.1f / %.1f µT",s.m[0],s.m[1],s.m[2]));
            if(experiment!=null&&experiment.active()){
                experiment.accept(s);progress.setText(experiment.status());
                if(experiment.model()!=null&&experiment.active()) {
                    double[] y=experiment.model().predict(s);
                    result.setText(String.format(Locale.US,"Predicción XYZ: %.1f / %.1f / %.1f µT\nModelo fijo · %s",y[0],y[1],y[2],experiment.stage()));
                }
                if(!experiment.active())finish();
            } else begin.setEnabled(!saving);
        }catch(Exception e){
            if(experiment!=null&&experiment.active())experiment.reject();
            connection.setText("Lectura descartada: "+e.getMessage());
        }
    }
    private void finish(){
        if(experiment==null||saveQueued)return;saveQueued=true;
        if(experiment.stage()==FieldExperiment.Stage.DONE){progress.setText("PRUEBA FINALIZADA");result.setText(experiment.report());}
        saving=true;begin.setEnabled(false);export.setEnabled(false);storageStatus.setText("Guardando mediciones…");
        final String csv=experiment.exportCsv();final long stamp=System.currentTimeMillis();
        storage.execute(()->{
            try {
                File history=new File(getFilesDir(),"field-sessions");if(!history.exists()&&!history.mkdirs())throw new java.io.IOException("No se creó el directorio");
                File sessionFile=new File(history,"field-"+stamp+".csv");write(sessionFile,csv);
                // Archive each session; the latest pointer is replaced atomically after a successful write.
                File temporary=new File(getFilesDir(),"worldlink-field-last.tmp");write(temporary,csv);
                if(!temporary.renameTo(lastFile))throw new java.io.IOException("No se pudo actualizar el último registro");
                runOnUiThread(()->{saving=false;storageStatus.setText("Sesión guardada. Exporta el CSV para analizarla.");export.setEnabled(true);
                    begin.setEnabled(visible&&SystemClock.elapsedRealtime()-lastReceived<1000);});
            }catch(Exception e){runOnUiThread(()->{saving=false;saveQueued=false;storageStatus.setText("No se pudo guardar: "+e.getMessage());export.setEnabled(true);});}
        });
    }
    private static void write(File path,String text)throws Exception{
        try(FileOutputStream stream=new FileOutputStream(path)){stream.write(text.getBytes(StandardCharsets.UTF_8));stream.getFD().sync();}
    }
    private void export(){
        if(saving)return;
        exportText=experiment!=null&&!experiment.active()?experiment.exportCsv():null;
        if(exportText==null&&!lastFile.exists()){storageStatus.setText("Todavía no hay mediciones");return;}
        Intent intent=new Intent(Intent.ACTION_CREATE_DOCUMENT);intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType("text/csv");
        intent.putExtra(Intent.EXTRA_TITLE,"WorldLink-Campo-"+System.currentTimeMillis()+".csv");startActivityForResult(intent,EXPORT);
    }
    @Override protected void onActivityResult(int request,int resultCode,Intent data){super.onActivityResult(request,resultCode,data);
        if(request!=EXPORT||resultCode!=RESULT_OK||data==null||data.getData()==null)return;
        final android.net.Uri uri=data.getData();final String text=exportText;
        storage.execute(()->{
            try(OutputStream out=getContentResolver().openOutputStream(uri,"wt")){
                if(out==null)throw new java.io.IOException("Destino no disponible");
                if(text!=null)out.write(text.getBytes(StandardCharsets.UTF_8));
                else try(FileInputStream in=new FileInputStream(lastFile)){byte[] buffer=new byte[8192];int n;while((n=in.read(buffer))!=-1)out.write(buffer,0,n);}
                runOnUiThread(()->storageStatus.setText("CSV exportado correctamente"));
            }catch(Exception e){runOnUiThread(()->storageStatus.setText("Exportación falló: "+e.getMessage()));}
        });
    }
    private LinearLayout card(LinearLayout parent){LinearLayout box=new LinearLayout(this);box.setOrientation(1);box.setPadding(dp(16),dp(14),dp(16),dp(16));
        GradientDrawable bg=new GradientDrawable();bg.setColor(0xff1b2d24);bg.setCornerRadius(dp(18));box.setBackground(bg);
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.topMargin=dp(18);parent.addView(box,p);return box;}
    private TextView label(LinearLayout parent,String text,int size,int color,boolean bold){TextView t=new TextView(this);t.setText(text);t.setTextSize(size);t.setTextColor(color);
        if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);t.setPadding(0,dp(5),0,dp(8));parent.addView(t);return t;}
    private Button button(LinearLayout parent,String text,Runnable action,boolean accent){Button b=new Button(this);b.setText(text);b.setTextSize(13);b.setAllCaps(false);
        b.setTextColor(accent?BG:WHITE);GradientDrawable bg=new GradientDrawable();bg.setColor(accent?GREEN:0xff304738);bg.setCornerRadius(dp(12));b.setBackground(bg);
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,dp(56));p.topMargin=dp(10);parent.addView(b,p);b.setOnClickListener(v->action.run());return b;}
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
}
