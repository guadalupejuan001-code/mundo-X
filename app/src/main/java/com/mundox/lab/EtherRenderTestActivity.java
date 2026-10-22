package com.mundox.lab;

import android.app.Activity;
import android.app.ActivityManager;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/** Standalone physical-device harness for the native Ether Render X v0.2 CPU path. */
public final class EtherRenderTestActivity extends Activity {
    private static final int BG=0xff080b10, PANEL=0xff111722, INK=0xffe8eef7;
    private static final int MUTED=0xff9aa8ba, ACCENT=0xff8ad8ff, GOOD=0xff8ee8ae;
    private static final int[] WIDTHS={320,640,960,1280};
    private static final int[] HEIGHTS={180,360,540,720};
    private static final String[] SCENES={"BÁSICA · 3","SOLAR-LIKE · 23","ESTRÉS · 64","OCLUSIÓN · 64"};

    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final AtomicBoolean busy=new AtomicBoolean();
    private TextView metrics,status;
    private ImageView image;
    private Button sceneButton,resolutionButton,tileButton;
    private int scene=0,resolution=1,tile=32;
    private volatile boolean destroyed;

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);

        LinearLayout root=new LinearLayout(this);
        root.setOrientation(LinearLayout.HORIZONTAL);
        root.setBackgroundColor(BG);
        root.setPadding(dp(12),dp(10),dp(12),dp(10));

        LinearLayout left=new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setPadding(0,0,dp(12),0);
        ScrollView scroll=new ScrollView(this);
        scroll.addView(left,new ScrollView.LayoutParams(-1,-2));
        root.addView(scroll,new LinearLayout.LayoutParams(0,-1,.40f));

        TextView title=text("ETHER RENDER X · DEVICE LAB",18,INK,true);
        left.addView(title);
        left.addView(text("v0.2 · ruta CPU/JNI real · sin Solar ni QuantumRenderer",11,MUTED,false));
        left.addView(text(deviceLine(),10,MUTED,false));

        sceneButton=button("ESCENA  "+SCENES[scene],()->{
            scene=(scene+1)%SCENES.length;
            sceneButton.setText("ESCENA  "+SCENES[scene]);
        });
        resolutionButton=button(resolutionLabel(),()->{
            resolution=(resolution+1)%WIDTHS.length;
            resolutionButton.setText(resolutionLabel());
        });
        tileButton=button("TILE  "+tile,()->{
            int[] values={8,16,32,64,128,256};
            int next=0;for(int i=0;i<values.length;i++)if(values[i]==tile)next=(i+1)%values.length;
            tile=values[next];tileButton.setText("TILE  "+tile);
        });
        left.addView(sceneButton);left.addView(resolutionButton);left.addView(tileButton);

        LinearLayout actions=new LinearLayout(this);actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.addView(button("RENDER",()->run(false,false,1)),weight());
        actions.addView(button("EXACTO",()->run(false,true,1)),weight());
        actions.addView(button("ENTRENAR",()->run(true,true,1)),weight());
        left.addView(actions);

        LinearLayout stress=new LinearLayout(this);stress.setOrientation(LinearLayout.HORIZONTAL);
        stress.addView(button("x10",()->run(false,false,10)),weight());
        stress.addView(button("RESET 32",()->{tile=32;tileButton.setText("TILE  32");status.setText("Tile manual restaurado.");}),weight());
        left.addView(stress);

        status=text("Listo. RENDER dibuja una imagen real. EXACTO compara contra brute-force. ENTRENAR prueba tiles y exige 0 diferencias.",11,ACCENT,false);
        status.setPadding(0,dp(8),0,dp(8));left.addView(status);
        metrics=text("",11,INK,false);
        metrics.setTypeface(android.graphics.Typeface.MONOSPACE);
        metrics.setTextIsSelectable(true);left.addView(metrics);

        LinearLayout right=new LinearLayout(this);
        right.setOrientation(LinearLayout.VERTICAL);
        right.setGravity(Gravity.CENTER);
        right.setBackgroundColor(PANEL);
        image=new ImageView(this);
        image.setScaleType(ImageView.ScaleType.FIT_CENTER);
        image.setBackgroundColor(Color.BLACK);
        right.addView(image,new LinearLayout.LayoutParams(-1,0,1f));
        TextView note=text("La imagen proviene de EtherRenderer. Esta APK mide CPU/JNI; todavía no es el backend GPU Android.",10,MUTED,false);
        note.setGravity(Gravity.CENTER);note.setPadding(dp(6),dp(5),dp(6),dp(5));right.addView(note);
        root.addView(right,new LinearLayout.LayoutParams(0,-1,.60f));

        setContentView(root);
        run(false,false,1);
    }

    private void run(boolean train,boolean exact,int repeats){
        if(!busy.compareAndSet(false,true)){status.setText("Ya hay una prueba ejecutándose.");return;}
        final int w=WIDTHS[resolution],h=HEIGHTS[resolution],s=scene,startTile=tile;
        setControls(false);
        status.setText((train?"Entrenando tiles":exact?"Comparando exactitud":"Renderizando")
                +" · "+w+"×"+h+" · "+SCENES[s]+"…");
        worker.execute(()->{
            try{
                long begin=System.nanoTime();
                JSONObject last=null;
                double sum=0,min=Double.POSITIVE_INFINITY,max=0;
                for(int i=0;i<repeats;i++){
                    JSONObject row=new JSONObject(NativeCore.etherRenderBenchmark(w,h,s,startTile,train&&i==0,exact&&i==0));
                    if(row.has("error"))throw new IllegalStateException(row.getString("error"));
                    last=row;
                    double ms=row.getDouble("render_ms");sum+=ms;min=Math.min(min,ms);max=Math.max(max,ms);
                }
                if(last==null)throw new IllegalStateException("Sin resultado");
                final JSONObject row=last;
                final int chosen=row.getInt("chosen_tile");
                int[] pixels=NativeCore.etherRenderFrame(w,h,s,chosen);
                if(pixels.length!=w*h)throw new IllegalStateException("Framebuffer incompleto: "+pixels.length);
                Bitmap bitmap=Bitmap.createBitmap(w,h,Bitmap.Config.ARGB_8888);
                bitmap.setPixels(pixels,0,w,0,0,w,h);
                double wallMs=(System.nanoTime()-begin)/1_000_000.0;
                final String report=report(row,repeats,sum/repeats,min,max,wallMs);
                runOnUiThread(()->{
                    if(destroyed){bitmap.recycle();return;}
                    tile=chosen;tileButton.setText("TILE  "+tile);
                    image.setImageBitmap(bitmap);
                    metrics.setText(report);
                    boolean clean=row.optInt("exact_mismatches",0)==0;
                    status.setText((clean?"PASS":"FAIL")+" · hash "+Long.toUnsignedString(row.optLong("frame_hash"))
                            +(train?" · tile aprendido "+chosen:""));
                });
            }catch(Throwable error){
                runOnUiThread(()->{if(!destroyed)status.setText("ERROR · "+error.getMessage());});
            }finally{
                busy.set(false);
                runOnUiThread(()->{if(!destroyed)setControls(true);});
            }
        });
    }

    private String report(JSONObject r,int repeats,double avg,double min,double max,double wall)throws Exception{
        long candidate=r.getLong("candidate_tests"), brute=r.getLong("brute_tests");
        double saved=brute==0?0:100.0*(1.0-candidate/(double)brute);
        StringBuilder out=new StringBuilder();
        out.append("engine       ").append(r.getString("engine")).append('\n');
        out.append("scene        ").append(SCENES[r.getInt("scene")]).append('\n');
        out.append("frame        ").append(r.getInt("width")).append('×').append(r.getInt("height")).append('\n');
        out.append("tile         ").append(r.getInt("tile")).append('\n');
        out.append("bodies       ").append(r.getInt("bodies")).append('\n');
        out.append("regions      ").append(r.getLong("regions")).append('\n');
        out.append("candidates   ").append(candidate).append('\n');
        out.append("brute tests  ").append(brute).append('\n');
        out.append("avoided      ").append(String.format(Locale.US,"%.2f%%",saved)).append('\n');
        out.append("depth prune  ").append(r.getLong("distance_pruned")).append('\n');
        out.append("max/region   ").append(r.getInt("max_candidates")).append('\n');
        out.append("mismatches   ").append(r.getInt("exact_mismatches")).append('\n');
        out.append("render ms    ").append(String.format(Locale.US,"%.3f",r.getDouble("render_ms"))).append('\n');
        out.append("train ms     ").append(String.format(Locale.US,"%.3f",r.getDouble("train_ms"))).append('\n');
        if(repeats>1){
            out.append("x").append(repeats).append(" avg   ").append(String.format(Locale.US,"%.3f ms",avg)).append('\n');
            out.append("range        ").append(String.format(Locale.US,"%.3f–%.3f ms",min,max)).append('\n');
        }
        out.append("wall total   ").append(String.format(Locale.US,"%.3f ms",wall)).append('\n');
        out.append("hash         ").append(Long.toUnsignedString(r.getLong("frame_hash"))).append('\n');
        return out.toString();
    }

    private String deviceLine(){
        ActivityManager manager=(ActivityManager)getSystemService(ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mi=new ActivityManager.MemoryInfo();manager.getMemoryInfo(mi);
        return Build.MANUFACTURER+" "+Build.MODEL+" · Android "+Build.VERSION.RELEASE
                +" · "+Build.SUPPORTED_ABIS[0]+" · RAM "+(mi.totalMem/(1024*1024))+" MiB";
    }

    private String resolutionLabel(){return "RES  "+WIDTHS[resolution]+"×"+HEIGHTS[resolution];}
    private LinearLayout.LayoutParams weight(){return new LinearLayout.LayoutParams(0,-2,1f);}
    private Button button(String label,Runnable action){
        Button b=new Button(this);b.setText(label);b.setTextSize(10);b.setAllCaps(false);
        b.setOnClickListener(v->action.run());return b;
    }
    private TextView text(String value,float size,int color,boolean bold){
        TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(color);
        if(bold)t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        return t;
    }
    private void setControls(boolean enabled){
        sceneButton.setEnabled(enabled);resolutionButton.setEnabled(enabled);tileButton.setEnabled(enabled);
    }
    private int dp(int x){return Math.round(x*getResources().getDisplayMetrics().density);}

    @Override protected void onDestroy(){
        destroyed=true;worker.shutdownNow();
        super.onDestroy();
    }
}
