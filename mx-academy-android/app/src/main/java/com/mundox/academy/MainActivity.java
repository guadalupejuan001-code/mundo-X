package com.mundox.academy;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.Window;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

public final class MainActivity extends Activity {
    static { System.loadLibrary("mxlearn"); }
    private native String evaluate(String source, int lesson);

    private static final int BG=Color.rgb(8,15,27);
    private static final int PANEL=Color.rgb(18,31,49);
    private static final int CODE=Color.rgb(11,22,38);
    private static final int EDGE=Color.rgb(43,62,79);
    private static final int WHITE=Color.rgb(235,246,252);
    private static final int MUTED=Color.rgb(153,174,192);
    private static final int TEAL=Color.rgb(69,224,186);
    private static final int GOLD=Color.rgb(255,207,111);

    private static final class Lesson {
        final String title, concept, goal, starter, hint;
        Lesson(String t,String c,String g,String s,String h) {
            title=t;concept=c;goal=g;starter=s;hint=h;
        }
    }
    private static final Lesson[] LESSONS={
        new Lesson("El registro inicial",
            "Un qubit es una unidad de información cuántica. En MX, qubits 1; "
            +"crea q[0] en |0⟩. show; inspecciona el estado.",
            "Crea un qubit que permanezca en cero.",
            "backend compact;\nqubits 1;\nshow;\n",
            "Ya comienza en |0⟩. Ejecuta el programa sin modificarlo."),
        new Lesson("Superposición con H",
            "h(q[0]); aplica Hadamard. Sobre |0⟩ prepara (|0⟩+|1⟩)/√2.",
            "Prepara un qubit en superposición.",
            "backend compact;\nqubits 1;\n// Añade Hadamard aquí.\nshow;\n",
            "Escribe h(q[0]); antes de show;"),
        new Lesson("Invertir con X",
            "x(q[0]); transforma |0⟩ en |1⟩. Es una puerta Pauli X.",
            "Deja q[0] en el estado |1⟩.",
            "backend compact;\nqubits 1;\n// Invierte este qubit.\nshow;\n",
            "Escribe x(q[0]);"),
        new Lesson("Entrelazamiento",
            "cx(control, objetivo); ejecuta CNOT. H seguido de CNOT "
            +"genera el estado de Bell (|00⟩+|11⟩)/√2.",
            "Entrelaza q[0] con q[1].",
            "backend compact;\nqubits 2;\nh(q[0]);\n// Conecta ambos.\nshow;\n",
            "Escribe cx(q[0], q[1]);"),
        new Lesson("Repetir sin copiar",
            "repeat N as i { ... } expande repeticiones al compilar. "
            +"El índice i empieza en cero y q[i] señala cada qubit.",
            "Usa un bucle para dejar los tres qubits en |1⟩.",
            "backend compact;\nqubits 3;\nrepeat 3 as i {\n"
            +"  // Invierte q[i].\n}\nshow;\n",
            "Dentro del bloque escribe x(q[i]);"),
        new Lesson("Tu propio circuito",
            "circuit nombre(a: qubit, b: qubit) { ... } define bloques "
            +"reutilizables con parámetros tipados.",
            "Completa pareja() para crear un Bell.",
            "backend compact;\nqubits 2;\ncircuit pareja(a: qubit, b: qubit) {\n"
            +"  h(a);\n  // Completa esta operación.\n}\npareja(q[0], q[1]);\nshow;\n",
            "Dentro del circuito añade cx(a, b);"),
        new Lesson("Fase T",
            "backend reduced; activa el motor que permite t(q[0]);, "
            +"una fase relativa π/4 más allá de Clifford.",
            "Aplica H y T a un qubit.",
            "backend reduced;\nqubits 1;\nh(q[0]);\n// Aplica T.\nshow;\n",
            "Escribe t(q[0]);"),
        new Lesson("GHZ de tres",
            "Combina H y CNOT para preparar (|000⟩+|111⟩)/√2.",
            "Construye tu propio estado GHZ de tres qubits.",
            "backend compact;\nlet N = 3;\nqubits N;\nh(q[0]);\n"
            +"// Conecta los tres qubits.\nshow;\n",
            "Escribe cx(q[0], q[1]); y cx(q[1], q[2]);")
    };

