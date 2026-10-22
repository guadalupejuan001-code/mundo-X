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

import org.json.JSONObject;

import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * ACTUAL 100-logical-qubit GHZ state in MIL Reduced and 100 visible spheres
 * through 2x50 independent EtherRender passes.
 *
 * All native scene actions (create, render, status, delete) are serialized
 * onto one worker. 100 spheres correspond to logical q0..q99; appearance
 * reflects the many-body Pauli coherence <X0 X1 ... X99>=cos(theta).
 * Not nuclear positions, not physical QPU, not quantum ray tracing.
 */
public final class Mil100EtherView extends View {
    static { System.loadLibrary("mxfusionrender"); }
    private static final int WIDTH=328,HEIGHT=208;
    private static final int BACK=Color.rgb(8,16,27);
    private static final int FORE=Color.rgb(231,247,252);
    private static final int AQUA=Color.rgb(111,239,218);
    private static final int SECONDARY=Color.rgb(173,197,209);
    private static final float TWO_PI=(float)(2.*Math.PI);

    private static native long createNative(int width,int height);
    private static native int[] renderNative(long handle,float yaw,
                     float pitch,float distance,float phase,boolean checkExact);
    private static native String statusNative(long handle);
    private static native void destroyNative(long handle);

    public interface OnFrame {
        /** Invoked on UI thread after the first or any subsequent complete frame. */
        void complete(String metrics,String error);
    }

    private final ExecutorService renderWorker=Executors.newSingleThreadExecutor(r->{
        Thread worker=new Thread(r,"MIL100-Ether-Multipass");
        worker.setPriority(Thread.NORM_PRIORITY-1);
        return worker;
    });
    private final Handler main=new Handler(Looper.getMainLooper());
    private final AtomicBoolean active=new AtomicBoolean(false);
    private final Paint imagePaint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    private final Paint textPaint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint overlayPaint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect src=new Rect(0,0,WIDTH,HEIGHT);
    private final RectF dst=new RectF();
    private Bitmap bitmap;
    private OnFrame listener;
    private long handle=0;
    private volatile boolean closed=false;
    private float phase=(float)(Math.PI/4.);
    private float yaw=.33f,pitch=.19f,distance=8.7f;
    private float touchX,touchY,pinchDistance;
    private boolean pinching=false;
    private int frames=0;
    private long renderRevision=0;
    private String lastStatus="";
    private String lastError=null;
    private double globalCoherence=Double.NaN;
    private double rasterMs=0,quantumMs=0;

    public Mil100EtherView(Context context){
        super(context);
        textPaint.setTypeface(android.graphics.Typeface.create(
                android.graphics.Typeface.MONOSPACE,android.graphics.Typeface.BOLD));
        setContentDescription("mil-100q-ether-3d-viewport");
        requestNativeFrame();
    }

    public void setFrameListener(OnFrame listener){
        this.listener=listener;
        if(listener!=null && (frames>0 || lastError!=null))
            listener.complete(lastStatus,lastError);
    }
    public void setQuantumPhase(float value){
        if(!Float.isFinite(value))return;
        phase=Math.max(0f,Math.min(TWO_PI,value));
        requestNativeFrame();
    }
    public float quantumPhase(){return phase;}
    public boolean hasNativeFrame(){return frames>0 && bitmap!=null&&!bitmap.isRecycled();}
    public int frameCount(){return frames;}
    public String lastStatus(){return lastStatus;}
    public String lastError(){return lastError;}
    public void resetCamera(){
        yaw=.33f;pitch=.19f;distance=8.7f;
        requestNativeFrame();
    }

    private void requestNativeFrame(){
        if(closed)return;
        renderRevision++;
        if(!active.compareAndSet(false,true))return;
        final long revision=renderRevision;
        final float fYaw=yaw,fPitch=pitch,fDistance=distance,fPhase=phase;
        try{
            renderWorker.execute(()->{
                int[] pixels=null;
                String status="";
                String error=null;
                try {
                    if(handle==0)handle=createNative(WIDTH,HEIGHT);
                    if(handle==0)throw new IllegalStateException(
                            "MIL 100q scene initialization returned no native handle");
                    pixels=renderNative(handle,fYaw,fPitch,fDistance,fPhase,false);
                    if(pixels==null||pixels.length!=WIDTH*HEIGHT)
                        throw new IllegalStateException(
                                "100-qubit Ether frame invalid or missing");
                    status=statusNative(handle);
                    if(status==null||status.isEmpty())
                        throw new IllegalStateException("MIL 100q status missing");
                }catch(Exception|LinkageError failure){
                    error=failure.getMessage();
                }
                final int[] currentPixels=pixels;
                final String currentStatus=status,currentError=error;
                main.post(()->{
                    active.set(false);
                    if(closed)return;
                    if(currentError==null && currentPixels!=null){
                        if(bitmap==null||bitmap.isRecycled()){
                            bitmap=Bitmap.createBitmap(
                                    WIDTH,HEIGHT,Bitmap.Config.ARGB_8888);
                        }
                        bitmap.setPixels(currentPixels,0,WIDTH,0,0,WIDTH,HEIGHT);
                        lastStatus=currentStatus;
                        lastError=null;
                        frames++;
                        try {
                            JSONObject parsed=new JSONObject(currentStatus);
                            globalCoherence=parsed.optDouble("global_x",Double.NaN);
                            rasterMs=parsed.optDouble("raster_ms",0);
                            quantumMs=parsed.optDouble("quantum_refresh_ms",0);
                        }catch(Exception parseError){
                            globalCoherence=Double.NaN;
                        }
                    }else{
                        lastError=currentError==null?
                                "No llegó una imagen del motor 100q":currentError;
                    }
                    invalidate();
                    if(listener!=null)listener.complete(lastStatus,lastError);
                    if(revision!=renderRevision&&lastError==null)requestNativeFrame();
                });
            });
        }catch(RejectedExecutionException e){
            active.set(false);
        }
    }

