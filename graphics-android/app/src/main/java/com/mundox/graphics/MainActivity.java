package com.mundox.graphics;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.Context;
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
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.Serializable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

/** Android host of the existing Graphics observer and its authoritative CompactCore. */
public final class MainActivity extends Activity {
    NativeCore core;
    QuantumRenderer renderer;
    GLSurfaceView surface;
    volatile boolean resumed;

    private static final int INK=0xff111a16, PANEL=0xff1c2922, LIME=0xffd6ed67;
    private static final int PAPER=0xffece6d7, MUTED=0xffa7b6a9;
    private static final Object SESSION_LOCK=new Object();
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService storage=Executors.newSingleThreadExecutor();
    private AtomicFile sessionFile;
    private volatile SavedSession savedSession;
    private volatile boolean saveProtected;
    private TextView status;
    private Button animateButton, viewButton, couplingButton, qubitButton, qualityButton;
    private boolean animate, overview=true, coupled=true;
    private int qubit, quality=640;
    private float animationTime;
    private long lastTick;
    private volatile boolean animationFrameQueued;
    private String restoreMessage;

    private static final class SavedSession implements Serializable {
        private static final long serialVersionUID=1L;
        NativeCore.Snapshot core;
        QuantumRenderer.Snapshot visual;
        boolean overview,coupled;
        int qubit,quality;
        float animationTime;
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        sessionFile=new AtomicFile(new File(getFilesDir(),"graphics-session-v1.bin"));
        ActivityManager manager=(ActivityManager)getSystemService(Context.ACTIVITY_SERVICE);
        if(manager.getDeviceConfigurationInfo().reqGlEsVersion<0x30000){
            TextView text=label("Mundo X Graphics necesita OpenGL ES 3.0 en este dispositivo.",18,PAPER);
            text.setPadding(dp(24),dp(64),dp(24),dp(24));text.setBackgroundColor(INK);setContentView(text);return;
        }
        try {
            SavedSession previous=getLastNonConfigurationInstance() instanceof SavedSession
                ?(SavedSession)getLastNonConfigurationInstance():readSession();
            if(previous!=null){
                core=new NativeCore(previous.core);
                overview=previous.visual.overview;coupled=previous.visual.coupling;
                qubit=Math.max(0,Math.min(core.qubits-1,previous.qubit));quality=previous.visual.resolutionLimit;
                animationTime=previous.visual.time;savedSession=previous;
                renderer=new QuantumRenderer(this,core,this::showStatus,previous.visual);
            }else{
                core=new NativeCore(20);core.resetGraph();
                renderer=new QuantumRenderer(this,core,this::showStatus);
            }
        }catch(Throwable error){
            if(core!=null)core.close();
            saveProtected=true;restoreMessage="No se pudo recuperar la sesión. Tu archivo se conserva; Restablecer permite empezar otra.";
            try{core=new NativeCore(20);core.resetGraph();renderer=new QuantumRenderer(this,core,this::showStatus);}
            catch(Throwable nativeError){
                TextView text=label("No se pudo iniciar Mundo X Graphics.\n"+nativeError,16,PAPER);
                text.setPadding(dp(24),dp(64),dp(24),dp(24));text.setBackgroundColor(INK);setContentView(text);return;
            }
        }
        buildScreen();
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
        TextView name=label("MUNDO X",11,LIME);name.setLetterSpacing(.22f);
        TextView graphics=label("Graphics",25,PAPER);graphics.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        title.addView(name);title.addView(graphics);heading.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        TextView version=label("0.1.1  /  ANDROID",10,MUTED);version.setLetterSpacing(.07f);heading.addView(version);root.addView(heading);

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
        LinearLayout navigation=row();
        viewButton=button(overview?"Vista: global":"Vista: calle",()->{
            overview=!overview;viewButton.setText(overview?"Vista: global":"Vista: calle");action(()->renderer.toggleView());
        });navigation.addView(viewButton);
        animateButton=button("Animar",()->{
            animate=!animate;animateButton.setText(animate?"Pausar":"Animar");lastTick=0;
            main.removeCallbacks(tick);if(animate&&resumed)main.post(tick);
        });navigation.addView(animateButton);
        qualityButton=button("Calidad "+quality,()->new AlertDialog.Builder(this).setTitle("Resolución de observación")
            .setItems(new String[]{"480 px · menor carga","640 px · equilibrada","960 px · más detalle"},(d,index)->{
                quality=new int[]{480,640,960}[index];qualityButton.setText("Calidad "+quality);action(()->renderer.resolution(quality));
            }).show());navigation.addView(qualityButton);
        navigation.addView(button("Objetos",()->new AlertDialog.Builder(this).setTitle("Objetos del mundo")
            .setItems(new String[]{"1.024","16.384","65.536","262.144","1.000.000","1.048.576"},(d,index)->{
                int count=new int[]{1024,16384,65536,262144,1000000,1048576}[index];action(()->renderer.objectCount(count));
            }).show()));
        controls.addView(scroll(navigation));

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
        quantum.addView(button("CX / CZ",()->{
            String[] gates={"CX · destino Q"+((qubit+1)%core.qubits),"CZ · destino Q"+((qubit+1)%core.qubits)};
            new AlertDialog.Builder(this).setTitle("Conectar con el siguiente qubit").setItems(gates,(d,index)->{
                final int control=qubit,target=(qubit+1)%core.qubits;action(()->renderer.gate(index==0?4:5,control,target));
            }).show();
        }));
        couplingButton=button(coupled?"Acoplamiento: sí":"Acoplamiento: no",()->{
            coupled=!coupled;couplingButton.setText(coupled?"Acoplamiento: sí":"Acoplamiento: no");final boolean active=coupled;action(()->renderer.coupling(active));
        });quantum.addView(couplingButton);controls.addView(scroll(quantum));

