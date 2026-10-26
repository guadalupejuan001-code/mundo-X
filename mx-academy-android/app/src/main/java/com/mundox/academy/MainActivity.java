package com.mundox.academy;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * MX Visual: first touch the operations, then optionally reveal their MX source.
 * Every probability displayed comes from the original Mundo X runtime via JNI.
 */
public final class MainActivity extends Activity {
    static { System.loadLibrary("mxlearn"); }
    private native String inspectNative(String mxSource);
    private native String evaluate(String mxSource, int mission);

    private static final int BG=Color.rgb(7,14,26);
    private static final int PANEL=Color.rgb(18,31,51);
    private static final int PANEL2=Color.rgb(12,24,41);
    private static final int EDGE=Color.rgb(44,68,91);
    private static final int WHITE=Color.rgb(238,247,255);
    private static final int MUTED=Color.rgb(166,188,206);
    private static final int TEAL=Color.rgb(77,232,196);
    private static final int PURPLE=Color.rgb(183,151,255);
    private static final int GOLD=Color.rgb(255,209,126);
    private static final Typeface MONO=Typeface.create("monospace",Typeface.NORMAL);

    private static final class Action {
        final String key,label,meaning,feedback;
        Action(String k,String l,String m,String f) {
            key=k;label=l;meaning=m;feedback=f;
        }
    }
    private static final Action X0=new Action("X0","INVERTIR · q0","Cambiar 0 ↔ 1",
        "Acabas de aplicar X. Invierte los valores de la base.");
    private static final Action H0=new Action("H0","ABRIR OPCIONES · q0","Puerta H",
        "Aplicaste H. Mira cómo cambian los resultados posibles.");
    private static final Action Z0=new Action("Z0","CAMBIAR FASE · q0","Puerta Z",
        "Aplicaste Z. A veces la fase cambia aunque las barras parezcan iguales.");
    private static final Action CX01=new Action("CX01","CONECTAR q0 → q1","Puerta CNOT",
        "Aplicaste CNOT: el control q0 afecta a q1. Observa las posibilidades conjuntas.");
    private static final Action CX12=new Action("CX12","CONECTAR q1 → q2","Puerta CNOT",
        "Conectaste q1 con q2. Consulta qué resultados conjuntos siguen siendo posibles.");
    private static final Action REPEAT=new Action("REPEAT","INVERTIR LOS 3","Repetición automática",
        "Una sola receta ha aplicado X a los tres qubits. En MX se llama repeat.");
    private static final Action RECIPE=new Action("RECIPE","CREAR RECETA BELL","Circuito reutilizable",
        "Has definido una receta con circuit y luego la has llamado.");
    private static final Action T0=new Action("T0","GIRAR FASE · q0","Puerta T (π/4)",
        "Aplicaste T. Cambia la fase relativa, no necesariamente las probabilidades en Z.");