    @Override protected void onDraw(Canvas canvas){
        super.onDraw(canvas);
        canvas.drawColor(BACK);
        final float w=getWidth(),h=getHeight();
        if(bitmap!=null && !bitmap.isRecycled()){
            float scale=Math.min(w/WIDTH,h/HEIGHT);
            float width=WIDTH*scale,height=HEIGHT*scale;
            dst.set((w-width)/2f,(h-height)/2f,(w+width)/2f,(h+height)/2f);
            canvas.drawBitmap(bitmap,src,dst,imagePaint);
        }
        overlayPaint.setColor(Color.argb(203,6,14,25));
        canvas.drawRoundRect(new RectF(dp(7),dp(8),w-dp(7),dp(48)),
                dp(7),dp(7),overlayPaint);
        textPaint.setColor(AQUA);
        textPaint.setTextSize(sp(12));
        canvas.drawText("MIL 100Q × ETHER RENDER X",dp(14),dp(24),textPaint);
        textPaint.setColor(SECONDARY);
        textPaint.setTextSize(sp(10));
        canvas.drawText("100 qbits GHZ · 2 × 50 esferas · C++",dp(14),dp(40),textPaint);
        overlayPaint.setColor(Color.argb(211,6,14,25));
        canvas.drawRoundRect(new RectF(dp(7),h-dp(49),w-dp(7),h-dp(7)),
                dp(7),dp(7),overlayPaint);
        textPaint.setColor(FORE);
        textPaint.setTextSize(sp(11));
        String c=Double.isFinite(globalCoherence)
                ?String.format(Locale.US,"<X^100> = %.4f",globalCoherence)
                :"Observando la coherencia global...";
        canvas.drawText(c,dp(14),h-dp(31),textPaint);
        textPaint.setColor(SECONDARY);
        textPaint.setTextSize(sp(10));
        canvas.drawText(String.format(Locale.US,
                "Ether: %.1f ms · Observables: %.1f ms",
                rasterMs,quantumMs),dp(14),h-dp(16),textPaint);
    }

    @Override public boolean onTouchEvent(MotionEvent event){
        if(closed)return false;
        switch(event.getActionMasked()){
            case MotionEvent.ACTION_DOWN:
                touchX=event.getX();touchY=event.getY();
                pinching=false;
                return true;
            case MotionEvent.ACTION_POINTER_DOWN:
                if(event.getPointerCount()>=2){
                    pinching=true;
                    pinchDistance=fingers(event);
                }
                return true;
            case MotionEvent.ACTION_POINTER_UP:
                pinching=false;
                pinchDistance=0f;
                touchX=event.getX(0);touchY=event.getY(0);
                return true;
            case MotionEvent.ACTION_MOVE:
                if(event.getPointerCount()>=2){
                    final float separation=fingers(event);
                    if(pinchDistance>5f && separation>5f){
                        distance=Math.max(5.2f,Math.min(16f,
                                distance*pinchDistance/separation));
                        requestNativeFrame();
                    }
                    pinchDistance=separation;
                }else if(!pinching){
                    float dx=event.getX()-touchX,dy=event.getY()-touchY;
                    yaw+=dx*.008f;
                    pitch=Math.max(-1.27f,Math.min(1.27f,pitch+dy*.006f));
                    touchX=event.getX();touchY=event.getY();
                    requestNativeFrame();
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
            case MotionEvent.ACTION_UP:
                pinching=false;return true;
            default:return true;
        }
    }
    private static float fingers(MotionEvent e){
        return (float)Math.hypot(e.getX(0)-e.getX(1),e.getY(0)-e.getY(1));
    }
    private int dp(float value){
        return (int)(value*getResources().getDisplayMetrics().density+.5f);
    }
    private float sp(float value){
        return value*getResources().getDisplayMetrics().scaledDensity;
    }

    public void shutdown(){
        if(closed)return;
        closed=true;
        // The native session must be destroyed AFTER any pending render,
        // in the same serialized native worker (no use-after-free).
        try {
            renderWorker.execute(()->{
                if(handle!=0){
                    destroyNative(handle);
                    handle=0;
                }
            });
        }catch(RejectedExecutionException ignored){}
        renderWorker.shutdown();
        if(bitmap!=null){
            bitmap.recycle();
            bitmap=null;
        }
    }
    @Override protected void onDetachedFromWindow(){
        shutdown();
        super.onDetachedFromWindow();
    }
}
