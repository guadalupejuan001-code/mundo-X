package com.mundox.cosmic;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.AtomicFile;
import android.view.GestureDetector;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import com.mundox.graphics.CosmicProject;
import com.mundox.graphics.InfiniteControls;
import com.mundox.graphics.InfiniteProgram;
import com.mundox.graphics.NativeCore;
import com.mundox.graphics.QuantumRenderer;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.OutputStream;
import java.io.Serializable;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import static com.mundox.cosmic.CosmicUi.*;

/** New Android home for Mundo X: one active graphics/core session and the original labs. */
public final class CosmicActivity extends Activity {
    NativeCore core;
    QuantumRenderer renderer;
    GLSurfaceView surface;
    volatile boolean resumed;

    private static final int OPEN_PROJECT=801,SAVE_PROJECT=802;
    private static final Object SESSION_IO=new Object();
    private static final AtomicLong SESSION_SAVES=new AtomicLong();
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService storage=Executors.newSingleThreadExecutor();
    private CosmicUi ui;
    private LinearLayout studio,motorControls,coreControls,projectList;
    private FrameLayout viewport,pages;
    private View labsPage,archivePage;
    private TextView sceneName,hudCount,hudState,selectedText,coreSummary,coreRevision,activeProjectLabel,activeProjectDetail;
    private Button playButton,couplingButton,qubitButton;
    private CoreSignalsView signals;
    private final LinearLayout[] nav=new LinearLayout[4];
    private final CosmicUi.Glyph[] navIcons=new CosmicUi.Glyph[4];
    private final TextView[] navLabels=new TextView[4];
    private AtomicFile sessionFile;
    private volatile SavedSession savedSession;
    private volatile boolean destroyed,sessionProtected,animationFrameQueued;
    private volatile String projectName="Ciudad cuántica";
    private volatile int tab,qubit;
    private volatile String addressA="0",addressB="1";
    private volatile boolean animate;
    private boolean coupled=true;
    private float animationTime;
    private long lastTick,projectLoadSerial;
    private long expansionSerial;
    private boolean expandingProject,expansionWasAnimating;
    private AlertDialog expansionDialog;
    private String statusText="Preparando tu mundo…";
    private String startupMessage;

    /** Private resume state preserves every renderer field, including disabled coupling. */
    private static final class SavedSession implements Serializable {
        private static final long serialVersionUID=1L;
        final NativeCore.Snapshot core;
        final QuantumRenderer.Snapshot visual;
        final String name;
        final String addressA,addressB;
        final int tab,qubit;
        final boolean animate;
        SavedSession(String name,NativeCore.Snapshot core,QuantumRenderer.Snapshot visual,int tab,int qubit,boolean animate,String addressA,String addressB){
            this.name=name;this.core=core;this.visual=visual;this.tab=tab;this.qubit=qubit;this.animate=animate;this.addressA=addressA;this.addressB=addressB;
        }
    }
    private static final class Retained {
        final SavedSession session;
        final int tab,qubit;
        final boolean animate;
        Retained(SavedSession s,int t,int q,boolean a){session=s;tab=t;qubit=q;animate=a;}
    }

    @Override public void onCreate(Bundle state){
        super.onCreate(state);ui=new CosmicUi(this);
        getWindow().setStatusBarColor(BG);getWindow().setNavigationBarColor(BG);
        if(Build.VERSION.SDK_INT>=30)getWindow().setDecorFitsSystemWindows(false);
        sessionFile=new AtomicFile(new File(getFilesDir(),"cosmic-session-v1.bin"));
        ActivityManager manager=(ActivityManager)getSystemService(Context.ACTIVITY_SERVICE);
        if(manager.getDeviceConfigurationInfo().reqGlEsVersion<0x30000){showFatal("Tu dispositivo necesita OpenGL ES 3.0 para abrir el motor gráfico.");return;}
        Retained retained=getLastNonConfigurationInstance() instanceof Retained?(Retained)getLastNonConfigurationInstance():null;
        try{
            SavedSession previous=retained!=null&&retained.session!=null?retained.session:readSession();
            if(previous!=null){
                // Validate portable values, but restore the original snapshots: the codec
                // deliberately omits remembered coupling and hidden lookAt orbit settings.
                CosmicProject.fromSnapshots(previous.name,previous.core,previous.visual);
                core=new NativeCore(previous.core);renderer=new QuantumRenderer(this,core,this::showStatus,previous.visual);
                core.query(previous.visual.observables);
                projectName=previous.name;savedSession=previous;coupled=previous.visual.coupling;animationTime=previous.visual.time;
                tab=Math.max(0,Math.min(3,previous.tab));qubit=core.isInfinite()?0:Math.max(0,Math.min(core.qubits-1,previous.qubit));animate=previous.animate;
                restoreAddresses(previous.addressA,previous.addressB,previous.visual.observables);
            }
        }catch(Throwable error){
            if(core!=null)core.close();core=null;renderer=null;savedSession=null;
            sessionProtected=true;startupMessage="No se pudo recuperar la sesión anterior. Su archivo se conserva; puedes abrir un proyecto o crear uno nuevo.";
        }
        try{
            if(core==null){
                core=new NativeCore(NativeCore.DEFAULT_QUBITS);core.resetGraph();renderer=new QuantumRenderer(this,core,this::showStatus);
                renderer.objectCount(16_384);renderer.scenicView();renderer.resolution(960);
            }
            renderer.setPresentation(QuantumRenderer.PRESENTATION_COSMIC);
            if(retained!=null){tab=Math.max(0,Math.min(3,retained.tab));qubit=core.isInfinite()?0:Math.max(0,Math.min(core.qubits-1,retained.qubit));animate=retained.animate;}
            buildScreen();
            if(startupMessage!=null)main.post(()->message(startupMessage));
        }catch(Throwable error){if(core!=null)core.close();core=null;showFatal("No se pudo iniciar tu motor.\n"+error.getMessage());}
    }

    private void showFatal(String message){TextView text=ui.text(message,18,TEXT,false);text.setPadding(ui.dp(24),ui.dp(72),ui.dp(24),ui.dp(24));text.setBackgroundColor(BG);setContentView(text);}

