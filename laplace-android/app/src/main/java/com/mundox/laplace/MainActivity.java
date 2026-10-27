package com.mundox.laplace;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.HorizontalScrollView;
import android.widget.ScrollView;
import android.widget.Button;
import android.widget.TextView;
import android.widget.CheckBox;
import android.widget.SeekBar;
import android.widget.Toast;
import android.graphics.Color;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Bitmap;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.content.Intent;
import android.net.Uri;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Offline, standalone, four-interface research app.
 * All field images and results originate from the actual LaplaceEngine.
 * No REVERSE, financial feed, trading, or quantum hardware are represented.
 */
public final class MainActivity extends Activity {
    private static final int BG=Color.rgb(11,17,23), PANEL=Color.rgb(20,30,37),
       EDGE=Color.rgb(39,55,64), INK=Color.rgb(238,247,246), MUTED=Color.rgb(161,177,184),
       GREEN=Color.rgb(134,210,179), GOLD=Color.rgb(234,193,118);
    private static final String[] TABS={"CAMPO","CONVERGENCIA","DETERMINISMO","RESULTADOS"};
    private static final int[] GRIDS={17,33,65,129};
    private LinearLayout content,tabs;
    private int page=0,grid=33,steps=40;
    private boolean shock=true,fieldBusy=false,auditBusy=false;
    private int seq;
    private LaplaceEngine.Field mode=LaplaceEngine.Field.QUARTIC;
    private LaplaceEngine.Solution solved;
    private LaplaceAudit.Report audited;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();