    private static final class Mission {
        final int width;
        final String title,story,goal,hint;
        final Action[] actions;
        Mission(int n,String t,String s,String g,String h,Action... choices) {
            width=n;title=t;story=s;goal=g;hint=h;actions=choices;
        }
    }
    private static final Mission[] MISSIONS={
        new Mission(1,"Conoce tu primer qubit",
            "No necesitas saber programar. Esto es un sistema que empieza en cero. "
            +"Observa abajo qué pasaría al consultarlo.",
            "Descubre qué resultado tiene sin hacer ninguna operación.",
            "Comienza en 0. Solo toca COMPROBAR.",
            new Action[]{}),
        new Mission(1,"Haz que cambie",
            "Piensa en un interruptor: empieza en 0. Puedes pedirle a tu máquina que lo invierta.",
            "Consigue que el único resultado posible sea 1.",
            "Pulsa INVERTIR una vez.",X0),
        new Mission(1,"Dos resultados posibles",
            "Un qubit puede prepararse en superposición. "
            +"Si lo medimos en la base Z, podría dar 0 o 1 con igual probabilidad.",
            "Consigue 50% de probabilidad para cada resultado.",
            "Pulsa ABRIR OPCIONES.",H0,X0),
        new Mission(2,"Relaciona dos qubits",
            "Ahora tienes q0 y q1. Primero prepara superposición; "
            +"después usa una operación controlada para relacionar sus resultados.",
            "Consigue que solo sean posibles 00 y 11, cada uno con 50%.",
            "El orden es H en q0 y luego CONECTAR q0 → q1.",H0,CX01,X0),
        new Mission(1,"El cambio invisible",
            "Una fase puede cambiar sin alterar de inmediato las probabilidades. "
            +"Otra operación puede revelar la diferencia mediante interferencia.",
            "Prueba H, después Z y después H. Observa cada cambio.",
            "La secuencia exacta es ABRIR → CAMBIAR FASE → ABRIR.",H0,Z0,X0),
        new Mission(3,"Enlaza tres",
            "Una misma correlación puede extenderse a tres qubits. "
            +"Busca el estado en que 000 y 111 sean los únicos resultados.",
            "Construye el estado GHZ de tres qubits.",
            "Primero H en q0, luego CNOT q0 → q1 y luego CNOT q1 → q2.",H0,CX01,CX12),
        new Mission(3,"Una orden para todos",
            "En MX puedes repetir una operación sin tocar cada qubit por separado.",
            "Deja los tres qubits en 111 usando una repetición.",
            "Pulsa INVERTIR LOS 3 y luego revela el código para ver repeat.",REPEAT,X0),
        new Mission(2,"Crea tu primera receta",
            "MX permite guardar varias operaciones con un nombre y volver a utilizarlas. "
            +"Eso es un circuito reutilizable.",
            "Construye una receta que cree el estado Bell.",
            "Pulsa CREAR RECETA BELL; después revela el código para ver circuit.",RECIPE,H0,CX01),
        new Mission(1,"Controla una fase nueva",
            "La puerta T incorpora una fase distinta de las anteriores. "
            +"Es útil para explorar circuitos no Clifford.",
            "Aplica H y después T. Las barras pueden seguir en 50/50.",
            "Primero ABRIR OPCIONES y después GIRAR FASE.",H0,T0)
    };

    private final ArrayList<String> steps=new ArrayList<>();
    private SharedPreferences preferences;
    private int mission=0;
    private int unlocked=0;
    private boolean codeRevealed=false;
    private TextView explanation, codePreview, feedback, codeToggle;
    private LinearLayout codeBox, histogram, sequence, options;
    private Button verify, next, undo;
    private ProgressBar progress;
    private QuantumView quantum;
    private String lastMessage="Empieza mirando las probabilidades.";
    private double[] lastProbabilities={1.0,0.0};

    private int dp(float v) {
        return (int)(v*getResources().getDisplayMetrics().density+.5f);
    }
    private GradientDrawable shape(int color,int radius,int stroke) {
        GradientDrawable b=new GradientDrawable();
        b.setColor(color);
        b.setCornerRadius(dp(radius));
        if(stroke!=Color.TRANSPARENT)b.setStroke(dp(1),stroke);
        return b;
    }
    private LinearLayout column() {
        LinearLayout v=new LinearLayout(this);
        v.setOrientation(LinearLayout.VERTICAL);
        return v;
    }
    private LinearLayout.LayoutParams lp(int width,int height,int top) {
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(width,height);
        p.topMargin=dp(top);
        return p;
    }
    private TextView text(String value,int size,int color,boolean bold) {
        TextView t=new TextView(this);
        t.setText(value);
        t.setTextColor(color);
        t.setTextSize(size);
        if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        return t;
    }
    private void line(LinearLayout parent,String s,int size,int color,boolean bold,int top) {
        parent.addView(text(s,size,color,bold),lp(-1,-2,top));
    }
    private Button button(String label, boolean primary) {
        Button b=new Button(this);
        b.setAllCaps(false);
        b.setText(label);
        b.setTextSize(14);
        b.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        b.setTextColor(primary?BG:WHITE);
        b.setBackground(shape(primary?TEAL:PANEL,13,primary?Color.TRANSPARENT:EDGE));
        b.setPadding(dp(12),0,dp(12),0);
        return b;
    }
    private LinearLayout surface(int background,int radius) {
        LinearLayout v=column();
        v.setBackground(shape(background,radius,EDGE));
        v.setPadding(dp(17),dp(16),dp(17),dp(17));
        return v;
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        Window window=getWindow();
        window.setStatusBarColor(BG);
        window.setNavigationBarColor(BG);
        preferences=getSharedPreferences("mx_visual_progress_v2",MODE_PRIVATE);
        unlocked=Math.max(0,Math.min(MISSIONS.length-1,preferences.getInt("unlocked",0)));
        mission=Math.max(0,Math.min(unlocked,preferences.getInt("last",0)));
        showMission();
    }

