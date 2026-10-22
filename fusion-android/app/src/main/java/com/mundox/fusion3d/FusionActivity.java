package com.mundox.fusion3d;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.view.WindowInsets;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.GridLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import com.cosmicx.pc.FusionPhysics;

import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

/**
 * Standalone, offline isotope/fusion research desk for Android.
 * Real tabulated mass differences & classical Coulomb potential;
 * physically delimited diagrams ray-traced by the original EtherRenderer.
 */
public final class FusionActivity extends Activity {
    private static final int EXPORT_RESULT=801;
    private static final int EXPORT_HAMILTONIAN=802;
    private static final int BG=Color.rgb(8,14,23);
    private static final int PANEL=Color.rgb(15,27,41);
    private static final int BORDER=Color.rgb(49,78,94);
    private static final int TEXT=Color.rgb(236,245,249);
    private static final int SUB=Color.rgb(158,183,199);
    private static final int GREEN=Color.rgb(120,239,215);
    private static final int ORANGE=Color.rgb(255,177,118);
    private static final int BLUE=Color.rgb(106,167,225);
    private static final String NIST="https://physics.nist.gov/cgi-bin/Compositions/stand_alone.pl?all=all&ascii=ascii&ele=&isotype=all";
    private static final String NNDC="https://www.nndc.bnl.gov/qcalc/";
    private static final String DOE="https://www.energy.gov/science/doe-explainsfusion-reactions";

    private final Handler ui=new Handler(Looper.getMainLooper());
    private final ExecutorService milWorker=Executors.newSingleThreadExecutor(r->{
        Thread t=new Thread(r,"Mundo-X-MIL-Radial");
        t.setPriority(Thread.NORM_PRIORITY-1);
        return t;
    });
    private volatile boolean closed=false;
    private boolean milRunning=false;
    private long milModelRevision=0;
    private int milQubits=3;
    private Button milRunButton,milSizeButton;
    private SeekBar milTimeControl;
    private TextView milTimeLabel,milSummary;
    private MilProbabilityView milChart;
    private MilRadialEvolver.Result lastMilResult;
    private final Runnable frameStepper=new Runnable() {
        @Override public void run(){
            if(!playing)return;
            int next=timeline.getProgress()+22;
            if(next>=1000){
                timeline.setProgress(1000);
                playing=false;
                playButton.setText("Reproducir trayectoria");
                return;
            }
            timeline.setProgress(next);
            ui.postDelayed(this,110);
        }
    };

    private int z1=1,z2=1,a1=2,a2=3,channel=0;
    private boolean expert=false,playing=false;
    private NuclearInteractionModel.Result nuclearModel;
    private RadialHamiltonian.Grid radialHamiltonian;
    private NuclearPotentialGraphView nuclearGraph;
    private TextView nuclearSummary, nuclearDepthCaption, hamiltonianSummary;
    private SeekBar nuclearDepth;
    private Button nuclearZoomButton, exportHamiltonianButton;
    private Button firstElement,secondElement,channelButton,playButton,exportButton,expertButton;
    private TextView selectedHint,collisionLabel,stageLabel,formulaSummary,reactionSummary,unknownWarning,stepInfo,detailsView;
    private EditText isotopeA,isotopeB;
    private SeekBar energyBar,timeline;
    private EtherRenderView renderer;
    private CoulombGraphView potential;
    private FusionPhysics.Analysis analysis;
    private CollisionModel.Result model;
    private boolean hasResult=false;

