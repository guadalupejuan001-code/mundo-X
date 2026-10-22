package com.mundox.fusion3d;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

import com.cosmicx.pc.FusionPhysics;

import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Scene viewer using Mundo X's ORIGINAL native EtherRenderer::render().
 *
 * Actual 3D spheres and orbit camera are ray-traced by the repository's own
 * C++ engine. Nucleon positions / movement are diagrams, NOT measurements of
 * nuclear coordinates nor a first-principles fusion simulation.
 *
 * One bounded frame worker; the UI thread never executes the native renderer.
 */
public final class EtherRenderView extends View {
    static { System.loadLibrary("mxfusionrender"); }

    private static final int W=344, H=224;
    private static final int SKY=Color.rgb(8,14,23);
    private static final int TEXT=Color.rgb(237,246,255);
    private static final int CYAN=Color.rgb(128,239,223);
    private static final int MUTED=Color.rgb(160,188,203);

    private static native int[] renderNative(
            int width,int height,
            int firstZ,int firstA,int secondZ,int secondA,
            int productZ1,int productA1,int productZ2,int productA2,
            float progress,float yaw,float pitch,float cameraDistance,
            boolean showProducts);

    private final Handler ui=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor(task->{
        Thread t=new Thread(task,"Ether-Render-X");
        t.setPriority(Thread.NORM_PRIORITY-1);
        return t;
    });
    private final AtomicBoolean occupied=new AtomicBoolean(false);
    private final Paint picture=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    private final Paint font=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bg=new Paint(Paint.ANTI_ALIAS_FLAG);
    private Bitmap bitmap;
    private final Rect original=new Rect(0,0,W,H);
    private final RectF display=new RectF();
    private int za=1,aa=2,zb=1,ab=3;
    private int pza=2,paa=4,pzb=0,pab=1;
    private boolean products=true,closed=false;
    private String interactionCaption="Física de interacción: seleccionar modelo";
    private float progress=0f;
    private float yaw=.25f,pitch=.17f,distance=5.8f;
    private long revision=0, completedRevision=-1, renderMs=0;
    private int frames=0;
    private float lastX,lastY,lastPinch=0f;
    private boolean pinching=false;

    public EtherRenderView(Context context){
        super(context);
        setLayerType(View.LAYER_TYPE_HARDWARE,null);
        font.setTypeface(android.graphics.Typeface.create(android.graphics.Typeface.MONOSPACE,
                android.graphics.Typeface.BOLD));
        font.setTextSize(sp(11));
        setContentDescription("Vista tridimensional nativa Ether Render X. Arrastra para orbitar; usa dos dedos para acercar y alejar.");
        requestFrame();
    }

    public void setScene(FusionPhysics.Analysis analysis,int selectedChannel) {
        if(analysis==null)return;
        za=analysis.first.z;aa=analysis.first.a;zb=analysis.second.z;ab=analysis.second.a;
        products=false;
        pza=0;paa=0;pzb=0;pab=0;
        // Only catalogue-validated channels can show products.
        if(analysis.hasDocumentedChannel() && selectedChannel>=0
                && selectedChannel<analysis.documentedChannels.size()){
            if(isPair(za,aa,zb,ab,1,2,1,3)) {pza=2;paa=4;pzb=0;pab=1;}
            else if(isPair(za,aa,zb,ab,1,2,1,2)){
                if(selectedChannel==0){pza=2;paa=3;pzb=0;pab=1;}
                else {pza=1;paa=3;pzb=1;pab=1;}
            } else if(isPair(za,aa,zb,ab,1,2,2,3)){pza=2;paa=4;pzb=1;pab=1;}
            else if(isPair(za,aa,zb,ab,1,2,3,6)){pza=2;paa=4;pzb=2;pab=4;}
            products=paa>0&&pab>0&&pza+pzb==za+zb&&paa+pab==aa+ab;
        }
        requestFrame();
    }

    private static boolean isPair(int za,int aa,int zb,int ab,
                                  int xz,int xa,int yz,int ya) {
        return za==xz&&aa==xa&&zb==yz&&ab==ya ||
                za==yz&&aa==ya&&zb==xz&&ab==xa;
    }

    public void setProgress(float value){
        progress=Math.max(0f,Math.min(1f,value));
        requestFrame();
    }

    /**
     * The overlay reflects a computed WKB number; it does not change
     * raytraced nucleon trajectories into physical quantum dynamics.
     */
    public void setNuclearModel(NuclearInteractionModel.Result value){
        if(value==null || !value.hasNuclearModel)
            interactionCaption="Interacción: sin canal nuclear calibrado";
        else if(value.belowBarrier)
            interactionCaption=String.format(Locale.US,
                "WKB: 10^(%.2f) [modelo]",value.log10Wkb);
        else
            interactionCaption="WKB sub-barrera: no aplicable";
        invalidate();
    }

    public float progress(){return progress;}
    public void resetCamera(){
        yaw=.25f;pitch=.17f;distance=5.8f;
        requestFrame();
    }