    private void showMission() {
        steps.clear();
        codeRevealed=false;
        lastMessage="Primero toca una acción. Luego comprueba el resultado.";
        final Mission lesson=MISSIONS[mission];
        ScrollView scroll=new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);
        LinearLayout root=column();
        root.setPadding(dp(18),dp(20),dp(18),dp(38));
        root.setFocusableInTouchMode(true);
        scroll.addView(root);
        setContentView(scroll);
        root.requestFocus();

        TextView brand=text("MX  /  VISUAL",18,TEAL,true);
        brand.setLetterSpacing(.12f);
        root.addView(brand,lp(-1,-2,5));
        line(root,"Aprendes tocando. El código viene después.",13,MUTED,false,5);
        line(root,"EXPERIMENTO "+(mission+1)+" DE "+MISSIONS.length,11,GOLD,true,25);
        progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
        progress.setMax(MISSIONS.length);
        progress.setProgress(completedCount());
        progress.setProgressTintList(ColorStateList.valueOf(TEAL));
        progress.setProgressBackgroundTintList(ColorStateList.valueOf(EDGE));
        root.addView(progress,lp(-1,dp(5),10));

        line(root,lesson.title,28,WHITE,true,25);
        line(root,lesson.story,15,MUTED,false,9);

        LinearLayout target=surface(PANEL,16);
        line(target,"TU MISIÓN",11,GOLD,true,0);
        line(target,lesson.goal,16,WHITE,true,9);
        root.addView(target,lp(-1,-2,22));

        LinearLayout lab=surface(PANEL2,20);
        line(lab,"ESTADO DEL EXPERIMENTO",11,TEAL,true,0);
        quantum=new QuantumView(this);
        lab.addView(quantum,lp(-1,dp(165),10));
        line(lab,"RESULTADOS POSIBLES",11,GOLD,true,8);
        line(lab,"Estas barras muestran probabilidades calculadas por Mundo X, sin medir el qubit.",12,MUTED,false,6);
        histogram=column();
        lab.addView(histogram,lp(-1,-2,9));
        root.addView(lab,lp(-1,-2,17));

        line(root,lesson.actions.length==0?"PRIMER PASO":"TOCA PARA EXPERIMENTAR",12,TEAL,true,22);
        if(lesson.actions.length==0) {
            line(root,"No tienes que hacer nada todavía: observa la barra y pulsa COMPROBAR.",14,WHITE,false,10);
        }
        options=column();
        root.addView(options,lp(-1,-2,7));
        for(int i=0;i<lesson.actions.length;i+=2) {
            LinearLayout row=new LinearLayout(this);
            for(int j=i;j<Math.min(i+2,lesson.actions.length);j++) {
                Action action=lesson.actions[j];
                LinearLayout tile=column();
                tile.setPadding(dp(12),dp(14),dp(10),dp(13));
                tile.setBackground(shape(PANEL,15,EDGE));
                TextView title=text(action.label,13,WHITE,true);
                TextView subtitle=text(action.meaning,11,MUTED,false);
                tile.addView(title);
                tile.addView(subtitle,lp(-1,-2,7));
                tile.setContentDescription(action.label+". "+action.meaning);
                tile.setOnClickListener(v->apply(action));
                LinearLayout.LayoutParams tileParam=new LinearLayout.LayoutParams(0,dp(79),1);
                if(j>i)tileParam.leftMargin=dp(9);
                row.addView(tile,tileParam);
            }
            options.addView(row,lp(-1,-2,9));
        }

        LinearLayout effect=surface(PANEL2,16);
        line(effect,"¿QUÉ ACABA DE PASAR?",11,GOLD,true,0);
        explanation=text(lastMessage,14,WHITE,false);
        effect.addView(explanation,lp(-1,-2,8));
        line(effect,"SECUENCIA DE ACCIONES",11,TEAL,true,16);
        sequence=column();
        effect.addView(sequence,lp(-1,-2,7));
        root.addView(effect,lp(-1,-2,18));

