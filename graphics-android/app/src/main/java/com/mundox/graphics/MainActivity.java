package com.mundox.graphics;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
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
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.Serializable;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Android host of the existing Graphics observer and its authoritative CompactCore. */
public final class MainActivity extends Activity {
    NativeCore core;
    QuantumRenderer renderer;
    GLSurfaceView surface;
    volatile boolean resumed;

    private static final int INK=0xff111a16, PANEL=0xff1c2922, LIME=0xffd6ed67;
    private static final int PAPER=0xffece6d7, MUTED=0xffa7b6a9;
    private static final Object SESSION_LOCK=new Object();
    private static final Object IMAGE_FILE_LOCK=new Object();
    private static final AtomicReference<String> IMAGE_EXPORT_OWNER=new AtomicReference<>();
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService storage=Executors.newSingleThreadExecutor();
    private AtomicFile sessionFile;
    private volatile SavedSession savedSession;
    private volatile boolean saveProtected;
    private TextView status;
    private Button animateButton, viewButton, couplingButton, qubitButton, qualityButton, styleButton;
    private boolean animate, overview=true, coupled=true;
    private int qubit, quality=640;
    private float animationTime;
    private long lastTick;
    private volatile boolean animationFrameQueued;
    private String restoreMessage;
    private volatile String projectName="Mundo X · mundo inicial";
    private long projectLoadSerial;
    private long expansionSerial;
    private boolean expandingProject, expansionWasAnimating;
    private AlertDialog expansionDialog;
    private static final int OPEN_PROJECT=701, SAVE_PROJECT=702, SAVE_IMAGE=703;
    private volatile boolean destroyed;
    private String imageExportToken;
    private boolean imageDestinationOpen;
    private boolean expandedViewport;
    private volatile QuantumRenderer.ExportControl activeImageExport;
    private AlertDialog imageProgressDialog;

    private static final class SavedSession implements Serializable {
        private static final long serialVersionUID=1L;
        NativeCore.Snapshot core;
        QuantumRenderer.Snapshot visual;
        boolean overview,coupled;
        int qubit,quality;
        float animationTime;
        String projectName;
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        expandedViewport=state!=null&&state.getBoolean("expandedViewport",false);
        if(state!=null&&state.getBoolean("imageDestinationOpen",false)){
            imageExportToken=state.getString("imageExportToken");
            imageDestinationOpen=imageExportToken!=null;
            if(imageDestinationOpen)IMAGE_EXPORT_OWNER.compareAndSet(null,imageExportToken);
        }
        sessionFile=new AtomicFile(new File(getFilesDir(),"graphics-session-v1.bin"));
        ActivityManager manager=(ActivityManager)getSystemService(Context.ACTIVITY_SERVICE);
        if(manager.getDeviceConfigurationInfo().reqGlEsVersion<0x30000){
            TextView text=label("COSMIC X Engine necesita OpenGL ES 3.0 en este dispositivo.",18,PAPER);
            text.setPadding(dp(24),dp(64),dp(24),dp(24));text.setBackgroundColor(INK);setContentView(text);return;
        }
        try {
            SavedSession previous=getLastNonConfigurationInstance() instanceof SavedSession
                ?(SavedSession)getLastNonConfigurationInstance():readSession();
            if(previous!=null){
                core=new NativeCore(previous.core);
                if(previous.projectName!=null)projectName=previous.projectName;
                overview=previous.visual.overview;coupled=previous.visual.coupling;
                qubit=Math.max(0,Math.min(core.qubits-1,previous.qubit));quality=previous.visual.resolutionLimit;
                animationTime=previous.visual.time;savedSession=previous;
                renderer=new QuantumRenderer(this,core,this::showStatus,previous.visual);
            }else{
                core=new NativeCore(NativeCore.DEFAULT_QUBITS);core.resetGraph();
                renderer=new QuantumRenderer(this,core,this::showStatus);
            }
        }catch(Throwable error){
            if(core!=null)core.close();
            saveProtected=true;restoreMessage="No se pudo recuperar la sesión. Tu archivo se conserva; Restablecer permite empezar otra.";
            try{core=new NativeCore(NativeCore.DEFAULT_QUBITS);core.resetGraph();renderer=new QuantumRenderer(this,core,this::showStatus);}
            catch(Throwable nativeError){
                TextView text=label("No se pudo iniciar COSMIC X Engine.\n"+nativeError,16,PAPER);
                text.setPadding(dp(24),dp(64),dp(24),dp(24));text.setBackgroundColor(INK);setContentView(text);return;
            }
        }
        applyPresentation();buildScreen();
    }

    private void applyPresentation(){
        int style=getSharedPreferences("graphics-ui",MODE_PRIVATE).getInt("presentation",QuantumRenderer.PRESENTATION_LEGACY);
        renderer.setPresentation(style==QuantumRenderer.PRESENTATION_COSMIC?style:QuantumRenderer.PRESENTATION_LEGACY);
    }