    private void requestFrame(){
        if(closed)return;
        revision++;
        if(!occupied.compareAndSet(false,true))return;
        final long thisRevision=revision;
        final int fza=za,faa=aa,fzb=zb,fab=ab;
        final int fpza=pza,fpaa=paa,fpzb=pzb,fpab=pab;
        final float phase=progress,theta=yaw,phi=pitch,camera=distance;
        final boolean channel=products;
        try{
            worker.execute(()->{
                int[] image=null;
                String error=null;
                long start=SystemClock.elapsedRealtime();
                try{
                    image=renderNative(W,H,fza,faa,fzb,fab,
                            fpza,fpaa,fpzb,fpab,phase,theta,phi,camera,channel);
                    if(image==null||image.length!=W*H)
                        throw new IllegalStateException("Ether Render X returned an invalid frame");
                }catch(Throwable fail){
                    error=fail.getMessage();
                }
                final int[] finalImage=image;
                final String failure=error;
                final long ms=SystemClock.elapsedRealtime()-start;
                ui.post(()->{
                    occupied.set(false);
                    if(closed)return;
                    if(failure==null&&finalImage!=null){
                        if(bitmap==null||bitmap.isRecycled()){
                            bitmap=Bitmap.createBitmap(W,H,Bitmap.Config.ARGB_8888);
                        }
                        bitmap.setPixels(finalImage,0,W,0,0,W,H);
                        completedRevision=thisRevision;
                        renderMs=ms;
                        frames++;
                    }
                    invalidate();
                    if(revision!=thisRevision)requestFrame();
                });
            });
        }catch(RejectedExecutionException fail){
            occupied.set(false);
        }
    }

    @Override protected void onDraw(Canvas canvas){
        super.onDraw(canvas);
        canvas.drawColor(SKY);
        final float w=getWidth(),h=getHeight();
        if(bitmap!=null&&!bitmap.isRecycled()){
            float scale=Math.min(w/W,h/H);
            float rw=W*scale,rh=H*scale;
            display.set((w-rw)*.5f,(h-rh)*.5f,(w+rw)*.5f,(h+rh)*.5f);
            canvas.drawBitmap(bitmap,original,display,picture);
        }
        bg.setColor(Color.argb(188,6,14,24));
        canvas.drawRoundRect(new RectF(dp(8),dp(8),w-dp(8),dp(48)),dp(8),dp(8),bg);
        font.setColor(CYAN);
        canvas.drawText("ETHER RENDER X  /  C++",dp(17),dp(25),font);
        font.setColor(MUTED);
        canvas.drawText("344×224 · raytracing · " + renderMs + " ms",dp(17),dp(41),font);
        bg.setColor(Color.argb(183,6,14,24));
        canvas.drawRoundRect(new RectF(dp(8),h-dp(80),w-dp(8),h-dp(56)),
                dp(7),dp(7),bg);
        font.setColor(CYAN);
        canvas.drawText(interactionCaption,dp(15),h-dp(63),font);
        bg.setColor(Color.argb(199,6,14,24));
        canvas.drawRoundRect(new RectF(dp(8),h-dp(53),w-dp(8),h-dp(8)),dp(8),dp(8),bg);
        font.setColor(TEXT);
        final String phaseName=progress<.55f?"01 · APROXIMACIÓN":
                progress<.68f?"02 · REGIÓN DE CONTACTO":
                        products?"03 · PRODUCTOS (DIAGRAMA)":"03 · CANAL NO DETERMINADO";
        canvas.drawText(phaseName,dp(17),h-dp(32),font);
        font.setColor(MUTED);
        canvas.drawText("Coral: protones   Azul: neutrones  ·  órbita táctil",
                dp(17),h-dp(16),font);
    }

    @Override public boolean onTouchEvent(MotionEvent event){
        if(closed)return false;
        switch(event.getActionMasked()){
            case MotionEvent.ACTION_DOWN:
                lastX=event.getX();lastY=event.getY();pinching=false;
                return true;
            case MotionEvent.ACTION_POINTER_DOWN:
                if(event.getPointerCount()>=2){
                    pinching=true;
                    lastPinch=distance(event);
                }
                return true;
            case MotionEvent.ACTION_POINTER_UP:
                lastPinch=0f;
                pinching=false;
                if(event.getPointerCount()>0){
                    lastX=event.getX(0);
                    lastY=event.getY(0);
                }
                return true;
            case MotionEvent.ACTION_MOVE:
                if(event.getPointerCount()>=2){
                    float next=distance(event);
                    if(lastPinch>4f && next>4f) {
                        distance=Math.max(4.5f,Math.min(16f,distance*lastPinch/next));
                        requestFrame();
                    }
                    lastPinch=next;
                }else if(!pinching){
                    float dx=event.getX()-lastX;
                    float dy=event.getY()-lastY;
                    yaw+=dx*.008f;
                    pitch=Math.max(-1.27f,Math.min(1.27f,pitch+dy*.007f));
                    requestFrame();
                    lastX=event.getX();lastY=event.getY();
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                pinching=false;return true;
            default:return true;
        }
    }
    private static float distance(MotionEvent event){
        float dx=event.getX(0)-event.getX(1);
        float dy=event.getY(0)-event.getY(1);
        return (float)Math.hypot(dx,dy);
    }
    private int dp(float x){return (int)(x*getResources().getDisplayMetrics().density+.5f);}
    private float sp(float x){return x*getResources().getDisplayMetrics().scaledDensity;}

    public boolean hasNativeFrame(){ return frames>0 && bitmap!=null && !bitmap.isRecycled(); }

    public String provenance(){
        return String.format(Locale.US,
                "Motor: Mundo X EtherRenderer nativo; escena %dx%d, último cuadro %d ms, %d cuadros; "+
                        "máximo 60 esferas. Geometría, colores y movimiento ilustrativos; no solver nuclear.",
                W,H,renderMs,frames);
    }
    public void shutdown(){
        closed=true;
        worker.shutdown();
        if(bitmap!=null){bitmap.recycle();bitmap=null;}
    }
    @Override protected void onDetachedFromWindow(){
        // The activity owns the viewer; no renders should continue offscreen.
        shutdown();
        super.onDetachedFromWindow();
    }
}