    private SharedPreferences prefs;
    private int level, unlocked;
    private EditText editor;
    private TextView console;
    private Button executeButton, next;
    private ProgressBar progress;

    private int dp(float n) {
        return (int)(n*getResources().getDisplayMetrics().density+0.5f);
    }
    private GradientDrawable shape(int fill,int radius,int border) {
        GradientDrawable s=new GradientDrawable();
        s.setColor(fill);s.setCornerRadius(dp(radius));
        if(border!=Color.TRANSPARENT)s.setStroke(dp(1),border);
        return s;
    }
    private LinearLayout column() {
        LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;
    }
    private LinearLayout.LayoutParams params(int w,int h,int top) {
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(w,h);
        p.topMargin=dp(top);return p;
    }
    private TextView text(String value,int size,int color,boolean bold) {
        TextView t=new TextView(this);
        t.setText(value);t.setTextSize(size);t.setTextColor(color);
        if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        return t;
    }
    private void label(LinearLayout l,String value,int size,int color,boolean bold,int top) {
        l.addView(text(value,size,color,bold),params(-1,-2,top));
    }
    private Button button(String caption,boolean filled) {
        Button b=new Button(this);
        b.setText(caption);b.setAllCaps(false);b.setTextSize(13);
        b.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        b.setTextColor(filled?BG:WHITE);
        b.setBackground(shape(filled?TEAL:PANEL,13,filled?Color.TRANSPARENT:EDGE));
        b.setPadding(dp(9),0,dp(9),0);
        return b;
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        Window w=getWindow();w.setStatusBarColor(BG);w.setNavigationBarColor(BG);
        prefs=getSharedPreferences("mx_academia_v1",MODE_PRIVATE);
        unlocked=Math.min(7,Math.max(0,prefs.getInt("unlocked",0)));
        level=Math.min(unlocked,Math.max(0,prefs.getInt("level",0)));
        render();
    }
    private void saveDraft() {
        if(editor!=null)prefs.edit().putString("draft_"+level,editor.getText().toString()).apply();
    }
    @Override protected void onPause(){saveDraft();super.onPause();}
    private void changeLesson(int target) {
        if(target<0||target>=LESSONS.length||target>unlocked)return;
        saveDraft();level=target;prefs.edit().putInt("level",target).apply();render();
    }
    private void render() {
        Lesson lesson=LESSONS[level];
        ScrollView scroll=new ScrollView(this);
        scroll.setBackgroundColor(BG);scroll.setFillViewport(true);
        LinearLayout root=column();
        root.setPadding(dp(20),dp(24),dp(20),dp(36));
        root.setFocusableInTouchMode(true);
        scroll.addView(root);setContentView(scroll);root.requestFocus();

        TextView brand=text("MX / ACADEMIA",17,TEAL,true);
        brand.setLetterSpacing(.16f);
        root.addView(brand,params(-1,-2,4));
        label(root,"Aprende tu lenguaje. Ejecuta tu núcleo.",12,MUTED,false,7);
        label(root,"LECCIÓN "+(level+1)+" / 8 · SIN INTERNET",12,GOLD,true,28);
        progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
        progress.setMax(8);
        progress.setProgress(unlocked+(prefs.getBoolean("done_7",false)?1:0));
        progress.setProgressTintList(ColorStateList.valueOf(TEAL));
        progress.setProgressBackgroundTintList(ColorStateList.valueOf(EDGE));
        root.addView(progress,params(-1,dp(5),11));

        label(root,lesson.title,28,WHITE,true,28);
        label(root,lesson.concept,15,MUTED,false,13);

        LinearLayout goal=column();
        goal.setPadding(dp(17),dp(15),dp(17),dp(17));
        goal.setBackground(shape(PANEL,17,EDGE));
        label(goal,"OBJETIVO",11,GOLD,true,0);
        label(goal,lesson.goal,16,WHITE,true,9);
        root.addView(goal,params(-1,-2,24));

        label(root,"TU ARCHIVO .MX",12,TEAL,true,25);
        label(root,"Edita el código y ejecuta el circuito.",12,MUTED,false,6);
        editor=new EditText(this);
        editor.setTypeface(Typeface.MONOSPACE);
        editor.setTextColor(WHITE);editor.setTextSize(14);
        editor.setGravity(Gravity.TOP|Gravity.START);
        editor.setSingleLine(false);editor.setHorizontallyScrolling(false);
        editor.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_MULTI_LINE
            |InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        editor.setPadding(dp(16),dp(16),dp(16),dp(16));
        editor.setBackground(shape(CODE,16,EDGE));
        editor.setMinLines(10);editor.setMaxLines(18);
        editor.setText(prefs.getString("draft_"+level,lesson.starter));
        editor.setSelection(editor.length());
        root.addView(editor,params(-1,-2,11));

        LinearLayout row=new LinearLayout(this);
        executeButton=button("▶ EJECUTAR MX",true);
        row.addView(executeButton,new LinearLayout.LayoutParams(0,dp(52),1));
        Button reset=button("REINICIAR",false);
        LinearLayout.LayoutParams resetp=new LinearLayout.LayoutParams(dp(113),dp(52));
        resetp.leftMargin=dp(8);row.addView(reset,resetp);
        root.addView(row,params(-1,-2,13));

        LinearLayout terminal=column();
        terminal.setPadding(dp(16),dp(15),dp(16),dp(18));
        terminal.setBackground(shape(CODE,16,EDGE));
        label(terminal,"SALIDA DEL NÚCLEO",11,GOLD,true,0);
        console=text("Ejecuta tu archivo MX.",13,MUTED,false);
        console.setTypeface(Typeface.MONOSPACE);
        console.setTextIsSelectable(true);
        terminal.addView(console,params(-1,-2,12));
        root.addView(terminal,params(-1,-2,16));

        LinearLayout hint=column();
        hint.setPadding(dp(16),dp(15),dp(16),dp(16));
        hint.setBackground(shape(PANEL,16,Color.TRANSPARENT));
        label(hint,"PISTA",11,TEAL,true,0);
        label(hint,lesson.hint,13,WHITE,false,8);
        root.addView(hint,params(-1,-2,17));

        LinearLayout nav=new LinearLayout(this);
        Button prev=button("← ANTERIOR",false);
        prev.setEnabled(level>0);
        nav.addView(prev,new LinearLayout.LayoutParams(0,dp(52),1));
        next=button("SIGUIENTE →",false);
        next.setEnabled(level<unlocked);
        LinearLayout.LayoutParams np=new LinearLayout.LayoutParams(0,dp(52),1);
        np.leftMargin=dp(8);nav.addView(next,np);
        root.addView(nav,params(-1,-2,22));
        label(root,"MX y el motor cuántico originales de Mundo X.",11,MUTED,false,22);

        prev.setOnClickListener(v->changeLesson(level-1));
        next.setOnClickListener(v->changeLesson(level+1));
        reset.setOnClickListener(v->{
            editor.setText(lesson.starter);editor.setSelection(editor.length());
            saveDraft();console.setText("Código inicial restaurado.");
        });
        executeButton.setOnClickListener(v->runLesson());
    }
    private void runLesson() {
        final int tested=level;
        final String source=editor.getText().toString();
        saveDraft();executeButton.setEnabled(false);
        console.setTextColor(MUTED);
        console.setText("Compilando y verificando el estado...");
        InputMethodManager imm=(InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
        if(imm!=null)imm.hideSoftInputFromWindow(editor.getWindowToken(),0);
        new Thread(()->{
            String r;
            try{r=evaluate(source,tested);}
            catch(Throwable error){r="ERROR\nNo se pudo iniciar el motor: "+error.getMessage();}
            final String result=r;
            runOnUiThread(()->{
                if(tested!=level||isFinishing())return;
                executeButton.setEnabled(true);
                boolean success=result.startsWith("APROBADO\n");
                console.setTextColor(success?TEAL:result.startsWith("ERROR")?GOLD:WHITE);
                console.setText(result);
                if(success){
                    prefs.edit().putBoolean("done_"+tested,true).apply();
                    if(tested==unlocked&&unlocked<7){
                        unlocked++;prefs.edit().putInt("unlocked",unlocked).apply();
                    }
                    next.setEnabled(level<unlocked);
                    progress.setProgress(unlocked+(prefs.getBoolean("done_7",false)?1:0));
                }
            });
        },"MX-grading").start();
    }
}