        LinearLayout controls=new LinearLayout(this);
        undo=button("↶ DESHACER",false);
        controls.addView(undo,new LinearLayout.LayoutParams(0,dp(48),1));
        Button reset=button("REINICIAR",false);
        LinearLayout.LayoutParams rp=new LinearLayout.LayoutParams(0,dp(48),1);
        rp.leftMargin=dp(8);
        controls.addView(reset,rp);
        root.addView(controls,lp(-1,-2,12));
        undo.setOnClickListener(v->undo());
        reset.setOnClickListener(v->{
            steps.clear();
            lastMessage="Reiniciaste el experimento: el registro vuelve a cero.";
            refresh();
        });

        verify=button("COMPROBAR MI EXPERIMENTO",true);
        root.addView(verify,lp(-1,dp(56),15));
        verify.setOnClickListener(v->check());

        feedback=text("La máquina te dirá si alcanzaste el objetivo.",13,MUTED,false);
        root.addView(feedback,lp(-1,-2,11));

        TextView hint=text("PISTA: "+lesson.hint,13,MUTED,false);
        root.addView(hint,lp(-1,-2,15));

        codeToggle=text("▸ MOSTRAR CÓMO SE ESCRIBE EN MX",13,TEAL,true);
        codeToggle.setPadding(dp(12),dp(17),dp(12),dp(17));
        codeToggle.setBackground(shape(PANEL,14,EDGE));
        codeToggle.setOnClickListener(v->{
            codeRevealed=!codeRevealed;
            syncCode();
        });
        root.addView(codeToggle,lp(-1,-2,23));
        codeBox=surface(PANEL2,14);
        codeBox.setVisibility(View.GONE);
        line(codeBox,"ESTE ES EL MISMO EXPERIMENTO EN TU LENGUAJE MX",11,GOLD,true,0);
        codePreview=text("",13,WHITE,false);
        codePreview.setTypeface(MONO);
        codePreview.setTextIsSelectable(true);
        codeBox.addView(codePreview,lp(-1,-2,12));
        Button copy=button("COPIAR CÓDIGO",false);
        copy.setOnClickListener(v->{
            ClipboardManager clipboard=(ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
            if(clipboard!=null) {
                clipboard.setPrimaryClip(ClipData.newPlainText("MX",makeSource()));
                copy.setText("COPIADO");
            }
        });
        codeBox.addView(copy,lp(-1,dp(45),12));
        root.addView(codeBox,lp(-1,-2,8));

        LinearLayout navigation=new LinearLayout(this);
        Button back=button("← ANTERIOR",false);
        back.setEnabled(mission>0);
        navigation.addView(back,new LinearLayout.LayoutParams(0,dp(52),1));
        next=button(mission==MISSIONS.length-1?"FIN DEL NIVEL":"SIGUIENTE →",false);
        next.setEnabled(mission<unlocked);
        LinearLayout.LayoutParams np=new LinearLayout.LayoutParams(0,dp(52),1);
        np.leftMargin=dp(8);
        navigation.addView(next,np);
        root.addView(navigation,lp(-1,-2,23));
        back.setOnClickListener(v->gotoMission(mission-1));
        next.setOnClickListener(v->gotoMission(mission+1));
        line(root,"Curso inicial de MX · Sin Internet · Núcleo original Mundo X",11,MUTED,false,21);

        refresh();
    }

