package com.mundox.worldlink.phone;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.google.android.gms.wearable.MessageClient;
import com.google.android.gms.wearable.MessageEvent;
import com.google.android.gms.wearable.Node;
import com.google.android.gms.wearable.Wearable;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Standalone real-physics laboratory for Galaxy Watch4 Classic.
 * Sensors and the photon actuator are real; no physical entanglement is claimed.
 */
public final class MainActivity extends Activity implements MessageClient.OnMessageReceivedListener {
    static final String SAMPLE = "/mundox/worldlink/v1/telemetry";
    static final String HAPTIC = "/mundox/worldlink/v1/haptic";
    static final String LIGHT_REQUEST = "/mundox/worldlink/v1/light-request";
    static final String SOUND_REQUEST = "/mundox/worldlink/v1/sound-command";
    private static final int BG=0xff0c1412, SURFACE=0xff192721, GREEN=0xffcbf47e;
    private static final int WHITE=0xffe8f0e9, MUTED=0xffa6b7aa, CYAN=0xff80dce0;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private PhotonEmitter photons;
    private SoundFieldEmitter sound;
    private final ExecutorService quantumWorker=Executors.newSingleThreadExecutor();
    private TextView quantumReport;
    private boolean quantumBusy;
    private int quantumGeneration;
    private TelemetryChart chart;
    private TextView linkStatus, watchName, lightValue, fieldValue, motionValue, pulseValue;
    private TextView caption, experiment, measurement, lastSignal, soundStatus;
    private Button signalButton, lightButton, armButton;
    private boolean watching, lightGraph=true, armed, measuring;
    private String sid="";
    private long seq=-1,lastReceipt=0, received;
    private int connectedNodes, lastMask;
    private float lux=Float.NaN, mag=Float.NaN, motion=Float.NaN;
    private float baselineLight=Float.NaN, baselineMag=Float.NaN;
    private float peakLight=Float.NaN;
    private long measurementStart;
    private String feedback="Esperando la primera señal de tu reloj.";
    private String experimentFeedback="Sin experimento: captura una referencia o emite un pulso.";
    private boolean activityVisible;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        photons=new PhotonEmitter(this);
        sound=new SoundFieldEmitter(this::showFeedback);
        drawInterface();
    }

    @Override protected void onStart() {
        super.onStart();
        activityVisible=true;
        Wearable.getMessageClient(this).addListener(this)
                .addOnFailureListener(e->showFeedback("Error de enlace: "+e.getMessage()));
        discoverWatch();
        handler.postDelayed(heartbeat,2500);
    }
    @Override protected void onStop() {
        activityVisible=false;
        armed=false;
        measuring=false;
        ++quantumGeneration;
        photons.off();
        sound.stop();
        handler.removeCallbacks(heartbeat);
        Wearable.getMessageClient(this).removeListener(this);
        updateArm();
        super.onStop();
    }
    @Override protected void onDestroy() {
        photons.off();
        ++quantumGeneration;
        quantumWorker.shutdown();
        sound.close();
        super.onDestroy();
    }
    private final Runnable heartbeat=new Runnable() {
        @Override public void run(){
            if (!activityVisible) return;
            if (SystemClock.elapsedRealtime()-lastReceipt>6500) {
                linkStatus.setText("SIN TELEMETRÍA");
                linkStatus.setTextColor(MUTED);
            }
            discoverWatch();
            handler.postDelayed(this,5000);
        }
    };

    private void discoverWatch() {
        Wearable.getNodeClient(this).getConnectedNodes().addOnSuccessListener(nodes->{
            connectedNodes=nodes.size();
            if(!activityVisible)return;
            watchName.setText(nodes.isEmpty()
                    ? "Reloj no detectado · abre WorldLink en el Watch4"
                    : SystemClock.elapsedRealtime()-lastReceipt < 5000
                        ? "Reloj enlazado · transmitiendo en vivo"
                        : "Reloj enlazado · los controles remotos están disponibles");
            if (nodes.isEmpty()) {
                linkStatus.setText("RELOJ SIN ENLACE");
                linkStatus.setTextColor(MUTED);
            }
        }).addOnFailureListener(e->showFeedback("No se pudo consultar Wear OS: "+e.getMessage()));
    }

    @Override public void onMessageReceived(MessageEvent event) {
        String path=event.getPath();
        if(LIGHT_REQUEST.equals(path)) {
            runOnUiThread(()->{
                if (activityVisible && armed) startLightExperiment();
                else showFeedback("Solicitud del reloj ignorada: activa SALIDA FÍSICA en el móvil.");
            });
            return;
        }
        if (SOUND_REQUEST.equals(path)) {
            final byte[] payload=event.getData();
            if(payload==null || payload.length>32)return;
            final String cmd=new String(payload,StandardCharsets.UTF_8);
            runOnUiThread(()->handleSoundCommand(cmd));
            return;
        }
        if (!SAMPLE.equals(path)) return;
        byte[] payload=event.getData();
        if (payload==null || payload.length>4096) return;
        runOnUiThread(()->readWatchSample(payload));
    }

    private void readWatchSample(byte[] payload) {
        if (!activityVisible) return;
        try {
            JSONObject j=new JSONObject(new String(payload,StandardCharsets.UTF_8));
            if (j.optInt("v",-1)!=1) return;
            String newSid=j.optString("sid","");
            long n=j.optLong("seq",-1);
            if (newSid.length()<6 || newSid.length()>80 || n<0) return;
            if (!sid.equals(newSid)) {
                sid=newSid;
                seq=-1;
                received=0;
                chart.setSignal(lightGraph?"ILUMINACIÓN":"CAMPO MAGNÉTICO",
                        lightGraph?"lux":"µT",lightGraph?GREEN:CYAN);
                baselineLight=Float.NaN;
                baselineMag=Float.NaN;
            }
            if(n<=seq)return;
            seq=n;
            received++;
            lastReceipt=SystemClock.elapsedRealtime();
            int mask=j.optInt("mask",0);
            lastMask=mask;

            float a=(float)j.optDouble("ax",Double.NaN);
            float b=(float)j.optDouble("ay",Double.NaN);
            float c=(float)j.optDouble("az",Double.NaN);
            float gx=(float)j.optDouble("gx",Double.NaN);
            float gy=(float)j.optDouble("gy",Double.NaN);
            float gz=(float)j.optDouble("gz",Double.NaN);
            float mx=(float)j.optDouble("mx",Double.NaN);
            float my=(float)j.optDouble("my",Double.NaN);
            float mz=(float)j.optDouble("mz",Double.NaN);
            float light=(float)j.optDouble("lux",Double.NaN);
            double bpm=j.optDouble("hr",Double.NaN);

            if ((mask&3)==3 && allFinite(a,b,c,gx,gy,gz)) {
                motion=(float)Math.sqrt(gx*gx+gy*gy+gz*gz);
                motionValue.setText(String.format(Locale.US,"%.2f rad/s",motion));
            }
            if ((mask&4)!=0 && allFinite(mx,my,mz)) {
                mag=(float)Math.sqrt(mx*mx+my*my+mz*mz);
                fieldValue.setText(String.format(Locale.US,"%.1f µT",mag));
                if (!lightGraph)chart.append(mag);
            }
            if ((mask&16)!=0 && Float.isFinite(light) && light>=0) {
                lux=light;
                lightValue.setText(String.format(Locale.US,"%.1f lux",lux));
                if (lightGraph)chart.append(lux);
                if (measuring && SystemClock.elapsedRealtime()-measurementStart<=2100) {
                    peakLight=Float.isFinite(peakLight)?Math.max(peakLight,lux):lux;
                }
            }
            if ((mask&8)!=0 && Double.isFinite(bpm) && bpm>0 && bpm<280)
                pulseValue.setText(String.format(Locale.US,"%.0f lpm",bpm));
            else pulseValue.setText("No disponible");
            linkStatus.setText("RECIBIENDO · "+received+" MUESTRAS");
            linkStatus.setTextColor(GREEN);
            lastSignal.setText("Última lectura: ahora · datos del reloj real");
            caption.setText("No hay señales inventadas. Si un sensor no está disponible, aparece —.");
        } catch(Exception ex) {
            showFeedback("Se descartó un paquete incompleto del reloj.");
        }
    }

    private static boolean allFinite(float... v) {
        for(float f:v) if(!Float.isFinite(f))return false;
        return true;
    }

    private void setGraph(boolean light) {
        lightGraph=light;
        chart.setSignal(light?"ILUMINACIÓN":"CAMPO MAGNÉTICO",light?"lux":"µT",light?GREEN:CYAN);
        signalButton.setText(light?"Ver gráfico: magnetismo":"Ver gráfico: luz");
        caption.setText(light?"Intensidad de luz recibida por el reloj.":"Magnitud del campo magnético en el reloj.");
    }

    private void captureBaseline() {
        if (!Float.isFinite(lux) && !Float.isFinite(mag)) {
            experimentFeedback="Aún no recibimos mediciones. Inicia WorldLink en el reloj.";
            updateExperiment();
            return;
        }
        baselineLight=lux;
        baselineMag=mag;
        experimentFeedback=(Float.isFinite(lux)
            ? String.format(Locale.US,"Luz base: %.1f lux. ",lux)
            : "Luz: sensor no disponible. ")
                +(Float.isFinite(mag)
            ? String.format(Locale.US,"Campo base: %.1f µT.",mag)
            : "Campo: sin lectura.");
        updateExperiment();
    }

    private void startLightExperiment() {
        if (measuring) {showFeedback("Pulso en curso: no repetir todavía.");return;}
        if (!photons.hasPermission()) {
            photons.requestPermission();
            showFeedback("Autoriza la linterna y vuelve a pulsar EMITIR.");
            return;
        }
        if (!Float.isFinite(lux)) {
            showFeedback("Luz no disponible en el reloj. Se puede emitir, pero no medir lux.");
        }
        if (Float.isFinite(lux)) baselineLight=lux;
        String status=photons.pulse();
        if (!photons.isOn()) {
            experimentFeedback=status;
            updateExperiment();
            return;
        }
        measurementStart=SystemClock.elapsedRealtime();
        peakLight=lux;
        measuring=true;
        experimentFeedback="EMISIÓN REAL · buscando cambio de luminosidad…";
        updateExperiment();
        showFeedback(status);
        handler.postDelayed(()->{
            if (!measuring || !activityVisible)return;
            measuring=false;
            if (Float.isFinite(baselineLight) && Float.isFinite(peakLight)) {
                float difference=peakLight-baselineLight;
                experimentFeedback=String.format(Locale.US,
                    "Antes %.1f lux → máximo %.1f lux\nΔ = %+.1f lux\n"
                    +"Repite varias veces y compara sin emitir luz.",
                    baselineLight,peakLight,difference);
            } else {
                experimentFeedback="Fotones emitidos; el reloj no proporcionó lux válidos para cuantificar el efecto.";
            }
            updateExperiment();
        },2250L);
    }

    private void vibrateWatch() {
        Wearable.getNodeClient(this).getConnectedNodes()
            .addOnSuccessListener(nodes->{
                if(nodes.isEmpty()){showFeedback("No hay reloj conectado.");return;}
                for(Node node:nodes) {
                    Wearable.getMessageClient(this).sendMessage(node.getId(),HAPTIC,new byte[]{1})
                        .addOnFailureListener(e->showFeedback("No llegó la orden: "+e.getMessage()));
                }
                showFeedback("Orden de vibración enviada (confirmar en el reloj).");
            }).addOnFailureListener(e->showFeedback("Error: "+e.getMessage()));
    }

    private void updateExperiment() {
        if(experiment!=null)experiment.setText(experimentFeedback);
    }
    private void updateArm() {
        if (armButton==null)return;
        armButton.setText(armed?"CONTROL REMOTO: ACTIVADO":"CONTROL REMOTO: DESACTIVADO");
        armButton.setTextColor(armed?0xff121b14:WHITE);
        armButton.setBackground(buttonBackground(armed?GREEN:0xff293c32));
    }
    private void handleSoundCommand(String cmd) {
        if(!activityVisible)return;
        // STOP is deliberately honored even when the remote control is disarmed.
        if("stop".equals(cmd)) {
            stopSound();
            return;
        }
        if(!armed) {
            showFeedback("Solicitud del reloj bloqueada: activa CONTROL REMOTO en el móvil.");
            return;
        }
        if("quantum".equals(cmd)) {
            runQuantum(true, -1f);
            return;
        }
        if(cmd.startsWith("quantum:") && cmd.length()<=22) {
            try {
                int angularHundredths=Integer.parseInt(cmd.substring(8));
                if(angularHundredths>=0 && angularHundredths<=1800) {
                    runQuantum(true,angularHundredths/100f);
                    return;
                }
            } catch(NumberFormatException ignored){}
            showFeedback("Dato del gesto inválido; no se emitió nada.");
            return;
        }
        switch(cmd) {
            case "tone:220": playTone(220);break;
            case "tone:440": playTone(440);break;
            case "sweep": playSweep();break;
            default: showFeedback("Se rechazó un comando remoto desconocido.");
        }
    }
    private void playTone(int hz) {
        String result=sound.emitTone(hz);
        if(soundStatus!=null)soundStatus.setText(result);
        showFeedback(result);
    }
    private void playSweep(){
        String result=sound.emitSweep();
        if(soundStatus!=null)soundStatus.setText(result);
        showFeedback(result);
    }
    private void stopSound(){
        ++quantumGeneration;
        sound.stop();
        if(soundStatus!=null)soundStatus.setText("Salida sonora detenida");
        showFeedback("Emisión acústica detenida");
    }

    private void runQuantum(boolean emitPhysical,float angularOverride) {
        if(quantumBusy) {
            showFeedback("MIL/Ether calculando un estado: espera a que termine antes de repetir.");
            return;
        }
        if(!QuantumBridge.available()) {
            showFeedback("Falta biblioteca nativa del núcleo: "+QuantumBridge.problem());
            return;
        }
        final boolean fresh=SystemClock.elapsedRealtime()-lastReceipt<6500L;
        final float m=fresh && (lastMask&4)!=0 && Float.isFinite(mag)?mag:-1f;
        final float rotation=Float.isFinite(angularOverride) && angularOverride>=0
                ? angularOverride
                : fresh && (lastMask&3)==3 && Float.isFinite(motion)?motion:-1f;
        final float light=fresh && (lastMask&16)!=0 && Float.isFinite(lux)?lux:-1f;
        final String source=m>=0 || rotation>=0 || light>=0
                ? "Reloj físico · parámetros medidos"
                : "Sin señal Watch4 · parámetros neutros explícitos";
        final int ticket=++quantumGeneration;
        quantumBusy=true;
        if(quantumReport!=null)quantumReport.setText("MIL 1.000 + Ether COW256 calculando...\n"+source);
        quantumWorker.execute(()->{
            try {
                JSONObject state=QuantumBridge.evolve(m,rotation,light);
                final int hz=state.getInt("frequency_hz");
                runOnUiThread(()->{
                    quantumBusy=false;
                    if(!activityVisible || ticket!=quantumGeneration)return;
                    String output="ESTADO BELL SIMULADO · q0 ↔ q999\n"+
                            String.format(Locale.US,"ZZ = %+.4f   XX = %+.4f\n",
                                state.optDouble("zz"),state.optDouble("xx"))+
                            String.format(Locale.US,"CHSH = %.3f (simulado; límite clásico 2)\\n",
                                state.optDouble("chsh_simulated"))+
                            String.format(Locale.US,"MIL: %d componentes · %d bytes lógicos\n",
                                state.optInt("components"),state.optInt("mil_logical_bytes"))+
                            String.format(Locale.US,"Ether: %d qubits activos · %d entradas memoria\n",
                                state.optInt("ether_active_qubits"),
                                state.optInt("ether_memory_overrides"))+
                            String.format(Locale.US,"Fase: %.3f rad · onda calculada: %d Hz\n",
                                state.optDouble("phase"),hz)+
                            String.format(Locale.US,"Cálculo: %.2f ms · evento %d\n",
                                state.optDouble("calculation_ms"),state.optInt("event"))+
                            source+"\n";
                    if(emitPhysical) {
                        String emission=sound.emitTone(hz);
                        output+="SALIDA REAL: "+emission;
                        showFeedback(emission+" · elegida por MIL, no por un tono fijo.");
                    } else output+="SIN EMISIÓN EXTERNA · Pulsa ENLAZAR ESTADO → SONIDO para actuar.";
                    if(quantumReport!=null)quantumReport.setText(output);
                });
            } catch(Exception error) {
                runOnUiThread(()->{
                    quantumBusy=false;
                    if(ticket==quantumGeneration && activityVisible) {
                        String message="No se obtuvo un estado MIL válido: "+error.getMessage();
                        if(quantumReport!=null)quantumReport.setText(message);
                        showFeedback(message);
                    }
                });
            }
        });
    }

    private void resetQuantum() {
        if(quantumBusy) {showFeedback("El cálculo actual debe finalizar primero.");return;}
        if(!QuantumBridge.available()){showFeedback(QuantumBridge.problem());return;}
        final int ticket=++quantumGeneration;
        quantumBusy=true;
        quantumWorker.execute(()->{
            try {
                JSONObject result=new JSONObject(QuantumBridge.nativeReset());
                if(!result.optBoolean("ok"))throw new IllegalStateException(result.optString("error"));
                runOnUiThread(()->{
                    quantumBusy=false;
                    if(!activityVisible||ticket!=quantumGeneration)return;
                    if(quantumReport!=null)quantumReport.setText("Estado reiniciado: Ether y MIL preparados de cero.\nNo se ha realizado ningún cambio físico.");
                    showFeedback("MIL y Ether reiniciados");
                });
            }catch(Exception e){
                runOnUiThread(()->{
                    quantumBusy=false;
                    if(activityVisible&&ticket==quantumGeneration)showFeedback("Error al reiniciar MIL/Ether: "+e.getMessage());
                });
            }
        });
    }

    private void showFeedback(String message){
        feedback=message;
        if(measurement!=null)measurement.setText(message);
    }

    @Override public void onRequestPermissionsResult(int request,String[] names,int[] results) {
        super.onRequestPermissionsResult(request,names,results);
        if (request==PhotonEmitter.PERMISSION_REQUEST) {
            boolean granted=results.length>0 && results[0]==PackageManager.PERMISSION_GRANTED;
            showFeedback(granted?"Permiso concedido. Pulsa EMITIR LUZ REAL.":"Permiso denegado: LED desactivado.");
        }
    }

    private void drawInterface() {
        ScrollView scroller=new ScrollView(this);
        scroller.setFillViewport(true);
        LinearLayout root=new LinearLayout(this);
        root.setBackgroundColor(BG);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18),dp(18),dp(18),dp(24));
        root.setOnApplyWindowInsetsListener((view,insets)->{
            if(android.os.Build.VERSION.SDK_INT>=30) {
                android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars());
                view.setPadding(dp(18)+bars.left,dp(18)+bars.top,dp(18)+bars.right,dp(24)+bars.bottom);
            }
            return insets;
        });

        TextView top=label("MUNDO X  /  WORLDLINK",12,GREEN,true);
        top.setLetterSpacing(.18f);root.addView(top);
        TextView title=label("INTERACCIÓN\nFÍSICA",32,WHITE,true);
        title.setPadding(0,dp(8),0,dp(4));root.addView(title);
        root.addView(label("Reloj  →  Orden  →  Campo físico  →  Materia",12,MUTED,false));

        // PRIMARY TASK: actuate, not watch a passive sensor dashboard.
        LinearLayout quantumCard=card(root);
        quantumCard.addView(label("00  /  MIL CORE 1000 + ETHER OPTIMIZADO",12,GREEN,true));
        quantumCard.addView(label("PUENTE CUÁNTICO → FÍSICO",20,WHITE,true));
        quantumCard.addView(label("El Watch4 transmite datos clásicos. MIL prepara un estado Bell "
                +"simulado entre q0 y q999; su correlación XX determina una frecuencia que "
                +"el altavoz emite físicamente. Ether conserva eventos usando memoria COW de 256 páginas.",
                12,MUTED,false));
        actionButton(quantumCard,"PREPARAR BELL q0 ↔ q999 (SIMULADO)",
                ()->runQuantum(false,-1f),false);
        actionButton(quantumCard,"MIL + ETHER → EMITIR ONDA REAL",
                ()->runQuantum(true,-1f),true);
        actionButton(quantumCard,"REINICIAR MIL / ETHER",this::resetQuantum,false);
        quantumReport=label(QuantumBridge.available()
                ? "Núcleo original conectado · pulsa PREPARAR BELL"
                : "Error al cargar el motor MIL/Ether: "+QuantumBridge.problem(),
                12,WHITE,false);
        quantumReport.setPadding(0,dp(10),0,dp(4));
        quantumCard.addView(quantumReport);
        quantumCard.addView(label("La correlación Bell aquí es virtual. La emisión de sonido "
                +"es física. Para entrelazar fotones reales se necesita una fuente cuántica "
                +"y detectores, que el reloj no incorpora.",11,MUTED,false));

        LinearLayout interactionCard=card(root);
        interactionCard.addView(label("01  /  ACTUAR SOBRE EL ENTORNO",12,GREEN,true));
        interactionCard.addView(label("CONTROL DE ONDAS",22,WHITE,true));
        interactionCard.addView(label("El altavoz del Samsung produce ondas mecánicas reales en el aire. "
                +"Podemos modificar frecuencia y explorar resonancias en objetos ligeros. "
                +"Mantén el volumen moderado; no acerques el altavoz a tus oídos.",
                12,MUTED,false));
        actionButton(interactionCard,"GENERAR ONDA 220 Hz",()->playTone(220),true);
        actionButton(interactionCard,"GENERAR ONDA 440 Hz",()->playTone(440),false);
        actionButton(interactionCard,"BARRIDO DE FRECUENCIAS · 180–1200 Hz",this::playSweep,false);
        actionButton(interactionCard,"DETENER TODAS LAS ONDAS SONORAS",this::stopSound,false);
        soundStatus=label("Emisor en espera · altavoz desactivado",12,WHITE,false);
        soundStatus.setPadding(0,dp(8),0,0);
        interactionCard.addView(soundStatus);
        interactionCard.addView(label("Prueba física: acerca el teléfono a una membrana ligera tensada "
                +"sobre un recipiente y observa si aparecen vibraciones al cambiar la frecuencia. "
                +"El efecto puede ser débil con un altavoz móvil.",11,MUTED,false));
        armButton=actionButton(interactionCard,"CONTROL REMOTO: DESACTIVADO",()->{
            armed=!armed;
            updateArm();
            showFeedback(armed
                ? "El reloj puede controlar la luz y el sonido mientras esta pantalla esté abierta."
                : "Se bloquearon nuevas órdenes del reloj.");
        },false);
        lightButton=actionButton(interactionCard,"EMITIR LUZ REAL · 0,95 s",this::startLightExperiment,false);

        LinearLayout statusCard=card(root);
        linkStatus=label("ESPERANDO EL RELOJ",13,MUTED,true);
        statusCard.addView(linkStatus);
        watchName=label("Abre WorldLink en Galaxy Watch4 Classic y pulsa INICIAR",12,WHITE,false);
        watchName.setPadding(0,dp(6),0,dp(6));statusCard.addView(watchName);
        actionButton(statusCard,"BUSCAR RELOJ",()->discoverWatch(),false);
        lastSignal=label("Sin señales registradas",11,MUTED,false);
        statusCard.addView(lastSignal);

        LinearLayout metricsCard=card(root);
        metricsCard.addView(label("03  /  OBSERVACIÓN OPCIONAL",12,GREEN,true));
        LinearLayout r1=row(metricsCard);
        lightValue=metric(r1,"LUZ","— lux");
        fieldValue=metric(r1,"CAMPO","— µT");
        LinearLayout r2=row(metricsCard);
        motionValue=metric(r2,"MOVIMIENTO","— rad/s");
        pulseValue=metric(r2,"PULSO","— lpm");
        caption=label("Los valores llegan del reloj, no de una simulación.",11,MUTED,false);
        caption.setPadding(0,dp(6),0,0);
        metricsCard.addView(caption);
        chart=new TelemetryChart(this);
        LinearLayout.LayoutParams cp=new LinearLayout.LayoutParams(-1,dp(158));
        cp.topMargin=dp(9);
        metricsCard.addView(chart,cp);
        signalButton=actionButton(metricsCard,"Ver gráfico: magnetismo",()->setGraph(!lightGraph),false);

        LinearLayout experimentCard=card(root);
        experimentCard.addView(label("04  /  MEDIR EL EFECTO",12,GREEN,true));
        experimentCard.addView(label("LUZ Y MAGNETISMO",21,WHITE,true));
        TextView desc=label("Captura el estado inicial. Acerca la cara del reloj al LED, "
                +"o acerca un imán pequeño al reloj para observar el campo. "
                +"No coloques imanes cerca de dispositivos médicos implantados.",12,MUTED,false);
        desc.setPadding(0,dp(4),0,dp(8));experimentCard.addView(desc);
        actionButton(experimentCard,"CAPTURAR REFERENCIA",this::captureBaseline,false);
         experiment=label(experimentFeedback,13,WHITE,false);
        experiment.setPadding(0,dp(10),0,dp(8));experimentCard.addView(experiment);

        LinearLayout actuatorCard=card(root);
        actuatorCard.addView(label("05  /  RELOJ",12,GREEN,true));
        actuatorCard.addView(label("RETORNO AL RELOJ",18,WHITE,true));
         actionButton(actuatorCard,"VIBRAR RELOJ",this::vibrateWatch,false);
        actuatorCard.addView(label("El reloj también controla las ondas de presión y la luz. "
                +"Activa CONTROL REMOTO en la sección superior. "
                +"Cada emisión tiene un límite temporal.",11,MUTED,false));
        updateArm();

        LinearLayout feedbackCard=card(root);
        feedbackCard.addView(label("ESTADO DEL EXPERIMENTO",12,GREEN,true));
        measurement=label(feedback,13,WHITE,false);
        feedbackCard.addView(measurement);

        TextView disclaimer=label("OBJETIVO REAL: producir y medir una interacción física reproducible. "
                +"WorldLink no crea entrelazamiento cuántico físico ni controla el espacio-tiempo. "
                +"Las mediciones de pulso no son diagnósticas.",11,MUTED,false);
        disclaimer.setPadding(0,dp(8),0,dp(14));root.addView(disclaimer);

        scroller.addView(root);
        setContentView(scroller);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
    }

    private LinearLayout card(LinearLayout root){
        LinearLayout box=new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(14),dp(14),dp(14),dp(14));
        GradientDrawable bg=new GradientDrawable();
        bg.setColor(SURFACE);
        bg.setCornerRadius(dp(18));
        bg.setStroke(dp(1),0xff34463a);
        box.setBackground(bg);
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);
        p.topMargin=dp(15);
        root.addView(box,p);
        return box;
    }
    private LinearLayout row(LinearLayout root) {
        LinearLayout row=new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0,dp(8),0,dp(4));
        root.addView(row);
        return row;
    }
    private TextView metric(LinearLayout row,String title,String initial){
        LinearLayout box=new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(10),dp(11),dp(8),dp(10));
        box.setBackground(buttonBackground(0xff24382e));
        LinearLayout.LayoutParams cell=new LinearLayout.LayoutParams(0,-2,1);
        cell.rightMargin=dp(6);
        row.addView(box,cell);
        TextView t=label(title,10,MUTED,true);
        box.addView(t);
        TextView value=label(initial,17,WHITE,true);
        value.setPadding(0,dp(4),0,0);
        box.addView(value);
        return value;
    }
    private Button actionButton(LinearLayout root,String title,Runnable callback,boolean accent){
        Button b=new Button(this);
        b.setText(title);
        b.setAllCaps(false);
        b.setTextSize(12);
        b.setTextColor(accent?0xff142015:WHITE);
        b.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        b.setBackground(buttonBackground(accent?GREEN:0xff2c4536));
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,dp(48));
        p.topMargin=dp(8);
        root.addView(b,p);
        b.setOnClickListener(v->callback.run());
        return b;
    }
    private GradientDrawable buttonBackground(int color){
        GradientDrawable bg=new GradientDrawable();
        bg.setColor(color);
        bg.setCornerRadius(dp(12));
        return bg;
    }
    private TextView label(String s,int size,int color,boolean bold){
        TextView t=new TextView(this);
        t.setText(s);t.setTextSize(size);t.setTextColor(color);
        if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        return t;
    }
    private int dp(float value){
        return Math.round(value*getResources().getDisplayMetrics().density);
    }
}