    private int dp(float d){return (int)(d*getResources().getDisplayMetrics().density+.5f);}
    private GradientDrawable shape(int fill,int stroke,int radius){
        GradientDrawable bg=new GradientDrawable();
        bg.setColor(fill);
        bg.setCornerRadius(dp(radius));
        if(stroke!=0)bg.setStroke(dp(1),stroke);
        return bg;
    }
    private TextView text(String content,int sp,int color){
        TextView t=new TextView(this);
        t.setText(content);
        t.setTextColor(color);
        t.setTextSize(sp);
        t.setLineSpacing(dp(2),1.06f);
        return t;
    }
    private TextView title(String content,int sp){
        TextView t=text(content,sp,TEXT);
        t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        return t;
    }
    private TextView section(String content) {
        TextView t=text(content,12,GREEN);
        t.setLetterSpacing(.11f);
        t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        t.setPadding(0,0,0,dp(9));
        return t;
    }
    private LinearLayout column(){
        LinearLayout v=new LinearLayout(this);
        v.setOrientation(LinearLayout.VERTICAL);
        return v;
    }
    private LinearLayout row(){
        LinearLayout v=new LinearLayout(this);
        v.setOrientation(LinearLayout.HORIZONTAL);
        v.setGravity(Gravity.CENTER_VERTICAL);
        return v;
    }
    private LinearLayout panel(LinearLayout parent){
        LinearLayout p=column();
        p.setPadding(dp(15),dp(15),dp(15),dp(15));
        p.setBackground(shape(PANEL,BORDER,16));
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);
        lp.topMargin=dp(14);
        parent.addView(p,lp);
        return p;
    }
    private Button button(String content,boolean primary){
        Button v=new Button(this);
        v.setText(content);
        v.setAllCaps(false);
        v.setTextSize(12);
        v.setTextColor(primary?BG:TEXT);
        v.setBackground(shape(primary?GREEN:Color.rgb(26,45,61),BORDER,11));
        v.setMinHeight(dp(45));
        return v;
    }
    private void half(LinearLayout parent,View first,View second){
        LinearLayout.LayoutParams l=new LinearLayout.LayoutParams(0,-2,1f);
        l.rightMargin=dp(7);
        parent.addView(first,l);
        parent.addView(second,new LinearLayout.LayoutParams(0,-2,1f));
    }
    private EditText isotope(int a){
        EditText v=new EditText(this);
        v.setText(Integer.toString(a));
        v.setTextColor(TEXT);
        v.setTextSize(16);
        v.setTypeface(Typeface.MONOSPACE);
        v.setSingleLine(true);
        v.setSelectAllOnFocus(true);
        v.setInputType(InputType.TYPE_CLASS_NUMBER);
        v.setBackground(shape(BG,BORDER,9));
        v.setPadding(dp(12),dp(7),dp(12),dp(7));
        return v;
    }

    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        if(state!=null){
            z1=state.getInt("z1",1);z2=state.getInt("z2",1);
            a1=state.getInt("a1",2);a2=state.getInt("a2",3);
            channel=state.getInt("channel",0);
            expert=state.getBoolean("expert",false);
        }
        build();
        setPresets(false,z1,a1,z2,a2);
        if(state!=null){
            energyBar.setProgress(state.getInt("energy",CollisionModel.energyToSlider(.02)));
            nuclearDepth.setProgress(state.getInt("nuclear_depth",15));
            timeline.setProgress(state.getInt("timeline",0));
            milQubits=state.getInt("mil_qubits",3)==2?2:3;
            milTimeControl.setProgress(state.getInt("mil_time",500));
            if(milSizeButton!=null)milSizeButton.setText(
                    milQubits==3?"3 qubits / 8 posiciones":"2 qubits / 4 posiciones");
        }
        updateReport();
    }

    @Override protected void onSaveInstanceState(Bundle out){
        super.onSaveInstanceState(out);
        out.putInt("z1",z1);out.putInt("z2",z2);
        out.putInt("a1",currentMass(isotopeA,a1));out.putInt("a2",currentMass(isotopeB,a2));
        out.putInt("channel",channel);
        out.putBoolean("expert",expert);
        out.putInt("energy",energyBar.getProgress());
        out.putInt("nuclear_depth",nuclearDepth.getProgress());
        out.putInt("mil_time",milTimeControl.getProgress());
        out.putInt("mil_qubits",milQubits);
        out.putInt("timeline",timeline.getProgress());
    }

    private void build(){
        ScrollView scroll=new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BG);
        if(Build.VERSION.SDK_INT>=30){
            scroll.setOnApplyWindowInsetsListener((view,insets)->{
                android.graphics.Insets bars=insets.getInsets(
                        WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
                android.graphics.Insets keyboard=insets.getInsets(WindowInsets.Type.ime());
                view.setPadding(bars.left,bars.top,bars.right,
                        Math.max(bars.bottom,keyboard.bottom));
                return insets;
            });
        }else scroll.setFitsSystemWindows(true);
        LinearLayout body=column();
        body.setPadding(dp(16),dp(17),dp(16),dp(32));
        scroll.addView(body);
        setContentView(scroll);

        TextView brand=text("MUNDO X / COSMIC X ENGINE / SCIENCE",11,GREEN);
        brand.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        brand.setLetterSpacing(.12f);
        body.addView(brand);
        TextView big=title("FUSION / 3D",29);
        big.setPadding(0,dp(5),0,dp(5));
        body.addView(big);
        body.addView(text("Investiga reacciones nucleares, inspecciona sus modelos físicos y explora una escena 3D calculada con Ether Render X.",13,SUB));
        TextView newModelInfo=text(
                "NUEVO v1.1  ·  Interacción Coulomb + Woods–Saxon, penetración WKB "+
                "y Hamiltoniano radial exportable. Desplázate hasta el panel 03 para estudiarlos.",
                11,GREEN);
        newModelInfo.setPadding(0,dp(10),0,0);
        body.addView(newModelInfo);

        LinearLayout viewport=panel(body);
        viewport.addView(section("VISOR ESPACIAL · ETHER RENDER X"));
        renderer=new EtherRenderView(this);
        renderer.setContentDescription("fusion-3d-native-ether-renderer");
        viewport.addView(renderer,new LinearLayout.LayoutParams(-1,dp(263)));
        viewport.addView(text("Renderizado nativo 3D propio: arrastra para orbitar y pellizca para zoom. Las partículas y trayectorias son representaciones, no posiciones nucleares observadas.",11,SUB));
        stageLabel=title("01 · Aproximación conceptual",12);
        stageLabel.setPadding(0,dp(10),0,dp(2));
        viewport.addView(stageLabel);
        timeline=new SeekBar(this);
        timeline.setMax(1000);
        timeline.setProgress(0);
        timeline.setContentDescription("Control de avance visual de la escena 3D, no tiempo físico");
        viewport.addView(timeline);
        timeline.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            @Override public void onStartTrackingTouch(SeekBar seekBar){}
            @Override public void onStopTrackingTouch(SeekBar seekBar){}
            @Override public void onProgressChanged(SeekBar seekBar,int value,boolean fromUser){
                if(renderer!=null)renderer.setProgress(value/1000f);
                updateStageText();
            }
        });
        LinearLayout animationRow=row();
        viewport.addView(animationRow);
        playButton=button("Reproducir trayectoria",true);
        Button rewind=button("Inicio",false);
        Button resetCamera=button("Cámara",false);
        LinearLayout.LayoutParams flex=new LinearLayout.LayoutParams(0,dp(48),2);
        flex.rightMargin=dp(6);
        animationRow.addView(playButton,flex);
        LinearLayout.LayoutParams small=new LinearLayout.LayoutParams(0,dp(48),1);
        small.rightMargin=dp(6);
        animationRow.addView(rewind,small);
        animationRow.addView(resetCamera,new LinearLayout.LayoutParams(0,dp(48),1));
        playButton.setOnClickListener(v->togglePlay());
        rewind.setOnClickListener(v->{stopPlay();timeline.setProgress(0);});
        resetCamera.setOnClickListener(v->renderer.resetCamera());

        LinearLayout choices=panel(body);
        choices.addView(section("01 / SELECCIONA DOS NÚCLEOS"));
        choices.addView(text("Cada elemento determina sus protones (Z). El número másico A indica protones + neutrones. Puedes elegir cualquier elemento de la tabla.",12,SUB));
        LinearLayout pickRow=row();
        choices.addView(pickRow);
        firstElement=button("Elegir A",true);
        secondElement=button("Elegir B",false);
        firstElement.setContentDescription("fusion-select-element-A");
        secondElement.setContentDescription("fusion-select-element-B");
        half(pickRow,firstElement,secondElement);
        firstElement.setOnClickListener(v->showElementPicker(true));
        secondElement.setOnClickListener(v->showElementPicker(false));
        LinearLayout massRow=row();
        choices.addView(massRow);
        LinearLayout first=column();
        first.addView(text("Isótopo A · nº másico",11,SUB));
        isotopeA=isotope(a1);
        first.addView(isotopeA,new LinearLayout.LayoutParams(-1,dp(48)));
        LinearLayout second=column();
        second.addView(text("Isótopo B · nº másico",11,SUB));
        isotopeB=isotope(a2);
        second.addView(isotopeB,new LinearLayout.LayoutParams(-1,dp(48)));
        half(massRow,first,second);
        selectedHint=text("Núcleo A · núcleo B",12,SUB);
        selectedHint.setPadding(0,dp(10),0,0);
        choices.addView(selectedHint);
        choices.addView(text("Seleccionar un número másico no garantiza que el isótopo exista o sea estable. Solo asignamos valores Q a canales tabulados.",11,ORANGE));
        Button analyze=button("ANALIZAR LOS NÚCLEOS",true);
        LinearLayout.LayoutParams analyzeLayout=new LinearLayout.LayoutParams(-1,dp(50));
        analyzeLayout.topMargin=dp(12);
        choices.addView(analyze,analyzeLayout);
        analyze.setOnClickListener(v->{
            stopPlay();
            timeline.setProgress(0);
            updateReport();
        });

        LinearLayout presets=panel(body);
        presets.addView(section("CANALES NUCLEARES CON DATOS"));
        presets.addView(text("Empieza con una reacción conocida o explora una pareja diferente.",12,SUB));
        LinearLayout pre1=row(),pre2=row();
        presets.addView(pre1);
        presets.addView(pre2);
        Button dT=button("²H + ³H",false);
        Button dD=button("²H + ²H",false);
        Button dHe=button("²H + ³He",false);
        Button dLi=button("²H + ⁶Li",false);
        half(pre1,dT,dD);half(pre2,dHe,dLi);
        dT.setOnClickListener(v->setPresets(true,1,2,1,3));
        dD.setOnClickListener(v->setPresets(true,1,2,1,2));
        dHe.setOnClickListener(v->setPresets(true,1,2,2,3));
        dLi.setOnClickListener(v->setPresets(true,1,2,3,6));

        LinearLayout collisions=panel(body);
        collisions.addView(section("02 / ENERGÍA DE COLISIÓN"));
        collisions.addView(text("Ajusta Ecm: energía cinética relativa en el sistema del centro de masas. No es la energía liberada Q.",12,SUB));
        collisionLabel=title("Ecm = 20 keV",17);
        collisionLabel.setPadding(0,dp(9),0,0);
        collisions.addView(collisionLabel);
        energyBar=new SeekBar(this);
        energyBar.setMax(1000);
        energyBar.setProgress(CollisionModel.energyToSlider(.020));
        energyBar.setContentDescription("fusion-center-of-mass-energy");
        collisions.addView(energyBar);
        energyBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            @Override public void onStartTrackingTouch(SeekBar b){}
            @Override public void onStopTrackingTouch(SeekBar b){updateReport();}
            @Override public void onProgressChanged(SeekBar b,int value,boolean user){
                double keV=CollisionModel.sliderToMeV(value)*1000;
                collisionLabel.setText(String.format(Locale.US,
                        "Ecm = %s keV",keV>=1000?String.format(Locale.US,"%.1f",keV):
                                keV>=10?String.format(Locale.US,"%.2f",keV):
                                        String.format(Locale.US,"%.3f",keV)));
                // A WKB integration is not a real-time slider effect:
                // compute on release, or when a saved value is restored.
                if(!user && isotopeA!=null&&formulaSummary!=null)updateReport();
            }
        });
        potential=new CoulombGraphView(this);
        collisions.addView(potential,new LinearLayout.LayoutParams(-1,dp(222)));
        formulaSummary=text("",12,TEXT);
        formulaSummary.setTypeface(Typeface.MONOSPACE);
        formulaSummary.setPadding(0,dp(11),0,dp(4));
        collisions.addView(formulaSummary);

        LinearLayout interactions=panel(body);
        interactions.addView(section("03 / INTERACCIÓN NUCLEAR Y TÚNEL"));
        interactions.addView(text(
                "Ahora calculamos la atracción nuclear aproximada (Woods-Saxon) "+
                "además de la repulsión de Coulomb. Para energías bajo la barrera, "+
                "la integral WKB estima la supresión de penetración cuántica. "+
                "NO equivale a probabilidad de fusión.",12,SUB));
        nuclearSummary=title("Preparando modelo nuclear…",13);
        nuclearSummary.setContentDescription("nuclear-wkb-model-result");
        nuclearSummary.setPadding(0,dp(12),0,dp(8));
        interactions.addView(nuclearSummary);
        nuclearDepthCaption=text("Profundidad de prueba V0 = 35 MeV",12,GREEN);
        interactions.addView(nuclearDepthCaption);
        nuclearDepth=new SeekBar(this);
        nuclearDepth.setMax(40);
        nuclearDepth.setProgress(15);
        nuclearDepth.setContentDescription("nuclear-woods-saxon-depth-control");
        interactions.addView(nuclearDepth);
        interactions.addView(text(
                "V0 = 20–60 MeV, parámetro ajustable de un potencial fenomenológico. "+
                "No se ha calibrado con datos de dispersión D–T.",11,ORANGE));
        nuclearGraph=new NuclearPotentialGraphView(this);
        nuclearGraph.setContentDescription("nuclear-potential-wkb-plot");
        interactions.addView(nuclearGraph,new LinearLayout.LayoutParams(-1,dp(240)));
        nuclearDepth.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            @Override public void onStartTrackingTouch(SeekBar b){}
            @Override public void onStopTrackingTouch(SeekBar b){updateReport();}
            @Override public void onProgressChanged(SeekBar b,int value,boolean fromUser){
                nuclearDepthCaption.setText("Profundidad de prueba V0 = "+(20+value)+" MeV");
                if(!fromUser && nuclearGraph!=null && energyBar!=null)updateReport();
            }
        });
        LinearLayout options=row();
        interactions.addView(options);
        nuclearZoomButton=button("Ver pozo completo",false);
        Button compareDepth=button("Sensibilidad V0",false);
        half(options,nuclearZoomButton,compareDepth);
        nuclearZoomButton.setOnClickListener(v->{
            boolean zoom=!nuclearGraph.isBarrierZoom();
            nuclearGraph.setBarrierZoom(zoom);
            nuclearZoomButton.setText(zoom?"Ver pozo completo":"Zoom de barrera");
        });
        compareDepth.setOnClickListener(v->compareNuclearDepths());
        hamiltonianSummary=text(
                "H radial: sin cálculo. Se prepara una matriz tridiagonal, no una simulación completa.",
                12,SUB);
        hamiltonianSummary.setContentDescription("radial-hamiltonian-grid-details");
        hamiltonianSummary.setPadding(0,dp(10),0,dp(5));
        interactions.addView(hamiltonianSummary);
        exportHamiltonianButton=button("Exportar Hamiltoniano radial .CSV",false);
        exportHamiltonianButton.setContentDescription("export-sparse-nuclear-hamiltonian");
        exportHamiltonianButton.setEnabled(false);
        LinearLayout.LayoutParams exportH=new LinearLayout.LayoutParams(-1,dp(46));
        exportH.topMargin=dp(8);
        interactions.addView(exportHamiltonianButton,exportH);
        exportHamiltonianButton.setOnClickListener(v->exportHamiltonian());
        interactions.addView(text(
                "El CSV contiene el operador radial de 256 posiciones, sin evolución. "+
                "MIL Core ejecuta una discretización reducida del mismo potencial "+
                "de 4 u 8 posiciones, con referencia matemática independiente.",11,SUB));

        LinearLayout milPanel=panel(body);
        milPanel.addView(section("04 / EVOLUCIÓN UNITARIA EN MIL CORE"));
        milPanel.addView(text(
                "El backend MIL Reduced evoluciona un Hamiltoniano radial de "+
                "4 u 8 posiciones, codificadas en 2 o 3 qubits. "+
                "Esto NO equivale a evolucionar la matriz de 256 posiciones. "+
                "Se comprueba contra RK4 aplicado directamente a la matriz pequeña.",12,SUB));
        milSizeButton=button("3 qubits / 8 posiciones",false);
        milSizeButton.setContentDescription("mil-radial-grid-qubits");
        milPanel.addView(milSizeButton,new LinearLayout.LayoutParams(-1,dp(46)));
        milSizeButton.setOnClickListener(v->{
            if(milRunning)return;
            milQubits=milQubits==3?2:3;
            milSizeButton.setText(milQubits==3?
                    "3 qubits / 8 posiciones":"2 qubits / 4 posiciones");
            clearMilResult();
        });
        milTimeLabel=text("Tiempo de evolución: τ=t/ħ",12,GREEN);
        milTimeLabel.setPadding(0,dp(12),0,dp(4));
        milPanel.addView(milTimeLabel);
        milTimeControl=new SeekBar(this);
        milTimeControl.setMax(1000);
        milTimeControl.setProgress(500);
        milTimeControl.setContentDescription("mil-radial-physical-time-control");
        milPanel.addView(milTimeControl);
        milTimeControl.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            @Override public void onStartTrackingTouch(SeekBar seekBar){}
            @Override public void onStopTrackingTouch(SeekBar seekBar){}
            @Override public void onProgressChanged(SeekBar seekBar,int value,boolean fromUser){
                double tau=milTimeFromSlider(value);
                milTimeLabel.setText(String.format(Locale.US,
                        "τ=t/ħ = %.4f MeV^-1; t ≈ %.3e s",
                        tau,tau*6.582119569e-22));
                if(fromUser)clearMilResult();
            }
        });
        milRunButton=button("EJECUTAR EVOLUCIÓN MIL CORE",true);
        milRunButton.setContentDescription("mil-run-hamiltonian-evolution");
        LinearLayout.LayoutParams runParams=new LinearLayout.LayoutParams(-1,dp(52));
        runParams.topMargin=dp(9);
        milPanel.addView(milRunButton,runParams);
        milRunButton.setOnClickListener(v->runMilEvolution());
        milSummary=text(
                "Selecciona una pareja con canal conocido y pulsa Ejecutar. "+
                "El tiempo evoluciona el Hamiltoniano reducido, no una trayectoria nuclear observada.",
                12,SUB);
        milSummary.setContentDescription("mil-radial-execution-result");
        milSummary.setTypeface(Typeface.MONOSPACE);
        milSummary.setTextIsSelectable(true);
        milSummary.setPadding(0,dp(11),0,dp(8));
        milPanel.addView(milSummary);
        milChart=new MilProbabilityView(this);
        milPanel.addView(milChart,new LinearLayout.LayoutParams(-1,dp(224)));
        milPanel.addView(text(
                "Verde: probabilidades del estado cuántico MIL. "+
                "Naranja: referencia RK4 independiente del mismo Hamiltoniano reducido. "+
                "Esta concordancia NO predice una tasa de fusión.",11,ORANGE));

        LinearLayout output=panel(body);
        output.addView(section("05 / RESULTADOS Y EVIDENCIA"));
        channelButton=button("Canal de reacción",false);
        channelButton.setContentDescription("fusion-reaction-branch");
        output.addView(channelButton,new LinearLayout.LayoutParams(-1,dp(48)));
        channelButton.setOnClickListener(v->selectChannel());
        reactionSummary=title("",15);
        reactionSummary.setPadding(0,dp(14),0,dp(7));
        output.addView(reactionSummary);
        unknownWarning=text("",12,ORANGE);
        output.addView(unknownWarning);
        stepInfo=text("",12,SUB);
        stepInfo.setPadding(0,dp(12),0,dp(12));
        output.addView(stepInfo);
        expertButton=button("Mostrar fórmulas y límites del modelo",false);
        output.addView(expertButton,new LinearLayout.LayoutParams(-1,dp(46)));
        expertButton.setOnClickListener(v->{
            expert=!expert;showExpert();
        });
        detailsView=text("",11,TEXT);
        detailsView.setTypeface(Typeface.MONOSPACE);
        detailsView.setTextIsSelectable(true);
        detailsView.setPadding(dp(6),dp(12),dp(6),dp(6));
        output.addView(detailsView);
        exportButton=button("EXPORTAR INFORME CIENTÍFICO .TXT",true);
        exportButton.setContentDescription("fusion-export-report");
        LinearLayout.LayoutParams saveLp=new LinearLayout.LayoutParams(-1,dp(52));
        saveLp.topMargin=dp(10);
        output.addView(exportButton,saveLp);
        exportButton.setOnClickListener(v->saveReport());

        LinearLayout sources=panel(body);
        sources.addView(section("DATOS ABIERTOS Y PROCEDENCIA"));
        sources.addView(text("Masas atómicas e isótopos: NIST. Reacciones y energías Q: canales comprobados mediante diferencias de masas y fuentes de fusión. El cálculo Coulomb es una aproximación clásica.",12,SUB));
        LinearLayout references=row();
        sources.addView(references);
        Button nist=button("NIST",false),nndc=button("NNDC",false),doe=button("DOE",false);
        LinearLayout.LayoutParams refLp=new LinearLayout.LayoutParams(0,dp(44),1);
        refLp.rightMargin=dp(6);
        references.addView(nist,refLp);
        LinearLayout.LayoutParams refLp2=new LinearLayout.LayoutParams(0,dp(44),1);
        refLp2.rightMargin=dp(6);
        references.addView(nndc,refLp2);
        references.addView(doe,new LinearLayout.LayoutParams(0,dp(44),1));
        nist.setOnClickListener(v->external(NIST));
        nndc.setOnClickListener(v->external(NNDC));
        doe.setOnClickListener(v->external(DOE));
        TextView contract=text("LIMITACIÓN EXPLÍCITA: WKB es una aproximación semiclasica "+
                "de la barrera de un potencial fenomenológico sin ajuste experimental. "+
                "Ether Render X dibuja cuerpos 3D; no predice trayectorias nucleares. "+
                "MIL Core todavía no ejecuta un Hamiltoniano nuclear validado. "+
                "Esta app no calcula tasas de fusión ni fabrica elementos.",11,ORANGE);
        contract.setPadding(0,dp(11),0,dp(3));
        sources.addView(contract);
    }

    private void setPresets(boolean resetTime,int firstZ,int firstA,int secondZ,int secondA){
        z1=firstZ;z2=secondZ;a1=firstA;a2=secondA;
        if(isotopeA!=null)isotopeA.setText(Integer.toString(a1));
        if(isotopeB!=null)isotopeB.setText(Integer.toString(a2));
        channel=0;
        firstElement.setText(symbolLabel(z1));
        secondElement.setText(symbolLabel(z2));
        selectedHint.setText(String.format(Locale.US,
                "A: %s · Z=%d · %d neutrones    |    B: %s · Z=%d · %d neutrones",
                FusionPhysics.SYMBOLS[z1-1],z1,a1-z1,
                FusionPhysics.SYMBOLS[z2-1],z2,a2-z2));
        if(resetTime&&timeline!=null){stopPlay();timeline.setProgress(0);}
        if(energyBar!=null)updateReport();
    }

    private String symbolLabel(int z){
        return FusionPhysics.SYMBOLS[z-1] + "  ·  " + FusionPhysics.NAMES[z-1] + "  (Z " + z + ")";
    }

    private void showElementPicker(boolean first){
        ScrollView contentScroll=new ScrollView(this);
        LinearLayout container=column();
        container.setPadding(dp(8),dp(8),dp(8),dp(6));
        contentScroll.addView(container);
        TextView help=text("Selecciona un elemento por su símbolo o búscalo por nombre/número.",12,SUB);
        container.addView(help);
        EditText search=new EditText(this);
        search.setSingleLine(true);
        search.setHint("Ejemplo: Fe, hierro, 26");
        search.setTextColor(TEXT);
        search.setHintTextColor(SUB);
        search.setTextSize(14);
        container.addView(search,new LinearLayout.LayoutParams(-1,dp(53)));
        LinearLayout found=column();
        container.addView(found);
        TextView tableLabel=section("TABLA PERIÓDICA");
        container.addView(tableLabel);
        HorizontalScrollView horizontal=new HorizontalScrollView(this);
        horizontal.setFillViewport(false);
        horizontal.setHorizontalScrollBarEnabled(true);
        container.addView(horizontal);
        GridLayout grid=new GridLayout(this);
        grid.setColumnCount(18);grid.setRowCount(9);
        horizontal.addView(grid);
        final AlertDialog dialog=new AlertDialog.Builder(this)
                .setTitle(first?"ELEGIR NÚCLEO A":"ELEGIR NÚCLEO B")
                .setView(contentScroll)
                .setNegativeButton("Cancelar",null)
                .create();
        final java.util.function.IntConsumer choose=z->{
            if(first){z1=z;a1=FusionPhysics.SUGGESTED_A[z-1];}
            else {z2=z;a2=FusionPhysics.SUGGESTED_A[z-1];}
            dialog.dismiss();
            setPresets(true,z1,a1,z2,a2);
        };
        for(int z=1;z<=118;z++){
            final int element=z;
            TextView cell=text(String.format(Locale.US,"%d\n%s",z,FusionPhysics.SYMBOLS[z-1]),11,TEXT);
            cell.setGravity(Gravity.CENTER);
            cell.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
            cell.setBackground(shape(elementColor(z),BORDER,5));
            cell.setContentDescription("Elemento " + FusionPhysics.NAMES[z-1] + " Z=" + z);
            cell.setOnClickListener(v->choose.accept(element));
            GridLayout.LayoutParams gp=new GridLayout.LayoutParams(
                    GridLayout.spec(FusionPhysics.periodicRow(z)),
                    GridLayout.spec(FusionPhysics.periodicColumn(z)));
            gp.width=dp(51);gp.height=dp(47);
            gp.setMargins(dp(2),dp(2),dp(2),dp(2));
            grid.addView(cell,gp);
        }
        search.addTextChangedListener(new TextWatcher(){
            @Override public void beforeTextChanged(CharSequence s,int start,int count,int after){}
            @Override public void onTextChanged(CharSequence s,int start,int before,int count){
                found.removeAllViews();
                String key=s.toString().trim().toLowerCase(Locale.ROOT);
                if(key.isEmpty())return;
                int shown=0;
                for(int z=1;z<=118&&shown<7;z++){
                    if(FusionPhysics.SYMBOLS[z-1].toLowerCase(Locale.ROOT).contains(key)
                        ||FusionPhysics.NAMES[z-1].toLowerCase(Locale.ROOT).contains(key)
                        ||Integer.toString(z).equals(key)){
                        final int selected=z;
                        Button option=button(symbolLabel(z),false);
                        found.addView(option,new LinearLayout.LayoutParams(-1,dp(44)));
                        option.setOnClickListener(v->choose.accept(selected));
                        shown++;
                    }
                }
                if(shown==0)found.addView(text("No coincide con los 118 elementos.",12,ORANGE));
            }
            @Override public void afterTextChanged(Editable s){}
        });
        dialog.show();
        // The full table is scrollable, but search is the default shortcut.
        if(dialog.getWindow()!=null){
            dialog.getWindow().setLayout(getResources().getDisplayMetrics().widthPixels-dp(22),
                    Math.min(getResources().getDisplayMetrics().heightPixels-dp(110),dp(590)));
        }
    }

    private int elementColor(int z){
        if(z==2||z==10||z==18||z==36||z==54||z==86||z==118)
            return Color.rgb(38,65,97);
        if(z==1||z==6||z==7||z==8||z==9||z==15||z==16||z==17)
            return Color.rgb(29,68,76);
        if(z>=58&&z<=71||z>=90&&z<=103)return Color.rgb(65,52,86);
        return Color.rgb(28,44,59);
    }

    private static int currentMass(EditText source,int fallback){
        if(source==null)return fallback;
        try {
            String text=source.getText().toString().trim();
            if(!text.matches("[0-9]{1,3}"))return fallback;
            return Integer.parseInt(text);
        }catch(Exception error){return fallback;}
    }
    private static int requireMass(EditText field,int z){
        String s=field.getText().toString().trim();
        if(!s.matches("[0-9]{1,3}"))throw new IllegalArgumentException("A debe ser un número entero");
        int value=Integer.parseInt(s);
        if(value<z||value>320)throw new IllegalArgumentException("A debe estar entre Z="+z+" y 320");
        return value;
    }

    private void updateReport(){
        if(isotopeA==null||isotopeB==null||energyBar==null||formulaSummary==null)return;
        try{
            int actualA=requireMass(isotopeA,z1);
            int actualB=requireMass(isotopeB,z2);
            a1=actualA;a2=actualB;
            analysis=FusionPhysics.analyze(z1,a1,z2,a2);
            model=CollisionModel.calculate(analysis,CollisionModel.sliderToMeV(energyBar.getProgress()));
            if(channel>=analysis.documentedChannels.size())channel=0;
            selectedHint.setText(String.format(Locale.US,
                    "%s: %d p + %d n   /   %s: %d p + %d n",
                    analysis.first.label(),z1,a1-z1,analysis.second.label(),z2,a2-z2));
            formulaSummary.setText(String.format(Locale.US,
                    "Ecm = %.4g MeV  ·  Rcontact ≈ %.3f fm\nVcoulomb(contact) ≈ %.4g MeV\nRetorno clásico rmin ≈ %.4g fm",
                    model.eCmMeV,model.rContactFm,model.coulombContactMeV,model.classicalTurningFm));
            potential.setData(model);
            nuclearModel=null;
            try {
                nuclearModel=NuclearInteractionModel.solve(
                        analysis,model.eCmMeV,20+nuclearDepth.getProgress());
                nuclearGraph.setResult(nuclearModel);
                if(!nuclearModel.hasNuclearModel){
                    nuclearSummary.setText(
                            "Sin potencial nuclear parametrizado para esta pareja. "+
                            "No se inventan valores de túnel ni se extrapola el modelo.");
                    nuclearSummary.setTextColor(ORANGE);
                }else if(nuclearModel.belowBarrier){
                    nuclearSummary.setText(String.format(Locale.US,
                            "Pico del modelo: %.4f MeV a %.3f fm\n"+
                            "Puntos de retorno: %.3f – %.3f fm\n"+
                            "Supresión WKB ≈ 10^(%.3f) (NO probabilidad de fusión)",
                            nuclearModel.barrierPeakMeV,nuclearModel.barrierPeakRadiusFm,
                            nuclearModel.innerTurningFm,nuclearModel.outerTurningFm,
                            nuclearModel.log10Wkb));
                    nuclearSummary.setTextColor(GREEN);
                }else{
                    nuclearSummary.setText(String.format(Locale.US,
                            "Pico del modelo: %.4f MeV a %.3f fm\n"+
                            "No se calcula WKB sub-barrera en esta energía.",
                            nuclearModel.barrierPeakMeV,nuclearModel.barrierPeakRadiusFm));
                    nuclearSummary.setTextColor(ORANGE);
                }
            } catch(RuntimeException numericalFailure) {
                nuclearGraph.setResult(null);
                nuclearSummary.setText("La integral nuclear no pudo evaluarse: "+
                        numericalFailure.getMessage());
                nuclearSummary.setTextColor(ORANGE);
            }
            renderer.setNuclearModel(nuclearModel);
            radialHamiltonian=null;
            if(nuclearModel!=null && nuclearModel.hasNuclearModel) {
                try {
                    radialHamiltonian=RadialHamiltonian.from(nuclearModel);
                    hamiltonianSummary.setText(String.format(Locale.US,
                            "H radial l=0 · %d puntos · Δr=%.4f fm · "+
                            "matriz tridiagonal real (%d diagonales + %d enlaces).",
                            radialHamiltonian.sites,radialHamiltonian.stepFm,
                            radialHamiltonian.sites,radialHamiltonian.sites-1));
                }catch(RuntimeException operatorFailure){
                    hamiltonianSummary.setText("No se pudo discretizar H: "+
                            operatorFailure.getMessage());
                }
            }else{
                hamiltonianSummary.setText(
                        "Hamiltoniano no parametrizado para esta pareja de núcleos.");
            }
            exportHamiltonianButton.setEnabled(radialHamiltonian!=null);
            boolean known=analysis.hasDocumentedChannel();
            channelButton.setEnabled(known&&analysis.documentedChannels.size()>1);
            channelButton.setText(known?(analysis.documentedChannels.size()>1?
                    "Rama "+(channel+1)+"/"+analysis.documentedChannels.size()+" · cambiar":
                    "Canal nuclear documentado"):"No existe canal documentado");
            if(known){
                FusionPhysics.Channel selected=analysis.documentedChannels.get(channel);
                reactionSummary.setText(analysis.first.label()+" + "+analysis.second.label()+" → "+selected.products);
                unknownWarning.setText(String.format(Locale.US,
                        "Q = %+.4f MeV · %.4g J por reacción\n"+
                        "Q es la diferencia de masa, NO la energía de colisión Ecm.",
                        selected.qMeV,selected.energyJoules()));
                unknownWarning.setTextColor(GREEN);
            }else{
                reactionSummary.setText("Combinación hipotética: Z="+analysis.totalZ+", A="+analysis.totalA);
                unknownWarning.setText("Producto físico desconocido para este canal. No se calcula Q ni se afirma que ocurra la fusión.");
                unknownWarning.setTextColor(ORANGE);
            }
            stepInfo.setText(model.classicalContact?
                "Modelo electrostático: Ecm supera la energía aproximada de contacto. "+
                "Llegar a esa distancia NO garantiza reacción ni indica su probabilidad.":
                "Modelo electrostático: Ecm está por debajo de la energía clásica de contacto. "+
                "El túnel cuántico podría permitir una reacción, pero NO se ha calculado su probabilidad.");
            renderer.setScene(analysis,channel);
            detailsView.setText(model.summary()+"\n\n"+
                    (nuclearModel==null?"Modelo nuclear sin solución en estos parámetros.":
                            nuclearModel.explain())+"\n"+
                    (radialHamiltonian==null?"No hay operador radial.":
                            radialHamiltonian.summary())+
                    "\n"+renderer.provenance());
            hasResult=true;
            exportButton.setEnabled(true);
            showExpert();
            updateStageText();
        }catch(IllegalArgumentException failure){
            analysis=null;model=null;nuclearModel=null;radialHamiltonian=null;hasResult=false;
            formulaSummary.setText("Revisa isótopos: "+failure.getMessage());
            reactionSummary.setText("Datos de entrada incompletos o inválidos");
            unknownWarning.setText("Introduce un A válido para cada núcleo.");
            detailsView.setText("");
            potential.setData(null);
            nuclearGraph.setResult(null);
            nuclearSummary.setText("Datos de interacción nuclear no disponibles.");
            hamiltonianSummary.setText("No hay operador nuclear para estos datos.");
            renderer.setNuclearModel(null);
            exportHamiltonianButton.setEnabled(false);
            exportButton.setEnabled(false);
        }
    }

    private void compareNuclearDepths(){
        if(analysis==null || !analysis.hasDocumentedChannel()){
            new AlertDialog.Builder(this).setTitle("Modelo no disponible")
                    .setMessage("Solo se usa el modelo de entrada con los "+
                            "isótopos de canales de referencia conocidos.")
                    .setPositiveButton("Entendido",null).show();
            return;
        }
        StringBuilder comparison=new StringBuilder(
                "MISMA PAREJA Y MISMA ENERGÍA\n"+
                "El parámetro V0 cambia la atracción nuclear aproximada, "+
                "no las masas ni la energía Q.\n\n");
        for(double depth:new double[]{25.,35.,45.}){
            try{
                NuclearInteractionModel.Result variant=NuclearInteractionModel.solve(
                        analysis,model.eCmMeV,depth);
                comparison.append(String.format(Locale.US,
                        "V0 = %.0f MeV  ·  Barrera = %.4f MeV",
                        depth,variant.barrierPeakMeV));
                if(variant.belowBarrier)
                    comparison.append(String.format(Locale.US,
                            "\n  log10 supresión WKB = %.4f\n",variant.log10Wkb));
                else comparison.append("\n  WKB sub-barrera no aplicable\n");
            }catch(RuntimeException failure){
                comparison.append("V0=").append(depth).append(": ")
                        .append(failure.getMessage()).append("\n");
            }
        }
        comparison.append("\nNo son bandas de incertidumbre experimental, "+
                "son una prueba de sensibilidad del modelo.");
        new AlertDialog.Builder(this).setTitle("Sensibilidad a la interacción nuclear")
                .setMessage(comparison.toString())
                .setPositiveButton("Cerrar",null).show();
    }

    private void selectChannel(){
        if(analysis==null||analysis.documentedChannels.size()<2)return;
        CharSequence[] options=new CharSequence[analysis.documentedChannels.size()];
        for(int i=0;i<options.length;i++){
            FusionPhysics.Channel c=analysis.documentedChannels.get(i);
            options[i]=c.products+"   |   Q="+String.format(Locale.US,"%.3f",c.qMeV)+" MeV";
        }
        new AlertDialog.Builder(this).setTitle("Elegir canal nuclear tabulado")
                .setSingleChoiceItems(options,channel,(dialog,which)->{
                    channel=which;dialog.dismiss();updateReport();
                }).setNegativeButton("Cancelar",null).show();
    }
    private void showExpert(){
        detailsView.setVisibility(expert?View.VISIBLE:View.GONE);
        expertButton.setText(expert?"Ocultar fórmulas y límites":"Mostrar fórmulas y límites del modelo");
    }
    private void togglePlay(){
        if(playing){stopPlay();return;}
        if(timeline.getProgress()>=990)timeline.setProgress(0);
        playing=true;
        playButton.setText("Pausar trayectoria");
        ui.post(frameStepper);
    }
    private void stopPlay(){
        playing=false;
        ui.removeCallbacks(frameStepper);
        if(playButton!=null)playButton.setText("Reproducir trayectoria");
    }
    private void updateStageText(){
        if(timeline==null||stageLabel==null)return;
        int p=timeline.getProgress();
        stageLabel.setText(p<550?"01 / Dos núcleos se aproximan (visualización)":
                p<680?"02 / Contacto geométrico ilustrativo · no tasa de reacción":
                analysis!=null&&analysis.hasDocumentedChannel()?
                "03 / Productos del canal tabulado · animación conceptual":
                "03 / Reacción no determinada · sin productos inventados");
    }

    private static double milTimeFromSlider(int progress){
        return .005 + Math.max(0,Math.min(1000,progress))*.075/1000.;
    }

    private void clearMilResult(){
        lastMilResult=null;
        if(milChart!=null)milChart.setResult(null);
        if(milSummary!=null && !milRunning)
            milSummary.setText("Sin evolución MIL para la configuración actual.");
    }

    private void runMilEvolution(){
        if(milRunning||closed)return;
        if(nuclearModel==null||!nuclearModel.hasNuclearModel){
            toast("Este par no tiene modelo nuclear de referencia.");
            return;
        }
        final RadialHamiltonian.Grid coarse;
        try {
            coarse=RadialHamiltonian.forMil(nuclearModel,milQubits);
        }catch(RuntimeException failure){
            toast("No se pudo crear la malla MIL: "+failure.getMessage());
            return;
        }
        final double tau=milTimeFromSlider(milTimeControl.getProgress());
        final int initialSite=Math.max(0,coarse.sites/2-1);
        final long version=milModelRevision;
        milRunning=true;
        milRunButton.setEnabled(false);
        milSizeButton.setEnabled(false);
        milSummary.setText("MIL Core: evolucionando el estado cuántico y calculando "+
                "una referencia independiente. La interfaz sigue disponible…");
        try {
            milWorker.execute(()->{
                MilRadialEvolver.Result state=null;
                String error=null;
                try {state=MilRadialEvolver.execute(coarse,initialSite,tau,12);}
                catch(Exception|LinkageError failure){error=failure.getMessage();}
                final MilRadialEvolver.Result finished=state;
                final String failureText=error;
                ui.post(()->{
                    if(closed)return;
                    milRunning=false;
                    milRunButton.setEnabled(true);
                    milSizeButton.setEnabled(true);
                    if(version!=milModelRevision){
                        clearMilResult();
                        milSummary.setText("El Hamiltoniano cambió durante la "+
                                "operación. Ejecuta de nuevo para estos parámetros.");
                        return;
                    }
                    if(failureText!=null||finished==null){
                        milSummary.setText("MIL Core no completó la evolución: "+
                                failureText+"\nNo se sustituyó el resultado por RK4.");
                        milChart.setResult(null);
                        return;
                    }
                    lastMilResult=finished;
                    milChart.setResult(finished);
                    milSummary.setText(String.format(Locale.US,
                            "MIL ejecutó %d puertas sobre %d qubits (%d sitios).\n"+
                            "Norma = %.9f; fidelidad vs RK4 = %.8f.\n"+
                            "Distancia L1 de poblaciones = %.5g.\n"+
                            "Tiempo físico = %.3e s.\n"+
                            "Componentes máximos = %d.\n\n"+
                            "Estos resultados describen la malla radial reducida, "+
                            "no un evento de fusión nuclear.",
                            finished.gates,finished.qubits,finished.sites,
                            finished.norm,finished.fidelity,finished.probabilityL1,
                            finished.physicalSeconds,finished.peakComponents));
                });
            });
        }catch(RejectedExecutionException error){
            milRunning=false;
            milRunButton.setEnabled(true);
            milSizeButton.setEnabled(true);
            milSummary.setText("No pudo iniciarse el hilo de cálculo MIL.");
        }
    }

    private void exportHamiltonian(){
        if(radialHamiltonian==null){
            toast("Selecciona un canal nuclear conocido antes de exportar H.");
            return;
        }
        Intent intent=new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.setType("text/csv");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.putExtra(Intent.EXTRA_TITLE,
                "Mundo-X-H-radial-"+analysis.first.label()+"-"+analysis.second.label()+".csv");
        try{startActivityForResult(intent,EXPORT_HAMILTONIAN);}
        catch(ActivityNotFoundException fail){toast("No hay selector de archivos disponible.");}
    }

    private void saveReport(){
        if(!hasResult||model==null)return;
        Intent intent=new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.setType("text/plain");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.putExtra(Intent.EXTRA_TITLE,"Mundo-X-Fusion3D-"+analysis.first.label()+"-"+analysis.second.label()+".txt");
        try{startActivityForResult(intent,EXPORT_RESULT);}
        catch(ActivityNotFoundException fail){toast("No hay un selector de archivos disponible.");}
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);
        if(result!=RESULT_OK||data==null||data.getData()==null)return;
        if(request==EXPORT_HAMILTONIAN) {
            if(radialHamiltonian==null)return;
            String csv=radialHamiltonian.csv();
            byte[] dataBytes=csv.getBytes(StandardCharsets.UTF_8);
            if(dataBytes.length>128*1024){
                toast("Operador fuera de presupuesto CSV (128 KiB)");
                return;
            }
            try(OutputStream stream=getContentResolver().openOutputStream(data.getData(),"wt")){
                if(stream==null)throw new IllegalStateException("Sin acceso al documento");
                stream.write(dataBytes);stream.flush();
                toast("Hamiltoniano radial exportado: "+radialHamiltonian.sites+" puntos");
            }catch(Exception error){toast("Error exportando H: "+error.getMessage());}
            return;
        }
        if(request!=EXPORT_RESULT||model==null||!hasResult)return;
        String report="MUNDO X / FUSION 3D / REPORTE\n"
                +"Modelo de masas NIST + Coulomb clásico + render 3D ilustrativo\n"
                +"Software local · No prueba de síntesis\n\n"
                +model.summary()
                +"\nCANAL VISUALIZADO: "+(analysis.hasDocumentedChannel()?
                    analysis.documentedChannels.get(channel).products:"NO DETERMINADO")
                +"\n\n"+(nuclearModel==null?
                        "MODELO NUCLEAR: sin resultado numérico":
                        nuclearModel.explain())
                +"\n\n"+(radialHamiltonian==null?
                        "HAMILTONIANO RADIAL: no disponible":
                        radialHamiltonian.summary())
                +"\n\n"+renderer.provenance()
                +"\n\nREFERENCIAS\nNIST: "+NIST+"\nNNDC QCalc: "+NNDC+"\nDOE: "+DOE+"\n";
        byte[] bytes=report.getBytes(StandardCharsets.UTF_8);
        if(bytes.length>30000){toast("Informe demasiado grande para exportar");return;}
        try(OutputStream stream=getContentResolver().openOutputStream(data.getData(),"wt")){
            if(stream==null)throw new IllegalStateException("Sin permiso del destino");
            stream.write(bytes);stream.flush();
            toast("Informe exportado: "+bytes.length+" bytes");
        }catch(Exception failure){toast("No se pudo guardar: "+failure.getMessage());}
    }
    private void external(String url){
        try{startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse(url)));}
        catch(ActivityNotFoundException fail){toast("No hay navegador externo disponible.");}
    }
    private void toast(String text){Toast.makeText(this,text,Toast.LENGTH_LONG).show();}

    @Override protected void onPause(){
        stopPlay();
        super.onPause();
    }
    @Override protected void onDestroy(){
        stopPlay();
        if(renderer!=null)renderer.shutdown();
        super.onDestroy();
    }
}
