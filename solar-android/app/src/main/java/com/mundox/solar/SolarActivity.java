package com.mundox.solar;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.DatePickerDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.AtomicFile;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicReference;
import org.json.JSONArray;

/** A solar explorer backed by one Ether/InfiniteSession through COSMIC X. */
public final class SolarActivity extends Activity implements SolarRenderer.Listener {
    private static final int BG=0xff050913, PANEL=0xeb0a1222, TEXT=0xffeff4fb;
    private static final int MUTED=0xffa2b0c3, GOLD=0xfff5c47a, TEAL=0xff69dbdf;
    private static final int SAVE_PNG=704;
    private static final Object PNG_LOCK=new Object();
    private static final AtomicReference<String> PNG_OWNER=new AtomicReference<>();
    private static final double[] SPEEDS={-30,-1,-.2,0,1.0/86400,.01,.2,1,7,30,120};
    private static final String[] SPEED_NAMES={"−30 días/s","−1 día/s","−0,2 días/s","Detenido","Tiempo real","0,01 día/s","0,2 días/s","1 día/s","7 días/s","30 días/s","120 días/s"};
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final SimpleDateFormat dateFormat=new SimpleDateFormat("dd MMM yyyy · HH:mm 'UTC'",new Locale("es"));
    private QuantumSolarBridge quantum;
    private SolarRenderer observer;
    private SolarSurface surface;
    private FrameLayout root;
    private LinearLayout header,bottom,bodyCard,quickRow,flight,altitudeRow;
    private TextView nameLabel,kindLabel,statsLabel,dateLabel,engineLabel,legendLabel,hintLabel;
    private Button fieldButton,pauseButton,scaleButton,flightButton,hideButton,qualityButton,entryButton,altitudeButton;
    private Joystick joystick;
    private boolean fields=false,wind=true,orbits=true,realScale=false,paused=false,freeFlight=false,cleanView=false,resumed;
    private boolean fluidPreview;
    private boolean uhd4k=false,uhd8k=false,entryActive=false,atmospheres=true,localPhysics=false,pngDestinationOpen;
    private volatile boolean destroyed;
    private double desiredEntryAltitude=35;
    private String resolutionMessage,pngToken;
    private SolarMapView activeMap;
    private AlertDialog mapDialog;
    private final ExecutorService images=Executors.newSingleThreadExecutor();
    private int body=3,speedIndex=6,quality=960;
    private boolean errorShown=false;
    private String latestStatus="Preparando el sistema solar";
    private final Runnable ticker=new Runnable(){
        @Override public void run(){
            if(!resumed||observer==null)return;
            updateTelemetry();
            if(observer.failure!=null&&!errorShown){errorShown=true;showRenderError(observer.failure);}
            handler.postDelayed(this,500);
        }
    };

    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);
        dateFormat.setTimeZone(TimeZone.getTimeZone("UTC"));
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setNavigationBarColor(BG);
        immersive();
        SharedPreferences prefs=getSharedPreferences("solar",MODE_PRIVATE);
        body=clamp(prefs.getInt("body",3),-1,SolarData.BODIES.length-1);
        speedIndex=clamp(prefs.getInt("speed",6),0,SPEEDS.length-1);
        quality=clamp(prefs.getInt("quality",960),480,1440);
        int defaultMode=BuildConfig.SOLAR_DEFAULT_MODE;
        uhd4k=prefs.getBoolean("uhd4k",defaultMode==1);uhd8k=prefs.getBoolean("uhd8k",defaultMode==2);
        if(uhd8k)uhd4k=false;
        fluidPreview=prefs.getBoolean("fluidPreview",defaultMode==0)&&!uhd4k&&!uhd8k;
        atmospheres=prefs.getBoolean("atmospheres",true);localPhysics=prefs.getBoolean("localPhysics",false);
        entryActive=prefs.getBoolean("entry",false);
        desiredEntryAltitude=Double.longBitsToDouble(prefs.getLong("entryAltitude",Double.doubleToLongBits(35)));
        double[] entryPose=readPose(prefs.getString("entryPose",null));
        fields=prefs.getBoolean("fields",false);wind=prefs.getBoolean("wind",true);
        orbits=prefs.getBoolean("orbits",true);realScale=prefs.getBoolean("realScale",false);
        paused=prefs.getBoolean("paused",false);
        int preparation=clamp(prefs.getInt("preset",2),0,3);
        double jd=nowJulian();
        if(saved!=null){
            body=clamp(saved.getInt("body",body),-1,SolarData.BODIES.length-1);
            jd=saved.getDouble("jd",jd);fields=saved.getBoolean("fields",fields);
            wind=saved.getBoolean("wind",wind);orbits=saved.getBoolean("orbits",orbits);
            realScale=saved.getBoolean("realScale",realScale);paused=saved.getBoolean("paused",paused);
            speedIndex=clamp(saved.getInt("speed",speedIndex),0,SPEEDS.length-1);
            preparation=clamp(saved.getInt("preset",preparation),0,3);
            quality=clamp(saved.getInt("quality",quality),480,1440);
            uhd4k=saved.getBoolean("uhd4k",uhd4k);uhd8k=saved.getBoolean("uhd8k",uhd8k);if(uhd8k)uhd4k=false;
            fluidPreview=saved.getBoolean("fluidPreview",fluidPreview)&&!uhd4k&&!uhd8k;
            atmospheres=saved.getBoolean("atmospheres",atmospheres);localPhysics=saved.getBoolean("localPhysics",localPhysics);
            entryActive=saved.getBoolean("entry",entryActive);
            desiredEntryAltitude=saved.getDouble("entryAltitude",desiredEntryAltitude);
            if(saved.containsKey("entryPose"))entryPose=saved.getDoubleArray("entryPose");
            if(saved.getBoolean("pngDestinationOpen",false)){
                pngToken=saved.getString("pngToken");pngDestinationOpen=pngToken!=null;
                if(pngDestinationOpen)PNG_OWNER.compareAndSet(null,pngToken);
            }
        }
        try{
            quantum=new QuantumSolarBridge(preparation);
            observer=new SolarRenderer(this,quantum,this);
            observer.setJulianDate(jd);observer.setSpeed(SPEEDS[speedIndex]);observer.setPaused(paused);
            observer.setFields(fields);observer.setWind(wind);observer.setOrbits(orbits);
            observer.setRealScale(realScale);observer.setQuality(quality);
            if(uhd8k)observer.setUhd8k(true);else observer.setUhd4k(uhd4k);
            if(fluidPreview)observer.setAdaptivePreview(true);
            observer.setAtmosphereEnabled(atmospheres);observer.setLocalPhysics(localPhysics);
            if(body<0)observer.overview();else observer.focus(body);
            if(entryActive&&body>0){
                if(entryPose!=null)observer.restoreEntryState(entryPose);
                else{observer.enterAtmosphere(body);observer.setEntryAltitudeKm(desiredEntryAltitude);}
            }else entryActive=false;
            surface=new SolarSurface(this,observer);surface.setPausedRendering(paused);
            root=new FrameLayout(this);root.setBackgroundColor(BG);
            root.addView(surface,new FrameLayout.LayoutParams(-1,-1));
            buildInterface();setContentView(root);installInsets();
        }catch(Throwable e){
            if(quantum!=null)quantum.close();quantum=null;
            LinearLayout box=column();box.setGravity(Gravity.CENTER);box.setPadding(dp(28),dp(28),dp(28),dp(28));box.setBackgroundColor(BG);
            box.addView(label("COSMIC X · SOLAR",25,GOLD,true));
            box.addView(label("No se pudo iniciar el núcleo original.",18,TEXT,false));
            box.addView(label(e.getClass().getSimpleName()+": "+e.getMessage(),13,MUTED,false));
            setContentView(box);return;
        }
        if(!prefs.getBoolean("welcomed",false)){
            hintLabel.setText("ARRASTRA PARA MIRAR  ·  PELLIZCA PARA ACERCARTE");
            handler.postDelayed(()->{if(hintLabel!=null)hintLabel.setVisibility(View.GONE);},9000);
            prefs.edit().putBoolean("welcomed",true).apply();
        }else hintLabel.setVisibility(View.GONE);
    }
    private void immersive(){getWindow().getDecorView().setSystemUiVisibility(
        View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY|View.SYSTEM_UI_FLAG_LAYOUT_STABLE|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);}

    private void installInsets(){
        root.setOnApplyWindowInsetsListener((view,insets)->{
            int left=insets.getSystemWindowInsetLeft(),right=insets.getSystemWindowInsetRight();
            int top=insets.getSystemWindowInsetTop(),lower=insets.getSystemWindowInsetBottom();
            if(android.os.Build.VERSION.SDK_INT>=28&&insets.getDisplayCutout()!=null){
                android.view.DisplayCutout cutout=insets.getDisplayCutout();
                left=Math.max(left,cutout.getSafeInsetLeft());right=Math.max(right,cutout.getSafeInsetRight());
                top=Math.max(top,cutout.getSafeInsetTop());lower=Math.max(lower,cutout.getSafeInsetBottom());
            }
            header.setPadding(dp(20)+left,dp(17)+top,dp(20)+right,dp(8));
            bottom.setPadding(dp(14)+left,dp(14),dp(14)+right,dp(16)+lower);
            boolean wide=getResources().getConfiguration().orientation==Configuration.ORIENTATION_LANDSCAPE;
            FrameLayout.LayoutParams fp=(FrameLayout.LayoutParams)flight.getLayoutParams();
            fp.leftMargin=left;fp.bottomMargin=(wide?dp(24):dp(entryActive?390:335))+lower;flight.setLayoutParams(fp);
            return insets;
        });
        root.requestApplyInsets();
    }

    private void buildInterface(){
        boolean wide=getResources().getConfiguration().orientation==Configuration.ORIENTATION_LANDSCAPE;
        header=column();header.setPadding(dp(20),dp(17),dp(20),dp(8));
        header.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,new int[]{0xec050913,0x00050913}));
        LinearLayout title=row();
        TextView brand=label("COSMIC X",24,TEXT,true);brand.setLetterSpacing(.13f);
        title.addView(brand,new LinearLayout.LayoutParams(0,-2,1));
        qualityButton=button(uhd8k?"8K UHD":uhd4k?"4K UHD":fluidPreview?"Auto fluido":"Calidad",0,this::qualityDialog);
        qualityButton.setContentDescription("Elegir resolución, incluidos 4K y 8K UHD");title.addView(qualityButton);
        hideButton=button("◌",48,()->toggleClean());hideButton.setTextSize(24);hideButton.setContentDescription("Ocultar o mostrar controles");title.addView(hideButton);
        header.addView(title);
        TextView sub=label("S O L A R   E X P L O R E R   ·   C O S M I C",10,GOLD,true);header.addView(sub);
        dateLabel=label("",12,MUTED,false);dateLabel.setPadding(0,dp(10),0,0);dateLabel.setOnClickListener(v->timeDialog());header.addView(dateLabel);
        engineLabel=label("Núcleo X conectado",10,TEAL,false);engineLabel.setOnClickListener(v->coreDialog());header.addView(engineLabel);
        FrameLayout.LayoutParams hp=new FrameLayout.LayoutParams(-1,-2,Gravity.TOP);root.addView(header,hp);

        bottom=column();bottom.setPadding(dp(14),dp(14),dp(14),dp(16));
        bottom.setBackground(new GradientDrawable(GradientDrawable.Orientation.BOTTOM_TOP,new int[]{0xfd050913,0xf9050913,0x00050913}));
        bodyCard=column();bodyCard.setPadding(dp(15),dp(12),dp(15),dp(10));bodyCard.setBackground(card(PANEL,0x303b5775,18));
        LinearLayout bodyTitle=row();
        LinearLayout words=column();words.setPadding(0,0,dp(6),0);
        kindLabel=label("",10,GOLD,true);kindLabel.setLetterSpacing(.09f);words.addView(kindLabel);
        nameLabel=label("Tierra",28,TEXT,true);words.addView(nameLabel);
        bodyTitle.addView(words,new LinearLayout.LayoutParams(0,-2,1));
        Button details=button("Ficha  ›",80,()->bodyDialog());bodyTitle.addView(details);bodyCard.addView(bodyTitle);
        statsLabel=label("",12,MUTED,false);statsLabel.setPadding(0,dp(4),0,0);bodyCard.addView(statsLabel);
        legendLabel=label("",11,TEAL,false);legendLabel.setPadding(0,dp(7),0,0);bodyCard.addView(legendLabel);
        bottom.addView(bodyCard);
        LinearLayout journey=row();journey.setPadding(0,dp(5),0,0);
        entryButton=button("↓  Descender",0,this::enterSelectedBody);
        journey.addView(entryButton,new LinearLayout.LayoutParams(0,dp(44),1));
        Button png=button("Guardar PNG",0,this::exportPng);
        journey.addView(png,new LinearLayout.LayoutParams(0,dp(44),1));bottom.addView(journey);
        altitudeRow=row();
        altitudeRow.addView(button("−  Bajar",0,()->stepAltitude(.65)),new LinearLayout.LayoutParams(0,dp(43),1));
        altitudeButton=button("Altitud",0,this::altitudeDialog);
        altitudeRow.addView(altitudeButton,new LinearLayout.LayoutParams(0,dp(43),1));
        altitudeRow.addView(button("+  Subir",0,()->stepAltitude(1.5)),new LinearLayout.LayoutParams(0,dp(43),1));
        bottom.addView(altitudeRow);
        HorizontalScrollView planets=new HorizontalScrollView(this);planets.setHorizontalScrollBarEnabled(false);planets.setFillViewport(false);
        LinearLayout destinations=row();destinations.setPadding(0,dp(9),0,dp(7));
        for(int id=0;id<=8;id++){final int target=id;Button chip=button(SolarData.BODIES[id].name,0,()->chooseBody(target));destinations.addView(chip);}
        Button moons=button("Lunas  +",0,()->destinationsDialog());destinations.addView(moons);
        planets.addView(destinations);bottom.addView(planets);
        quickRow=row();
        Button map=button("◎  Mapa",0,this::openMap);map.setContentDescription("Mapa interactivo del sistema solar");quickRow.addView(map,new LinearLayout.LayoutParams(0,dp(46),1));
        fieldButton=button("Campos",0,()->toggleFields());quickRow.addView(fieldButton,new LinearLayout.LayoutParams(0,dp(46),1));
        pauseButton=button("Ⅱ",50,()->togglePause());pauseButton.setTextSize(22);quickRow.addView(pauseButton);
        Button clock=button("Tiempo",0,()->timeDialog());quickRow.addView(clock,new LinearLayout.LayoutParams(0,dp(46),1));
        Button settings=button("☷",46,()->settingsDialog());settings.setTextSize(22);settings.setContentDescription("Capas, calidad y controles");quickRow.addView(settings);
        bottom.addView(quickRow);
        FrameLayout.LayoutParams bp=new FrameLayout.LayoutParams(wide?dp(400):-1,-2,Gravity.BOTTOM|(wide?Gravity.RIGHT:Gravity.LEFT));root.addView(bottom,bp);

        flight=column();flight.setPadding(dp(14),0,0,0);
        flightButton=button("✧  Volar",98,()->toggleFlight());flight.addView(flightButton);
        joystick=new Joystick();joystick.setVisibility(View.GONE);flight.addView(joystick,new LinearLayout.LayoutParams(dp(112),dp(112)));
        FrameLayout.LayoutParams fp=new FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM|Gravity.LEFT);fp.bottomMargin=wide?dp(24):dp(entryActive?390:335);root.addView(flight,fp);

        hintLabel=label("",10,MUTED,true);hintLabel.setGravity(Gravity.CENTER);hintLabel.setPadding(dp(14),dp(12),dp(14),dp(12));hintLabel.setBackground(card(PANEL,0x303b5775,24));
        FrameLayout.LayoutParams hint=new FrameLayout.LayoutParams(-2,-2,Gravity.CENTER_HORIZONTAL|Gravity.TOP);hint.topMargin=dp(155);root.addView(hintLabel,hint);
        updateBody();updateButtons();updateTelemetry();
    }

    private void chooseBody(int value){
        body=clamp(value,-1,SolarData.BODIES.length-1);freeFlight=false;entryActive=false;
        final int id=body;
        runGL(()->{observer.setFreeFlight(false);if(id<0)observer.overview();else observer.focus(id);});
        updateBody();updateButtons();
    }
    private void toggleFields(){fields=!fields;final boolean b=fields;runGL(()->observer.setFields(b));updateButtons();updateBody();}
    private void togglePause(){paused=!paused;final boolean b=paused;runGL(()->observer.setPaused(b));surface.setPausedRendering(b);updateButtons();}
    private void toggleFlight(){if(entryActive){exitEntry();return;}freeFlight=!freeFlight;final boolean b=freeFlight;runGL(()->observer.setFreeFlight(b));updateButtons();}
    private void toggleClean(){
        cleanView=!cleanView;bottom.setVisibility(cleanView?View.GONE:View.VISIBLE);flight.setVisibility(cleanView?View.GONE:View.VISIBLE);
        dateLabel.setVisibility(cleanView?View.GONE:View.VISIBLE);engineLabel.setVisibility(cleanView?View.GONE:View.VISIBLE);
        hideButton.setText(cleanView?"☰":"◌");
    }
    private void updateButtons(){
        if(fieldButton==null)return;
        fieldButton.setText(fields?"Campos ✓":"Campos");fieldButton.setTextColor(fields?TEAL:TEXT);
        fieldButton.setEnabled(!entryActive);fieldButton.setAlpha(entryActive?.5f:1f);
        pauseButton.setText(paused?"▶":"Ⅱ");pauseButton.setContentDescription(paused?"Reanudar tiempo":"Pausar tiempo");
        flightButton.setText(entryActive?"↑  Órbita":freeFlight?"✧  Volando":"✧  Volar");flightButton.setTextColor(freeFlight||entryActive?TEAL:TEXT);
        joystick.setVisibility(freeFlight||entryActive?View.VISIBLE:View.GONE);
        entryButton.setText(entryActive?"↑  Regresar a órbita":"↓  Descender");entryButton.setEnabled(body>0);
        entryButton.setAlpha(body>0?1f:.5f);altitudeRow.setVisibility(entryActive?View.VISIBLE:View.GONE);
        qualityButton.setText(uhd8k?"8K UHD":uhd4k?"4K UHD":fluidPreview?"Auto fluido":"Calidad");
        qualityButton.setTextColor(uhd4k||uhd8k?GOLD:fluidPreview?TEAL:TEXT);
        if(root!=null)root.requestApplyInsets();
    }
    private void updateBody(){
        if(nameLabel==null)return;
        if(body<0){
            kindLabel.setText("NUESTRO VECINDARIO CÓSMICO");nameLabel.setText("Sistema solar");
            statsLabel.setText("Sol · 8 planetas · "+(SolarData.BODIES.length-9)+" lunas para explorar");
        }else{
            SolarData.Body b=SolarData.BODIES[body];kindLabel.setText(b.type.toUpperCase(new Locale("es")));nameLabel.setText(b.name);
            statsLabel.setText(String.format(new Locale("es"),"Radio  %,.0f km · g  %.2f m/s² · escape  %.2f km/s",b.radiusKm,SolarData.gravityMs2(body,0),SolarData.escapeVelocityKmS(body,0)));
        }
        String scale=realScale?"PROPORCIONES REALES":"ESCALA DE EXPLORACIÓN";
        if(entryActive&&body>=0){
            SolarEnvironment e=SolarEnvironment.environment(body);
            legendLabel.setText(e.label.toUpperCase(new Locale("es"))+"  ·  "+(localPhysics?"GRAVEDAD LOCAL ACTIVA":"ALTURA MANUAL"));
        }else if(fields&&body>=0){SolarData.Body b=SolarData.BODIES[body];
            String field=b.fieldType==1?"CAMPO INTRÍNSECO":b.fieldType==2?"CAMPO INDUCIDO":b.fieldType==3?"INDUCIDO + CORTEZA":b.fieldType==4?"CAMPO SOLAR":"SIN MODELO LOCAL";
            legendLabel.setText(field+"  ·  líneas ilustrativas");
        }else legendLabel.setText(scale+"  ·  toca un destino para viajar");
    }
    private void updateTelemetry(){
        if(dateLabel==null||observer==null)return;
        double jd=observer.julianDate();
        boolean actualPause=observer.isPaused();if(paused!=actualPause){paused=actualPause;if(surface!=null)surface.setPausedRendering(actualPause);updateButtons();}
        dateLabel.setText(dateFormat.format(new Date((long)((jd-2440587.5)*86400000)))+"    "+(paused?"Pausa":SPEED_NAMES[speedIndex]));
        engineLabel.setText(String.format(Locale.ROOT,"NÚCLEO X  ·  %d × %d  ·  %.0f fps%s",observer.renderedWidth(),observer.renderedHeight(),observer.fps,entryActive&&localPhysics?String.format(Locale.ROOT," · g %.2f m/s² · vr %.3f km/s",observer.localGravityMs2(),observer.radialVelocityKmS()):""));
        if(entryActive&&observer.isEntryMode()){
            desiredEntryAltitude=observer.altitudeKm();altitudeButton.setText(altitudeText(desiredEntryAltitude));
        }
        if(activeMap!=null)activeMap.setJulianDate(jd);
        String notice=observer.resolutionNotice();
        if(notice!=null&&!notice.equals(resolutionMessage)){
            resolutionMessage=notice;uhd4k=observer.isUhd4k();uhd8k=observer.isUhd8k();updateButtons();message(notice);
        }
    }
    @Override public void status(String status){latestStatus=status;}
    @Override public void selected(int id){handler.post(()->{if(destroyed)return;body=clamp(id,-1,SolarData.BODIES.length-1);freeFlight=false;entryActive=observer!=null&&observer.isEntryMode();if(activeMap!=null)activeMap.setSelectedBody(Math.max(0,body));updateBody();updateButtons();});}
    private void runGL(Runnable action){if(surface!=null&&!destroyed){surface.queueEvent(()->{if(!destroyed)action.run();});surface.requestRender();}}

    void selectDestination(int id){chooseBody(id);}
    SolarMapView mapView(){return activeMap;}
    void openMap(){
        if(destroyed||observer==null)return;
        if(mapDialog!=null&&mapDialog.isShowing())return;
        LinearLayout content=column();content.setPadding(dp(18),dp(20),dp(18),dp(16));content.setBackgroundColor(BG);
        content.addView(label("ATLAS DEL SISTEMA SOLAR",20,TEXT,true));
        TextView selectedText=label("Toca un destino. Pellizca para ampliar.",13,TEAL,false);content.addView(selectedText);
        TextView scaleText=label("Órbitas comprimidas · tamaños ampliados · lunas en su mapa local",11,MUTED,false);
        scaleText.setPadding(0,dp(5),0,dp(8));content.addView(scaleText);
        Button descend=button("Descender",0,()->{if(mapDialog!=null)mapDialog.dismiss();if(!entryActive)enterSelectedBody();});
        activeMap=new SolarMapView(this,id->{
            selectDestination(id);
            selectedText.setText(SolarData.BODIES[id].name+" · "+SolarEnvironment.environment(id).label);
            descend.setEnabled(id>0);
        });
        activeMap.setJulianDate(observer.julianDate());activeMap.setSelectedBody(Math.max(0,body));
        if(body>=0)selectedText.setText(SolarData.BODIES[body].name+" · "+SolarEnvironment.environment(body).label);
        descend.setEnabled(body>0);
        content.addView(activeMap,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout actions=row();
        actions.addView(button("Sistema 3D",0,()->{chooseBody(-1);mapDialog.dismiss();}),new LinearLayout.LayoutParams(0,dp(48),1));
        actions.addView(descend,new LinearLayout.LayoutParams(0,dp(48),1));
        actions.addView(button("Ver destino",0,()->mapDialog.dismiss()),new LinearLayout.LayoutParams(0,dp(48),1));
        content.addView(actions);
        mapDialog=new AlertDialog.Builder(this).setView(content).create();
        mapDialog.setOnDismissListener(d->{activeMap=null;mapDialog=null;});
        showDialog(mapDialog);
        if(mapDialog.getWindow()!=null)mapDialog.getWindow().setLayout(-1,-1);
    }

    void enterSelectedBody(){
        if(entryActive){exitEntry();return;}
        if(body<=0){message("Elige un planeta o una luna en el mapa.");return;}
        final int destination=body;SolarEnvironment e=SolarEnvironment.environment(destination);
        entryActive=true;freeFlight=false;desiredEntryAltitude=e.entryAltitudeKm;
        runGL(()->observer.enterAtmosphere(destination));updateBody();updateButtons();
        message(e.gasGiant?"Entrada entre nubes. Regula la altura con Altitud.":"Arrastra para mirar. Pellizca para cambiar la altura.");
    }
    void exitEntry(){
        entryActive=false;freeFlight=false;
        runGL(()->observer.exitAtmosphere());updateBody();updateButtons();
    }
    void setEntryAltitude(double km){
        if(!entryActive||body<1||!Double.isFinite(km))return;
        desiredEntryAltitude=SolarEnvironment.environment(body).clampAltitudeKm(km);
        final double altitude=desiredEntryAltitude;runGL(()->observer.setEntryAltitudeKm(altitude));
        if(altitudeButton!=null)altitudeButton.setText(altitudeText(altitude));
    }
    private void stepAltitude(double factor){
        if(!entryActive||body<1)return;
        SolarEnvironment e=SolarEnvironment.environment(body);
        double current=observer.isEntryMode()?observer.altitudeKm():desiredEntryAltitude;
        double offset=Math.max(1,current-e.minAltitudeKm+1);
        setEntryAltitude(e.minAltitudeKm+offset*factor-1);
    }
    private String altitudeText(double km){return String.format(new Locale("es"),Math.abs(km)<10?"%.1f km":"%,.0f km",km);}
    private void altitudeDialog(){
        if(!entryActive||body<1)return;
        final SolarEnvironment e=SolarEnvironment.environment(body);
        LinearLayout content=dialogColumn();
        TextView value=label(altitudeText(observer.altitudeKm()),26,TEAL,true);content.addView(value);
        content.addView(label(e.altitudeReference,12,MUTED,false));
        SeekBar altitude=new SeekBar(this);altitude.setMax(1000);
        final double range=e.maxAltitudeKm-e.minAltitudeKm;
        double position=Math.log1p(Math.max(0,observer.altitudeKm()-e.minAltitudeKm))/Math.log1p(range);
        altitude.setProgress((int)Math.round(Math.max(0,Math.min(1,position))*1000));
        altitude.setContentDescription("Altitud de la cámara durante el descenso");
        altitude.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar s,int p,boolean user){
                if(user){double km=e.minAltitudeKm+Math.expm1(p/1000.0*Math.log1p(range));setEntryAltitude(km);value.setText(altitudeText(desiredEntryAltitude));}
            }
            public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}
        });content.addView(altitude,new LinearLayout.LayoutParams(-1,dp(64)));
        LinearLayout presets=row();
        double[] heights={e.maxAltitudeKm,e.entryAltitudeKm,e.gasGiant?-100:e.minAltitudeKm};
        String[] names={"Desde arriba","Entrada",e.gasGiant?"Nubes profundas":"Rasante"};
        for(int i=0;i<heights.length;i++){final double km=e.clampAltitudeKm(heights[i]);
            presets.addView(button(names[i],0,()->{
                setEntryAltitude(km);value.setText(altitudeText(km));
                altitude.setProgress((int)Math.round(Math.log1p(km-e.minAltitudeKm)/Math.log1p(range)*1000));
            }),new LinearLayout.LayoutParams(0,dp(46),1));
        }content.addView(presets);
        content.addView(label(e.description,13,MUTED,false));
        AlertDialog d=new AlertDialog.Builder(this).setTitle("Descenso · "+SolarData.BODIES[body].name).setView(content).setPositiveButton("Continuar",null).setNegativeButton("Volver a órbita",(dialog,which)->exitEntry()).create();showDialog(d);
    }
    private void qualityDialog(){
        String[] options={"Auto fluido · ajuste entre 480–1280 px",
                "Ahorro · fijo hasta 640 px","Equilibrada · fijo hasta 960 px",
                "Alta · fijo hasta 1440 px","4K UHD · 8,3 millones de píxeles",
                "8K UHD · 33,2 millones de píxeles"};
        int checked=uhd8k?5:uhd4k?4:fluidPreview?0:quality<=640?1:quality<=960?2:3;
        AlertDialog d=new AlertDialog.Builder(this).setTitle("Calidad de imagen").setSingleChoiceItems(options,checked,(dialog,index)->{
            if(index==5)setUltraQuality(true);
            else if(index==4)setUhdQuality(true);
            else if(index==0)setFluidQuality();
            else setAdaptiveQuality(new int[]{640,960,1440}[index-1]);
            dialog.dismiss();
        }).setNegativeButton("Cerrar",null).create();showDialog(d);
    }
    void setFluidQuality(){
        final int start=Math.max(480,Math.min(1280,quality));
        quality=start;fluidPreview=true;uhd4k=uhd8k=false;resolutionMessage=null;
        runGL(()->{observer.setUhd8k(false);observer.setUhd4k(false);
            observer.setQuality(start);observer.setAdaptivePreview(true);});
        updateButtons();
    }
    void setAdaptiveQuality(int pixels){
        quality=pixels;fluidPreview=false;uhd4k=uhd8k=false;resolutionMessage=null;
        runGL(()->{observer.setUhd8k(false);observer.setUhd4k(false);observer.setQuality(pixels);});updateButtons();
    }
    void setUhdQuality(boolean enabled){
        uhd4k=enabled; if(enabled){uhd8k=false;fluidPreview=false;} resolutionMessage=null;
        runGL(()->observer.setUhd4k(enabled));updateButtons();
    }
    void setUltraQuality(boolean enabled){
        uhd8k=enabled; if(enabled){uhd4k=false;fluidPreview=false;} resolutionMessage=null;
        runGL(()->observer.setUhd8k(enabled));updateButtons();
    }
    private void message(String text){handler.post(()->{if(!destroyed&&!isFinishing())Toast.makeText(this,text,Toast.LENGTH_LONG).show();});}

    private AtomicFile pendingPng(){return new AtomicFile(new File(getCacheDir(),"solar-pending.png"));}
    private void exportPng(){
        if(observer==null||observer.failure!=null){message("Espera a que la escena esté disponible.");return;}
        String token=UUID.randomUUID().toString();
        if(!PNG_OWNER.compareAndSet(null,token)){message("Hay una imagen pendiente de guardar.");return;}
        pngToken=token;pngDestinationOpen=false;final SolarRenderer source=observer;
        message("Preparando la imagen a resolución completa…");
        runGL(()->{
            try{source.requestCapture(image->{
                if(destroyed){PNG_OWNER.compareAndSet(token,null);return;}
                try{images.execute(()->stagePng(image,token));}
                catch(Exception error){PNG_OWNER.compareAndSet(token,null);message("No se pudo preparar PNG: "+error.getMessage());}
            });}catch(Exception error){PNG_OWNER.compareAndSet(token,null);message("No se pudo capturar: "+error.getMessage());}
        });
    }
    private void stagePng(SolarRenderer.Capture image,String token){
        AtomicFile file=pendingPng();FileOutputStream output=null;
        try{
            synchronized(PNG_LOCK){
                if(!token.equals(PNG_OWNER.get()))return;
                output=file.startWrite();SolarPng.write(image,output);file.finishWrite(output);output=null;
            }
            handler.post(()->{
                if(destroyed||isFinishing()||!token.equals(PNG_OWNER.get())){PNG_OWNER.compareAndSet(token,null);return;}
                Intent save=new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("image/png");
                save.putExtra(Intent.EXTRA_TITLE,"COSMIC-X-Solar-"+image.width+"x"+image.height+".png");
                try{pngDestinationOpen=true;startActivityForResult(save,SAVE_PNG);}
                catch(Exception error){pngDestinationOpen=false;PNG_OWNER.compareAndSet(token,null);message("No se pudo abrir el destino: "+error.getMessage());}
            });
        }catch(Exception|OutOfMemoryError error){
            if(output!=null)file.failWrite(output);PNG_OWNER.compareAndSet(token,null);
            message("No se pudo preparar PNG: "+error.getMessage());
        }
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);if(request!=SAVE_PNG)return;
        pngDestinationOpen=false;final String token=pngToken;pngToken=null;
        final Uri uri=data==null?null:data.getData();
        if(result!=RESULT_OK||uri==null){
            synchronized(PNG_LOCK){if(token!=null&&token.equals(PNG_OWNER.get())){pendingPng().delete();PNG_OWNER.compareAndSet(token,null);}}
            return;
        }
        images.execute(()->{
            try{
                synchronized(PNG_LOCK){
                    if(token==null||!token.equals(PNG_OWNER.get()))throw new IllegalStateException("La captura pendiente cambió");
                    try(InputStream input=pendingPng().openRead();OutputStream output=getContentResolver().openOutputStream(uri,"wt")){
                        if(output==null)throw new IllegalStateException("Destino no disponible");
                        byte[] block=new byte[65536];int n;while((n=input.read(block))!=-1)output.write(block,0,n);output.flush();
                    }pendingPng().delete();
                }message("Imagen guardada a resolución completa.");
            }catch(Exception error){message("No se pudo guardar PNG: "+error.getMessage());}
            finally{if(token!=null)PNG_OWNER.compareAndSet(token,null);}
        });
    }
    private static double[] readPose(String text){
        if(text==null)return null;
        try{JSONArray json=new JSONArray(text);if(json.length()!=6)return null;double[] pose=new double[6];
            for(int i=0;i<6;i++){pose[i]=json.getDouble(i);if(!Double.isFinite(pose[i]))return null;}return pose;
        }catch(Exception error){return null;}
    }
    private static String poseText(double[] pose){
        if(pose==null)return null;
        JSONArray data=new JSONArray();try{for(double v:pose)data.put(v);return data.toString();}catch(Exception error){return null;}
    }

    private void destinationsDialog(){
        String[] names=new String[SolarData.BODIES.length];for(int i=0;i<names.length;i++)names[i]=SolarData.BODIES[i].name;
        AlertDialog d=new AlertDialog.Builder(this).setTitle("Elige tu destino").setItems(names,(dialog,id)->chooseBody(id)).setNegativeButton("Cerrar",null).create();showDialog(d);
    }
    private void bodyDialog(){
        if(body<0){destinationsDialog();return;}
        SolarData.Body b=SolarData.BODIES[body];
        LinearLayout content=dialogColumn();
        content.addView(label(b.description,16,TEXT,false));
        content.addView(section("DATOS DEL CUERPO"));
        SolarEnvironment environment=SolarEnvironment.environment(b.id);
        content.addView(label(environment.label+" · "+environment.composition,14,TEAL,false));
        content.addView(label(environment.description,13,MUTED,false));
        String axisInfo=b.id<=8?String.format(new Locale("es"),"Inclinación del eje: %.2f°",b.obliquityDeg):"Giro y orientación: aproximación ligada al plano orbital";
        String orbitalPeriod=b.id==0?"No aplica en este modelo heliocéntrico":String.format(new Locale("es"),"%,.3f días",b.orbitalPeriodDays);
        content.addView(label(String.format(new Locale("es"),"Radio medio: %,.1f km\nRotación sideral: %s\n%s\nPeríodo orbital: %s",b.radiusKm,periodText(b.rotationHours),axisInfo,orbitalPeriod),14,MUTED,false));
        content.addView(section("SU ENTORNO MAGNÉTICO"));content.addView(label(b.fieldDescription,14,TEXT,false));
        if(b.fieldType==1)content.addView(label(String.format(new Locale("es"),"Referencia dipolar: %.4g µT en el ecuador.\nInclinación magnética de referencia: %.2f°.",b.fieldMicroT,b.magneticTiltDeg),13,TEAL,false));
        if(b.fieldType==1)content.addView(button("Sonda del campo magnético",0,()->probeDialog(b.id)));
        content.addView(section("EXPLORAR CERCA"));
        for(SolarData.Body moon:SolarData.BODIES)if(moon.parent==b.id&&moon.id>8){
            final int id=moon.id;Button go=button("Viajar a "+moon.name,0,()->chooseBody(id));content.addView(go);
        }
        content.addView(label("Las órbitas planetarias usan elementos de JPL. Las fases de las lunas, las texturas procedurales y las líneas de campo son aproximaciones visuales. Consulta Modelo y fuentes para ver su alcance.",12,MUTED,false));
        AlertDialog d=new AlertDialog.Builder(this).setTitle(b.name).setView(scroll(content)).setPositiveButton("Explorar",(dialog,which)->chooseBody(b.id)).setNegativeButton("Cerrar",null).create();showDialog(d);
    }
    private void timeDialog(){
        LinearLayout content=dialogColumn();
        TextView current=label(SPEED_NAMES[speedIndex],23,GOLD,true);content.addView(current);
        content.addView(label("Acelera, detén o invierte el tiempo orbital.",14,MUTED,false));
        SeekBar speed=new SeekBar(this);speed.setMax(SPEEDS.length-1);speed.setProgress(speedIndex);
        speed.setContentDescription("Velocidad del tiempo");
        speed.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar s,int p,boolean user){if(!user)return;speedIndex=p;current.setText(SPEED_NAMES[p]);paused=p==3;runGL(()->{observer.setSpeed(SPEEDS[p]);observer.setPaused(p==3);});updateButtons();}
            public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}
        });content.addView(speed,new LinearLayout.LayoutParams(-1,dp(54)));
        Button today=button("Volver a la fecha de hoy",0,()->runGL(()->observer.setJulianDate(nowJulian())));content.addView(today);
        content.addView(button("Elegir una fecha",0,()->datePicker()));
        content.addView(label("Intervalo del modelo: 1800–2050. La fecha se muestra en UTC. La integración se pausa al salir de la app.",12,MUTED,false));
        AlertDialog d=new AlertDialog.Builder(this).setTitle("Ritmo del universo").setView(content).setPositiveButton("Listo",null).create();showDialog(d);
    }
    private void datePicker(){
        Calendar c=Calendar.getInstance(TimeZone.getTimeZone("UTC"));c.setTimeInMillis((long)((observer.julianDate()-2440587.5)*86400000));
        DatePickerDialog d=new DatePickerDialog(this,(picker,y,m,day)->{
            Calendar chosen=Calendar.getInstance(TimeZone.getTimeZone("UTC"));chosen.clear();chosen.set(y,m,day,12,0,0);
            double jd=chosen.getTimeInMillis()/86400000.0+2440587.5;runGL(()->observer.setJulianDate(jd));
        },c.get(Calendar.YEAR),c.get(Calendar.MONTH),c.get(Calendar.DAY_OF_MONTH));
        d.getDatePicker().setMinDate((long)((SolarData.MIN_JD-2440587.5)*86400000));
        d.getDatePicker().setMaxDate((long)((SolarData.MAX_JD-2440587.5)*86400000));d.show();
    }
    private void settingsDialog(){
        LinearLayout content=dialogColumn();
        content.addView(section("CAPAS DEL SISTEMA"));
        addToggle(content,"Campos magnéticos",fields,b->{fields=b;runGL(()->observer.setFields(b));updateButtons();updateBody();});
        addToggle(content,"Viento solar",wind,b->{wind=b;runGL(()->observer.setWind(b));});
        addToggle(content,"Órbitas",orbits,b->{orbits=b;runGL(()->observer.setOrbits(b));});
        addToggle(content,"Atmósferas y bruma",atmospheres,b->{atmospheres=b;runGL(()->observer.setAtmosphereEnabled(b));});
        addToggle(content,"Proporciones reales",realScale,b->{realScale=b;freeFlight=false;runGL(()->observer.setRealScale(b));updateBody();updateButtons();});
        addToggle(content,"Gravedad local durante descenso",localPhysics,b->{localPhysics=b;runGL(()->observer.setLocalPhysics(b));updateBody();});
        content.addView(label("Exploración amplía los cuerpos y comprime distancias para verlos juntos. Proporciones reales conserva sus tamaños y distancias; usa los destinos para acercarte.",12,MUTED,false));
        content.addView(section("CALIDAD DE IMAGEN"));
        content.addView(button("Auto fluido · 480–1280 px",0,this::setFluidQuality));
        LinearLayout qualities=row();int[] sizes={640,960,1440};String[] labels={"Ahorro","Equilibrada","Alta"};
        for(int i=0;i<sizes.length;i++){final int px=sizes[i];Button b=button(labels[i],0,()->setAdaptiveQuality(px));qualities.addView(b,new LinearLayout.LayoutParams(0,dp(46),1));}content.addView(qualities);
        content.addView(button("4K UHD · 3840 × 2160 / vertical",0,()->setUhdQuality(true)));
        content.addView(button("8K UHD · 7680 × 4320 / vertical",0,()->setUltraQuality(true)));
        content.addView(label("4K/8K usan framebuffers reales cuando el dispositivo los admite. Si faltan límites o memoria, el renderer conserva la última resolución válida.",12,MUTED,false));
        content.addView(section("TU MOTOR"));content.addView(button("Núcleo X · estados y trazas",0,()->coreDialog()));
        content.addView(button("Modelo físico y fuentes",0,()->modelDialog()));
        content.addView(button("Cómo navegar",0,()->helpDialog()));
        AlertDialog d=new AlertDialog.Builder(this).setTitle("Observatorio").setView(scroll(content)).setPositiveButton("Listo",null).create();showDialog(d);
    }
    private interface ToggleAction{void changed(boolean b);}
    private void addToggle(LinearLayout parent,String title,boolean initial,ToggleAction action){
        android.widget.Switch toggle=new android.widget.Switch(this);toggle.setText(title);toggle.setTextSize(15);toggle.setTextColor(TEXT);toggle.setChecked(initial);toggle.setPadding(0,dp(10),0,dp(10));
        toggle.setOnCheckedChangeListener((button,checked)->action.changed(checked));parent.addView(toggle,new LinearLayout.LayoutParams(-1,dp(51)));
    }
    private void coreDialog(){
        LinearLayout content=dialogColumn();
        content.addView(label("Ether Core combina reglas globales y una preparación en 8 direcciones del mismo estado. El observador gráfico consulta ese estado para distribuir y animar las trazas del viento solar y del campo.",15,TEXT,false));
        TextView live=label(coreReadout(),13,TEAL,false);live.setPadding(0,dp(14),0,dp(14));content.addView(live);
        for(int i=0;i<QuantumSolarBridge.PRESETS.length;i++){final int preset=i;
            content.addView(button(QuantumSolarBridge.PRESETS[i],0,()->runGL(()->{
                try{quantum.setPreset(preset);handler.post(()->live.setText(coreReadout()));}
                catch(Throwable e){handler.post(()->showRenderError(e.getMessage()));}
            })));
        }
        content.addView(label("Activa Campos o Viento solar para observar el acoplamiento. Cambiar la preparación modifica las trazas visuales; las órbitas y los valores físicos permanecen definidos por los modelos astronómicos. Este acoplamiento no demuestra aceleración cuántica ni reproduce la dinámica cuántica de un planeta.",12,MUTED,false));
        AlertDialog d=new AlertDialog.Builder(this).setTitle("Núcleo X conectado").setView(scroll(content)).setPositiveButton("Listo",null).create();showDialog(d);
    }
    private String coreReadout(){
        float[] c=quantum.coefficients();
        return String.format(Locale.ROOT,"%s\n⟨X₀⟩ %.1f   ⟨Y₀⟩ %.1f   ⟨Z₀⟩ %.1f\n⟨X₀X₁⟩ %.1f   ⟨Y₀Y₁⟩ %.1f   ⟨Z₀Z₁⟩ %.1f\nComponentes finitos: %d bytes de tableau",quantum.status(),c[0],c[1],c[2],c[3],c[4],c[5],quantum.stateBytes());
    }
    private void probeDialog(int id){
        SolarData.Body b=SolarData.BODIES[id];LinearLayout content=dialogColumn();
        content.addView(label("Mueve la sonda y observa cómo cambia el campo de un dipolo ideal.",15,TEXT,false));
        TextView readout=label("",17,TEAL,true);readout.setPadding(0,dp(16),0,dp(12));content.addView(readout);
        content.addView(label("Altura sobre la superficie",13,MUTED,false));
        SeekBar height=new SeekBar(this);height.setMax(100);height.setProgress(0);height.setContentDescription("Altitud de la sonda magnética");content.addView(height,new LinearLayout.LayoutParams(-1,dp(52)));
        content.addView(label("Latitud magnética: ecuador → polo",13,MUTED,false));
        SeekBar latitude=new SeekBar(this);latitude.setMax(90);latitude.setProgress(0);latitude.setContentDescription("Latitud magnética de la sonda");content.addView(latitude,new LinearLayout.LayoutParams(-1,dp(52)));
        Runnable update=()->{
            double r=1+19*height.getProgress()/100.0,angle=Math.toRadians(latitude.getProgress());
            double[] pos={r*Math.cos(angle),r*Math.sin(angle),0};
            double[] field=SolarData.dipoleField(pos,new double[]{0,1,0},b.fieldMicroT);
            double strength=Math.sqrt(field[0]*field[0]+field[1]*field[1]+field[2]*field[2]);
            readout.setText(String.format(new Locale("es"),"|B|  %.5g µT\nAltura  %,.0f km\nDistancia al centro  %.2f radios\nLatitud magnética  %d°",strength,(r-1)*b.radiusKm,r,latitude.getProgress()));
        };
        SeekBar.OnSeekBarChangeListener change=new SeekBar.OnSeekBarChangeListener(){
            public void onProgressChanged(SeekBar s,int p,boolean user){update.run();}
            public void onStartTrackingTouch(SeekBar s){} public void onStopTrackingTouch(SeekBar s){}
        };height.setOnSeekBarChangeListener(change);latitude.setOnSeekBarChangeListener(change);update.run();
        content.addView(label("En este modelo B disminuye como 1/r³. En la superficie, el campo polar es el doble del ecuatorial. La sonda calcula el campo exterior de un dipolo interno de referencia; no incluye las corrientes del plasma ni el viento solar y pierde realismo lejos del planeta.",13,MUTED,false));
        AlertDialog d=new AlertDialog.Builder(this).setTitle("Sonda · "+b.name).setView(content).setPositiveButton("Listo",null).create();showDialog(d);
    }
    private void modelDialog(){
        String text="ÓRBITAS\nElementos y variaciones seculares de NASA/JPL, ecuación de Kepler y coordenadas heliocéntricas. Aproximación válida de 1800 a 2050; no es una efeméride de navegación. La fecha civil se aproxima a la escala temporal del modelo.\n\nLUNAS\nRadios y períodos de referencia. Trayectorias simplificadas y fases ilustrativas; no predice eclipses, tránsitos ni aterrizajes.\n\nMAGNETISMO\nDipolos inclinados para campos intrínsecos. Venus: entorno inducido. Marte: entorno inducido y corteza magnetizada. Sol: representación de corona y viento. Las líneas exteriores, colas y trazas son un esquema; no se resuelve una simulación magnetohidrodinámica ni se usan mediciones en vivo.\n\nIMÁGENES\nTierra: NASA/GSFC Scientific Visualization Studio, Blue Marble. Luna: NASA Scientific Visualization Studio, LRO/LROC, CGI Moon Kit (mapa preparado para visualización). Júpiter: NASA/JPL/Space Science Institute, Cassini PIA07782. El resto de superficies, nubes, corona y fondo estelar se generan de forma procedural. No son mapas geológicos ni un catálogo de estrellas.\n\nCOSMIC X ENGINE\nNúcleo original, puente JNI original y extensión del observador gráfico de Mundo X. Cálculo nativo y GLES del dispositivo. Las trazas consumen observables reales del núcleo; no se atribuye una ventaja de rendimiento que no se haya medido.\n\nFUENTES\nssd.jpl.nasa.gov/planets/approx_pos.html\nssd.jpl.nasa.gov/planets/phys_par.html\nssd.jpl.nasa.gov/sats/phys_par/\nscience.nasa.gov/solar-system/\nsvs.gsfc.nasa.gov/2915/\nsvs.gsfc.nasa.gov/4720/\nscience.nasa.gov/photojournal/cassinis-best-maps-of-jupiter-cylindrical-map/";
        text="EXPLORACIÓN 4K Y DESCENSO\nEl render UHD usa RenderResolution del motor COSMIC X Engine 4K: 3840 × 2160 o 2160 × 3840 píxeles. La pantalla muestra la imagen ajustada y el PNG conserva su tamaño completo.\n\nATMÓSFERAS\nCada cuerpo conserva su clasificación: atmósfera, exosfera, plumas locales o ausencia de una envoltura densa en el modelo. Composición y datos de referencia se basan en NASA/ESA. Alturas de las capas visibles, colores, densidad, nubes y relieve aparente son aproximaciones gráficas; no una predicción meteorológica ni un mapa topográfico medido. En los gigantes se recorre una envoltura nubosa sin un suelo donde aterrizar.\n\nMAPA\nProyección de las posiciones del catálogo, con radios orbitales comprimidos para explorar. Los tamaños se amplían y las lunas aparecen en un mapa local separado.\n\n"+text;
        LinearLayout content=dialogColumn();TextView info=label(text,14,TEXT,false);info.setTextIsSelectable(true);content.addView(info);
        AlertDialog d=new AlertDialog.Builder(this).setTitle("Modelo y fuentes").setView(scroll(content)).setPositiveButton("Cerrar",null).create();showDialog(d);
    }
    private void helpDialog(){
        AlertDialog d=new AlertDialog.Builder(this).setTitle("Navega tu sistema solar").setMessage("• Mapa: elige un planeta y después una luna en su mapa local. Sistema 3D recupera la vista orbital completa.\n\n• Descender: pasa a la cámara cercana al destino. Altitud permite entrar en el cielo o las nubes y acercarte a la superficie de referencia.\n\n• Durante el descenso, arrastra para mirar; pellizca o usa Subir/Bajar para cambiar altura. El control circular avanza sobre el planeta.\n\n• Volver a órbita: recupera la navegación exterior y sus capas magnéticas.\n\n• En órbita, pellizca para acercarte y arrastra dos dedos para desplazar la cámara. Doble toque: vuelve a centrar el destino.\n\n• Volar: mueve libremente la cámara por el espacio.\n\n• Calidad → 4K UHD: activa la resolución completa. Guardar PNG exporta la escena.\n\n• Tiempo: acelera, retrocede o cambia la fecha. Campos muestra el entorno magnético desde la órbita.\n\n• El círculo superior oculta los controles para disfrutar la vista.").setPositiveButton("Entendido",null).create();showDialog(d);
    }
    private void showRenderError(String error){
        if(isFinishing())return;
        AlertDialog d=new AlertDialog.Builder(this).setTitle("El observador necesita atención").setMessage(error).setPositiveButton("Cerrar",null).create();showDialog(d);
    }
    private void showDialog(AlertDialog d){
        d.show();if(d.getWindow()!=null)d.getWindow().setBackgroundDrawable(card(0xff0e1929,0xff29405c,18));
        Button positive=d.getButton(AlertDialog.BUTTON_POSITIVE),negative=d.getButton(AlertDialog.BUTTON_NEGATIVE);
        if(positive!=null)positive.setTextColor(GOLD);if(negative!=null)negative.setTextColor(MUTED);
    }

    private class Joystick extends View{
        private final Paint paint=new Paint(3);private float x,y;private boolean held;private long last;
        private final Runnable advance=new Runnable(){public void run(){
            if(!held||!resumed)return;long now=android.os.SystemClock.uptimeMillis();float dt=Math.min(.06f,(now-last)/1000f);last=now;
            float f=-y*dt*1.6f,r=x*dt*1.6f;runGL(()->observer.move(f,r));postDelayed(this,16);
        }};
        Joystick(){super(SolarActivity.this);setContentDescription("Control de vuelo. Arrastra arriba para avanzar y a los lados para desplazarte.");}
        @Override protected void onDraw(Canvas c){super.onDraw(c);float cx=getWidth()/2f,cy=getHeight()/2f,rr=Math.min(cx,cy)-dp(7);
            paint.setStyle(Paint.Style.FILL);paint.setColor(0x98091423);c.drawCircle(cx,cy,rr,paint);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(1));paint.setColor(0x6669dbdf);c.drawCircle(cx,cy,rr,paint);
            c.drawLine(cx-rr*.6f,cy,cx+rr*.6f,cy,paint);c.drawLine(cx,cy-rr*.6f,cx,cy+rr*.6f,paint);
            paint.setStyle(Paint.Style.FILL);paint.setColor(0xdd69dbdf);c.drawCircle(cx+x*rr*.65f,cy+y*rr*.65f,dp(16),paint);
        }
        @Override public boolean onTouchEvent(MotionEvent e){
            if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL){held=false;x=y=0;removeCallbacks(advance);performClick();invalidate();return true;}
            float cx=getWidth()/2f,cy=getHeight()/2f;x=(e.getX()-cx)/(cx*.7f);y=(e.getY()-cy)/(cy*.7f);float len=(float)Math.sqrt(x*x+y*y);if(len>1){x/=len;y/=len;}
            if(!held){held=true;last=android.os.SystemClock.uptimeMillis();post(advance);}invalidate();return true;
        }
        @Override public boolean performClick(){super.performClick();return true;}
        void stop(){held=false;x=y=0;removeCallbacks(advance);}
    }

    private LinearLayout column(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;}
    private LinearLayout row(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.HORIZONTAL);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    private LinearLayout dialogColumn(){LinearLayout l=column();l.setPadding(dp(22),dp(10),dp(22),dp(16));return l;}
    private ScrollView scroll(View content){ScrollView s=new ScrollView(this);s.setFillViewport(false);s.addView(content);return s;}
    private TextView section(String title){TextView t=label(title,11,GOLD,true);t.setLetterSpacing(.06f);t.setPadding(0,dp(20),0,dp(8));return t;}
    private TextView label(String text,int size,int color,boolean bold){
        TextView t=new TextView(this);t.setText(text);t.setTextColor(color);t.setTextSize(size);t.setFontFeatureSettings("kern");
        t.setTypeface(Typeface.create(bold?"sans-serif-medium":"sans-serif",bold?Typeface.BOLD:Typeface.NORMAL));t.setLineSpacing(dp(2),1);return t;
    }
    private Button button(String text,int width,Runnable action){
        Button b=new Button(this);b.setText(text);b.setTextSize(12);b.setTextColor(TEXT);b.setAllCaps(false);b.setMinWidth(0);b.setMinimumWidth(0);b.setMinHeight(0);b.setMinimumHeight(0);
        b.setPadding(dp(12),0,dp(12),0);b.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));b.setBackground(card(0xc7142032,0x40405570,12));
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(width>0?dp(width):-2,dp(43));p.setMargins(dp(2),dp(2),dp(3),dp(2));b.setLayoutParams(p);b.setOnClickListener(v->action.run());return b;
    }
    private GradientDrawable card(int color,int stroke,int radius){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(radius));d.setStroke(dp(1),stroke);return d;}
    private int dp(float n){return (int)(n*getResources().getDisplayMetrics().density+.5f);}
    private static int clamp(int n,int lo,int hi){return Math.max(lo,Math.min(hi,n));}
    private static String periodText(double hours){if(hours==0)return "variable";return String.format(new Locale("es"),Math.abs(hours)>=48?"%.2f días":"%.2f h",Math.abs(hours)>=48?Math.abs(hours)/24:Math.abs(hours))+(hours<0?" ↶":"");}
    private static double nowJulian(){return System.currentTimeMillis()/86400000.0+2440587.5;}

    @Override protected void onResume(){super.onResume();resumed=true;immersive();if(surface!=null){surface.queueEvent(()->observer.resetFrameClock());surface.onResume();surface.setPausedRendering(paused);surface.requestRender();}handler.post(ticker);}
    @Override protected void onPause(){
        resumed=false;handler.removeCallbacks(ticker);if(joystick!=null)joystick.stop();
        if(surface!=null)surface.onPause();savePreferences();super.onPause();
    }
    private void savePreferences(){
        if(quantum==null)return;
        double[] pose=observer==null?null:observer.entryState();
        getSharedPreferences("solar",MODE_PRIVATE).edit().putInt("body",body).putInt("speed",speedIndex)
                .putInt("quality",quality).putInt("preset",quantum.preset()).putBoolean("fields",fields)
                .putBoolean("wind",wind).putBoolean("orbits",orbits).putBoolean("realScale",realScale)
                .putBoolean("paused",paused).putBoolean("uhd4k",uhd4k).putBoolean("uhd8k",uhd8k).putBoolean("fluidPreview",fluidPreview).putBoolean("atmospheres",atmospheres).putBoolean("localPhysics",localPhysics)
                .putBoolean("entry",entryActive).putLong("entryAltitude",Double.doubleToLongBits(desiredEntryAltitude))
                .putString("entryPose",poseText(pose)).apply();
    }
    @Override protected void onSaveInstanceState(Bundle state){
        super.onSaveInstanceState(state);state.putInt("body",body);state.putInt("speed",speedIndex);state.putBoolean("fields",fields);state.putBoolean("wind",wind);state.putBoolean("orbits",orbits);state.putBoolean("realScale",realScale);state.putBoolean("paused",paused);
        if(observer!=null)state.putDouble("jd",observer.julianDate());if(quantum!=null)state.putInt("preset",quantum.preset());
        state.putInt("quality",quality);state.putBoolean("uhd4k",uhd4k);state.putBoolean("uhd8k",uhd8k);state.putBoolean("fluidPreview",fluidPreview);state.putBoolean("atmospheres",atmospheres);state.putBoolean("localPhysics",localPhysics);
        state.putBoolean("entry",entryActive);state.putDouble("entryAltitude",desiredEntryAltitude);
        state.putDoubleArray("entryPose",observer==null?null:observer.entryState());
        if(pngDestinationOpen&&pngToken!=null){state.putBoolean("pngDestinationOpen",true);state.putString("pngToken",pngToken);}
    }
    @Override protected void onDestroy(){
        destroyed=true;if(!pngDestinationOpen&&pngToken!=null)PNG_OWNER.compareAndSet(pngToken,null);
        if(mapDialog!=null)mapDialog.dismiss();handler.removeCallbacksAndMessages(null);images.shutdown();
        if(quantum!=null)quantum.close();super.onDestroy();
    }
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(focus)immersive();}
    @Override public void onBackPressed(){if(cleanView)toggleClean();else if(entryActive)exitEntry();else if(freeFlight)toggleFlight();else if(body>=0)chooseBody(-1);else super.onBackPressed();}

    // The instrumentation exercises the same Activity, native core and renderer.
    public SolarRenderer renderer(){return observer;}
    public SolarSurface surface(){return surface;}
    public QuantumSolarBridge bridge(){return quantum;}
    public int selectedDestination(){return body;}
    public void navigateForTest(int id){chooseBody(id);}
    public void fieldsForTest(boolean enabled){fields=enabled;runGL(()->observer.setFields(enabled));updateButtons();updateBody();}
    public void pauseForTest(boolean enabled){paused=enabled;runGL(()->observer.setPaused(enabled));if(surface!=null)surface.setPausedRendering(enabled);updateButtons();}
    public void scaleForTest(boolean enabled){realScale=enabled;runGL(()->observer.setRealScale(enabled));updateBody();}
}
