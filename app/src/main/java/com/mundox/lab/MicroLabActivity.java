package com.mundox.lab;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import org.json.JSONObject;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/** Touch-first laboratory backed by native models and an actual four-qubit state. */
public final class MicroLabActivity extends Activity implements MicroLabRenderer.Observer {
    private static final int INK=0xff081216, PANEL=0xff112328, TEXT=0xffe6eee5, MUTED=0xff9bb1b6, LIME=0xffbfe87b;
    private final Handler ui=new Handler(Looper.getMainLooper());
    private final AtomicBoolean closed=new AtomicBoolean();
    private long handle;
    private GLSurfaceView surface;
    private MicroLabRenderer renderer;
    private MicroStateView stateView;
    private TextView metric, detail, hint, quantumLabel;
    private Button play, coupling, seedButton, brushButton, countButton;
    private final Button[] modes=new Button[3], targets=new Button[4];
    private LinearLayout particleControls, colonyControls;
    private int mode, target, brush, countIndex=1, seed=20261018;
    private boolean playing=true, foreground, coupled=true;
    private JSONObject latest=new JSONObject();
    volatile String latestSnapshot="{}";
    volatile String lastError="";
    private final Runnable pulse=new Runnable(){ public void run(){
        if(foreground&&!closed.get()&&playing){surface.requestRender();ui.postDelayed(this,33);}
    }};

    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);
        getWindow().setStatusBarColor(INK);getWindow().setNavigationBarColor(INK);
        handle=NativeCore.microCreate(seed);
        renderer=new MicroLabRenderer(handle,closed,this);
        LinearLayout root=column();root.setBackgroundColor(INK);root.setPadding(dp(16),dp(8),dp(16),dp(8));
        root.setOnApplyWindowInsetsListener((v,i)->{v.setPadding(dp(16)+i.getSystemWindowInsetLeft(),dp(8)+i.getSystemWindowInsetTop(),
                dp(16)+i.getSystemWindowInsetRight(),dp(8)+i.getSystemWindowInsetBottom());return i.consumeSystemWindowInsets();});
        LinearLayout header=row();
        TextView brand=text("MUNDO X  /  MICRO",16,LIME,true);header.addView(brand,weight());
        Button model=button("Modelo", "micro_model");model.setOnClickListener(v->showModel());header.addView(model);
        root.addView(header,new LinearLayout.LayoutParams(-1,dp(42)));
        LinearLayout tabs=row();String[] names={"Partículas","Colonia","Estado"};
        for(int i=0;i<3;i++){final int index=i;modes[i]=button(names[i],"micro_mode_"+i);modes[i].setOnClickListener(v->selectMode(index));tabs.addView(modes[i],weight());}
        root.addView(tabs,new LinearLayout.LayoutParams(-1,dp(44)));

        FrameLayout scene=new FrameLayout(this);scene.setBackgroundColor(PANEL);
        surface=new GLSurfaceView(this);surface.setTag("micro_surface");surface.setContentDescription("Micromundo: arrastra para girar, pellizca para acercar, toca para intervenir");
        surface.setEGLContextClientVersion(3);surface.setPreserveEGLContextOnPause(true);surface.setRenderer(renderer);surface.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        scene.addView(surface,new FrameLayout.LayoutParams(-1,-1));installTouch();
        hint=text("",10,MUTED,false);hint.setPadding(dp(10),dp(6),dp(10),dp(6));hint.setBackgroundColor(0xcc081216);
        FrameLayout.LayoutParams hp=new FrameLayout.LayoutParams(-1,-2,Gravity.BOTTOM);scene.addView(hint,hp);
        float available=getResources().getDisplayMetrics().heightPixels/getResources().getDisplayMetrics().density;
        root.addView(scene,new LinearLayout.LayoutParams(-1,dp(Math.max(200,Math.min(300,available*.35f)))));
        metric=text("Preparando el micromundo…",14,TEXT,true);metric.setTag("micro_metric");metric.setPadding(0,dp(8),0,0);root.addView(metric);
        detail=text("",10,MUTED,false);detail.setPadding(0,dp(3),0,dp(5));root.addView(detail);
        LinearLayout transport=row();
        play=button("Pausar", "micro_play");play.setOnClickListener(v->setPlaying(!playing));transport.addView(play,weight());
        Button step=button("Paso", "micro_step");step.setOnClickListener(v->{setPlaying(false);labCommand(11,0,0,0,0);});transport.addView(step,weight());
        Button reset=button("Reiniciar", "micro_reset");reset.setOnClickListener(v->labCommand(8,0,0,0,0));transport.addView(reset,weight());
        root.addView(transport,new LinearLayout.LayoutParams(-1,dp(44)));

        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(false);scroll.setTag("micro_controls");
        LinearLayout controls=column();controls.setPadding(0,dp(8),0,dp(16));scroll.addView(controls);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout context=row();
        coupling=button("Campo: conectado", "micro_coupling");coupling.setTextSize(11);coupling.setOnClickListener(v->{coupled=!coupled;coupling.setText(coupled?"Campo: conectado":"Campo: apagado");style(coupling,coupled);labCommand(4,coupled?1:0,0,0,0);});context.addView(coupling,weight());
        seedButton=button("Semilla · "+seed,"micro_seed");seedButton.setTextSize(10);seedButton.setOnClickListener(v->chooseSeed());context.addView(seedButton,weight());controls.addView(context);
        particleControls=column();LinearLayout pc=row();
        pc.addView(text("TRAZADORES",10,MUTED,true),weight());countButton=button("4.096 partículas","micro_count");
        countButton.setOnClickListener(v->{int[] counts={2048,4096,8192,16384};countIndex=(countIndex+1)%counts.length;int n=counts[countIndex];countButton.setText(String.format(Locale.US,"%,d partículas",n));labCommand(7,n,0,0,0);});pc.addView(countButton);particleControls.addView(pc);
        particleControls.addView(slider("Agitación",12,100,(n)->labCommand(5,0,0,n/1000f,0)));controls.addView(particleControls);
        colonyControls=column();LinearLayout cc=row();cc.addView(text("INTERVENCIÓN AL TOCAR",10,MUTED,true),weight());
        brushButton=button("Añadir alimento","micro_brush");brushButton.setOnClickListener(v->{brush=1-brush;brushButton.setText(brush==0?"Añadir alimento":"Sembrar 12 células");updateHint();});cc.addView(brushButton);colonyControls.addView(cc);
        colonyControls.addView(slider("Crecimiento",80,200,(n)->labCommand(6,0,0,n/100f,0)));controls.addView(colonyControls);
        quantumLabel=text("NÚCLEO · 4 QUBITS SIMULADOS",11,LIME,true);quantumLabel.setPadding(0,dp(10),0,dp(4));controls.addView(quantumLabel);
        LinearLayout qr=row();for(int q=0;q<4;q++){final int index=q;targets[q]=button("q"+q,"micro_q_"+q);targets[q].setOnClickListener(v->{target=index;refreshTargets();});qr.addView(targets[q],weight());}controls.addView(qr);
        LinearLayout gates=row();String[] gateNames={"H","T","S","X","CX →"};
        for(int g=0;g<5;g++){final int gate=g;Button b=button(gateNames[g],"micro_gate_"+g);b.setOnClickListener(v->labCommand(1,gate,target,0,0));gates.addView(b,weight());}controls.addView(gates);
        TextView cx=text("CX une el qubit elegido con el siguiente (q3 → q0).",10,MUTED,false);cx.setPadding(0,dp(4),0,dp(6));controls.addView(cx);
        LinearLayout presets=row();String[] presetsText={"Cero","Superp.","Bell + T","Interf."};
        for(int p=0;p<4;p++){final int value=p;Button b=button(presetsText[p],"micro_preset_"+p);b.setTextSize(11);b.setOnClickListener(v->labCommand(2,value,0,0,0));presets.addView(b,weight());}controls.addView(presets);
        stateView=new MicroStateView(this);stateView.setTag("micro_state");controls.addView(stateView,new LinearLayout.LayoutParams(-1,dp(196)));
        Button measure=button("Medir los 4 qubits", "micro_measure");measure.setOnClickListener(v->labCommand(3,0,0,0,0));controls.addView(measure);
        TextView legend=text("S: entrelazamiento de cada qubit con el resto, en bits. Color de barras: fase de la amplitud. Orden de bits: q3 q2 q1 q0.",10,MUTED,false);legend.setPadding(0,dp(8),0,0);controls.addView(legend);
        setContentView(root);root.requestApplyInsets();selectMode(0);refreshTargets();style(coupling,true);
    }
    private interface Changed {void value(int value);}
    private View slider(String name,int initial,int max,Changed changed){
        LinearLayout box=column();TextView label=text(name+" · "+String.format(Locale.US,"%.3f",name.equals("Agitación")?initial/1000f:initial/100f),11,MUTED,false);box.addView(label);
        SeekBar slider=new SeekBar(this);slider.setMax(max);slider.setProgress(initial);slider.setProgressTintList(android.content.res.ColorStateList.valueOf(LIME));
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}
            public void onProgressChanged(SeekBar s,int n,boolean user){if(user){label.setText(name+" · "+String.format(Locale.US,"%.3f",name.equals("Agitación")?n/1000f:n/100f));changed.value(n);}}});box.addView(slider,new LinearLayout.LayoutParams(-1,dp(32)));return box;
    }
    void labCommand(int op,int a,int b,float x,float y){
        if(closed.get())return;surface.queueEvent(()->renderer.command(op,a,b,x,y));surface.requestRender();
    }
    void setPlaying(boolean value){
        playing=value;play.setText(playing?"Pausar":"Continuar");style(play,playing);surface.queueEvent(()->renderer.play(value));
        ui.removeCallbacks(pulse);if(playing&&foreground)ui.post(pulse);surface.requestRender();
    }
    private void selectMode(int value){
        mode=value;for(int i=0;i<3;i++)style(modes[i],i==value);
        particleControls.setVisibility(value==0?View.VISIBLE:View.GONE);colonyControls.setVisibility(value==1?View.VISIBLE:View.GONE);
        updateHint();labCommand(0,value,0,0,0);
    }
    private void updateHint(){hint.setText(mode==0?"Arrastra: orbitar · pellizca: zoom · toca: pulso":mode==1?
            "Toca: "+(brush==0?"alimento":"sembrar")+" · arrastra: orbitar · pellizca: zoom":"16 estados · altura ∝ √probabilidad · color: fase");}
    private void refreshTargets(){for(int q=0;q<4;q++)style(targets[q],q==target);stateView.update(latest,target);}
    @Override public void state(String json,float computeMs,float fps){
        ui.post(()->{if(closed.get())return;try{
            latest=new JSONObject(json);latestSnapshot=json;int current=latest.getInt("mode");
            String title=current==0?String.format(Locale.US,"%,d partículas",latest.getInt("particles")):current==1?
                latest.getInt("cells")+" células · "+latest.getInt("divisions")+" divisiones":"4 qubits · 16 amplitudes";
            metric.setText(title+String.format(Locale.US,"     t %.1f",latest.getDouble("time")));
            detail.setText(String.format(Locale.US,"Modelo %.2f ms · %s · memoria nativa ≈ %.0f KiB",computeMs,
                    playing?String.format(Locale.US,"%.0f fps",fps):"pausa",latest.getDouble("model_owned_bytes")/1024));
            if(latest.optBoolean("capacity_reached"))detail.setText("Límite visible: 2.048 células · división detenida al llenarse");
            int outcome=latest.optInt("measurement",-1);quantumLabel.setText(outcome<0?"NÚCLEO · 4 QUBITS SIMULADOS":
                    "ÚLTIMA MEDICIÓN · |"+String.format(Locale.US,"%4s",Integer.toBinaryString(outcome)).replace(' ','0')+"⟩");
            stateView.update(latest,target);
        }catch(Exception e){error(e.getMessage());}});
    }
    @Override public void error(String message){ui.post(()->{if(closed.get())return;lastError=message==null?"Error del laboratorio":message;setPlaying(false);detail.setText(lastError);});}
    private void installTouch(){
        ScaleGestureDetector scale=new ScaleGestureDetector(this,new ScaleGestureDetector.SimpleOnScaleGestureListener(){
            @Override public boolean onScale(ScaleGestureDetector d){float factor=d.getScaleFactor();surface.queueEvent(()->renderer.zoom(factor));surface.requestRender();return true;}});
        surface.setOnTouchListener(new View.OnTouchListener(){float x,y,startX,startY;boolean moved;
            public boolean onTouch(View v,MotionEvent e){scale.onTouchEvent(e);int action=e.getActionMasked();
                if(action==MotionEvent.ACTION_DOWN){x=startX=e.getX();y=startY=e.getY();moved=false;v.getParent().requestDisallowInterceptTouchEvent(true);}
                else if(action==MotionEvent.ACTION_POINTER_DOWN)moved=true;
                else if(action==MotionEvent.ACTION_MOVE){float nx=e.getX(),ny=e.getY();if(Math.hypot(nx-startX,ny-startY)>dp(6))moved=true;
                    if(e.getPointerCount()==1&&moved&&!scale.isInProgress()){float dx=nx-x,dy=ny-y;surface.queueEvent(()->renderer.orbit(dx,dy));surface.requestRender();}x=nx;y=ny;}
                else if(action==MotionEvent.ACTION_UP){if(!moved){float tx=e.getX(),ty=e.getY();int selectedBrush=brush;surface.queueEvent(()->renderer.touch(tx,ty,selectedBrush));surface.requestRender();v.performClick();}v.getParent().requestDisallowInterceptTouchEvent(false);}
                else if(action==MotionEvent.ACTION_CANCEL)v.getParent().requestDisallowInterceptTouchEvent(false);
                return true;}});
    }
    private void chooseSeed(){
        EditText input=new EditText(this);input.setInputType(InputType.TYPE_CLASS_NUMBER);input.setText(String.valueOf(seed));input.setSelectAllOnFocus(true);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Semilla reproducible").setMessage("La misma semilla, controles y pasos repiten el experimento en esta compilación.")
            .setView(input).setNegativeButton("Cancelar",null).setPositiveButton("Reiniciar",null).create();
        dialog.setOnShowListener(d->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{try{
            int value=Integer.parseInt(input.getText().toString());if(value<0)throw new NumberFormatException();seed=value;
            seedButton.setText("Semilla · "+seed);labCommand(10,seed,0,0,0);dialog.dismiss();
        }catch(NumberFormatException e){input.setError("Usa un entero entre 0 y 2147483647");}}));dialog.show();
    }
    private void showModel(){new AlertDialog.Builder(this).setTitle("Qué estás simulando")
        .setMessage("Partículas: trazadores brownianos con difusión, confinamiento y paredes reflectantes; no son átomos con interacciones moleculares.\n\n"
            +"Colonia: agentes que se mueven, consumen una malla de nutrientes, crecen y se dividen. Sin especie, unidades físicas ni calibración experimental.\n\n"
            +"Núcleo: FactorizedCore, cuatro qubits con H, T, S, X y CX. El campo externo se calcula a partir de sus observables. Esta conexión es una regla del laboratorio, no una ley de biología cuántica.\n\n"
            +"Los dos modelos avanzan en pasos de 1/60; t es tiempo del modelo. Si el dispositivo se retrasa, la simulación va más lenta. Cambiar de vista conserva ambos modelos. Los qubits cambian al aplicar puertas o medir.\n\n"
            +"Tope: 16.384 trazadores y 2.048 células. La división se detiene al alcanzar el límite. Las cifras de memoria estiman datos nativos; excluyen Java, GPU y memoria total de la app. La GPU dibuja; el modelo se calcula en CPU.\n\n"
            +"Los vectores de Bloch son proyecciones. S mide entrelazamiento qubit/resto porque el estado global es puro. Las fases se muestran en la convención del simulador; una fase global no cambia probabilidades.")
        .setPositiveButton("Explorar",null).show();}
    @Override protected void onResume(){super.onResume();foreground=true;surface.onResume();surface.queueEvent(()->renderer.play(playing));ui.removeCallbacks(pulse);if(playing)ui.post(pulse);surface.requestRender();}
    @Override protected void onPause(){foreground=false;ui.removeCallbacks(pulse);surface.queueEvent(()->renderer.play(false));surface.onPause();super.onPause();}
    @Override protected void onDestroy(){closed.set(true);ui.removeCallbacksAndMessages(null);NativeCore.microDestroy(handle);super.onDestroy();}
    private int dp(float n){return Math.round(n*getResources().getDisplayMetrics().density);}
    private LinearLayout column(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;}
    private LinearLayout row(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.HORIZONTAL);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    private LinearLayout.LayoutParams weight(){return new LinearLayout.LayoutParams(0,-2,1);}
    private TextView text(String s,float size,int color,boolean bold){TextView t=new TextView(this);t.setText(s);t.setTextSize(size);t.setTextColor(color);t.setFontFeatureSettings("tnum");if(bold)t.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));return t;}
    private Button button(String name,String tag){Button b=new Button(this);b.setText(name);b.setTag(tag);b.setTextSize(12);b.setAllCaps(false);b.setMinWidth(0);b.setMinimumWidth(0);b.setMinHeight(dp(42));b.setMinimumHeight(dp(42));b.setPadding(dp(7),dp(4),dp(7),dp(4));style(b,false);return b;}
    private void style(Button b,boolean active){GradientDrawable bg=new GradientDrawable();bg.setColor(active?LIME:PANEL);bg.setCornerRadius(dp(8));bg.setStroke(dp(1),INK);b.setBackground(bg);b.setTextColor(active?INK:TEXT);}
}
