package com.mundox.fusion3d;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

/** Real device CPU benchmark: equal scenes old versus Ether Surface X,
 * and large-scene 8K observation WITHOUT claiming 8K display FPS. */
public final class EtherBenchmarkActivity extends Activity {
    static { System.loadLibrary("mxfusionrender"); }
    // Package-private for the emulator instrumentation oracle.
    static native String benchmarkNative(int preset);

    private static final int BG=Color.rgb(8,14,23);
    private static final int TEXT=Color.rgb(232,243,251);
    private static final int MUTED=Color.rgb(149,184,199);
    private static final int CYAN=Color.rgb(111,240,214);

    private final Handler ui=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor(task->{
        Thread thread=new Thread(task,"Ether-X-Device-Benchmark");
        thread.setPriority(Thread.NORM_PRIORITY-1);
        return thread;
    });
    private volatile boolean closed=false;
    private Spinner preset;
    private TextView output;
    private Button measure,share;
    private String jsonReport;

    private int dp(float d){return (int)(d*getResources().getDisplayMetrics().density+.5f);}
    private TextView label(String text,int size,int color){
        TextView view=new TextView(this);
        view.setText(text);
        view.setTextColor(color);
        view.setTextSize(size);
        view.setPadding(0,dp(8),0,dp(8));
        return view;
    }
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        ScrollView scroll=new ScrollView(this);
        scroll.setBackgroundColor(BG);
        LinearLayout col=new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(20),dp(28),dp(20),dp(40));
        scroll.addView(col);setContentView(scroll);
        TextView heading=label("ETHER SURFACE X / LAB",23,CYAN);
        col.addView(heading);
        col.addView(label("Prueba real de CPU en este dispositivo. El visor de Fusion usa exclusivamente EtherSurfaceBuffer; el motor antiguo se ejecuta SOLO como referencia A/B aquí.",13,TEXT));
        col.addView(label("Seleccione una escena. Las tres primeras comparan exactamente la misma resolución, cámara y objetos; 8K solo mide preparación y superficies, no dibuja una pantalla 8K.",12,MUTED));
        preset=new Spinner(this);
        ArrayAdapter<String> options=new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item,
                new String[]{"320 × 180 · 32 cuerpos (A/B)",
                             "344 × 224 · 60 cuerpos (A/B del visor)",
                             "640 × 360 · 64 cuerpos (A/B)",
                             "8K · 4.096 cuerpos (mapa simbólico)"});
        preset.setAdapter(options);
        col.addView(preset,new LinearLayout.LayoutParams(-1,dp(54)));
        measure=new Button(this);
        measure.setText("MEDIR EN ESTE TELÉFONO");
        measure.setAllCaps(false);
        measure.setContentDescription("ether-benchmark-measure");
        col.addView(measure,new LinearLayout.LayoutParams(-1,dp(57)));
        share=new Button(this);
        share.setText("Compartir resultado JSON");
        share.setAllCaps(false);
        share.setEnabled(false);
        col.addView(share,new LinearLayout.LayoutParams(-1,dp(49)));
        output=label("Sin pruebas ejecutadas. No hay resultados medidos para este dispositivo.",13,TEXT);
        output.setTypeface(android.graphics.Typeface.MONOSPACE);
        output.setTextIsSelectable(true);
        output.setContentDescription("ether-benchmark-result");
        col.addView(output);
        TextView notice=label("Interpretación: velocidad > 1,00 significa que Ether Surface X fue más rápido en la prueba A/B. La inversa significa regresión. Los FPS CPU equivalentes son 1000/ms del cálculo, no fotogramas realmente presentados en la pantalla. La comparación no incluye tiempo de GPU ni refresco del panel.",12,MUTED);
        col.addView(notice);
        measure.setOnClickListener(v->runMeasurement());
        share.setOnClickListener(v->{
            if(jsonReport==null)return;
            Intent intent=new Intent(Intent.ACTION_SEND);
            intent.setType("application/json");
            intent.putExtra(Intent.EXTRA_TEXT,jsonReport);
            startActivity(Intent.createChooser(intent,"Compartir métricas Ether X"));
        });
    }

    private void runMeasurement(){
        final int selected=preset.getSelectedItemPosition();
        measure.setEnabled(false);
        share.setEnabled(false);
        output.setText("Calculando sobre CPU real... La interfaz sigue activa.");
        try{
            worker.execute(()->{
                String result=null,failure=null;
                try{
                    long started=SystemClock.elapsedRealtime();
                    String raw=benchmarkNative(selected);
                    if(raw==null)throw new IllegalStateException("Benchmark native returned null");
                    JSONObject obj=new JSONObject(raw);
                    obj.put("device_model",Build.MODEL);
                    obj.put("device_manufacturer",Build.MANUFACTURER);
                    obj.put("android_api",Build.VERSION.SDK_INT);
                    obj.put("abi",Build.SUPPORTED_ABIS.length>0?Build.SUPPORTED_ABIS[0]:"unknown");
                    obj.put("native_heap_bytes",Debug.getNativeHeapAllocatedSize());
                    obj.put("elapsed_total_ms",SystemClock.elapsedRealtime()-started);
                    obj.put("method","median of 5 native image passes, identical scene; 8K uses symbolic and packed captures only");
                    obj.put("presentation_measured",false);
                    result=obj.toString(2);
                }catch(Throwable error){failure=error.toString();}
                final String answer=result,problem=failure;
                ui.post(()->{
                    if(closed)return;
                    measure.setEnabled(true);
                    if(problem!=null){output.setText("Error de benchmark: "+problem);return;}
                    jsonReport=answer;
                    share.setEnabled(true);
                    try{
                        JSONObject obj=new JSONObject(answer);
                        String summary;
                        if(selected==3){
                            summary=String.format(Locale.US,
                                    "8K · 4.096 cuerpos\nPreparación simbólica: %.2f ms\nCaptura de superficies: %.2f ms\nBuffer compacto: %.2f MiB\nPíxeles visibles: %d\nMuestras distintas: %d\n\nNO se ha generado un fotograma 8K completo.\n\n",
                                    obj.getDouble("lazy_prepare_ms"),
                                    obj.getDouble("packed_capture_ms"),
                                    obj.getDouble("packed_buffer_bytes")/1048576.,
                                    obj.getInt("visible_pixels"),
                                    obj.getInt("pixel_mismatches"));
                        }else{
                            summary=String.format(Locale.US,
                                    "ESCENA %dx%d · %d cuerpos\nAnterior: %.2f ms\nEther Surface X: %.2f ms\nVelocidad relativa: %.2fx\nCPU equivalente anterior: %.1f FPS\nCPU equivalente Ether X: %.1f FPS\nDiferencias de píxel: %d\nBuffer compacto: %.2f MiB\n\nEstas cifras NO son FPS de pantalla.\n\n",
                                    obj.getInt("width"),obj.getInt("height"),obj.getInt("bodies"),
                                    obj.getDouble("old_native_ms"),obj.getDouble("new_native_ms"),
                                    obj.getDouble("speedup"),
                                    obj.getDouble("old_cpu_fps_equivalent"),
                                    obj.getDouble("new_cpu_fps_equivalent"),
                                    obj.getInt("pixel_mismatches"),
                                    obj.getDouble("packed_buffer_bytes")/1048576.);
                        }
                        output.setText(summary+"RESULTADO REPRODUCIBLE (JSON)\n\n"+answer);
                    }catch(Exception ex){output.setText(answer);}
                });
            });
        }catch(RejectedExecutionException error){
            measure.setEnabled(true);
            output.setText("El medidor no está disponible: "+error);
        }
    }
    @Override protected void onDestroy(){
        closed=true;
        worker.shutdownNow();
        super.onDestroy();
    }
}
