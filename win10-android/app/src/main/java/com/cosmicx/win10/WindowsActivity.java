package com.cosmicx.win10;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.provider.OpenableColumns;
import android.system.Os;
import android.system.OsConstants;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import com.cosmicx.pc.NativeMachine;
import org.json.JSONObject;

import java.io.IOException;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

/**
 * Windows 10 installation-media research interface for the ORIGINAL Mundo X
 * x86/x64 C++ emulator. Does not claim Win10 desktop or installer compatibility.
 * Uses SAF + pread and shows actual emitted VGA/CPU state, never a fake desktop.
 */
public final class WindowsActivity extends Activity {
    private static final int PICK_ISO=501;
    private static final int BG=0xff07101c;
    private static final int SURFACE=0xff101e32;
    private static final int STROKE=0xff29445b;
    private static final int TEXT=0xffeaf5fa;
    private static final int MUTED=0xffa4bac9;
    private static final int CYAN=0xff6af2e9;
    private static final int AMBER=0xffffc47f;

    private final Handler ui=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor(task->{
        Thread thread=new Thread(task,"MundoX-Win10-Emulator");
        thread.setPriority(Thread.NORM_PRIORITY-1);
        return thread;
    });
    private volatile long machine=0;
    private volatile boolean closed=false;
    private volatile boolean working=true;
    private volatile boolean runRequested=false;
    private volatile boolean bootReady=false;
    private volatile boolean hasIso=false;
    private volatile boolean selectedBios=false;

    private String isoDescription="ISO no seleccionada";
    private String isoMetadata="";
    private String machineJson="{}";
    private String vgaText="";
    private TextView isoView,engineView,guestView,noticeView,headline;
    private Button pick,again,run,pause,step,reset,demoX64,demoEther,copy;
    private LinearLayout operations;