    private void buildScreen(){
        if(Build.VERSION.SDK_INT>=30)getWindow().setDecorFitsSystemWindows(false);
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setBackgroundColor(INK);
        root.setOnApplyWindowInsetsListener((v,insets)->{
            int l,t,r,b;
            if(Build.VERSION.SDK_INT>=30){android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());l=bars.left;t=bars.top;r=bars.right;b=bars.bottom;}
            else{l=insets.getSystemWindowInsetLeft();t=insets.getSystemWindowInsetTop();r=insets.getSystemWindowInsetRight();b=insets.getSystemWindowInsetBottom();}
            v.setPadding(l,t,r,b);return insets;
        });
        LinearLayout heading=new LinearLayout(this);heading.setGravity(Gravity.CENTER_VERTICAL);heading.setPadding(dp(16),dp(10),dp(12),dp(8));
        LinearLayout title=new LinearLayout(this);title.setOrientation(LinearLayout.VERTICAL);
        TextView name=label(getPackageName().endsWith(".ultra")?"COSMIC X ENGINE 8K":getPackageName().endsWith(".uhd")?"COSMIC X ENGINE 4K":"COSMIC X ENGINE",21,PAPER);name.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        TextView graphics=label("Computación cuántica",12,LIME);
        TextView project=label(projectName,10,MUTED);project.setSingleLine(true);project.setEllipsize(android.text.TextUtils.TruncateAt.END);
        title.addView(name);title.addView(graphics);title.addView(project);heading.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        TextView version=label(applicationVersion(),10,MUTED);version.setLetterSpacing(.07f);heading.addView(version);root.addView(heading);
        TextView visualLabel=label("MOTOR GRÁFICO CUÁNTICO",10,LIME);visualLabel.setLetterSpacing(.10f);visualLabel.setPadding(dp(16),0,0,dp(4));root.addView(visualLabel);

        FrameLayout viewport=new FrameLayout(this);viewport.setBackgroundColor(INK);
        surface=new GLSurfaceView(this);surface.setEGLContextClientVersion(3);
        surface.setEGLConfigChooser(8,8,8,8,24,0);surface.setPreserveEGLContextOnPause(true);
        surface.setRenderer(renderer);surface.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        viewport.addView(surface,new FrameLayout.LayoutParams(-1,-1));
        status=label("Preparando el mundo…",11,PAPER);status.setPadding(dp(12),dp(8),dp(12),dp(8));
        status.setBackgroundColor(0xdb111a16);status.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
        FrameLayout.LayoutParams statusLayout=new FrameLayout.LayoutParams(-1,-2,Gravity.TOP);viewport.addView(status,statusLayout);
        TextView hint=label("ARRASTRA · GIRA     PELLIZCA · ZOOM     TOCA · ELIGE",9,MUTED);
        hint.setGravity(Gravity.CENTER);hint.setPadding(dp(5),dp(6),dp(5),dp(6));hint.setBackgroundColor(0xdb111a16);
        viewport.addView(hint,new FrameLayout.LayoutParams(-1,-2,Gravity.BOTTOM));
        root.addView(viewport,new LinearLayout.LayoutParams(-1,0,1));
        installGestures();

        LinearLayout controls=new LinearLayout(this);controls.setOrientation(LinearLayout.VERTICAL);controls.setPadding(dp(8),dp(6),dp(8),dp(5));
        LinearLayout projects=row();
        projects.addView(button("Nuevo",this::newProject));
        projects.addView(button("Abrir .cosmic",this::openProject));
        projects.addView(button("Guardar",this::exportProject));
        if(core.qubits<NativeCore.DEFAULT_QUBITS){
            Button expand=button("Ampliar a "+NativeCore.DEFAULT_QUBITS+" · añadir |0⟩",this::expandProject);
            expand.setTag("project_expand_qubits");projects.addView(expand);
        }
        projects.addView(button("Guardar PNG",this::exportImage));
        projects.addView(button("PNG 8K",()->exportImage(QuantumRenderer.RESOLUTION_UHD_8K)));
        projects.addView(button("Ejemplos",()->new AlertDialog.Builder(this).setTitle("Proyectos del mismo núcleo y motor gráfico")
            .setItems(new String[]{"Observador Bell · 2 qubits","Ciudad GHZ · 3 qubits"},(dialog,index)->
                loadAssetProject(index==0?"bell-observer.cosmic":"ghz-city.cosmic")).show()));
        controls.addView(scroll(projects));
        LinearLayout navigation=row();
        viewButton=button(overview?"Vista: global":"Vista: calle",()->{
            overview=!overview;viewButton.setText(overview?"Vista: global":"Vista: calle");action(()->renderer.toggleView());
        });navigation.addView(viewButton);
        animateButton=button("Animar",()->{
            animate=!animate;animateButton.setText(animate?"Pausar":"Animar");lastTick=0;
            main.removeCallbacks(tick);if(animate&&resumed)main.post(tick);
        });navigation.addView(animateButton);
        qualityButton=button("Calidad "+quality,this::chooseQuality);navigation.addView(qualityButton);
        styleButton=button(renderer.presentation()==QuantumRenderer.PRESENTATION_COSMIC?"Estilo: cósmico":"Estilo: original",()->{
            final int style=renderer.presentation()==QuantumRenderer.PRESENTATION_COSMIC?QuantumRenderer.PRESENTATION_LEGACY:QuantumRenderer.PRESENTATION_COSMIC;
            getSharedPreferences("graphics-ui",MODE_PRIVATE).edit().putInt("presentation",style).apply();
            action(()->renderer.setPresentation(style));
        });navigation.addView(styleButton);
        navigation.addView(button("Objetos",()->new AlertDialog.Builder(this).setTitle("Objetos del mundo")
            .setItems(new String[]{"1.024","16.384","65.536","262.144","1.000.000","1.048.576"},(d,index)->{
                int count=new int[]{1024,16384,65536,262144,1000000,1048576}[index];action(()->renderer.objectCount(count));
            }).show()));
        controls.addView(scroll(navigation));