    private void apply(Action choice) {
        if(steps.size()>=18) {
            feedback.setText("Máximo 18 acciones: reinicia o deshaz algunas.");
            return;
        }
        steps.add(choice.key);
        lastMessage=choice.feedback;
        feedback.setText("Observa el cambio. Puedes seguir probando o comprobar.");
        feedback.setTextColor(MUTED);
        refresh();
    }
    private void undo() {
        if(steps.isEmpty())return;
        steps.remove(steps.size()-1);
        lastMessage="Deshiciste la última operación. El estado se recalcula.";
        refresh();
    }
    private int completedCount() {
        int count=0;
        for(int i=0;i<MISSIONS.length;i++)
            if(preferences.getBoolean("done_"+i,false))count++;
        return count;
    }
    private void gotoMission(int target) {
        if(target<0||target>=MISSIONS.length||target>unlocked)return;
        mission=target;
        preferences.edit().putInt("last",target).apply();
        showMission();
    }
    private String makeSource() {
        StringBuilder source=new StringBuilder();
        source.append("backend ").append(mission==8?"reduced":"compact").append(";\n");
        source.append("qubits ").append(MISSIONS[mission].width).append(";\n");
        for(String gate:steps) {
            switch(gate) {
                case "X0":source.append("x(q[0]);\n");break;
                case "H0":source.append("h(q[0]);\n");break;
                case "Z0":source.append("z(q[0]);\n");break;
                case "T0":source.append("t(q[0]);\n");break;
                case "CX01":source.append("cx(q[0], q[1]);\n");break;
                case "CX12":source.append("cx(q[1], q[2]);\n");break;
                case "REPEAT":source.append("repeat 3 as i {\n  x(q[i]);\n}\n");break;
                case "RECIPE":
                    source.append("circuit pareja(a: qubit, b: qubit) {\n");
                    source.append("  h(a);\n  cx(a, b);\n}\npareja(q[0], q[1]);\n");
                    break;
                default:throw new IllegalStateException("Acción desconocida.");
            }
        }
        source.append("show;\n");
        return source.toString();
    }
    private void syncCode() {
        codeBox.setVisibility(codeRevealed?View.VISIBLE:View.GONE);
        codeToggle.setText(codeRevealed?"▾ OCULTAR CÓDIGO":"▸ MOSTRAR CÓMO SE ESCRIBE EN MX");
        codePreview.setText(makeSource());
    }
    private String actionName(String key) {
        switch(key) {
            case "H0":return "H en q0 · abrir";
            case "X0":return "X en q0 · invertir";
            case "Z0":return "Z en q0 · fase";
            case "CX01":return "CNOT q0 → q1";
            case "CX12":return "CNOT q1 → q2";
            case "REPEAT":return "REPETIR · X en 3 qubits";
            case "RECIPE":return "RECETA · pareja";
            case "T0":return "T en q0 · fase";
            default:return key;
        }
    }
    private void updateSteps() {
        sequence.removeAllViews();
        if(steps.isEmpty()) {
            line(sequence,"Todavía no has aplicado ninguna.",12,MUTED,false,0);
        } else {
            for(int i=0;i<steps.size();i++) {
                TextView item=text((i+1)+".  "+actionName(steps.get(i)),13,WHITE,false);
                item.setPadding(dp(9),dp(7),dp(9),dp(7));
                item.setBackground(shape(PANEL,8,Color.TRANSPARENT));
                sequence.addView(item,lp(-1,-2,i==0?0:5));
            }
        }
        undo.setEnabled(!steps.isEmpty());
    }
    private String basisName(int value,int count) {
        StringBuilder s=new StringBuilder("|");
        for(int i=0;i<count;i++)s.append((value>>i)&1);
        return s.append("⟩").toString();
    }
    private void showProbabilities(int n,double[] probabilities) {
        histogram.removeAllViews();
        int visible=0;
        for(int i=0;i<probabilities.length;i++) {
            double p=probabilities[i];
            if(n>2 && p<0.0001)continue;
            LinearLayout row=new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            TextView ket=text(basisName(i,n),14,WHITE,true);
            ket.setTypeface(MONO);
            row.addView(ket,new LinearLayout.LayoutParams(dp(67),dp(32)));
            ProgressBar bar=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
            bar.setMax(1000);
            bar.setProgress((int)Math.round(1000*p));
            bar.setProgressTintList(ColorStateList.valueOf(TEAL));
            bar.setProgressBackgroundTintList(ColorStateList.valueOf(EDGE));
            row.addView(bar,new LinearLayout.LayoutParams(0,dp(13),1));
            TextView amount=text(String.format(Locale.US,"%.0f%%",100*p),13,WHITE,true);
            amount.setGravity(Gravity.END);
            row.addView(amount,new LinearLayout.LayoutParams(dp(62),dp(32)));
            histogram.addView(row,lp(-1,-2,visible==0?0:5));
            visible++;
        }
        if(n>2&&visible<probabilities.length)
            line(histogram,"Los demás resultados tienen probabilidad 0%.",11,MUTED,false,7);
    }
    private void refresh() {
        updateSteps();
        explanation.setText(lastMessage);
        if(codeRevealed)syncCode();
        try {
            JSONObject report=new JSONObject(inspectNative(makeSource()));
            if(!report.optBoolean("ok",false)) {
                feedback.setText("No se pudo calcular: "+report.optString("error"));
                feedback.setTextColor(GOLD);
                return;
            }
            int n=report.getInt("qubits");
            JSONArray a=report.getJSONArray("probabilities");
            double[] probabilities=new double[a.length()];
            for(int i=0;i<a.length();i++)probabilities[i]=a.getDouble(i);
            lastProbabilities=probabilities;
            showProbabilities(n,probabilities);
            quantum.setExperiment(n,steps);
        } catch(Exception e) {
            feedback.setText("Error del experimento: "+e.getMessage());
            feedback.setTextColor(GOLD);
        }
    }
    private void check() {
        try {
            String grade=evaluate(makeSource(),mission);
            boolean success=grade.startsWith("APROBADO\n");
            feedback.setText(grade);
            feedback.setTextColor(success?TEAL:GOLD);
            if(success) {
                preferences.edit().putBoolean("done_"+mission,true).apply();
                if(mission==unlocked&&mission<MISSIONS.length-1) {
                    unlocked++;
                    preferences.edit().putInt("unlocked",unlocked).apply();
                }
                next.setEnabled(mission<unlocked);
                progress.setProgress(completedCount());
                lastMessage="Tu experimento coincide con el objetivo. Ahora puedes revelar cómo se escribe en MX.";
                explanation.setText(lastMessage);
            }
        } catch(Exception e) {
            feedback.setText("No fue posible comprobar: "+e.getMessage());
            feedback.setTextColor(GOLD);
        }
    }

