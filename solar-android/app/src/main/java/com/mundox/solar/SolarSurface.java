package com.mundox.solar;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.os.Handler;
import android.os.Looper;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;

/** Touch input is always delivered to the GL thread, including two-finger pan. */
public final class SolarSurface extends GLSurfaceView {
    private final SolarRenderer observer;
    private final ScaleGestureDetector scale;
    private final GestureDetector gestures;
    private float previousX,previousY;
    private boolean twoFingers;
    private final Handler interactionHandler=new Handler(Looper.getMainLooper());
    private final Runnable restoreFullUhd;
    public SolarSurface(Context context, SolarRenderer renderer) {
        super(context);
        observer=renderer;
        restoreFullUhd=()->{
            if(isAttachedToWindow())mutate(()->observer.setInteractionPreview(false));
        };
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8,8,8,8,24,0);
        setPreserveEGLContextOnPause(true);
        setRenderer(renderer);
        setRenderMode(RENDERMODE_CONTINUOUSLY);
        setContentDescription("Vista tridimensional. Arrastra para mirar, pellizca para acercarte y usa dos dedos para desplazarte.");
        scale=new ScaleGestureDetector(context,new ScaleGestureDetector.SimpleOnScaleGestureListener(){
            @Override public boolean onScale(ScaleGestureDetector detector){
                final float f=detector.getScaleFactor();
                mutate(()->observer.zoom(f));return true;
            }
        });
        gestures=new GestureDetector(context,new GestureDetector.SimpleOnGestureListener(){
            @Override public boolean onDown(MotionEvent event){return true;}
            @Override public boolean onSingleTapConfirmed(MotionEvent event){
                final float x=event.getX(),y=event.getY();
                mutate(()->observer.pick(x,y));performClick();return true;
            }
            @Override public boolean onDoubleTap(MotionEvent event){
                mutate(()->{if(observer.isEntryMode())observer.exitAtmosphere();else observer.focus(Math.max(0,observer.selectedBody()));});return true;
            }
            @Override public boolean onScroll(MotionEvent first,MotionEvent last,float dx,float dy){
                if(last.getPointerCount()==1&&!scale.isInProgress()&&!twoFingers){
                    mutate(()->observer.rotate(dx,dy));
                }
                return true;
            }
        });
    }
    private void mutate(Runnable action){queueEvent(action);requestRender();}
    public void setPausedRendering(boolean paused){
        setRenderMode(paused?RENDERMODE_WHEN_DIRTY:RENDERMODE_CONTINUOUSLY);
        if(paused)requestRender();
    }
    @Override public boolean onTouchEvent(MotionEvent event){
        final int action=event.getActionMasked();
        if(action==MotionEvent.ACTION_DOWN){
            interactionHandler.removeCallbacks(restoreFullUhd);
            mutate(()->observer.setInteractionPreview(true));
        }
        scale.onTouchEvent(event);
        if(event.getPointerCount()==2){
            float x=(event.getX(0)+event.getX(1))*.5f;
            float y=(event.getY(0)+event.getY(1))*.5f;
            if(twoFingers&&action==MotionEvent.ACTION_MOVE){
                float dx=x-previousX,dy=y-previousY;
                mutate(()->observer.pan(dx,dy));
            }
            previousX=x;previousY=y;twoFingers=true;
        }
        gestures.onTouchEvent(event);
        if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL){
            twoFingers=false;
            interactionHandler.removeCallbacks(restoreFullUhd);
            interactionHandler.postDelayed(restoreFullUhd,140);
        }
        return true;
    }
    @Override protected void onDetachedFromWindow(){
        interactionHandler.removeCallbacks(restoreFullUhd);
        super.onDetachedFromWindow();
    }
    @Override public boolean performClick(){super.performClick();return true;}
}
