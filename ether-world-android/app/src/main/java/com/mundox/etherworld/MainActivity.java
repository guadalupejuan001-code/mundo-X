package com.mundox.etherworld;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.util.AtomicFile;
import android.view.Gravity;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * An actual interactive virtual world built atop native Ether/VisualScene
 * and Ether Renderer X; never claims to map the user's physical room.
 * UI, disk access and JNI operations are intentionally serialized.
 */
public final class MainActivity extends Activity {
    private static final int BG=0xff09121b,PANEL=0xff142530,INK=0xffeff9f3;
    private static final int MUTED=0xffadbec3,ACCENT=0xffa4f4df,ORANGE=0xffffc57e;
    private static final int MAX_SAVE=1024*1024;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final List<Button> controls=new ArrayList<>();
    private AtomicFile saved;
    private EtherWorldView scene;
    private TextView status,summary,details,console,coordinate;
    private long handle;
    private int cx,cz,sx,sz;
    private boolean busy,destroyed;
    private String lastAction="Inicio del mundo";

    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        saved=new AtomicFile(new File(getFilesDir(),"ether_world_v1.world"));
        buildUi();
        if(!WorldNative.ready()) {
            status.setText("Error: biblioteca Ether nativa ausente");
            console.setText(WorldNative.problem());
            setControlsEnabled(false);
            return;
        }
        submit("Recuperando universo...",()->{
            String stored=null;
            if(saved.getBaseFile().exists()) {
                try {stored=readSave();}
                catch(Exception e) { stored=null; }
            }
            if(stored!=null) {
                try {
                    handle=WorldNative.nativeLoad(stored);
                    return "Sesión anterior restaurada: mismo universo, mismas reglas y cambios.";
                } catch(Exception error) {
                    // The original file stays intact on disk until the user
                    // explicitly performs a mutation or chooses Nuevo Mundo.
                    handle=WorldNative.nativeCreate(1000);
                    return "El guardado existente está dañado o es incompatible: "
                            +error.getMessage()+". Se abrió una sesión temporal.";
                }
            }
            handle=WorldNative.nativeCreate(1000);
            return "Universo inicial creado con semilla 1000. Revelada la región de origen.";
        },false,false);
    }

    private void buildUi() {
        ScrollView scroll=new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root=new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);
        root.setPadding(dp(16),dp(16),dp(16),dp(24));
        if(Build.VERSION.SDK_INT>=30) {
            root.setOnApplyWindowInsetsListener((view,insets)->{
                android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars());
                view.setPadding(dp(16)+bars.left,dp(16)+bars.top,
                        dp(16)+bars.right,dp(24)+bars.bottom);
                return insets;
            });
        }

        TextView eyebrow=text("MUNDO X  /  ETHER WORLD",13,ACCENT,true);
        eyebrow.setLetterSpacing(.16f);
        root.addView(eyebrow);
        TextView title=text("EL ESPACIO\nCOMO REGLAS",30,INK,true);
        title.setPadding(0,dp(6),0,dp(7));
        root.addView(title);
        root.addView(text("Tu universo se reconstruye al observar: no carga "
                +"1.048.576 posiciones en memoria. Camina, escanea, modifica y evoluciona.",
                13,MUTED,false));
        coordinate=text("Centro: (0,0)   |   Selección: (0,0)",13,INK,true);
        coordinate.setPadding(0,dp(13),0,dp(6));
        root.addView(coordinate);

        LinearLayout mainControls=row(root);
        action(mainControls,"ESCANEAR",()->action("scan",0,0,0,"Escaneo realizado"),true);
        action(mainControls,"IMPULSO",()->action("pulse",0,0,0,"Perturbación emitida"),false);
        action(mainControls,"EVOLUCIONAR ×16",()->action("step",0,0,16,"16 pasos de evolución"),false);

        scene=new EtherWorldView(this,new EtherWorldView.Actions() {
            @Override public void onSelect(int x,int z) {
                action("select",x,z,0,"Celda seleccionada: "+x+","+z);
            }
            @Override public void onRotate(int hundredths) {
                action("orbit",0,0,hundredths,"Rotación de cámara");
            }
        });
        LinearLayout.LayoutParams sceneLayout=new LinearLayout.LayoutParams(-1,-2);
        sceneLayout.topMargin=dp(12);
        root.addView(scene,sceneLayout);

        LinearLayout stateCard=card(root);
        status=text("INICIALIZANDO MOTOR ETHER...",14,ACCENT,true);
        stateCard.addView(status);
        summary=text("Esperando el mapa",12,INK,false);
        summary.setPadding(0,dp(5),0,0);stateCard.addView(summary);
        details=text("La optimización se verificará en el motor nativo.",11,MUTED,false);
        details.setPadding(0,dp(6),0,0);stateCard.addView(details);

        LinearLayout explore=card(root);
        explore.addView(text("01   /   EXPLORAR LA ESTRUCTURA",13,ACCENT,true));
        explore.addView(text("Muévete por las posiciones lógicas. Las regiones no escaneadas "
                +"mantienen su regla original, pero permanecen ocultas en el mapa.",
                12,MUTED,false));
        LinearLayout north=row(explore);
        action(north,"NORTE ↑",()->action("move",0,-1,0,"Avanzaste al norte"),false);
        LinearLayout compass=row(explore);
        action(compass,"← OESTE",()->action("move",-1,0,0,"Avanzaste al oeste"),false);
        action(compass,"ESTE →",()->action("move",1,0,0,"Avanzaste al este"),false);
        LinearLayout south=row(explore);
        action(south,"SUR ↓",()->action("move",0,1,0,"Avanzaste al sur"),false);
        LinearLayout navigation=row(explore);
        action(navigation,"IR A SELECCIÓN",()->action("focus",sx,sz,0,"Visor movido a selección"),false);
        action(navigation,"SEGUIR SONDA",()->action("follow",0,0,0,"Siguiendo la primera sonda"),false);
        action(explore,"REGRESAR AL ORIGEN",()->action("origin",0,0,0,"Regresaste al origen"),false);

        LinearLayout matter=card(root);
        matter.addView(text("02   /   CREAR, TRANSFORMAR Y BORRAR",13,ACCENT,true));
        matter.addView(text("Toca una celda del mapa y decide qué existe allí. "
                +"Solo se guarda la excepción; el millón de celdas conserva sus reglas.",
                12,MUTED,false));
        LinearLayout constructs=row(matter);
        action(constructs,"+ FARO",()->action("edit",0,0,1,"Faro materializado"),true);
        action(constructs,"+ CRISTAL",()->action("edit",0,0,2,"Cristal materializado"),false);
        LinearLayout destruction=row(matter);
        action(destruction,"VACÍO",()->action("edit",0,0,3,"Región convertida en vacío"),false);
        action(destruction,"BARRERA",()->action("edit",0,0,4,"Barrera creada"),false);
        action(matter,"RESTAURAR REGLA ORIGINAL",()->action("edit",0,0,0,
                "Celda restaurada desde su regla generadora"),false);

        LinearLayout observer=card(root);
        observer.addView(text("03   /   OBSERVADOR Y MOTOR ORIGINAL",13,ACCENT,true));
        observer.addView(text("Ether Render X usa máscaras de regiones y trazado exacto "
                +"de objetos cercanos. Puedes contrastarlo con su oráculo de fuerza bruta.",
                12,MUTED,false));
        LinearLayout rotate=row(observer);
        action(rotate,"GIRAR ↶",()->action("orbit",0,0,-30,"Cámara girada"),false);
        action(rotate,"GIRAR ↷",()->action("orbit",0,0,30,"Cámara girada"),false);
        LinearLayout zoom=row(observer);
        action(zoom,"ZOOM +",()->action("zoom",0,0,-1,"Cámara acercada"),false);
        action(zoom,"ZOOM −",()->action("zoom",0,0,1,"Cámara alejada"),false);
        LinearLayout oracle=row(observer);
        action(oracle,"VERIFICAR EXACTITUD",this::verifyExact,false);
        action(oracle,"OPTIMIZAR REGIONES",this::optimize,false);

        LinearLayout saveCard=card(root);
        saveCard.addView(text("04   /   PERSISTENCIA DE TU UNIVERSO",13,ACCENT,true));
        saveCard.addView(text("El estado Ether, las zonas escaneadas y las sondas "
                +"se guardan localmente; no requieren servidor ni Internet.",
                12,MUTED,false));
        LinearLayout saveRow=row(saveCard);
        action(saveRow,"GUARDAR",this::saveNow,false);
        action(saveRow,"RESTAURAR",this::loadNow,false);
        action(saveCard,"CREAR OTRO UNIVERSO",this::confirmNew,false);

        LinearLayout logCard=card(root);
        logCard.addView(text("BITÁCORA DEL MUNDO",12,ACCENT,true));
        console=text("Creando espacio lógico...",12,INK,false);
        console.setPadding(0,dp(8),0,dp(8));
        logCard.addView(console);
        TextView disclaimer=text("MAPA VIRTUAL · Los campos, sondas y qubits de esta app "
                +"son simulados en CPU. No representan un escaneo real de tu habitación "
                +"ni alteran directamente el espacio-tiempo externo.",
                11,MUTED,false);
        disclaimer.setPadding(0,dp(18),0,dp(15));root.addView(disclaimer);

        scroll.addView(root);
        setContentView(scroll);
        setControlsEnabled(false);
    }

    private void verifyExact() {
        submit("Contrastando Ether Render X con su oráculo...",()->{
            return "Verificación: el motor calcula su imagen optimizada "
                    +"y la confronta con todos los cuerpos visibles.";
        },true,false,true);
    }

    private void optimize() {
        submit("Entrenando tamaño de región...",()->{
            String result=WorldNative.nativeOptimize(handle);
            JSONObject j=new JSONObject(result);
            if(!j.optBoolean("ok",false))
                throw new IllegalStateException("El optimizador no respondió");
            if(j.optInt("oracle_mismatches",-1)!=0)
                throw new IllegalStateException("La optimización perdió exactitud");
            return String.format(Locale.US,
                    "Calibración aceptada: región %d px; mediana %.2f ms. "
                            +"El oráculo exacto obtuvo cero diferencias.",
                    j.optInt("tile"),j.optDouble("median_ms"));
        },true,false);
    }

    private void confirmNew() {
        new AlertDialog.Builder(this)
                .setTitle("Crear un universo distinto")
                .setMessage("Se sustituirá el mundo guardado. "
                        +"¿Quieres generar nuevas reglas, posiciones y sondas?")
                .setNegativeButton("Cancelar",null)
                .setPositiveButton("Crear",(d,which)->submit(
                    "Construyendo un universo nuevo...",()->{
                        int seed=(int)(System.currentTimeMillis() ^ System.nanoTime());
                        long candidate=WorldNative.nativeCreate(seed);
                        long old=handle;handle=candidate;
                        if(old!=0)WorldNative.nativeClose(old);
                        return "Nuevo universo generado con semilla "+seed;
                    },true,true))
                .show();
    }

    private void saveNow() {
        submit("Guardando mundo completo...",()->{
            persist(WorldNative.nativeSave(handle));
            return "Universo guardado: reglas, ediciones Ether, sondas, tiempo y mapa.";
        },false,false);
    }

    private void loadNow() {
        submit("Restaurando mundo...",()->{
            String text=readSave();
            long candidate=WorldNative.nativeLoad(text);
            long old=handle;handle=candidate;
            if(old!=0)WorldNative.nativeClose(old);
            return "Universo restaurado desde almacenamiento privado.";
        },true,false);
    }

    private void action(String op,int a,int b,int c,String label) {
        submit(label,()->{
            WorldNative.nativeAction(handle,op,a,b,c);
            return label;
        },true,!"select".equals(op)&&!"orbit".equals(op)&&!"zoom".equals(op));
    }

    private interface Work {String call() throws Exception;}
    private void submit(String label,Work work,boolean withRender,boolean autosave) {
        submit(label,work,withRender,autosave,false);
    }
    private void submit(String label,Work work,boolean withRender,
                        boolean autosave,boolean oracle){
        if(busy){toastStatus("Operación anterior en curso");return;}
        busy=true;
        setControlsEnabled(false);
        toastStatus(label);
        worker.execute(()->{
            try {
                String result=work.call();
                int[] image=new int[WorldNative.VIEW_WIDTH*WorldNative.VIEW_HEIGHT];
                int[] map=new int[WorldNative.MAP_WIDTH*WorldNative.MAP_WIDTH];
                String metadata=WorldNative.nativeRender(handle,image,map,oracle);
                if(autosave)persist(WorldNative.nativeSave(handle));
                JSONObject j=new JSONObject(metadata);
                if(!j.optBoolean("ok",false))throw new IllegalStateException(
                    j.optString("error","Imagen Ether inválida"));
                runOnUiThread(()->{
                    if(destroyed)return;
                    try {
                        cx=j.getInt("center_x");cz=j.getInt("center_z");
                        sx=j.getInt("selected_x");sz=j.getInt("selected_z");
                        scene.update(image,map,cx,cz,sx,sz);
                        fillStats(j,oracle);
                        lastAction=result;
                        console.setText(result);
                        status.setText("MUNDO ACTIVO  ·  ETHER / X");
                    }catch(Exception e) {
                        console.setText("No se pudo presentar el mapa: "+e.getMessage());
                    }finally {
                        busy=false;
                        setControlsEnabled(true);
                    }
                });
            } catch(Exception e) {
                final String error=e.getMessage()==null?e.getClass().getSimpleName():e.getMessage();
                runOnUiThread(()->{
                    if(destroyed)return;
                    console.setText("ERROR · "+error);
                    status.setText("Rechazado sin cerrar el mundo");
                    busy=false;
                    setControlsEnabled(true);
                });
            }
        });
    }

    private void fillStats(JSONObject j,boolean oracle) {
        coordinate.setText(String.format(Locale.US,
            "Observador (%d,%d)   ·   Selección (%d,%d)",cx,cz,sx,sz));
        summary.setText(String.format(Locale.US,
            "1.048.576 posiciones lógicas  ·  %d reveladas  ·  %d cambios Ether\n"
            +"%d sondas  ·  %d perturbaciones  ·  %.2f s simulados",
            j.optInt("explored"),j.optInt("ether_edits"),
            j.optInt("probes"),j.optInt("pulses"),j.optLong("ticks")*.05));
        details.setText(String.format(Locale.US,
            "Render Ether X: %d objetos / %d celdas consultadas"
            +" · %.1f ms por imagen\n"
            +"Candidatos: %.2f del trazado total · bloque %d"
            +" · qubits simulados activos %d%s",
            j.optInt("rendered_bodies"),j.optInt("reconstructed_cells"),
            j.optDouble("frame_ms"),j.optDouble("candidate_ratio"),
            j.optInt("ether_tile"),j.optInt("simulated_qubits"),
            oracle?" · diferencias exactas "+j.optInt("exact_mismatches"):""));
    }

    private String readSave() throws Exception {
        File path=saved.getBaseFile();
        if(!path.exists())throw new IllegalStateException("No existe todavía un universo guardado.");
        if(path.length()>MAX_SAVE)throw new IllegalStateException(
                "El archivo supera el presupuesto de 1 MiB.");
        byte[] bytes=saved.readFully();
        if(bytes.length>MAX_SAVE)throw new IllegalStateException("Estado demasiado grande");
        return new String(bytes,StandardCharsets.UTF_8);
    }
    private void persist(String text) throws Exception {
        if(text==null)throw new IllegalStateException("El mundo no se pudo serializar");
        byte[] bytes=text.getBytes(StandardCharsets.UTF_8);
        if(bytes.length>MAX_SAVE)throw new IllegalStateException("El mundo excede 1 MiB");
        FileOutputStream out=null;
        try {
            out=saved.startWrite();
            out.write(bytes);
            saved.finishWrite(out);
        } catch(Exception e) {
            if(out!=null)saved.failWrite(out);
            throw e;
        }
    }

    private void setControlsEnabled(boolean enable){
        for(Button button:controls)button.setEnabled(enable);
    }
    private void toastStatus(String text){if(status!=null)status.setText(text);}

    private LinearLayout card(LinearLayout root){
        LinearLayout b=new LinearLayout(this);
        b.setOrientation(LinearLayout.VERTICAL);
        b.setPadding(dp(13),dp(14),dp(13),dp(14));
        GradientDrawable g=new GradientDrawable();
        g.setColor(PANEL);g.setCornerRadius(dp(16));
        g.setStroke(dp(1),0xff284752);
        b.setBackground(g);
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);
        p.topMargin=dp(12);
        root.addView(b,p);
        return b;
    }
    private LinearLayout row(LinearLayout root) {
        LinearLayout l=new LinearLayout(this);
        l.setOrientation(LinearLayout.HORIZONTAL);
        l.setPadding(0,dp(7),0,0);
        root.addView(l,new LinearLayout.LayoutParams(-1,-2));
        return l;
    }
    private Button action(LinearLayout parent,String title,Runnable work,boolean accent) {
        Button b=new Button(this);
        b.setAllCaps(false);
        b.setText(title);
        b.setTextSize(11);
        b.setTextColor(accent?BG:INK);
        b.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        GradientDrawable g=new GradientDrawable();
        g.setColor(accent?ACCENT:0xff284550);
        g.setCornerRadius(dp(10));
        b.setBackground(g);
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,dp(48),1f);
        p.rightMargin=dp(5);
        parent.addView(b,p);
        b.setOnClickListener(v->work.run());
        controls.add(b);
        return b;
    }
    private TextView text(String value,int size,int color,boolean bold){
        TextView t=new TextView(this);
        t.setText(value);
        t.setTextSize(size);
        t.setTextColor(color);
        if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        return t;
    }
    private int dp(float f){
        return Math.round(f*getResources().getDisplayMetrics().density);
    }
    @Override protected void onDestroy() {
        destroyed=true;
        worker.execute(()->{
            if(handle!=0){
                try{WorldNative.nativeClose(handle);}catch(Exception ignored){}
            }
        });
        worker.shutdown();
        super.onDestroy();
    }
}