        LinearLayout editing=row();
        editing.addView(button("Subir objeto",()->edit(1)));editing.addView(button("Ocultar",()->edit(-1)));
        editing.addView(button("Restaurar ID",()->edit(0)));
        editing.addView(button("Restablecer",()->new AlertDialog.Builder(this).setTitle("Restablecer el mundo")
            .setMessage("Se restaurará el estado inicial del núcleo y se borrarán las ediciones de objetos.")
            .setNegativeButton("Cancelar",null).setPositiveButton("Restablecer",(d,w)->{
                animate=false;animateButton.setText("Animar");main.removeCallbacks(tick);animationTime=0;saveProtected=false;restoreMessage=null;
                action(()->renderer.reset());
            }).show()));
        editing.addView(button("Información",()->new AlertDialog.Builder(this).setTitle("Mundo X Graphics 0.1.1")
            .setMessage("El núcleo emulado de 20 qubits aporta observables a la geometría y al color. Las puertas modifican ese mismo estado.\n\nEl mundo contiene hasta 1.048.576 cajas con identidad estable. Toca una caja para editarla. Cámara, puertas y ediciones se conservan al cerrar la app.\n\nLa calidad cambia la resolución de la imagen; el número de objetos se elige por separado. Los milisegundos muestran el dibujo sincronizado de ese fotograma, no los FPS completos.\n\nAutor de la idea y dirección: X.")
            .setPositiveButton("Entendido",null).show()));controls.addView(scroll(editing));
        root.addView(controls);setContentView(root);root.requestApplyInsets();
        if(restoreMessage!=null)showStatus(restoreMessage);
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
        if(surface==null)return;
        surface.queueEvent(()->{
            try{change.run();captureSession();}catch(Throwable error){showStatus("No se pudo aplicar el cambio: "+error.getMessage());}
        });surface.requestRender();
    }
    private void showStatus(String message){main.post(()->{if(status!=null&&!isDestroyed())status.setText(restoreMessage==null?message:restoreMessage+"\n"+message);});}

    private final Runnable tick=new Runnable(){
        @Override public void run(){
            if(!animate||!resumed||surface==null)return;
            long now=System.nanoTime();if(lastTick!=0)animationTime+=Math.min(.25f,(now-lastTick)/1e9f);lastTick=now;
            if(!animationFrameQueued){
                animationFrameQueued=true;final float seconds=animationTime;
                surface.queueEvent(()->{renderer.setTime(seconds);animationFrameQueued=false;});surface.requestRender();
            }
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
        if(saveProtected)return;
        SavedSession snapshot=new SavedSession();snapshot.core=core.snapshot();snapshot.visual=renderer.snapshot();
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
        if(surface!=null){
            surface.queueEvent(()->{try{captureSession();}catch(Throwable error){showStatus("No se pudo guardar el estado: "+error.getMessage());}});surface.onPause();
            try{storage.submit(()->{}).get(1500,TimeUnit.MILLISECONDS);}
            catch(Exception error){showStatus("El último guardado sigue pendiente: "+error.getMessage());}
        }
        super.onPause();
    }
    @Override public Object onRetainNonConfigurationInstance(){return savedSession;}
    @Override protected void onDestroy(){
        main.removeCallbacks(tick);storage.shutdown();if(core!=null)core.close();super.onDestroy();
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