    // Light, offline, animated conceptual qubit view. The probability chart
    // below is the authoritative mathematical result from the native engine.
    private final class QuantumView extends View {
        private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
        private int n=1;
        private boolean link01=false,link12=false;
        private long motionStart=0L;
        QuantumView(Context context) {super(context);}
        void setExperiment(int count,List<String> actions) {
            n=count;link01=actions.contains("CX01")||actions.contains("RECIPE");
            link12=actions.contains("CX12");
            motionStart=SystemClock.uptimeMillis();
            invalidate();
        }
        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float w=getWidth(),h=getHeight();
            float mid=h*.56f;
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(dp(1));
            for(int i=0;i<7;i++) {
                p.setColor(Color.argb(30,90,159,191));
                float x=w*(i+1)/8f;
                canvas.drawLine(x,dp(7),x,h-dp(8),p);
            }
            float[] xs=new float[3];
            for(int i=0;i<n;i++)xs[i]=w*(i+1)/(n+1f);
            if(n>1) {
                p.setColor(PURPLE);p.setStrokeWidth(dp(2.3f));
                if(link01)canvas.drawLine(xs[0],mid,xs[1],mid,p);
                if(link12)canvas.drawLine(xs[1],mid,xs[2],mid,p);
            }
            long elapsed=SystemClock.uptimeMillis()-motionStart;
            float motion=(float)Math.max(0,1.0-Math.min(1.0,elapsed/850.0));
            for(int i=0;i<n;i++) {
                final float x=xs[i],radius=dp(28);
                p.setStyle(Paint.Style.FILL);
                p.setColor(Color.argb(32,77,232,196));
                canvas.drawCircle(x,mid,radius+dp(13)+motion*dp(8),p);
                p.setColor(PANEL);
                canvas.drawCircle(x,mid,radius,p);
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeWidth(dp(2.5f));
                p.setColor(TEAL);
                canvas.drawCircle(x,mid,radius,p);
                p.setStrokeWidth(dp(1));
                p.setColor(Color.argb(130,183,151,255));
                RectF orbit=new RectF(x-radius-dp(10),mid-radius*.7f,
                    x+radius+dp(10),mid+radius*.7f);
                canvas.drawOval(orbit,p);
                p.setStyle(Paint.Style.FILL);
                p.setColor(WHITE);
                p.setTypeface(Typeface.DEFAULT_BOLD);
                p.setTextAlign(Paint.Align.CENTER);
                p.setTextSize(dp(17));
                canvas.drawText("q"+i,x,mid+dp(6),p);
            }
            p.setColor(MUTED);
            p.setTextSize(dp(11));
            p.setTextAlign(Paint.Align.CENTER);
            canvas.drawText(n==1?"1 QUBIT":n+" QUBITS",w/2,dp(22),p);
            if(elapsed<850)postInvalidateDelayed(33);
        }
    }
}