    private void buildScreen(){
        LinearLayout root=ui.column();root.setBackgroundColor(BG);
        root.setOnApplyWindowInsetsListener((v,insets)->{
            if(Build.VERSION.SDK_INT>=30){android.graphics.Insets b=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());v.setPadding(b.left,b.top,b.right,b.bottom);}
            else v.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());
            return insets;
        });
        LinearLayout heading=ui.row();heading.setPadding(ui.dp(20),ui.dp(12),ui.dp(20),ui.dp(12));
        CosmicUi.Glyph mark=new CosmicUi.Glyph(this,"nucleo",CYAN);heading.addView(mark,new LinearLayout.LayoutParams(ui.dp(35),ui.dp(35)));
        LinearLayout title=ui.column();title.setPadding(ui.dp(11),0,0,0);
        LinearLayout brand=ui.row();TextView name=ui.text("COSMIC X",23,TEXT,true);name.setLetterSpacing(.04f);brand.addView(name);
        TextView engine=ui.eyebrow("ENGINE",MUTED);engine.setPadding(ui.dp(9),ui.dp(4),0,0);brand.addView(engine);title.addView(brand);
        TextView subtitle=ui.eyebrow("COMPUTACIÓN CUÁNTICA",CYAN);subtitle.setTextSize(9);subtitle.setPadding(0,ui.dp(5),0,0);title.addView(subtitle);
        heading.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        TextView info=ui.text("0.3",11,MUTED,true);info.setGravity(Gravity.CENTER);info.setContentDescription("Información de COSMIC X Engine 0.3.1");info.setTag("app_info");
        ui.clickable(info,PANEL,12);info.setOnClickListener(v->showInfo());heading.addView(info,new LinearLayout.LayoutParams(ui.dp(42),ui.dp(42)));root.addView(heading);

        pages=new FrameLayout(this);root.addView(pages,new LinearLayout.LayoutParams(-1,0,1));
        studio=ui.column();pages.addView(studio,new FrameLayout.LayoutParams(-1,-1));
        viewport=new FrameLayout(this);viewport.setBackground(ui.shape(PANEL,22,0));viewport.setClipToOutline(true);
        LinearLayout.LayoutParams vp=new LinearLayout.LayoutParams(-1,0,1);vp.setMargins(ui.dp(12),0,ui.dp(12),0);studio.addView(viewport,vp);
        surface=new GLSurfaceView(this);surface.setTag("cosmic_surface");surface.setEGLContextClientVersion(3);surface.setEGLConfigChooser(8,8,8,8,24,0);
        surface.setPreserveEGLContextOnPause(true);surface.setRenderer(renderer);surface.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        viewport.addView(surface,new FrameLayout.LayoutParams(-1,-1));installGestures();

        LinearLayout hud=ui.row();hud.setPadding(ui.dp(16),ui.dp(14),ui.dp(16),ui.dp(20));
        hud.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,new int[]{0xdd080d18,0x00080d18}));
        hudCount=ui.text("16.384 objetos",12,TEXT,true);hudCount.setTag("render_objects");hud.addView(hudCount,new LinearLayout.LayoutParams(0,-2,1));
        hudState=ui.text("INICIANDO",9,CYAN,true);hudState.setLetterSpacing(.1f);hudState.setTag("render_state");hud.addView(hudState);viewport.addView(hud,new FrameLayout.LayoutParams(-1,ui.dp(66),Gravity.TOP));

        LinearLayout caption=ui.column();caption.setPadding(ui.dp(17),ui.dp(26),ui.dp(17),ui.dp(15));
        caption.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,new int[]{0x00080d18,0xea080d18}));
        caption.addView(ui.eyebrow("MOTOR GRÁFICO CUÁNTICO",CYAN));
        sceneName=ui.text(projectName,25,TEXT,true);sceneName.setSingleLine(true);sceneName.setEllipsize(android.text.TextUtils.TruncateAt.END);sceneName.setPadding(0,ui.dp(5),0,ui.dp(5));caption.addView(sceneName);
        LinearLayout hintRow=ui.row();selectedText=ui.text("Arrastra · gira     Pellizca · acércate",10,MUTED,false);selectedText.setTag("selected_object");hintRow.addView(selectedText,new LinearLayout.LayoutParams(0,-2,1));
        TextView edit=ui.text("EDITAR ↗",10,TEXT,true);edit.setTag("edit_selected");edit.setPadding(ui.dp(12),ui.dp(10),0,ui.dp(10));edit.setOnClickListener(v->editMenu());hintRow.addView(edit);caption.addView(hintRow);
        viewport.addView(caption,new FrameLayout.LayoutParams(-1,-2,Gravity.BOTTOM));

        motorControls=buildMotorControls();motorControls.setTag("panel_motor");studio.addView(motorControls);
        coreControls=buildCoreControls();coreControls.setTag("panel_nucleo");studio.addView(coreControls);
        labsPage=buildLabs();labsPage.setTag("panel_labs");pages.addView(labsPage,new FrameLayout.LayoutParams(-1,-1));
        archivePage=buildArchive();archivePage.setTag("panel_archivo");pages.addView(archivePage,new FrameLayout.LayoutParams(-1,-1));
        root.addView(buildNavigation());setContentView(root);root.requestApplyInsets();selectTab(tab,false);
        surface.queueEvent(()->{try{captureSession();refreshCore();}catch(Exception error){showStatus(error.getMessage());}});surface.requestRender();
    }

    private LinearLayout buildMotorControls(){
        LinearLayout box=ui.column();box.setPadding(ui.dp(20),ui.dp(13),ui.dp(20),ui.dp(10));
        LinearLayout controls=ui.row();playButton=ui.button(animate?"Pausar":"Animar","toggle_animation",true,this::toggleAnimation);
        ui.weightedButton(controls,playButton,true);
        ui.weightedButton(controls,ui.button("Cámara","scene_camera",false,()->action(()->renderer.toggleView())),false);
        ui.weightedButton(controls,ui.button("Escena","scene_options",false,this::sceneMenu),false);box.addView(controls);
        LinearLayout connect=ui.row();connect.setPadding(ui.dp(2),ui.dp(10),0,0);
        LinearLayout connectedLabel=ui.column();connectedLabel.addView(ui.eyebrow("NÚCLEO + MOTOR",CYAN));
        coreSummary=ui.text(core.domainLabel()+" · mismo estado activo",11,MUTED,false);coreSummary.setPadding(0,ui.dp(4),0,0);connectedLabel.addView(coreSummary);connect.addView(connectedLabel,new LinearLayout.LayoutParams(0,-2,1));
        TextView explore=ui.text("Controlar →",12,TEXT,true);explore.setTag("open_core");explore.setPadding(ui.dp(12),ui.dp(12),0,ui.dp(12));explore.setOnClickListener(v->selectTab(1,true));connect.addView(explore);box.addView(connect);return box;
    }

    private LinearLayout buildCoreControls(){
        LinearLayout outer=ui.column();outer.setPadding(ui.dp(20),ui.dp(10),ui.dp(20),ui.dp(8));
        LinearLayout head=ui.row();head.addView(ui.eyebrow("NÚCLEO CUÁNTICO",VIOLET),new LinearLayout.LayoutParams(0,-2,1));
        qubitButton=ui.button(core.isInfinite()?"A "+InfiniteProgram.shortAddress(addressA)+" ▾":"Qubit "+qubit+" ▾","select_qubit",false,this::chooseQubit);qubitButton.setTextSize(11);head.addView(qubitButton,new LinearLayout.LayoutParams(ui.dp(104),ui.dp(44)));outer.addView(head);
        signals=new CoreSignalsView(this);outer.addView(signals,new LinearLayout.LayoutParams(-1,ui.dp(112)));
        LinearLayout gates=ui.row();String[] names={"H","S","X","Z","Y","CX","CZ"};int[] ops={0,1,2,3,6,4,5};
        for(int j=0;j<names.length;j++){final int operation=ops[j];Button gate=ui.button(names[j],"gate_"+names[j].toLowerCase(Locale.ROOT),j==0,()->applyGate(operation));
            gate.setPadding(ui.dp(5),0,ui.dp(5),0);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(ui.dp(45),ui.dp(44));if(j>0)p.leftMargin=ui.dp(5);gates.addView(gate,p);
            if(operation==4||operation==5){gate.setEnabled(core.isInfinite()||core.qubits>1);gate.setAlpha(core.isInfinite()||core.qubits>1?1:.35f);}}
        if(core.isInfinite()){Button extra=ui.button("Infinito…","infinite_actions",false,this::infiniteActions);gates.addView(extra,new LinearLayout.LayoutParams(ui.dp(98),ui.dp(44)));}
        HorizontalScrollView scroller=new HorizontalScrollView(this);scroller.setHorizontalScrollBarEnabled(false);scroller.addView(gates);outer.addView(scroller);
        LinearLayout footer=ui.row();footer.setPadding(0,ui.dp(8),0,0);
        coreRevision=ui.text("Observando el estado",10,MUTED,false);coreRevision.setTag("core_revision");footer.addView(coreRevision,new LinearLayout.LayoutParams(0,-2,1));
        couplingButton=ui.button(coupled?"Campo conectado":"Campo apagado","coupling",false,this::toggleCoupling);couplingButton.setTextSize(10);footer.addView(couplingButton,new LinearLayout.LayoutParams(ui.dp(145),ui.dp(44)));outer.addView(footer);
        return outer;
    }

    private View buildLabs(){
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setBackgroundColor(BG);
        LinearLayout body=ui.column();body.setPadding(ui.dp(20),ui.dp(14),ui.dp(20),ui.dp(20));scroll.addView(body);
        body.addView(ui.eyebrow("EXPERIMENTOS DE MUNDO X",CYAN));body.addView(ui.space(9));body.addView(ui.text("Tu laboratorio.",30,TEXT,true));body.addView(ui.space(8));
        TextView intro=ui.text("Explora tus ideas, toca sus reglas y observa qué cambia.",13,MUTED,false);intro.setLineSpacing(ui.dp(3),1);body.addView(intro);body.addView(ui.space(20));
        LinearLayout featured=ui.row();
        for(String id:new String[]{"micromundo","circuitos"}){
            Experiments.Item item=experiment(id);LinearLayout card=ui.column();card.setPadding(ui.dp(15),ui.dp(15),ui.dp(12),ui.dp(15));ui.clickable(card,PANEL,19);
            card.setTag("experiment_"+item.id);card.setContentDescription(item.title+". "+item.subtitle);card.setOnClickListener(v->launch(item));
            CosmicUi.Glyph glyph=new CosmicUi.Glyph(this,item.id,item.accent);card.addView(glyph,new LinearLayout.LayoutParams(ui.dp(31),ui.dp(31)));
            card.addView(ui.space(13));card.addView(ui.text(item.title,18,TEXT,true));card.addView(ui.space(7));
            TextView detail=ui.text(item.id.equals("micromundo")?"Partículas · Colonia\nEstado cuántico":"Puertas · Protocolos\nMemoria",11,MUTED,false);detail.setLineSpacing(ui.dp(3),1);card.addView(detail);
            LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,ui.dp(164),1);if(featured.getChildCount()>0)p.leftMargin=ui.dp(12);featured.addView(card,p);
        }body.addView(featured);body.addView(ui.space(22));
        body.addView(ui.eyebrow("TODOS TUS EXPERIMENTOS · "+Experiments.ALL.length,MUTED));body.addView(ui.space(10));
        for(Experiments.Item item:Experiments.ALL){if(item.id.equals("micromundo")||item.id.equals("circuitos"))continue;
            LinearLayout card=ui.row();card.setPadding(ui.dp(12),ui.dp(12),ui.dp(13),ui.dp(12));ui.clickable(card,PANEL,16);card.setTag("experiment_"+item.id);card.setContentDescription(item.title+". "+item.subtitle);card.setOnClickListener(v->launch(item));
            card.addView(new CosmicUi.Glyph(this,item.id,item.accent),new LinearLayout.LayoutParams(ui.dp(29),ui.dp(29)));
            LinearLayout words=ui.column();words.setPadding(ui.dp(13),0,ui.dp(8),0);words.addView(ui.text(item.title,15,TEXT,true));TextView detail=ui.text(item.subtitle,11,MUTED,false);detail.setPadding(0,ui.dp(5),0,0);words.addView(detail);card.addView(words,new LinearLayout.LayoutParams(0,-2,1));
            card.addView(ui.text("↗",18,item.accent,false));LinearLayout.LayoutParams p=ui.full();p.bottomMargin=ui.dp(8);body.addView(card,p);
        }
        body.addView(ui.space(10));TextView credit=ui.text("Idea, creación y dirección: X\nCódigo de Mundo X · Experimentos en Android",11,MUTED,false);credit.setLineSpacing(ui.dp(4),1);body.addView(credit);return scroll;
    }

    private View buildArchive(){
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setBackgroundColor(BG);
        LinearLayout body=ui.column();body.setPadding(ui.dp(20),ui.dp(14),ui.dp(20),ui.dp(22));scroll.addView(body);
        body.addView(ui.eyebrow("ESTADO + MUNDO + CÁMARA",CYAN));body.addView(ui.space(9));body.addView(ui.text("Tus proyectos.",30,TEXT,true));body.addView(ui.space(8));body.addView(ui.text("Guarda un instante. Continúa desde ahí.",13,MUTED,false));body.addView(ui.space(21));
        LinearLayout active=ui.column();active.setPadding(ui.dp(17),ui.dp(17),ui.dp(17),ui.dp(17));active.setBackground(ui.shape(PANEL,20,LINE));
        active.addView(ui.eyebrow("SESIÓN ACTIVA",CYAN));active.addView(ui.space(10));activeProjectLabel=ui.text(projectName,21,TEXT,true);activeProjectLabel.setTag("active_project_name");active.addView(activeProjectLabel);active.addView(ui.space(6));
        activeProjectDetail=ui.text("",12,MUTED,false);active.addView(activeProjectDetail);active.addView(ui.space(16));
        LinearLayout files=ui.row();ui.weightedButton(files,ui.button("Guardar .cosmic","project_save",true,this::exportProject),true);ui.weightedButton(files,ui.button("Abrir archivo","project_open",false,this::openProject),false);active.addView(files);
        active.addView(ui.space(8));active.addView(ui.button("Guardar copia en este dispositivo","project_local_copy",false,this::saveLocalCopy),ui.full());body.addView(active);
        if(!core.isInfinite()&&core.qubits<NativeCore.DEFAULT_QUBITS){
            active.addView(ui.space(8));active.addView(ui.button("Ampliar a "+NativeCore.DEFAULT_QUBITS+" qubits · añadir |0⟩","project_expand_qubits",false,this::expandProject),ui.full());
        }
        body.addView(ui.space(16));body.addView(ui.button("+  Crear un mundo","new_project",false,this::newProject),ui.full());body.addView(ui.space(24));body.addView(ui.eyebrow("PUNTOS DE PARTIDA",MUTED));body.addView(ui.space(10));
        body.addView(projectCard("Ciudad cuántica",NativeCore.DEFAULT_QUBITS+" qubits · escena cercana","example_city",CYAN,()->createScene("Ciudad cuántica",NativeCore.DEFAULT_QUBITS,16_384,2026)),ui.full());body.addView(ui.space(8));
        body.addView(projectCard("Mundo infinito","Pares Bell · direcciones sin tamaño total finito","example_infinite",VIOLET,()->createInfiniteScene("Mundo infinito",NativeCore.BELL_PAIRS,16_384,2026)),ui.full());body.addView(ui.space(8));
        body.addView(projectCard("Observador Bell","2 qubits · correlaciones de Pauli","example_bell",VIOLET,()->loadAsset("bell-observer.cosmic")),ui.full());body.addView(ui.space(8));
        body.addView(projectCard("Ciudad GHZ","3 qubits · un millón de objetos","example_ghz",GOLD,()->loadAsset("ghz-city.cosmic")),ui.full());
        body.addView(ui.space(24));body.addView(ui.eyebrow("GUARDADOS AQUÍ",MUTED));body.addView(ui.space(10));projectList=ui.column();body.addView(projectList);refreshSavedProjects();return scroll;
    }

    private View projectCard(String name,String description,String tag,int color,Runnable run){
        LinearLayout card=ui.row();card.setPadding(ui.dp(13),ui.dp(13),ui.dp(13),ui.dp(13));ui.clickable(card,PANEL,15);card.setTag(tag);card.setOnClickListener(v->run.run());
        card.addView(new CosmicUi.Glyph(this,"motor",color),new LinearLayout.LayoutParams(ui.dp(27),ui.dp(27)));LinearLayout words=ui.column();words.setPadding(ui.dp(12),0,0,0);words.addView(ui.text(name,14,TEXT,true));TextView sub=ui.text(description,11,MUTED,false);sub.setPadding(0,ui.dp(5),0,0);words.addView(sub);card.addView(words,new LinearLayout.LayoutParams(0,-2,1));card.addView(ui.text("↗",18,color,false));return card;
    }

    private View buildNavigation(){
        LinearLayout outer=ui.column();outer.setPadding(ui.dp(12),ui.dp(3),ui.dp(12),ui.dp(6));LinearLayout row=ui.row();
        String[] ids={"motor","nucleo","labs","archivo"},names={"Motor","Núcleo","Laboratorios","Proyectos"};
        for(int j=0;j<4;j++){final int index=j;nav[j]=ui.column();nav[j].setGravity(Gravity.CENTER);nav[j].setPadding(0,ui.dp(8),0,ui.dp(7));nav[j].setTag("nav_"+ids[j]);nav[j].setContentDescription(names[j]);nav[j].setOnClickListener(v->selectTab(index,true));
            navIcons[j]=new CosmicUi.Glyph(this,ids[j],MUTED);nav[j].addView(navIcons[j],new LinearLayout.LayoutParams(ui.dp(24),ui.dp(24)));
            navLabels[j]=ui.text(names[j],10,MUTED,true);navLabels[j].setGravity(Gravity.CENTER);navLabels[j].setPadding(0,ui.dp(5),0,0);nav[j].addView(navLabels[j]);row.addView(nav[j],new LinearLayout.LayoutParams(0,ui.dp(60),1));}
        outer.addView(row);return outer;
    }

    private void selectTab(int next,boolean user){
        boolean wasStudio=tab<2;tab=next;boolean showStudio=next<2;
        if(user&&!showStudio&&wasStudio&&surface!=null){main.removeCallbacks(tick);captureAndPause(surface,System.nanoTime()+TimeUnit.MILLISECONDS.toNanos(1500));}
        studio.setVisibility(showStudio?View.VISIBLE:View.GONE);motorControls.setVisibility(next==0?View.VISIBLE:View.GONE);coreControls.setVisibility(next==1?View.VISIBLE:View.GONE);
        labsPage.setVisibility(next==2?View.VISIBLE:View.GONE);archivePage.setVisibility(next==3?View.VISIBLE:View.GONE);
        for(int j=0;j<4;j++){boolean active=j==next;nav[j].setBackground(ui.shape(active?PANEL:BG,15,0));nav[j].setSelected(active);navLabels[j].setTextColor(active?CYAN:MUTED);navIcons[j].color(active?CYAN:MUTED);}
        if(showStudio&&surface!=null){if(resumed)surface.onResume();surface.requestRender();if(user)action(()->{});if(animate&&resumed){lastTick=0;main.removeCallbacks(tick);main.post(tick);}}
        if(next==3){refreshProjectLabels();if(user)refreshSavedProjects();}
    }

    private static Experiments.Item experiment(String id){for(Experiments.Item item:Experiments.ALL)if(item.id.equals(id))return item;throw new IllegalArgumentException(id);}
    private void launch(Experiments.Item item){startActivity(new Intent(this,item.activity));}

    private void installGestures(){
        ScaleGestureDetector scale=new ScaleGestureDetector(this,new ScaleGestureDetector.SimpleOnScaleGestureListener(){
            @Override public boolean onScale(ScaleGestureDetector d){float factor=d.getScaleFactor();action(()->renderer.zoom(factor));return true;}
        });
        GestureDetector gesture=new GestureDetector(this,new GestureDetector.SimpleOnGestureListener(){
            @Override public boolean onDown(MotionEvent event){return true;}
            @Override public boolean onSingleTapUp(MotionEvent event){int x=(int)event.getX(),y=(int)event.getY();action(()->renderer.pick(x,y));surface.performClick();return true;}
            @Override public boolean onScroll(MotionEvent first,MotionEvent next,float dx,float dy){if(!scale.isInProgress()&&next.getPointerCount()==1)action(()->renderer.orbit(-dx,-dy));return true;}
        });
        surface.setOnTouchListener((v,event)->{scale.onTouchEvent(event);gesture.onTouchEvent(event);return true;});
        surface.setContentDescription("Ciudad del motor gráfico de Mundo X. Arrastra para girar, pellizca para acercarte, toca un edificio para seleccionarlo.");
    }

    private void restoreAddresses(String first,String second,String[] observables){
        if(!core.isInfinite()){addressA="0";addressB="1";return;}
        String[] focus=InfiniteProgram.focus(observables);
        try{addressA=InfiniteProgram.address(first);addressB=InfiniteProgram.address(second);InfiniteProgram.pair(addressA,addressB);}
        catch(Exception ignored){addressA=focus[0];addressB=focus[1];}
    }
    private void chooseQubit(){
        if(core.isInfinite()){
            InfiniteControls.addresses(this,addressA,addressB,(a,b)->action(()->{
                renderer.observeAddresses(a,b);addressA=a;addressB=b;
                main.post(()->qubitButton.setText("A "+InfiniteProgram.shortAddress(a)+" ▾"));
            }));return;
        }
        String[] names=new String[core.qubits];for(int q=0;q<names.length;q++)names[q]="Qubit "+q;
        new AlertDialog.Builder(this).setTitle("Qubit activo").setSingleChoiceItems(names,qubit,(d,which)->{
            qubit=which;qubitButton.setText("Qubit "+qubit+" ▾");d.dismiss();action(()->{});
        }).show();
    }
    private void infiniteActions(){InfiniteControls.actions(this,core,addressA,script->action(()->{
        String output=core.executeInfinite(script);main.post(()->{if(!destroyed)InfiniteControls.result(this,output);});
    }));}
    private void applyGate(int operation){
        if(core.isInfinite()){final String a=addressA,b=addressB;action(()->renderer.gate(operation,a,b));}
        else {final int q=qubit;action(()->renderer.gate(operation,q,(q+1)%core.qubits));}
    }
    private void toggleCoupling(){coupled=!coupled;couplingButton.setText(coupled?"Campo conectado":"Campo apagado");final boolean enabled=coupled;action(()->renderer.coupling(enabled));}
    private void toggleAnimation(){animate=!animate;playButton.setText(animate?"Pausar":"Animar");lastTick=0;main.removeCallbacks(tick);if(animate&&resumed&&tab<2)main.post(tick);}

    private void sceneMenu(){new AlertDialog.Builder(this).setTitle("Tu escena").setItems(new String[]{"Cámara de presentación","Cantidad de objetos","Calidad del renderizado","Semilla del mundo","Editar el objeto seleccionado","Detalle del motor"},(d,which)->{
        if(which==0)action(()->renderer.scenicView());else if(which==1)chooseObjects();else if(which==2)chooseQuality();else if(which==3)chooseSeed();else if(which==4)editMenu();else showInfo();}).show();}
    private void chooseObjects(){int[] counts={1_024,4_096,16_384,65_536,262_144,1_000_000,1_048_576};String[] labels={"1.024","4.096","16.384","65.536","262.144","1.000.000","1.048.576"};new AlertDialog.Builder(this).setTitle("Objetos del mundo").setItems(labels,(d,i)->action(()->renderer.objectCount(counts[i]))).show();}
    private void chooseQuality(){int[] pixels={480,640,960,1280};new AlertDialog.Builder(this).setTitle("Detalle de la imagen").setItems(new String[]{"480 px · ligero","640 px · equilibrado","960 px · detallado","1.280 px · máximo detalle"},(d,i)->action(()->renderer.resolution(pixels[i]))).show();}
    private void chooseSeed(){EditText input=field("Semilla de 0 a 4.294.967.295",String.valueOf(savedSession==null?2026:savedSession.visual.seed),true);AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Semilla del mundo").setView(input).setNegativeButton("Cancelar",null).setPositiveButton("Aplicar",null).create();dialog.setOnShowListener(d->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{try{long seed=Long.parseLong(input.getText().toString());if(seed<0||seed>0xffffffffL)throw new IllegalArgumentException("Semilla fuera de rango");action(()->renderer.worldSeed(seed));dialog.dismiss();}catch(Exception error){input.setError(error.getMessage());}}));dialog.show();}
    private void editMenu(){
        QuantumRenderer.FrameStats stats=renderer.frameStats();if(stats==null||stats.selectedId<0){message("Toca un edificio del mundo para seleccionar su ID.");return;}
        new AlertDialog.Builder(this).setTitle("Objeto "+stats.selectedId).setItems(new String[]{"Subir altura","Ocultar","Restaurar su regla original"},(d,which)->action(()->renderer.editSelected(new int[]{1,7,0}[which]))).show();
    }

    private void action(Runnable change){
        GLSurfaceView target=surface;if(target==null||destroyed||expandingProject)return;
        target.queueEvent(()->{if(destroyed)return;try{change.run();captureSession();refreshCore();}catch(Throwable error){showStatus("No se pudo aplicar: "+error.getMessage());}});target.requestRender();
    }
    /** Native observations and snapshots are taken in the same GL queue as mutations. */
    private void refreshCore(){
        final NativeCore observed=core;
        if(observed.isInfinite()){
            final String a=addressA,b=addressB;NativeCore.Observation result=observed.observe(InfiniteProgram.pair(a,b));
            NativeCore.InfiniteStats stats=observed.infiniteStats();
            main.post(()->{if(destroyed||core!=observed)return;
                signals.updateInfinite(a,result.values[0],result.values[1],result.values[4]);
                coreRevision.setText("Infinito · "+stats.backgroundName()+" · revisión "+Long.toUnsignedString(result.revision)+
                    "\n"+stats.activeQubits+" activos · "+stats.components+" componentes · "+stats.tableauBytes+" bytes tableau"+
                    "\nCX/CZ → "+InfiniteProgram.shortAddress(b));
                coreSummary.setText("Infinito · "+stats.activeQubits+" activos · fondo "+stats.backgroundName());refreshProjectLabels();
            });return;
        }
        final int q=qubit;char[] x=new char[observed.qubits],z=new char[observed.qubits];Arrays.fill(x,'I');Arrays.fill(z,'I');x[q]='X';z[q]='Z';float[] values=observed.query(new String[]{new String(x),new String(z)});NativeCore.Snapshot state=observed.snapshot();
        main.post(()->{if(destroyed||core!=observed)return;signals.update(q,values[0],values[1]);coreRevision.setText("Revisión "+state.version+" · "+(state.gateHistory.length/3)+" puertas\nCX/CZ → q"+((q+1)%state.qubits));coreSummary.setText(state.qubits+" qubits · revisión "+state.version);refreshProjectLabels();});
    }
    private void showStatus(String message){
        statusText=message;main.post(()->{if(destroyed||hudState==null)return;QuantumRenderer.FrameStats s=renderer.frameStats();
            if(renderer.failure!=null){hudState.setText("RENDER DETENIDO");hudState.setTextColor(GOLD);selectedText.setText(renderer.failure);return;}
            if(s==null)return;hudState.setText(s.infinite?"INFINITO · ACTIVO":s.qubits+" QUBITS · ACTIVO");hudState.setTextColor(CYAN);hudCount.setText(format(s.objectCount)+" objetos");selectedText.setText(s.selectedId>=0?"Objeto "+format(s.selectedId)+" · seleccionado":"Arrastra · gira     Pellizca · acércate");});
    }
    private void refreshProjectLabels(){if(activeProjectLabel==null)return;activeProjectLabel.setText(projectName);SavedSession s=savedSession;if(s!=null)activeProjectDetail.setText((s.core.infinite?"Infinito":s.core.qubits+" qubits")+" · "+format(s.visual.objectCount)+" objetos\nGuardado automático de la sesión");}
    private static String format(long value){return String.format(Locale.US,"%,d",value).replace(',','.');}
    private void message(String text){if(!destroyed)Toast.makeText(this,text,Toast.LENGTH_LONG).show();}

    private final Runnable tick=new Runnable(){@Override public void run(){
        if(!animate||!resumed||tab>=2||surface==null||destroyed)return;long now=System.nanoTime();if(lastTick!=0)animationTime=Math.min(1_000_000f,animationTime+Math.min(.25f,(now-lastTick)/1e9f));lastTick=now;
        if(!animationFrameQueued){animationFrameQueued=true;final float seconds=animationTime;surface.queueEvent(()->{try{renderer.setTime(seconds);}catch(Exception error){showStatus(error.getMessage());}finally{animationFrameQueued=false;}});surface.requestRender();}
        if(animationTime>=1_000_000f){animate=false;playButton.setText("Animar");return;}main.postDelayed(this,33);
    }};

    private SavedSession readSession() throws Exception {
        synchronized(SESSION_IO){
            try(FileInputStream file=sessionFile.openRead()){
                if(file.getChannel().size()>CosmicProject.MAX_FILE_BYTES)throw new IllegalArgumentException("Sesión demasiado grande");
                try(ObjectInputStream input=new ObjectInputStream(file)){
                    Object value=input.readObject();if(!(value instanceof SavedSession))throw new IllegalArgumentException("Sesión privada inválida");return (SavedSession)value;
                }
            }catch(FileNotFoundException absent){return null;}
        }
    }
    /** Runs in the renderer queue; the saved core and visual state describe the same instant. */
    private void captureSession() throws Exception {
        if(sessionProtected)return;
        SavedSession snapshot=new SavedSession(projectName,core.snapshot(),renderer.snapshot(),tab,qubit,animate,addressA,addressB);savedSession=snapshot;
        final long serial=SESSION_SAVES.incrementAndGet();
        storage.execute(()->{
            if(savedSession!=snapshot||serial!=SESSION_SAVES.get())return;
            synchronized(SESSION_IO){
                if(savedSession!=snapshot||serial!=SESSION_SAVES.get())return;
                FileOutputStream file=null;
                try{file=sessionFile.startWrite();ObjectOutputStream output=new ObjectOutputStream(file);output.writeObject(snapshot);output.flush();sessionFile.finishWrite(file);}
                catch(Exception error){if(file!=null)sessionFile.failWrite(file);main.post(()->message("No se pudo guardar la sesión: "+error.getMessage()));}
            }
        });
    }
    /** A paused GLSurfaceView still executes its event queue; acknowledge capture explicitly. */
    private void captureAndPause(GLSurfaceView target,long deadline){
        CountDownLatch captured=new CountDownLatch(1);
        target.queueEvent(()->{try{if(target==surface&&!destroyed)captureSession();}catch(Exception error){showStatus("No se pudo guardar la sesión: "+error.getMessage());}finally{captured.countDown();}});
        try{captured.await(Math.max(0,deadline-System.nanoTime()),TimeUnit.NANOSECONDS);}
        catch(InterruptedException interrupted){Thread.currentThread().interrupt();}
        target.onPause();
    }
    private static void write(AtomicFile target,byte[] bytes) throws Exception{FileOutputStream output=null;try{output=target.startWrite();output.write(bytes);target.finishWrite(output);}catch(Exception error){if(output!=null)target.failWrite(output);throw error;}}

    private AtomicFile pendingExport(){return new AtomicFile(new File(getCacheDir(),"cosmic-pending-export.json"));}
    private void exportProject(){
        surface.queueEvent(()->{try{byte[] bytes=CosmicProject.encode(CosmicProject.fromSnapshots(projectName,core.snapshot(),renderer.snapshot()));storage.execute(()->{try{
            write(pendingExport(),bytes);main.post(()->{if(destroyed)return;Intent intent=new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/json").putExtra(Intent.EXTRA_TITLE,"COSMIC-X.cosmic");try{startActivityForResult(intent,SAVE_PROJECT);}catch(Exception error){message(error.getMessage());}});
        }catch(Exception error){main.post(()->message("No se pudo preparar el archivo: "+error.getMessage()));}});}catch(Exception error){main.post(()->message(error.getMessage()));}});
    }
    private void openProject(){try{startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"),OPEN_PROJECT);}catch(Exception error){message(error.getMessage());}}
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);if(result!=RESULT_OK||data==null||data.getData()==null)return;Uri uri=data.getData();
        if(request==OPEN_PROJECT){final long serial=++projectLoadSerial;storage.execute(()->{try(InputStream input=getContentResolver().openInputStream(uri)){prepareProject(CosmicProject.read(input),serial);}catch(Exception error){main.post(()->message("Tu proyecto se conserva. No se pudo abrir: "+error.getMessage()));}});}
        else if(request==SAVE_PROJECT)storage.execute(()->{try(InputStream staged=pendingExport().openRead()){byte[] bytes=CosmicProject.encode(CosmicProject.read(staged));try(OutputStream output=getContentResolver().openOutputStream(uri,"wt")){if(output==null)throw new IllegalArgumentException("Destino sin acceso");output.write(bytes);output.flush();}pendingExport().delete();main.post(()->message("Proyecto .cosmic guardado."));}catch(Exception error){main.post(()->message("No se pudo guardar: "+error.getMessage()));}});
    }
    private void loadAsset(String name){final long serial=++projectLoadSerial;storage.execute(()->{try(InputStream input=getAssets().open("projects/"+name)){prepareProject(CosmicProject.read(input),serial);}catch(Exception error){main.post(()->message(error.getMessage()));}});}
    private EditText field(String hint,String value,boolean numeric){EditText input=new EditText(this);input.setHint(hint);input.setText(value);input.setTextColor(TEXT);input.setTextSize(14);input.setSingleLine(true);input.setSelectAllOnFocus(true);input.setInputType(numeric?InputType.TYPE_CLASS_NUMBER:InputType.TYPE_CLASS_TEXT);return input;}
    private void newProject(){
        LinearLayout form=ui.column();form.setPadding(ui.dp(22),ui.dp(5),ui.dp(22),0);
        EditText name=field("Nombre","Mi nuevo mundo",false),qubits=field("Qubits",String.valueOf(NativeCore.DEFAULT_QUBITS),true),objects=field("Objetos","16384",true),seed=field("Semilla","2026",true);
        for(int j=0;j<4;j++){form.addView(ui.text(new String[]{"Nombre","Qubits · 1 a "+NativeCore.MAX_QUBITS,"Objetos · 1 a 1.048.576","Semilla · 0 a 4.294.967.295"}[j],11,MUTED,false));form.addView(new EditText[]{name,qubits,objects,seed}[j]);}
        InfiniteControls.ModeChoice mode=new InfiniteControls.ModeChoice(this,qubits);form.addView(mode.button,0);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Crear un mundo").setView(form).setNegativeButton("Cancelar",null).setPositiveButton("Crear",null).create();
        dialog.setOnShowListener(d->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{try{
            String title=name.getText().toString().trim();int n=mode.mode==0?Integer.parseInt(qubits.getText().toString()):0,count=Integer.parseInt(objects.getText().toString());long worldSeed=Long.parseLong(seed.getText().toString());
            if(title.isEmpty()||title.codePointCount(0,title.length())>128||(mode.mode==0&&(n<1||n>NativeCore.MAX_QUBITS))||count<1||count>QuantumRenderer.MAX_OBJECTS||worldSeed<0||worldSeed>0xffffffffL)throw new IllegalArgumentException("Revisa los rangos de tu proyecto.");
            if(mode.mode==0)createScene(title,n,count,worldSeed);else createInfiniteScene(title,mode.background(),count,worldSeed);dialog.dismiss();
        }catch(Exception error){message(error.getMessage());}}));dialog.show();
    }
    private void expandProject(){
        if(surface==null||expandingProject||core.isInfinite()||core.qubits>=NativeCore.DEFAULT_QUBITS)return;
        final long serial=++projectLoadSerial;final NativeCore source=core;
        expansionSerial=serial;expandingProject=true;expansionWasAnimating=animate;animate=false;main.removeCallbacks(tick);
        playButton.setText("Animar");
        expansionDialog=new AlertDialog.Builder(this).setTitle("Ampliando a "+NativeCore.DEFAULT_QUBITS+" qubits")
            .setMessage("Conservando el proyecto y añadiendo qubits en |0⟩…").setCancelable(false).show();
        surface.queueEvent(()->{try{
            if(source!=core){main.post(()->finishExpansion(serial,false));return;}
            CosmicProject.Document expanded=CosmicProject.expandQubits(projectName,source.snapshot(),renderer.snapshot(),NativeCore.DEFAULT_QUBITS);
            storage.execute(()->{try{prepareProject(expanded,serial);}
                catch(Exception error){main.post(()->{finishExpansion(serial,false);message("No se pudo ampliar el proyecto: "+error.getMessage());});}});
        }catch(Exception error){main.post(()->{finishExpansion(serial,false);message("No se pudo ampliar el proyecto: "+error.getMessage());});}});
        surface.requestRender();
    }
    private void finishExpansion(long serial,boolean installed){
        if(expansionSerial!=serial)return;
        expandingProject=false;expansionSerial=0;
        if(expansionDialog!=null){expansionDialog.dismiss();expansionDialog=null;}
        if(!installed&&expansionWasAnimating&&!destroyed&&!isFinishing()){
            animate=true;playButton.setText("Pausar");lastTick=0;if(resumed)main.post(tick);
        }
    }
    private void createScene(String name,int n,int count,long seed){final long serial=++projectLoadSerial;storage.execute(()->{try(NativeCore candidate=new NativeCore(n)){
        candidate.resetGraph();QuantumRenderer observer=new QuantumRenderer(this,candidate,s->{});observer.objectCount(count);observer.worldSeed(seed);observer.scenicView();observer.resolution(960);
        prepareProject(CosmicProject.fromSnapshots(name,candidate.snapshot(),observer.snapshot()),serial);
    }catch(Exception error){main.post(()->message("No se pudo crear: "+error.getMessage()));}});}
    private void createInfiniteScene(String name,int background,int count,long seed){final long serial=++projectLoadSerial;storage.execute(()->{
        try(NativeCore candidate=NativeCore.infinite(background)){
            QuantumRenderer observer=new QuantumRenderer(this,candidate,s->{});observer.objectCount(count);observer.worldSeed(seed);observer.scenicView();observer.resolution(960);
            prepareProject(CosmicProject.fromSnapshots(name,candidate.snapshot(),observer.snapshot()),serial);
        }catch(Exception error){main.post(()->message("No se pudo crear: "+error.getMessage()));}
    });}
    private void prepareProject(CosmicProject.Document project,long serial) throws Exception{
        NativeCore candidate=new NativeCore(project.core);
        try{candidate.query(project.graphics.observables);QuantumRenderer observer=new QuantumRenderer(this,candidate,this::showStatus,project.graphics);observer.setPresentation(QuantumRenderer.PRESENTATION_COSMIC);
            main.post(()->{if(destroyed||isFinishing()||serial!=projectLoadSerial){candidate.close();finishExpansion(serial,false);return;}
                finishExpansion(serial,true);installProject(project,candidate,observer);});
        }catch(Throwable error){candidate.close();throw error;}
    }
    private void installProject(CosmicProject.Document project,NativeCore candidate,QuantumRenderer observer){
        main.removeCallbacks(tick);animate=false;animationFrameQueued=false;GLSurfaceView previousSurface=surface;NativeCore previousCore=core;
        if(previousSurface!=null){previousSurface.onPause();if(previousSurface.getParent() instanceof ViewGroup)((ViewGroup)previousSurface.getParent()).removeView(previousSurface);}
        core=candidate;renderer=observer;projectName=project.name;coupled=project.graphics.coupling;qubit=0;animationTime=project.graphics.time;sessionProtected=false;tab=0;
        restoreAddresses(null,null,project.graphics.observables);
        savedSession=new SavedSession(project.name,project.core,project.graphics,0,0,false,addressA,addressB);
        buildScreen();if(resumed)surface.onResume();if(previousCore!=null)previousCore.close();surface.requestRender();
    }
    private File projectsDirectory(){File directory=new File(getFilesDir(),"projects");if(!directory.isDirectory()&&!directory.mkdirs())throw new IllegalStateException("No se pudo crear la carpeta de proyectos");return directory;}
    private void saveLocalCopy(){surface.queueEvent(()->{try{CosmicProject.Document document=CosmicProject.fromSnapshots(projectName,core.snapshot(),renderer.snapshot());storage.execute(()->{try{
        File target=new File(projectsDirectory(),"project-"+java.util.UUID.randomUUID()+".cosmic");write(new AtomicFile(target),CosmicProject.encode(document));main.post(()->{if(!destroyed){refreshSavedProjects();message("Copia guardada en tus proyectos.");}});
    }catch(Exception error){main.post(()->message(error.getMessage()));}});}catch(Exception error){main.post(()->message(error.getMessage()));}});}
    private void refreshSavedProjects(){
        if(projectList==null||destroyed)return;final LinearLayout target=projectList;target.removeAllViews();target.addView(ui.text("Leyendo proyectos…",12,MUTED,false));
        storage.execute(()->{try{File[] files=projectsDirectory().listFiles((dir,name)->name.endsWith(".cosmic"));if(files==null)files=new File[0];Arrays.sort(files,Comparator.comparingLong(File::lastModified).reversed());
            final File[] ordered=files;final String[] names=new String[files.length];for(int j=0;j<files.length;j++){try(InputStream input=new FileInputStream(files[j])){names[j]=CosmicProject.read(input).name;}catch(Exception error){names[j]="Proyecto no legible · "+files[j].getName();}}
            main.post(()->{if(destroyed||target!=projectList)return;target.removeAllViews();if(ordered.length==0){target.addView(ui.text("Tus copias aparecerán aquí. La sesión activa se guarda automáticamente.",12,MUTED,false));return;}
                for(int j=0;j<ordered.length;j++){final File file=ordered[j];target.addView(projectCard(names[j],"Abrir copia guardada","local_project_"+j,CYAN,()->{final long serial=++projectLoadSerial;storage.execute(()->{try(InputStream input=new FileInputStream(file)){prepareProject(CosmicProject.read(input),serial);}catch(Exception error){main.post(()->message(error.getMessage()));}});}),ui.full());target.addView(ui.space(8));}});
        }catch(Exception error){main.post(()->{if(!destroyed&&target==projectList){target.removeAllViews();target.addView(ui.text(error.getMessage(),12,GOLD,false));}});}});
    }

    private void showInfo(){
        QuantumRenderer.FrameStats s=renderer.frameStats();String details=s==null?statusText:String.format(Locale.US,"%,d objetos · %d × %d px\n%.1f ms por dibujo · %d observables\n%s",s.objectCount,s.width,s.height,s.drawMs,s.observables,s.device);
        new AlertDialog.Builder(this).setTitle("COSMIC X Engine · 0.3.1").setMessage("Computación cuántica de Mundo X\nNúcleo cuántico + motor gráfico cuántico integrado\n\n"+details+"\n\nLa escena observa el núcleo activo. Las puertas cambian ese estado; mover la cámara y elegir objetos lo conservan. Los valores del núcleo son expectativas de Pauli calculadas en software.\n\nTus 14 experimentos originales están dentro de esta app. Cada laboratorio conserva su modelo y controles.\n\nLos archivos .cosmic guardan las operaciones, observables, semilla, objetos y cámara. La nueva iluminación es una presentación del mismo motor.\n\nIdea, creación y dirección: X. Desarrollo sobre el código de Mundo X con asistencia de IA.").setPositiveButton("Continuar",null).show();
    }

    @Override protected void onResume(){super.onResume();resumed=true;if(surface!=null&&tab<2){surface.onResume();surface.requestRender();if(animate){lastTick=0;main.post(tick);}}}
    @Override protected void onPause(){
        resumed=false;main.removeCallbacks(tick);if(surface!=null){long deadline=System.nanoTime()+TimeUnit.MILLISECONDS.toNanos(1500);captureAndPause(surface,deadline);
            try{storage.submit(()->{}).get(Math.max(0,deadline-System.nanoTime()),TimeUnit.NANOSECONDS);}catch(Exception error){/* queued atomic save finishes on the storage executor */}}
        super.onPause();
    }
    @Override public Object onRetainNonConfigurationInstance(){return new Retained(savedSession,tab,qubit,animate);}
    @Override public void onBackPressed(){if(tab!=0){selectTab(0,true);return;}super.onBackPressed();}
    @Override protected void onDestroy(){
        // Prepared project callbacks must run their destroyed guard and close their candidate.
        destroyed=true;++projectLoadSerial;main.removeCallbacks(tick);
        if(expansionDialog!=null){expansionDialog.dismiss();expansionDialog=null;}expansionSerial=0;expandingProject=false;
        if(surface!=null){surface.onPause();if(surface.getParent() instanceof ViewGroup)((ViewGroup)surface.getParent()).removeView(surface);}
        storage.shutdown();if(core!=null)core.close();super.onDestroy();
    }
}