        TextView coreLabel=label("NÚCLEO CUÁNTICO",10,LIME);coreLabel.setLetterSpacing(.10f);coreLabel.setPadding(dp(6),dp(4),0,dp(2));controls.addView(coreLabel);
        LinearLayout quantum=row();
        qubitButton=button("Qubit "+qubit,()->{
            String[] names=new String[core.qubits];for(int q=0;q<names.length;q++)names[q]="Qubit "+q;
            new AlertDialog.Builder(this).setTitle("Qubit de control").setSingleChoiceItems(names,qubit,(dialog,which)->{
                qubit=which;qubitButton.setText("Qubit "+qubit);dialog.dismiss();action(()->{});
            }).show();
        });quantum.addView(qubitButton);
        quantum.addView(button("H",()->applyGate(0)));quantum.addView(button("S",()->applyGate(1)));
        quantum.addView(button("X",()->applyGate(2)));quantum.addView(button("Z",()->applyGate(3)));
        quantum.addView(button("Y",()->applyGate(6)));
        Button pairGate=button("CX / CZ",()->{
            String[] gates={"CX · destino Q"+((qubit+1)%core.qubits),"CZ · destino Q"+((qubit+1)%core.qubits)};
            new AlertDialog.Builder(this).setTitle("Conectar con el siguiente qubit").setItems(gates,(d,index)->{
                final int control=qubit,target=(qubit+1)%core.qubits;action(()->renderer.gate(index==0?4:5,control,target));
            }).show();
        });pairGate.setEnabled(core.qubits>1);pairGate.setAlpha(core.qubits>1?1f:.4f);quantum.addView(pairGate);
        couplingButton=button(coupled?"Acoplamiento: sí":"Acoplamiento: no",()->{
            coupled=!coupled;couplingButton.setText(coupled?"Acoplamiento: sí":"Acoplamiento: no");final boolean active=coupled;action(()->renderer.coupling(active));
        });quantum.addView(couplingButton);controls.addView(scroll(quantum));