    public boolean isLabStandalone(){return true;}
    public String currentTabForTesting(){return TABS[page];}
    public boolean isMeasuredFieldReadyForTesting(){return solved!=null&&solved.converged;}
    public double measuredResidualForTesting(){return solved==null?Double.NaN:solved.maxResidual;}
    public boolean auditPassedForTesting(){return audited!=null&&audited.passed;}
    public void openTabForTesting(int p){if(p<0||p>3)throw new IllegalArgumentException();page=p;render();}
    private int dp(float x){return Math.round(getResources().getDisplayMetrics().density*x);}
    private LinearLayout vertical(){LinearLayout l=new LinearLayout(this);l.setOrientation(1);return l;}
    private LinearLayout horizontal(){LinearLayout l=new LinearLayout(this);l.setOrientation(0);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    private static String num(double v){return String.format(Locale.ROOT,"%.3e",v);}
    private GradientDrawable bg(int fill,int outline){
        GradientDrawable d=new GradientDrawable();d.setColor(fill);d.setCornerRadius(dp(11));
        if(outline!=0)d.setStroke(dp(1),outline);return d;
    }
    private TextView txt(String value,int size,int color,boolean bold){
        TextView t=new TextView(this);t.setText(value);t.setTextColor(color);t.setTextSize(size);
        t.setIncludeFontPadding(false);t.setLineSpacing(dp(3),1f);
        if(bold)t.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));return t;
    }
    private void text(LinearLayout box,String message,int font,int color,boolean bold,int bottom){
        TextView t=txt(message,font,color,bold);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);
        p.bottomMargin=dp(bottom);box.addView(t,p);
    }
    private void section(View v,int bottom){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.bottomMargin=dp(bottom);content.addView(v,p);}
    private LinearLayout card(){LinearLayout c=vertical();c.setPadding(dp(16),dp(16),dp(16),dp(16));c.setBackground(bg(PANEL,EDGE));return c;}
    private Button button(String title,boolean positive,Runnable call){
        Button b=new Button(this);b.setText(title);b.setAllCaps(false);b.setTextSize(12);
        b.setTextColor(positive?BG:INK);b.setBackground(bg(positive?GREEN:PANEL,positive?0:EDGE));
        b.setOnClickListener(v->call.run());b.setMinimumHeight(0);b.setMinHeight(0);
        return b;
    }
    private void action(String label,Runnable fn){Button b=button(label,true,fn);section(b,14);}
    private void metric(String label,String result,String detail){
        LinearLayout c=card();text(c,label,10,GREEN,true,8);text(c,result,21,INK,true,7);text(c,detail,11,MUTED,false,0);section(c,10);
    }
    private void header(String kicker,String title,String description){
        text(content,kicker,10,GREEN,true,8);text(content,title,26,INK,true,12);
        text(content,description,12,MUTED,false,18);
    }
    private void options(String[] choices,int chosen,java.util.function.IntConsumer selected){
        HorizontalScrollView scroll=new HorizontalScrollView(this);scroll.setHorizontalScrollBarEnabled(false);
        LinearLayout row=horizontal();
        for(int i=0;i<choices.length;i++){
            final int index=i;Button b=button(choices[i],false,()->selected.accept(index));
            b.setTextColor(chosen==i?GREEN:INK);
            b.setBackground(bg(chosen==i?Color.rgb(30,62,52):PANEL,chosen==i?GREEN:EDGE));
            LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-2,dp(42));p.rightMargin=dp(8);
            row.addView(b,p);
        }
        scroll.addView(row);section(scroll,12);
    }
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);getWindow().setNavigationBarColor(BG);
        LinearLayout root=vertical();root.setBackgroundColor(BG);setContentView(root);
        LinearLayout bar=horizontal();bar.setPadding(dp(19),dp(19),dp(16),dp(18));
        TextView symbol=txt("∇",30,GREEN,true);symbol.setGravity(Gravity.CENTER);
        symbol.setBackground(bg(Color.rgb(26,51,45),Color.rgb(60,110,83)));
        bar.addView(symbol,new LinearLayout.LayoutParams(dp(48),dp(48)));
        LinearLayout wordmark=vertical();wordmark.setPadding(dp(12),0,0,0);
        text(wordmark,"LAPLACE X",19,INK,true,5);
        text(wordmark,"INVESTIGACIÓN · AUTÓNOMO",10,MUTED,false,0);
        bar.addView(wordmark,new LinearLayout.LayoutParams(0,-2,1));
        bar.addView(txt("OFFLINE",10,GREEN,true),new LinearLayout.LayoutParams(-2,-2));
        root.addView(bar);
        HorizontalScrollView nav=new HorizontalScrollView(this);
        nav.setFillViewport(true);nav.setHorizontalScrollBarEnabled(false);
        tabs=horizontal();nav.addView(tabs);
        root.addView(nav,new LinearLayout.LayoutParams(-1,dp(50)));
        View divider=new View(this);divider.setBackgroundColor(EDGE);root.addView(divider,new LinearLayout.LayoutParams(-1,dp(1)));
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setVerticalScrollBarEnabled(false);
        content=vertical();content.setPadding(dp(17),dp(17),dp(17),dp(26));
        scroll.addView(content);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        TextView footer=txt("∇²u = 0  ·  SIN DATOS BURSÁTILES  ·  SIN REVERSE",9,MUTED,false);
        footer.setGravity(Gravity.CENTER);footer.setPadding(dp(10),dp(9),dp(10),dp(12));
        root.addView(footer);
        render();
    }
    @Override protected void onDestroy(){worker.shutdownNow();super.onDestroy();}
    private void render(){
        tabs.removeAllViews();for(int i=0;i<4;i++){
            final int id=i;TextView chip=txt(TABS[i],10,id==page?GREEN:MUTED,id==page);
            chip.setGravity(Gravity.CENTER);chip.setPadding(dp(12),dp(10),dp(12),dp(10));
            chip.setBackgroundColor(id==page?Color.rgb(27,50,44):BG);
            chip.setOnClickListener(v->{page=id;render();});tabs.addView(chip,new LinearLayout.LayoutParams(-2,-1));
        }
        content.removeAllViews();
        switch(page){case 0:field();break;case 1:convergence();break;case 2:determinism();break;case 3:results();break;}
    }
    private void field(){
        header("01 / ECUACIÓN REAL","Campo armónico",
                "Resolvemos ∂²u/∂x² + ∂²u/∂y² = 0 con fronteras de Dirichlet y un operador de cinco puntos.");
        text(content,"CAMPO DE PRUEBA",10,GREEN,true,9);
        LaplaceEngine.Field[] modes=LaplaceEngine.Field.values();
        String[] labels=new String[modes.length];int active=0;
        for(int i=0;i<modes.length;i++){labels[i]=modes[i].display;if(mode==modes[i])active=i;}
        options(labels,active,id->{mode=modes[id];solved=null;render();});
        text(content,mode.expression,16,INK,true,16);
        text(content,"RESOLUCIÓN ESPACIAL",10,GREEN,true,9);
        String[] names={"17 × 17","33 × 33","65 × 65","129 × 129"};active=0;
        for(int i=0;i<GRIDS.length;i++)if(grid==GRIDS[i])active=i;
        options(names,active,id->{grid=GRIDS[id];solved=null;render();});
        if(solved==null){
            LinearLayout c=card();
            text(c,"CAMPO SIN RESOLVER",15,INK,true,8);
            text(c,"La imagen y las cifras se mostrarán únicamente después de ejecutar el cálculo.",11,MUTED,false,0);
            section(c,14);
        }else{
            LinearLayout c=card();
            text(c,"VISUALIZACIÓN DE DATOS CALCULADOS",10,GREEN,true,8);
            Heatmap map=new Heatmap();map.setValue(solved);
            c.addView(map,new LinearLayout.LayoutParams(-1,dp(240)));
            text(c,"Azul: potencial menor · Dorado: potencial mayor",10,MUTED,false,0);
            section(c,12);
            metric("RESIDUO MÁXIMO |Δₕu|",num(solved.maxResidual),
                    "Desviación de la ecuación discretizada.");
            metric("ERROR FRENTE A SOLUCIÓN ANALÍTICA",num(solved.maxError),
                    "Error de aproximación, no incertidumbre del mercado.");
            metric("RESULTADO EN EL CENTRO",String.format(Locale.ROOT,"%.8f",solved.center),
                    solved.iterations+" iteraciones · "+String.format(Locale.ROOT,"%.2f",solved.durationNanos/1e6)+" ms medidos aquí.");
        }
        Button run=button(fieldBusy?"Calculando…":"Resolver ecuación",true,this::solveField);
        run.setEnabled(!fieldBusy);section(run,13);
        text(content,"Método numérico: diferencias finitas y sobre-relajación sucesiva. Ningún valor de bolsa está involucrado.",10,MUTED,false,0);
    }
    private void solveField(){
        if(fieldBusy)return;fieldBusy=true;render();
        final LaplaceEngine.Field f=mode;final int n=grid;final int identity=++seq;
        worker.execute(()->{
            LaplaceEngine.Solution v=null;Throwable err=null;
            try{v=LaplaceEngine.solve(f,n);}catch(Throwable e){err=e;}
            final LaplaceEngine.Solution done=v;final Throwable failure=err;
            runOnUiThread(()->{
                if(isFinishing()||isDestroyed()||identity!=seq)return;
                fieldBusy=false;if(failure==null)solved=done;
                else Toast.makeText(this,"Error: "+failure.getMessage(),Toast.LENGTH_LONG).show();
                if(page==0)render();
            });
        });
    }
    private void convergence(){
        header("02 / RESULTADOS CUANTIFICABLES","Ganancia de precisión",
                "Comparación matemática real: disminución del error frente a una solución analítica al refinar la malla.");
        if(audited==null){auditCall();return;}
        LinearLayout plot=card();
        text(plot,"ERROR MÁXIMO · ESCALA LOGARÍTMICA",10,GREEN,true,14);
        AccuracyGraph graph=new AccuracyGraph();graph.setReport(audited);
        plot.addView(graph,new LinearLayout.LayoutParams(-1,dp(210)));section(plot,12);
        LinearLayout table=card();
        text(table,"MALLA       ERROR               ITERACIONES",11,MUTED,true,13);
        for(int i=0;i<audited.dimensions.length;i++)
            text(table,String.format(Locale.ROOT,"%3d²        %.8f          %d",
                    audited.dimensions[i],audited.quarticErrors[i],audited.quarticIterations[i]),12,INK,false,9);
        section(table,12);
        double improvement=100*(1-audited.quarticErrors[3]/audited.quarticErrors[0]);
        metric("GANANCIA EN PRECISIÓN · 17² → 129²",String.format(Locale.ROOT,"%.2f %%",improvement),
                "Reducción del error de discretización en el problema ensayado.");
        metric("CONVERGENCIA DEL ESQUEMA",String.format(Locale.ROOT,"×%.3f · ×%.3f · ×%.3f",
                audited.quadraticGain1,audited.quadraticGain2,audited.quadraticGain3),
                "El orden 2 produce aproximadamente ×4 por refinamiento.");
        action("Volver a medir",this::beginAudit);
    }
    private void auditCall(){
        LinearLayout c=card();text(c,auditBusy?"CALCULANDO…":"MEDICIONES NO EJECUTADAS",15,INK,true,9);
        text(c,"Los resultados no aparecen hasta comprobarlos con el núcleo matemático.",11,MUTED,false,0);
        section(c,12);
        Button b=button(auditBusy?"En proceso…":"Ejecutar auditoría independiente",true,this::beginAudit);
        b.setEnabled(!auditBusy);section(b,13);
    }
    private void beginAudit(){
        if(auditBusy)return;auditBusy=true;render();
        worker.execute(()->{
            LaplaceAudit.Report r=null;Throwable e=null;
            try{r=LaplaceAudit.evaluate();}catch(Throwable t){e=t;}
            final LaplaceAudit.Report complete=r;final Throwable fail=e;
            runOnUiThread(()->{
                if(isFinishing()||isDestroyed())return;
                auditBusy=false;
                if(fail==null)audited=complete;
                else Toast.makeText(this,"Auditoría falló: "+fail.getMessage(),Toast.LENGTH_LONG).show();
                if(page==1||page==3)render();
            });
        });
    }
    private void determinism(){
        header("03 / EXPERIMENTO APARTE","Demonio de Laplace",
                "El determinismo clásico no es la ecuación espacial ∇²u=0. Comparamos un cálculo temporal con un evento desconocido.");
        LinearLayout controls=card();
        text(controls,"REGLA SINTÉTICA · Pₜ₊₁ = Pₜ + 7 + 3t",13,INK,true,12);
        text(controls,"PASOS: "+steps,12,GREEN,true,6);
        SeekBar seek=new SeekBar(this);seek.setMax(70);seek.setProgress(steps-10);
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onStartTrackingTouch(SeekBar b){}
            public void onStopTrackingTouch(SeekBar b){render();}
            public void onProgressChanged(SeekBar b,int progress,boolean fromUser){if(fromUser)steps=progress+10;}
        });controls.addView(seek,new LinearLayout.LayoutParams(-1,dp(44)));
        CheckBox event=new CheckBox(this);event.setChecked(shock);
        event.setText("Incluir un evento desconocido de +250 céntimos en el paso 20");
        event.setTextSize(11);event.setTextColor(INK);
        event.setOnCheckedChangeListener((button,enabled)->{shock=enabled;render();});
        controls.addView(event);section(controls,13);
        int predicted=10000+7*steps+3*steps*(steps-1)/2;
        int difference=shock&&steps>=20?250:0;
        metric("PREDICCIÓN CON REGLA CONOCIDA",predicted+" ¢","Exacta dentro del modelo matemático artificial.");
        metric("RESULTADO CON EVENTO EXTERNO",(predicted+difference)+" ¢","El evento no estaba entre los datos iniciales.");
        metric("ERROR POR INFORMACIÓN NO CONOCIDA",difference+" ¢","No es error de aritmética; falta información futura.");
        text(content,"Este experimento no predice activos financieros ni pertenece a la ecuación diferencial de Laplace.",11,GOLD,false,0);
    }
    private void results(){
        header("04 / AUDITORÍA","Resultados verificables",
                "Separa evidencia matemática, convergencia y limitaciones físicas de afirmaciones sobre el futuro.");
        LinearLayout explain=card();
        text(explain,"ECUACIÓN IMPLEMENTADA",10,GREEN,true,10);
        text(explain,"∂²u/∂x² + ∂²u/∂y² = 0",19,INK,true,12);
        text(explain,"Dominio [0,1]² · 2D · Contorno Dirichlet · diferencias finitas de cinco puntos · SOR.",12,MUTED,false,12);
        text(explain,"El demonio de Laplace es una hipótesis filosófica. Esta ecuación real estudia campos espaciales estacionarios.",11,GOLD,false,0);
        section(explain,12);
        if(audited==null)auditCall();
        else{
            metric("ESTADO DE LA AUDITORÍA",audited.passed?"APROBADA":"NO APROBADA",
                    audited.checks+" comprobaciones independientes ejecutadas en este dispositivo.");
            metric("MÁXIMO RESIDUO NUMÉRICO",num(audited.maxObservedResidual),
                    "Se verificaron fronteras, residuo y principio del máximo.");
            action("Repetir auditoría",this::beginAudit);
            action("Exportar CSV de resultados",this::exportCsv);
        }
        LinearLayout limits=card();
        text(limits,"LÍMITES CONFIRMADOS",10,GREEN,true,12);
        text(limits,"• Esta ecuación es estacionaria, no temporal.\n• El cálculo usa la CPU clásica de Android.\n• No contiene una bolsa de valores ni operaciones.\n• No hay qubits físicos ni sensor de información futura.\n• REVERSE y BBY Q permanecen sin cambios.",11,MUTED,false,0);
        section(limits,0);
    }
    private void exportCsv(){
        if(audited==null)return;
        Intent intent=new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType("text/csv");
        intent.putExtra(Intent.EXTRA_TITLE,"laplace-x-resultados.csv");
        startActivityForResult(intent,7001);
    }
    @Override protected void onActivityResult(int req,int result,Intent intent){
        super.onActivityResult(req,result,intent);
        if(req!=7001||result!=RESULT_OK||audited==null||intent==null||intent.getData()==null)return;
        Uri uri=intent.getData();
        try(OutputStream out=getContentResolver().openOutputStream(uri,"w")){
            if(out==null)throw new IllegalStateException("No se abrió el documento");
            out.write(audited.asCsv().getBytes(StandardCharsets.UTF_8));
            Toast.makeText(this,"Resultados exportados",Toast.LENGTH_SHORT).show();
        }catch(Exception e){Toast.makeText(this,"Error al exportar: "+e.getMessage(),Toast.LENGTH_LONG).show();}
    }
    private final class Heatmap extends View {
        private final Paint brush=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
        private Bitmap bitmap;
        Heatmap(){super(MainActivity.this);}
        void setValue(LaplaceEngine.Solution state){
            int n=state.points;int[] pixels=new int[n*n];
            double span=Math.max(1e-12,state.maxBoundary-state.minBoundary);
            for(int y=0;y<n;y++)for(int x=0;x<n;x++){
                double ratio=Math.min(1,Math.max(0,(state.value(x,n-1-y)-state.minBoundary)/span));
                int a,b;double k;
                if(ratio<0.5){a=Color.rgb(26,44,71);b=Color.rgb(38,148,146);k=ratio*2;}
                else{a=Color.rgb(38,148,146);b=Color.rgb(242,195,113);k=2*ratio-1;}
                pixels[y*n+x]=Color.rgb((int)(Color.red(a)*(1-k)+Color.red(b)*k),
                     (int)(Color.green(a)*(1-k)+Color.green(b)*k),(int)(Color.blue(a)*(1-k)+Color.blue(b)*k));
            }
            bitmap=Bitmap.createBitmap(pixels,n,n,Bitmap.Config.ARGB_8888);
            invalidate();
        }
        @Override protected void onDraw(Canvas c){
            if(bitmap==null)return;
            float side=Math.min(getWidth()-dp(12),getHeight()-dp(12));
            float left=(getWidth()-side)/2,top=(getHeight()-side)/2;
            RectF dest=new RectF(left,top,left+side,top+side);
            c.drawBitmap(bitmap,null,dest,brush);
            brush.setStyle(Paint.Style.STROKE);brush.setColor(INK);brush.setStrokeWidth(dp(1));c.drawRect(dest,brush);
            brush.setStyle(Paint.Style.FILL);
        }
    }
    private final class AccuracyGraph extends View {
        private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        private LaplaceAudit.Report values;
        AccuracyGraph(){super(MainActivity.this);}
        void setReport(LaplaceAudit.Report r){values=r;invalidate();}
        @Override protected void onDraw(Canvas c){
            if(values==null)return;
            float left=dp(42),right=getWidth()-dp(16),top=dp(15),bottom=getHeight()-dp(35);
            paint.setColor(EDGE);paint.setStrokeWidth(dp(1));
            for(int i=0;i<5;i++)c.drawLine(left,top+(bottom-top)*i/4,right,top+(bottom-top)*i/4,paint);
            double high=Math.log10(values.quarticErrors[0]),low=Math.log10(values.quarticErrors[3]);
            Path curve=new Path();
            for(int i=0;i<4;i++){
                float x=left+(right-left)*i/3f;
                float y=top+(float)((high-Math.log10(values.quarticErrors[i]))/(high-low))*(bottom-top);
                if(i==0)curve.moveTo(x,y);else curve.lineTo(x,y);
                paint.setTextSize(dp(10));paint.setColor(MUTED);paint.setTextAlign(Paint.Align.CENTER);
                c.drawText(values.dimensions[i]+"²",x,bottom+dp(18),paint);
            }
            paint.setColor(GREEN);paint.setStrokeWidth(dp(3));paint.setStyle(Paint.Style.STROKE);
            c.drawPath(curve,paint);paint.setStyle(Paint.Style.FILL);
        }
    }
}