    private int dp(float size){return (int)(size*getResources().getDisplayMetrics().density+.5f);}
    private GradientDrawable shape(int fill,int stroke){
        GradientDrawable d=new GradientDrawable();
        d.setColor(fill);d.setCornerRadius(dp(14));
        if(stroke!=0)d.setStroke(dp(1),stroke);
        return d;
    }
    private TextView text(String content,int size,int color,boolean bold){
        TextView t=new TextView(this);
        t.setText(content);t.setTextSize(size);t.setTextColor(color);
        if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        t.setLineSpacing(dp(2),1.06f);
        return t;
    }
    private Button button(String label,boolean prominent){
        Button b=new Button(this);
        b.setText(label);b.setAllCaps(false);b.setTextSize(12);
        b.setTextColor(prominent?0xff001919:TEXT);
        b.setBackground(shape(prominent?CYAN:0xff20334a,prominent?0:STROKE));
        b.setPadding(dp(10),0,dp(10),0);
        return b;
    }
    private LinearLayout panel(LinearLayout parent){
        LinearLayout p=new LinearLayout(this);p.setOrientation(LinearLayout.VERTICAL);
        p.setPadding(dp(15),dp(14),dp(15),dp(14));
        p.setBackground(shape(SURFACE,STROKE));
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);
        lp.topMargin=dp(12);
        parent.addView(p,lp);
        return p;
    }
    private LinearLayout row(LinearLayout parent,int height){
        LinearLayout r=new LinearLayout(this);r.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,dp(height));
        lp.topMargin=dp(9);
        parent.addView(r,lp);
        return r;
    }
    private void pair(LinearLayout row,Button a,Button b){
        LinearLayout.LayoutParams one=new LinearLayout.LayoutParams(0,-1,1);
        one.rightMargin=dp(6);row.addView(a,one);
        row.addView(b,new LinearLayout.LayoutParams(0,-1,1));
    }
    private void refreshButtons(){
        if(pick==null)return;
        boolean usable=machine!=0&&!working&&!closed;
        pick.setEnabled(usable);
        again.setEnabled(usable&&getPreferences(0).contains("iso_uri"));
        run.setEnabled(usable&&bootReady&&selectedBios);
        step.setEnabled(usable&&bootReady);
        reset.setEnabled(usable&&bootReady);
        pause.setEnabled(runRequested || working);
        demoX64.setEnabled(usable);
        demoEther.setEnabled(usable);
        copy.setEnabled(!closed);
    }

    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);
        getWindow().setStatusBarColor(BG);getWindow().setNavigationBarColor(BG);
        ScrollView sc=new ScrollView(this);sc.setFillViewport(true);
        sc.setBackgroundColor(BG);sc.setFitsSystemWindows(true);
        LinearLayout content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(15),dp(19),dp(15),dp(30));
        sc.addView(content);setContentView(sc);

        content.addView(text("MUNDO X  /  WINDOWS 10",23,CYAN,true));
        content.addView(text("Laboratorio x86/x64 · Motor propio + Ether",12,MUTED,true));
        noticeView=text(
            "Windows 10 aún NO es compatible: probaremos su ISO real sin inventar un escritorio. " +
            "El emulador ejecuta instrucciones de arranque BIOS y muestra dónde se detiene.",
            12,AMBER,true);
        LinearLayout noticePanel=panel(content);noticePanel.addView(noticeView);

        LinearLayout source=panel(content);
        source.addView(text("01  /  TU ISO OFICIAL",13,CYAN,true));
        isoView=text("Ningún archivo seleccionado. El sistema no copiará tu ISO.",12,TEXT,false);
        isoView.setTextIsSelectable(true);
        LinearLayout.LayoutParams fileSpace=new LinearLayout.LayoutParams(-1,-2);
        fileSpace.topMargin=dp(9);source.addView(isoView,fileSpace);
        pick=button("Seleccionar ISO de Windows 10",true);
        again=button("Reabrir ISO",false);
        pair(row(source,52),pick,again);
        pick.setContentDescription("mundo-x-win10-pick-iso");
        pick.setOnClickListener(v->chooseIso());
        again.setOnClickListener(v->{
            String savedUri=getPreferences(0).getString("iso_uri",null);
            if(savedUri!=null)openIso(Uri.parse(savedUri),false);
        });

        LinearLayout cpuPanel=panel(content);
        cpuPanel.addView(text("02  /  PROCESADOR Y MEMORIA ETHER",13,CYAN,true));
        cpuPanel.addView(text("2.048 MiB lógicos x86 • 256 MiB máximo de páginas residentes. " +
            "Las páginas vacías se representan por una regla de cero; la RAM física de Android se informa aparte. " +
            "Ventana de 256 bytes conectada al estado de Ether.",11,MUTED,false));
        engineView=text("Inicializando el intérprete C++…",12,TEXT,false);
        LinearLayout.LayoutParams mlp=new LinearLayout.LayoutParams(-1,-2);mlp.topMargin=dp(9);
        cpuPanel.addView(engineView,mlp);

        LinearLayout bootPanel=panel(content);
        bootPanel.addView(text("03  /  ARRANQUE EXPERIMENTAL",13,CYAN,true));
        run=button("Arrancar BIOS (ISO)",true);
        pause=button("Detener",false);
        pair(row(bootPanel,52),run,pause);
        step=button("Un paso x86",false);
        reset=button("Reiniciar medio",false);
        pair(row(bootPanel,48),step,reset);
        run.setOnClickListener(v->runGuest());
        pause.setOnClickListener(v->{runRequested=false;
            Toast.makeText(this,"Solicitada pausa de CPU",Toast.LENGTH_SHORT).show();});
        step.setOnClickListener(v->command(()->NativeMachine.run(machine,1)));
        reset.setOnClickListener(v->command(()->NativeMachine.reset(machine)));

        LinearLayout evidence=panel(content);
        evidence.addView(text("04  /  PRUEBA DE TU TECNOLOGÍA",13,CYAN,true));
        evidence.addView(text("Estas demostraciones originales funcionan sin Windows. " +
            "La CPU emulada ejecuta código real y la prueba Ether comunica con InfiniteCore.",
            11,MUTED,false));
        demoX64=button("Probar CPU x64 propia",false);
        demoEther=button("Probar x86 → Ether",false);
        pair(row(evidence,48),demoX64,demoEther);
        demoX64.setOnClickListener(v->testOwnCore(false));
        demoEther.setOnClickListener(v->testOwnCore(true));

        LinearLayout display=panel(content);
        display.addView(text("05  /  VÍDEO Y DIAGNÓSTICO REAL",13,CYAN,true));
        display.addView(text("Se muestran exclusivamente los caracteres escritos por el huésped en VGA. " +
            "Los motivos de parada provienen del intérprete, no de una interfaz ficticia.",
            11,MUTED,false));
        guestView=text("SIN DATOS DE VÍDEO DEL HUÉSPED",11,0xff96ddc6,false);
        guestView.setTypeface(Typeface.MONOSPACE);
        guestView.setTextIsSelectable(true);
        LinearLayout.LayoutParams glp=new LinearLayout.LayoutParams(-1,-2);
        glp.topMargin=dp(10);
        display.addView(guestView,glp);
        copy=button("Copiar diagnóstico para analizar la ISO",false);
        LinearLayout.LayoutParams clp=new LinearLayout.LayoutParams(-1,dp(51));
        clp.topMargin=dp(12);display.addView(copy,clp);
        copy.setOnClickListener(v->{
            String info="MUNDO X WINDOWS ISO LAB\n"+isoDescription+
                "\n"+isoMetadata+"\nCPU:\n"+machineJson+"\nVIDEO:\n"+vgaText;
            ClipboardManager board=(ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
            board.setPrimaryClip(ClipData.newPlainText("Mundo X ISO diagnóstico",info));
            Toast.makeText(this,"Diagnóstico copiado",Toast.LENGTH_SHORT).show();
        });
        content.addView(text("Motor propio. Sin QEMU, sin Wine, sin ISO incorporada. " +
            "El resultado se puede comprobar contra tu ISO real.",10,MUTED,false));
        refreshButtons();

        worker.execute(()->{
            try {
                long m=NativeMachine.createWithBudget(2048,256);
                JSONObject status=new JSONObject(NativeMachine.inspect(m));
                machine=m;
                ui.post(()->showState(status));
            }catch(Exception | LinkageError error){
                fail("No se pudo iniciar el motor x86/Ether: "+error.getMessage());
            }finally{
                working=false;ui.post(this::refreshButtons);
            }
        });
    }

    private interface Operation{String run() throws Exception;}
    private void command(Operation op){
        if(working||machine==0||closed)return;
        runRequested=false;working=true;refreshButtons();
        worker.execute(()->{
            try{
                JSONObject state=new JSONObject(op.run());
                displayState(state);
            }catch(Throwable e){fail("Operación: "+e.getMessage());}
            finally{working=false;ui.post(this::refreshButtons);}
        });
    }
    private void testOwnCore(boolean ether){
        if(working||machine==0)return;
        bootReady=false;
        selectedBios=false;
        isoMetadata="Prueba interna de CPU/Ether, no es la ISO seleccionada.";
        working=true;refreshButtons();
        worker.execute(()->{
            try{
                if(ether)NativeMachine.bootQuantumDemo(machine,true);
                else NativeMachine.bootLongModeDemo(machine);
                bootReady=true;
                String raw="";
                for(int i=0;i<50;i++){
                    raw=NativeMachine.run(machine,5000);
                    JSONObject info=new JSONObject(raw);
                    String state=info.optString("status","");
                    if(!"running".equals(state)&&!"ready".equals(state))break;
                }
                final String label=ether?"Demo real x86→Ether":"Demo real de CPU x64";
                isoDescription=label+" (no es Windows 10)";
                displayState(new JSONObject(raw));
                ui.post(()->isoView.setText(isoDescription));
            }catch(Throwable e){fail("Demo propia: "+e.getMessage());}
            finally{working=false;ui.post(this::refreshButtons);}
        });
    }

    private void chooseIso(){
        if(working||machine==0)return;
        runRequested=false;
        Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|
                   Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try{startActivityForResult(i,PICK_ISO);}
        catch(RuntimeException ex){fail("Selector Android no disponible");}
    }
    @Override protected void onActivityResult(int req,int result,Intent data){
        super.onActivityResult(req,result,data);
        if(req!=PICK_ISO||result!=RESULT_OK||data==null||data.getData()==null)return;
        Uri uri=data.getData();
        boolean persistent=false;
        try{
            int permissions=data.getFlags()&Intent.FLAG_GRANT_READ_URI_PERMISSION;
            if(permissions!=0){
                getContentResolver().takePersistableUriPermission(uri,permissions);
                persistent=true;
            }
        }catch(SecurityException ignored){}
        openIso(uri,persistent);
    }
    private String displayName(Uri uri){
        try(Cursor cursor=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},
                null,null,null)){
            if(cursor!=null&&cursor.moveToFirst()&&!cursor.isNull(0))
                return cursor.getString(0);
        }catch(RuntimeException ignored){}
        return "ISO seleccionada";
    }
    private void openIso(Uri uri,boolean persist){
        if(working||machine==0||closed)return;
        runRequested=false;
        bootReady=false;
        working=true;
        refreshButtons();
        ui.post(()->isoView.setText("Analizando catálogo BIOS/UEFI de la ISO…"));
        worker.execute(()->{
            try(ParcelFileDescriptor pfd=getContentResolver().openFileDescriptor(uri,"r")){
                if(pfd==null)throw new IOException("Android no abrió el archivo");
                long size=pfd.getStatSize();
                if(size<19*2048)throw new IOException("ISO demasiado corta o tamaño desconocido");
                try{
                    Os.lseek(pfd.getFileDescriptor(),0,OsConstants.SEEK_CUR);
                }catch(Exception e){
                    throw new IOException("La ISO debe estar local y permitir acceso aleatorio",e);
                }
                JSONObject catalog=new JSONObject(NativeMachine.probeIso(pfd.getFd(),size));
                final boolean mayBoot=catalog.optBoolean("can_attempt_bios_boot",false);
                JSONObject status=null;
                if(mayBoot){
                    status=new JSONObject(NativeMachine.mount(machine,pfd.getFd(),size,"iso"));
                    if(!"ready".equals(status.optString("status"))){
                        throw new IOException("El cargador BIOS rechazó la ISO: "+
                                status.optString("message","diagnóstico desconocido"));
                    }
                }
                bootReady=mayBoot;
                selectedBios=mayBoot;
                String name=displayName(uri);
                isoDescription=name+" · "+String.format(Locale.US,"%.2f GiB",
                    size/1073741824.0);
                isoMetadata=catalog.toString(2);
                if(persist)getPreferences(0).edit().putString("iso_uri",uri.toString()).apply();
                final JSONObject state=status;
                String summary=isoDescription+"\nVolumen: "+catalog.optString("volume_id")+
                    "\nBIOS x86: "+(catalog.optBoolean("bios_entry")?"detectado":"ausente")+
                    "\nUEFI: "+(catalog.optBoolean("uefi_entry")?"detectado":"ausente")+
                    "\nLeídos para analizar: "+catalog.optLong("bytes_examined")+" bytes"+
                    "\nISO copiada: NO"+
                    "\n"+(mayBoot?"Arranque BIOS real preparado; pulsa Arrancar.":
                        "Esta ISO no tiene entrada BIOS inicial compatible. UEFI sigue pendiente.")+
                    "\n\n"+isoMetadata;
                ui.post(()->{
                    if(closed)return;
                    isoView.setText(summary);
                    if(state!=null)showState(state);
                });
            }catch(Throwable e){
                bootReady=false;
                fail("No se pudo analizar/montar la ISO: "+e.getMessage());
            }finally{
                working=false;ui.post(this::refreshButtons);
            }
        });
    }

    private void runGuest(){
        if(working||!bootReady||!selectedBios||machine==0||closed)return;
        runRequested=true;
        working=true;
        refreshButtons();
        worker.execute(()->{
            long started=SystemClock.elapsedRealtime();
            JSONObject last=null;
            try{
                // Cooperative finite budget; never background-run indefinitely.
                for(int slice=0;slice<80&&runRequested&&!closed;slice++){
                    last=new JSONObject(NativeMachine.run(machine,5000));
                    final String state=last.optString("status","");
                    if((slice%3)==0)displayState(last);
                    if(!"running".equals(state)&&!"ready".equals(state))break;
                }
                if(last!=null)displayState(last);
                final long elapsed=SystemClock.elapsedRealtime()-started;
                ui.post(()->{
                    if(closed)return;
                    Toast.makeText(this,"Ejecutado durante "+elapsed+
                          " ms de CPU invitada; consulta el diagnóstico",Toast.LENGTH_LONG).show();
                });
            }catch(Throwable e){fail("La ejecución se detuvo: "+e.getMessage());}
            finally{
                runRequested=false;working=false;ui.post(this::refreshButtons);
            }
        });
    }

    private void displayState(JSONObject state){
        try{
            String frame=NativeMachine.screen(machine);
            vgaText=frame==null?"":frame;
            ui.post(()->{
                if(closed)return;
                guestView.setText(vgaText.trim().isEmpty()?"SIN TEXTO DEL HUÉSPED":vgaText);
                showState(state);
            });
        }catch(Throwable e){fail("No se pudo leer VGA: "+e.getMessage());}
    }
    private void showState(JSONObject state){
        if(closed)return;
        machineJson=state.toString();
        long ram=state.optLong("ram_allocated_bytes",0);
        long cap=state.optLong("ram_budget_bytes",0);
        long pss=Debug.getPss()/1024;  // KiB -> MiB, Android process total.
        String ramUsed=ram<1048576L ? ((ram+1023L)/1024L)+" KiB" :
                String.format(Locale.US,"%.2f MiB",ram/1048576.0);
        JSONObject kbc=state.optJSONObject("i8042");
        String kbcStatus=kbc==null ? "\n8042: anterior al parche" :
            "\n8042 comandos: "+kbc.optLong("command_writes")+
            " · datos: "+kbc.optLong("data_writes")+
            " · último: 0x"+String.format(Locale.US,"%02X",kbc.optInt("last_command"))+
            "\nA20: "+(kbc.optBoolean("a20_enabled")?"habilitada":"deshabilitada");
        boolean windows=state.optBoolean("windows_ready",false);
        final String summary="Motor: "+state.optString("engine","Mundo X x86/x64")+
            "\nEstado CPU: "+state.optString("status","...")+
            " · "+state.optString("cpu_mode","real16")+
            "\nPasos CPU: "+state.optLong("instructions")+
            " · BIOS disco: "+state.optLong("disk_reads")+
            "\nRIP: "+state.optString("rip_hex","")+
            "\nRAM huésped usada: "+ramUsed+" / "+(cap/1048576)+" MiB"+
            "\nRAM del proceso (PSS): "+pss+" MiB"+
            "\nVentana Ether: "+state.optInt("ether_memory_bytes")+" bytes"+kbcStatus+
            "\nWindows listo: "+(windows?"sí":"NO (faltan dispositivos e instrucciones)")+
            "\nDetalle: "+state.optString("message","")+
            "\n"+state.optString("pending","");
        engineView.setText(summary);
        engineView.setTextColor("fault".equals(state.optString("status"))?AMBER:TEXT);
        refreshButtons();
    }
    private void fail(String error){
        ui.post(()->{
            if(closed)return;
            isoView.setText(error+"\n\n"+isoDescription);
            Toast.makeText(this,error,Toast.LENGTH_LONG).show();
            refreshButtons();
        });
    }

    @Override public void onTrimMemory(int level){
        super.onTrimMemory(level);
        if(level>=TRIM_MEMORY_RUNNING_LOW)runRequested=false;
    }
    @Override protected void onPause(){
        runRequested=false;
        super.onPause();
    }
    @Override protected void onDestroy(){
        closed=true;runRequested=false;
        worker.execute(()->{
            if(machine!=0){
                NativeMachine.destroy(machine);
                machine=0;
            }
        });
        worker.shutdown();
        super.onDestroy();
    }
}