        LinearLayout editing=row();
        editing.addView(button("Subir objeto",()->edit(1)));editing.addView(button("Ocultar",()->edit(-1)));
        editing.addView(button("Restaurar ID",()->edit(0)));
        editing.addView(button("Restablecer",()->new AlertDialog.Builder(this).setTitle("Restablecer el mundo")
            .setMessage("Se preparará el estado de grafo con los qubits actuales y se borrarán las ediciones de objetos.")
            .setNegativeButton("Cancelar",null).setPositiveButton("Restablecer",(d,w)->{
                animate=false;animateButton.setText("Animar");main.removeCallbacks(tick);animationTime=0;saveProtected=false;restoreMessage=null;
                action(()->renderer.reset());
            }).show()));
        editing.addView(button("Información",()->new AlertDialog.Builder(this).setTitle("COSMIC X Engine · "+applicationVersion())
            .setMessage("Computación cuántica de Mundo X\n\nNúcleo cuántico: el proyecto activo tiene "+core.qubits+" qubits emulados. Sus puertas modifican el estado compartido. El motor utiliza sus observables para representar la escena.\n\nLos archivos .cosmic reúnen el estado, sus operaciones, los observables originales, el mundo y la cámara. Puedes abrir el mismo proyecto en Android y en el visor de escritorio.\n\n8K UHD genera 7.680 × 4.320 píxeles, o 4.320 × 7.680 en vertical. Calidad → 8K activa el fotograma completo cuando el dispositivo puede alojarlo. PNG 8K permite guardar esa resolución por bloques desde cualquier calidad de vista. La preparación puede cancelarse y mantiene fija la escena.\n\nLa imagen se ajusta a la pantalla conservando sus proporciones. Se muestra la resolución realmente activa. El tiempo de envío de trabajo de CPU se distingue de una medición sincronizada de dibujo; ninguno representa los FPS sostenidos.\n\nIdea, creación y dirección del proyecto: X. Desarrollado con asistencia de IA sobre el código propio de Mundo X.")
            .setPositiveButton("Entendido",null).show()));controls.addView(scroll(editing));
        root.addView(controls);
        final View[] chrome={heading,visualLabel,controls};
        Button expand=button(expandedViewport?"Controles":"Expandir",()->{});
        expand.setContentDescription("Expandir la escena o mostrar sus controles");
        expand.setOnClickListener(v->{
            expandedViewport=!expandedViewport;
            for(View item:chrome)item.setVisibility(expandedViewport?View.GONE:View.VISIBLE);
            expand.setText(expandedViewport?"Controles":"Expandir");
        });
        FrameLayout.LayoutParams expandLayout=new FrameLayout.LayoutParams(-2,dp(40),Gravity.BOTTOM|Gravity.RIGHT);
        expandLayout.setMargins(0,0,dp(8),dp(28));viewport.addView(expand,expandLayout);
        for(View item:chrome)item.setVisibility(expandedViewport?View.GONE:View.VISIBLE);
        setContentView(root);root.requestApplyInsets();
        if(restoreMessage!=null)showStatus(restoreMessage);
    }

    private void chooseQuality(){
        final int[] limits={480,640,960,1280,1920};
        new AlertDialog.Builder(this).setTitle("Resolución de la imagen")
            .setItems(new String[]{"480 px · menor carga","640 px · equilibrada","960 px · más detalle","1.280 px · ajuste a pantalla","1.920 px · ajuste a pantalla","4K UHD · 8,3 millones de píxeles","8K UHD · 33,2 millones de píxeles"},(dialog,index)->{
                if(index==limits.length)action(()->renderer.uhd4k());
                else if(index==limits.length+1)action(()->renderer.uhd8k());
                else{quality=limits[index];final int requested=quality;action(()->renderer.resolution(requested));}
            }).show();
    }

    private void openProject(){
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
        try{startActivityForResult(intent,OPEN_PROJECT);}catch(Exception error){showStatus("No se pudo abrir el selector: "+error.getMessage());}
    }

    private void loadAssetProject(String file){
        final long serial=++projectLoadSerial;
        storage.execute(()->{
            try(InputStream input=getAssets().open("projects/"+file)){prepareProject(CosmicProject.read(input),serial);}
            catch(Exception error){showStatus("No se pudo abrir el proyecto: "+error.getMessage());}
        });
    }

    private void newProject(){
        LinearLayout form=new LinearLayout(this);form.setOrientation(LinearLayout.VERTICAL);form.setPadding(dp(18),dp(4),dp(18),0);
        EditText name=field(form,"Nombre","Mundo X · nuevo",false);
        EditText qubits=field(form,"Qubits del proyecto · 1 a "+NativeCore.MAX_QUBITS,String.valueOf(NativeCore.DEFAULT_QUBITS),true);
        EditText objects=field(form,"Objetos · 1 a 1.048.576","1000000",true);
        EditText seed=field(form,"Semilla · 0 a 4.294.967.295","2026",true);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Nuevo proyecto").setView(form)
            .setNegativeButton("Cancelar",null).setPositiveButton("Crear",null).create();
        dialog.setOnShowListener(ignored->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{
            try{
                final String title=name.getText().toString().trim();
                final int n=Integer.parseInt(qubits.getText().toString()),count=Integer.parseInt(objects.getText().toString());
                final long worldSeed=Long.parseLong(seed.getText().toString());
                if(n<1||n>NativeCore.MAX_QUBITS||count<1||count>QuantumRenderer.MAX_OBJECTS||worldSeed<0||worldSeed>0xffffffffL)
                    throw new IllegalArgumentException("Revisa los rangos indicados");
                final long serial=++projectLoadSerial;
                dialog.dismiss();
                storage.execute(()->{
                    try(NativeCore prepared=new NativeCore(n)){
                        prepared.resetGraph();QuantumRenderer observer=new QuantumRenderer(this,prepared,this::showStatus);
                        observer.objectCount(count);observer.worldSeed(worldSeed);
                        prepareProject(CosmicProject.fromSnapshots(title,prepared.snapshot(),observer.snapshot()),serial);
                    }catch(Exception error){showStatus("No se pudo crear el proyecto: "+error.getMessage());}
                });
            }catch(Exception error){Toast.makeText(this,error.getMessage(),Toast.LENGTH_LONG).show();}
        }));
        dialog.show();
    }

    private void expandProject(){
        if(surface==null||destroyed||expandingProject||activeImageExport!=null||core.qubits>=NativeCore.DEFAULT_QUBITS)return;
        final long serial=++projectLoadSerial;final NativeCore source=core;
        expansionSerial=serial;expandingProject=true;expansionWasAnimating=animate;animate=false;main.removeCallbacks(tick);
        animateButton.setText("Animar");
        expansionDialog=new AlertDialog.Builder(this).setTitle("Ampliando a "+NativeCore.DEFAULT_QUBITS+" qubits")
            .setMessage("Conservando el proyecto y añadiendo qubits en |0⟩…").setCancelable(false).show();
        surface.queueEvent(()->{
            try{
                if(source!=core){main.post(()->finishExpansion(serial,false));return;}
                CosmicProject.Document expanded=CosmicProject.expandQubits(projectName,source.snapshot(),renderer.snapshot(),NativeCore.DEFAULT_QUBITS);
                storage.execute(()->{try{prepareProject(expanded,serial);}
                    catch(Exception error){main.post(()->finishExpansion(serial,false));showStatus("No se pudo ampliar el proyecto: "+error.getMessage());}});
            }catch(Exception error){main.post(()->finishExpansion(serial,false));showStatus("No se pudo ampliar el proyecto: "+error.getMessage());}
        });
        surface.requestRender();
    }

    private void finishExpansion(long serial,boolean installed){
        if(expansionSerial!=serial)return;
        expandingProject=false;expansionSerial=0;
        if(expansionDialog!=null){expansionDialog.dismiss();expansionDialog=null;}
        if(!installed&&expansionWasAnimating&&!isFinishing()&&!isDestroyed()){
            animate=true;animateButton.setText("Pausar");lastTick=0;if(resumed)main.post(tick);
        }
    }

    private EditText field(LinearLayout form,String label,String value,boolean number){
        TextView caption=label(label,12,MUTED);form.addView(caption);
        EditText input=new EditText(this);input.setText(value);input.setTextColor(PAPER);input.setSingleLine(true);
        input.setInputType(number?android.text.InputType.TYPE_CLASS_NUMBER:android.text.InputType.TYPE_CLASS_TEXT);
        form.addView(input);return input;
    }

    private void prepareProject(CosmicProject.Document project,long serial) throws Exception{
        NativeCore candidate=new NativeCore(project.core);
        try{
            candidate.query(project.graphics.observables);
            QuantumRenderer observer=new QuantumRenderer(this,candidate,this::showStatus,project.graphics);
            main.post(()->{
                if(isFinishing()||isDestroyed()||serial!=projectLoadSerial){candidate.close();finishExpansion(serial,false);return;}
                finishExpansion(serial,true);
                installProject(project,candidate,observer);
            });
        }catch(Throwable error){candidate.close();throw error;}
    }

    /** Commit a fully validated candidate on the UI thread after the old GL thread pauses. */
    private void installProject(CosmicProject.Document project,NativeCore candidate,QuantumRenderer observer){
        cancelImagePreparation();
        if(!imageDestinationOpen&&imageExportToken!=null){IMAGE_EXPORT_OWNER.compareAndSet(imageExportToken,null);imageExportToken=null;}
        main.removeCallbacks(tick);animate=false;animationFrameQueued=false;
        GLSurfaceView previousSurface=surface;NativeCore previousCore=core;
        if(previousSurface!=null){
            previousSurface.onPause();
            // Detach joins the previous GL thread before its borrowed core is replaced.
            if(previousSurface.getParent() instanceof ViewGroup)
                ((ViewGroup)previousSurface.getParent()).removeView(previousSurface);
        }
        core=candidate;renderer=observer;applyPresentation();projectName=project.name;overview=project.graphics.overview;
        coupled=project.graphics.coupling;quality=project.graphics.resolutionLimit;qubit=0;animationTime=project.graphics.time;
        saveProtected=false;restoreMessage=null;
        SavedSession current=new SavedSession();current.core=project.core;current.visual=project.graphics;
        current.projectName=project.name;current.qubit=0;current.quality=quality;current.animationTime=animationTime;
        current.overview=overview;current.coupled=coupled;savedSession=current;
        buildScreen();if(resumed)surface.onResume();
        if(previousCore!=null)previousCore.close();
        surface.queueEvent(this::captureSession);surface.requestRender();
    }

    /** Instrumentation uses the same candidate validation and UI commit as file imports. */
    void installProjectForTest(CosmicProject.Document project) throws Exception{
        NativeCore candidate=new NativeCore(project.core);
        try{
            candidate.query(project.graphics.observables);
            QuantumRenderer observer=new QuantumRenderer(this,candidate,this::showStatus,project.graphics);
            ++projectLoadSerial;installProject(project,candidate,observer);
        }catch(Throwable error){if(core!=candidate)candidate.close();throw error;}
    }

    private AtomicFile pendingExportFile(){return new AtomicFile(new File(getCacheDir(),"cosmic-export-pending.json"));}
    private AtomicFile pendingImageFile(){return new AtomicFile(new File(getCacheDir(),"cosmic-image-pending.png"));}

    /** Stage a streamed native-resolution PNG before opening Android's destination picker. */
    private void exportImage(){exportImage(QuantumRenderer.RESOLUTION_ADAPTIVE);}

    private void exportImage(int mode){
        if(surface==null||destroyed||expandingProject)return;
        if(renderer.failure!=null){showStatus("La escena necesita recuperarse antes de guardar una imagen");return;}
        final String token=UUID.randomUUID().toString();
        if(!IMAGE_EXPORT_OWNER.compareAndSet(null,token)){
            Toast.makeText(this,"Hay una imagen en preparación o pendiente de guardar",Toast.LENGTH_SHORT).show();return;
        }
        imageExportToken=token;imageDestinationOpen=false;
        final QuantumRenderer source=renderer;
        animate=false;main.removeCallbacks(tick);lastTick=0;
        if(animateButton!=null)animateButton.setText("Animar");
        final int[] previousPercent={-1};
        final QuantumRenderer.ExportControl control=new QuantumRenderer.ExportControl(1024,64,(rows,total)->{
            final int percent=(int)(100L*rows/total);
            if(percent==previousPercent[0])return;previousPercent[0]=percent;
            main.post(()->{
                if(!destroyed&&token.equals(IMAGE_EXPORT_OWNER.get())&&imageProgressDialog!=null)
                    imageProgressDialog.setMessage(percent+" % · La escena permanece fija durante la captura.");
            });
        });
        activeImageExport=control;
        imageProgressDialog=new AlertDialog.Builder(this)
            .setTitle(mode==QuantumRenderer.RESOLUTION_UHD_8K?"Preparando PNG 8K":"Preparando PNG")
            .setMessage("0 % · La escena permanece fija durante la captura.")
            .setNegativeButton("Cancelar",(dialog,which)->control.cancel()).create();
        imageProgressDialog.setOnCancelListener(dialog->control.cancel());
        imageProgressDialog.setCanceledOnTouchOutside(false);imageProgressDialog.show();
        showStatus("Preparando una imagen a resolución completa…");
        surface.queueEvent(()->{
            stageImage(source,mode,control,token);
        });surface.requestRender();
    }

    /** Runs on the renderer's GL thread; working memory never contains the full 8K image. */
    private void stageImage(QuantumRenderer source,int mode,QuantumRenderer.ExportControl control,String token){
        AtomicFile staged=pendingImageFile();FileOutputStream output=null;
        try{
            if(destroyed||renderer!=source||control.isCancelled())throw new java.io.IOException("Captura cancelada");
            final QuantumRenderer.ExportResult image;
            synchronized(IMAGE_FILE_LOCK){
                if(!token.equals(IMAGE_EXPORT_OWNER.get()))return;
                output=staged.startWrite();
                try{
                    image=source.exportPng(output,mode,control);
                    if(control.isCancelled())throw new java.io.IOException("Captura cancelada");
                    staged.finishWrite(output);output=null;
                }
                catch(Exception|OutOfMemoryError error){staged.failWrite(output);output=null;throw error;}
            }
            final String filename="COSMIC-X-"+image.width+"x"+image.height+".png";
            main.post(()->{
                if(destroyed||isFinishing()||!resumed||renderer!=source||control.isCancelled()
                        ||!token.equals(IMAGE_EXPORT_OWNER.get())){
                    IMAGE_EXPORT_OWNER.compareAndSet(token,null);
                    if(token.equals(imageExportToken))dismissImageProgress();
                    return;
                }
                dismissImageProgress();
                Intent save=new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                    .setType("image/png").putExtra(Intent.EXTRA_TITLE,filename);
                try{imageDestinationOpen=true;startActivityForResult(save,SAVE_IMAGE);}
                catch(Exception error){imageDestinationOpen=false;IMAGE_EXPORT_OWNER.compareAndSet(token,null);showStatus("No se pudo abrir el destino: "+error.getMessage());}
            });
        }catch(Exception|OutOfMemoryError error){
            if(output!=null)staged.failWrite(output);
            IMAGE_EXPORT_OWNER.compareAndSet(token,null);
            showStatus(control.isCancelled()?"Captura cancelada":"No se pudo preparar PNG: "+error.getMessage());
        }finally{
            if(activeImageExport==control)activeImageExport=null;
            main.post(()->{
                if(activeImageExport==null)dismissImageProgress();
            });
        }
    }

    private void dismissImageProgress(){
        if(imageProgressDialog!=null){imageProgressDialog.dismiss();imageProgressDialog=null;}
    }
    private void cancelImagePreparation(){
        QuantumRenderer.ExportControl control=activeImageExport;
        if(control!=null)control.cancel();
        dismissImageProgress();
    }
    private void exportProject(){
        if(surface==null)return;
        surface.queueEvent(()->{
            try{
                final byte[] bytes=CosmicProject.encode(CosmicProject.fromSnapshots(projectName,core.snapshot(),renderer.snapshot()));
                storage.execute(()->{
                    AtomicFile staged=pendingExportFile();FileOutputStream output=null;
                    try{
                        output=staged.startWrite();output.write(bytes);staged.finishWrite(output);
                        main.post(()->{
                            if(isFinishing()||isDestroyed())return;
                            Intent save=new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                                .setType("application/json").putExtra(Intent.EXTRA_TITLE,"Mundo-X.cosmic");
                            try{startActivityForResult(save,SAVE_PROJECT);}
                            catch(Exception error){showStatus("No se pudo abrir el destino: "+error.getMessage());}
                        });
                    }catch(Exception error){if(output!=null)staged.failWrite(output);showStatus("No se pudo preparar el proyecto: "+error.getMessage());}
                });
            }catch(Exception error){showStatus("No se pudo preparar el proyecto: "+error.getMessage());}
        });
    }

    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);
        if(request==SAVE_IMAGE){
            imageDestinationOpen=false;
            final String token=imageExportToken;imageExportToken=null;
            if(result!=RESULT_OK||data==null||data.getData()==null){
                synchronized(IMAGE_FILE_LOCK){
                    if(token!=null&&token.equals(IMAGE_EXPORT_OWNER.get())){pendingImageFile().delete();IMAGE_EXPORT_OWNER.compareAndSet(token,null);}
                }return;
            }
            final Uri imageUri=data.getData();
            storage.execute(()->{
                try{
                    synchronized(IMAGE_FILE_LOCK){
                        if(token==null||!token.equals(IMAGE_EXPORT_OWNER.get()))throw new IllegalStateException("La captura pendiente ya cambió");
                        try(InputStream staged=pendingImageFile().openRead();OutputStream output=getContentResolver().openOutputStream(imageUri,"wt")){
                            if(output==null)throw new IllegalArgumentException("Destino sin acceso");
                            byte[] chunk=new byte[64*1024];int count;
                            while((count=staged.read(chunk))!=-1)output.write(chunk,0,count);
                            output.flush();
                        }
                        // A provider may fail while closing: retain the complete staged PNG then.
                        pendingImageFile().delete();
                    }
                    showStatus("Imagen PNG guardada a su resolución completa");
                }catch(Exception error){showStatus("No se pudo guardar la imagen: "+error.getMessage());}
                finally{if(token!=null)IMAGE_EXPORT_OWNER.compareAndSet(token,null);}
            });return;
        }
        if(result!=RESULT_OK||data==null||data.getData()==null)return;
        Uri uri=data.getData();
        if(request==OPEN_PROJECT){
            final long serial=++projectLoadSerial;
            storage.execute(()->{
                try(InputStream input=getContentResolver().openInputStream(uri)){
                    if(input==null)throw new IllegalArgumentException("Archivo sin contenido");
                    prepareProject(CosmicProject.read(input),serial);
                }catch(Exception error){showStatus("El proyecto actual se conserva. No se pudo importar: "+error.getMessage());}
            });
        }else if(request==SAVE_PROJECT){
            storage.execute(()->{
                try(InputStream staged=pendingExportFile().openRead()){
                    byte[] bytes=CosmicProject.encode(CosmicProject.read(staged));
                    try(OutputStream output=getContentResolver().openOutputStream(uri,"wt")){
                        if(output==null)throw new IllegalArgumentException("Destino sin acceso");
                        output.write(bytes);output.flush();
                    }
                    pendingExportFile().delete();showStatus("Proyecto .cosmic guardado");
                }catch(Exception error){showStatus("No se pudo exportar el proyecto: "+error.getMessage());}
            });
        }
    }

    private void installGestures(){
        ScaleGestureDetector scale=new ScaleGestureDetector(this,new ScaleGestureDetector.SimpleOnScaleGestureListener(){
            @Override public boolean onScale(ScaleGestureDetector detector){final float factor=detector.getScaleFactor();action(()->renderer.zoom(factor));return true;}
        });
        GestureDetector gestures=new GestureDetector(this,new GestureDetector.SimpleOnGestureListener(){
            @Override public boolean onDown(MotionEvent e){return true;}
            @Override public boolean onSingleTapUp(MotionEvent e){final int x=(int)e.getX(),y=(int)e.getY();action(()->renderer.pick(x,y));surface.performClick();return true;}
            @Override public boolean onScroll(MotionEvent first,MotionEvent next,float dx,float dy){
                if(!scale.isInProgress()&&next.getPointerCount()==1)action(()->renderer.orbit(-dx,-dy));return true;
            }
        });
        surface.setOnTouchListener((view,event)->{scale.onTouchEvent(event);gestures.onTouchEvent(event);return true;});
        surface.setContentDescription("Mundo tridimensional: arrastra para girar, pellizca para acercar y toca para seleccionar un objeto");
    }

    private void applyGate(int operation){final int q=qubit;action(()->renderer.gate(operation,q,0));}
    private void edit(int operation){action(()->{
        if(renderer.selectedId()<0){main.post(()->Toast.makeText(this,"Toca primero una caja del mundo",Toast.LENGTH_SHORT).show());return;}
        renderer.editSelected(operation);
    });}
    private void action(Runnable change){
        if(surface==null||destroyed||expandingProject)return;
        surface.queueEvent(()->{
            if(destroyed)return;
            try{change.run();captureSession();}catch(Throwable error){showStatus("No se pudo aplicar el cambio: "+error.getMessage());}
        });surface.requestRender();
    }
    private void showStatus(String message){main.post(()->{
        if(destroyed||status==null)return;
        status.setText(restoreMessage==null?message:restoreMessage+"\n"+message);
        final QuantumRenderer observed=renderer;QuantumRenderer.FrameStats frame=observed.frameStats();
        if(qualityButton!=null&&frame!=null){
            quality=observed.resolution();qualityButton.setText(observed.isUhd8k()?"Calidad: 8K UHD":observed.isUhd4k()?"Calidad: 4K UHD":"Calidad "+quality);
            if(savedSession!=null&&savedSession.visual.resolutionMode!=frame.resolutionMode&&surface!=null){
                surface.queueEvent(()->{if(!destroyed&&renderer==observed)captureSession();});
            }
        }
        if(styleButton!=null)styleButton.setText(observed.presentation()==QuantumRenderer.PRESENTATION_COSMIC?"Estilo: cósmico":"Estilo: original");
    });}

    private final Runnable tick=new Runnable(){
        @Override public void run(){
            if(!animate||!resumed||surface==null)return;
            long now=System.nanoTime();if(lastTick!=0)animationTime=Math.min(1000000f,animationTime+Math.min(.25f,(now-lastTick)/1e9f));lastTick=now;
            if(!animationFrameQueued){
                animationFrameQueued=true;final float seconds=animationTime;
                surface.queueEvent(()->{
                    try{renderer.setTime(seconds);}
                    catch(Exception error){showStatus("No se pudo avanzar el tiempo: "+error.getMessage());}
                    finally{animationFrameQueued=false;}
                });surface.requestRender();
            }
            if(animationTime>=1000000f){animate=false;animateButton.setText("Animar");return;}
            main.postDelayed(this,33);
        }
    };

    private SavedSession readSession() throws Exception {
        synchronized(SESSION_LOCK){
            FileInputStream file;
            try{file=sessionFile.openRead();}catch(FileNotFoundException absent){return null;}
            try(ObjectInputStream input=new ObjectInputStream(file)){
                Object value=input.readObject();if(!(value instanceof SavedSession))throw new IllegalStateException("Formato de sesión inválido");return (SavedSession)value;
            }
        }
    }
    /** Called only on the GL thread so core and visual state describe one point in time. */
    private void captureSession(){
        if(saveProtected||destroyed)return;
        SavedSession snapshot=new SavedSession();snapshot.core=core.snapshot();snapshot.visual=renderer.snapshot();
        snapshot.projectName=projectName;
        snapshot.overview=snapshot.visual.overview;snapshot.coupled=snapshot.visual.coupling;
        snapshot.qubit=qubit;snapshot.quality=snapshot.visual.resolutionLimit;snapshot.animationTime=snapshot.visual.time;
        savedSession=snapshot;
        storage.execute(()->{
            if(savedSession!=snapshot)return;
            synchronized(SESSION_LOCK){
                FileOutputStream file=null;
                try{file=sessionFile.startWrite();ObjectOutputStream output=new ObjectOutputStream(file);output.writeObject(snapshot);output.flush();sessionFile.finishWrite(file);}
                catch(Exception error){if(file!=null)sessionFile.failWrite(file);showStatus("No se pudo guardar la sesión: "+error.getMessage());}
            }
        });
    }

    @Override protected void onResume(){super.onResume();resumed=true;if(surface!=null){surface.onResume();surface.requestRender();if(animate){lastTick=0;main.post(tick);}}}
    @Override protected void onPause(){
        resumed=false;main.removeCallbacks(tick);
        cancelImagePreparation();
        if(surface!=null){
            surface.queueEvent(()->{try{captureSession();}catch(Throwable error){showStatus("No se pudo guardar el estado: "+error.getMessage());}});surface.onPause();
            try{storage.submit(()->{}).get(1500,TimeUnit.MILLISECONDS);}
            catch(Exception error){showStatus("El último guardado sigue pendiente: "+error.getMessage());}
        }
        super.onPause();
    }
    @Override public Object onRetainNonConfigurationInstance(){return savedSession;}
    @Override protected void onSaveInstanceState(Bundle state){
        state.putBoolean("expandedViewport",expandedViewport);
        if(imageDestinationOpen&&imageExportToken!=null){state.putBoolean("imageDestinationOpen",true);state.putString("imageExportToken",imageExportToken);}
        super.onSaveInstanceState(state);
    }
    @Override protected void onDestroy(){
        destroyed=true;
        cancelImagePreparation();
        if((isFinishing()||!imageDestinationOpen)&&imageExportToken!=null)
            IMAGE_EXPORT_OWNER.compareAndSet(imageExportToken,null);
        main.removeCallbacks(tick);if(expansionDialog!=null){expansionDialog.dismiss();expansionDialog=null;}
        expansionSerial=0;expandingProject=false;storage.shutdown();if(core!=null)core.close();super.onDestroy();
    }

    private String applicationVersion(){
        try{return getPackageManager().getPackageInfo(getPackageName(),0).versionName;}
        catch(android.content.pm.PackageManager.NameNotFoundException error){return "";}
    }
    private int dp(float value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private TextView label(String text,float size,int color){TextView view=new TextView(this);view.setText(text);view.setTextSize(size);view.setTextColor(color);return view;}
    private LinearLayout row(){LinearLayout row=new LinearLayout(this);row.setGravity(Gravity.CENTER_VERTICAL);return row;}
    private HorizontalScrollView scroll(LinearLayout row){HorizontalScrollView view=new HorizontalScrollView(this);view.setHorizontalScrollBarEnabled(false);view.addView(row);return view;}
    private Button button(String name,Runnable action){
        Button button=new Button(this);button.setText(name);button.setAllCaps(false);button.setTextSize(12);button.setTextColor(PAPER);
        button.setMinWidth(dp(43));button.setMinimumWidth(dp(43));button.setMinHeight(dp(43));button.setMinimumHeight(dp(43));button.setPadding(dp(12),dp(4),dp(12),dp(4));
        GradientDrawable background=new GradientDrawable();background.setColor(PANEL);background.setCornerRadius(dp(9));background.setStroke(dp(1),0xff314236);button.setBackground(background);
        LinearLayout.LayoutParams layout=new LinearLayout.LayoutParams(-2,dp(43));layout.setMargins(dp(3),dp(3),dp(3),dp(3));button.setLayoutParams(layout);
        button.setOnClickListener(v->action.run());return button;
    }
}
