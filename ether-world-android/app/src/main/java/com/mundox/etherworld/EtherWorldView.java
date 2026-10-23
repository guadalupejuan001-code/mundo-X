package com.mundox.etherworld;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.MotionEvent;
import android.view.View;

import java.util.Locale;

/**
 * Interactive Ether viewport + spatial discovery map.
 * The ARGB pixels come directly from Ether Renderer X in native C++.
 */
final class EtherWorldView extends View {
    interface Actions {
        void onSelect(int x,int z);
        void onRotate(int hundredths);
    }
    private static final int BG=0xff0a141d,INACTIVE=0xff233848;
    private static final int ACCENT=0xff9beddb,WHITE=0xffe8f3f2,SECONDARY=0xffa2babe;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Bitmap viewport=Bitmap.createBitmap(
            WorldNative.VIEW_WIDTH,WorldNative.VIEW_HEIGHT,Bitmap.Config.ARGB_8888);
    private final Bitmap regionMap=Bitmap.createBitmap(
            WorldNative.MAP_WIDTH,WorldNative.MAP_WIDTH,Bitmap.Config.ARGB_8888);
    private final RectF viewRect=new RectF(),mapRect=new RectF();
    private final Actions actions;
    private int centerX,centerZ,selectedX,selectedZ;
    private String hint="Pulsa ESCANEAR para revelar el territorio";
    private boolean hasImage;
    private float downX,downY;
    private boolean dragging;

    EtherWorldView(Context context,Actions callback) {
        super(context);
        this.actions=callback;
        setLayerType(View.LAYER_TYPE_SOFTWARE,null);
        setMinimumHeight(dp(380));
        setContentDescription("Vista 3D y mapa interactivo del mundo virtual Ether");
    }

    void update(int[] pixels,int[] map,int cx,int cz,int sx,int sz) {
        if(pixels.length!=WorldNative.VIEW_WIDTH*WorldNative.VIEW_HEIGHT ||
           map.length!=WorldNative.MAP_WIDTH*WorldNative.MAP_WIDTH)
            throw new IllegalArgumentException("Dimensiones de mundo incorrectas");
        viewport.setPixels(pixels,0,WorldNative.VIEW_WIDTH,0,0,
                WorldNative.VIEW_WIDTH,WorldNative.VIEW_HEIGHT);
        regionMap.setPixels(map,0,WorldNative.MAP_WIDTH,0,0,
                WorldNative.MAP_WIDTH,WorldNative.MAP_WIDTH);
        centerX=cx;centerZ=cz;selectedX=sx;selectedZ=sz;
        hasImage=true;
        hint="Toca el mapa para seleccionar una celda · desliza la vista para girar";
        invalidate();
    }

    private int dp(float value) {
        return Math.round(value*getResources().getDisplayMetrics().density);
    }
    @Override protected void onMeasure(int widthSpec,int heightSpec) {
        int width=MeasureSpec.getSize(widthSpec);
        int pad=dp(10);
        float usable=Math.max(dp(160),width-2*pad);
        int viewportHeight=Math.round(usable*WorldNative.VIEW_HEIGHT/
                (float)WorldNative.VIEW_WIDTH);
        int mapHeight=Math.min(Math.round(usable*.70f),dp(248));
        int height=dp(98)+viewportHeight+mapHeight+dp(46);
        setMeasuredDimension(width,Math.max(height,dp(360)));
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int w=getWidth(),h=getHeight(),margin=dp(9);
        paint.setColor(BG);
        paint.setStyle(Paint.Style.FILL);
        canvas.drawRoundRect(0,0,w,h,dp(19),dp(19),paint);

        label(canvas,"ETHERVIEW  /  ESPACIO GENERADO POR REGLAS",
                margin+dp(12),dp(27),11,ACCENT,true);
        label(canvas,"No existe un arreglo de 1 millón de objetos en RAM",
                margin+dp(12),dp(45),10,SECONDARY,false);

        float contentWidth=w-2f*margin;
        float top=dp(58);
        float vpHeight=contentWidth*WorldNative.VIEW_HEIGHT/(float)WorldNative.VIEW_WIDTH;
        viewRect.set(margin,top,w-margin,top+vpHeight);
        drawFramed(canvas,viewRect);
        if(hasImage) {
            paint.setFilterBitmap(true);
            paint.setColor(Color.WHITE);
            canvas.drawBitmap(viewport,null,viewRect,paint);
            paint.setFilterBitmap(false);
        } else {
            label(canvas,"CREANDO TU UNIVERSO...",
                    viewRect.left+dp(14),viewRect.centerY(),15,ACCENT,true);
        }
        // Crosshair represents the observer's current focal direction.
        float crossX=viewRect.centerX(),crossY=viewRect.centerY();
        paint.setColor(0x99e4fef9);
        paint.setStrokeWidth(dp(1.1f));
        canvas.drawLine(crossX-dp(8),crossY,crossX+dp(8),crossY,paint);
        canvas.drawLine(crossX,crossY-dp(8),crossX,crossY+dp(8),paint);

        float mapHead=viewRect.bottom+dp(25);
        label(canvas,"ETHER MAP / TOPOLOGÍA OBSERVADA",
                margin+dp(9),mapHead,11,ACCENT,true);
        float mapSize=Math.min(contentWidth*.70f,dp(248));
        float ml=(w-mapSize)*.5f,mt=mapHead+dp(9);
        mapRect.set(ml,mt,ml+mapSize,mt+mapSize);
        drawFramed(canvas,mapRect);
        if(hasImage) {
            paint.setFilterBitmap(false);
            paint.setColor(Color.WHITE);
            canvas.drawBitmap(regionMap,null,mapRect,paint);
        } else {
            label(canvas,"SIN MAPA",mapRect.left+dp(12),mapRect.centerY(),
                    13,SECONDARY,false);
        }
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(1.5f));
        paint.setColor(0xffb5e8d7);
        canvas.drawRoundRect(mapRect,dp(5),dp(5),paint);
        paint.setStyle(Paint.Style.FILL);
        label(canvas,String.format(Locale.US,"FOCO %d,%d     SELECCIÓN %d,%d",
                centerX,centerZ,selectedX,selectedZ),
                margin+dp(9),mapRect.bottom+dp(25),11,WHITE,true);
        // Hint is deliberately below the map and never overlaps the scene.
        label(canvas,"Arrastra la escena para rotar · pulsa el mapa para editar",
                margin+dp(9),mapRect.bottom+dp(42),10,SECONDARY,false);
    }

    private void drawFramed(Canvas canvas,RectF r){
        paint.setColor(INACTIVE);
        paint.setStyle(Paint.Style.FILL);
        canvas.drawRoundRect(r,dp(8),dp(8),paint);
    }
    private void label(Canvas canvas,String text,float x,float y,int sp,int color,boolean bold){
        paint.setColor(color);
        paint.setTextSize(sp*getResources().getDisplayMetrics().scaledDensity);
        paint.setTypeface(bold?Typeface.create("sans-serif-medium",Typeface.BOLD)
                :Typeface.create("sans-serif",Typeface.NORMAL));
        canvas.drawText(text,x,y,paint);
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        if(actions==null)return false;
        switch(e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                downX=e.getX();downY=e.getY();dragging=false;
                return true;
            case MotionEvent.ACTION_MOVE:
                if(Math.abs(e.getX()-downX)>dp(9)||Math.abs(e.getY()-downY)>dp(9))
                    dragging=true;
                return true;
            case MotionEvent.ACTION_UP:
                if(mapRect.contains(e.getX(),e.getY())&&!dragging){
                    float fx=(e.getX()-mapRect.left)/mapRect.width();
                    float fz=(e.getY()-mapRect.top)/mapRect.height();
                    int dx=Math.min(WorldNative.MAP_TILES-1,
                            Math.max(0,(int)(fx*WorldNative.MAP_TILES)))
                            -WorldNative.MAP_TILES/2;
                    int dz=Math.min(WorldNative.MAP_TILES-1,
                            Math.max(0,(int)(fz*WorldNative.MAP_TILES)))
                            -WorldNative.MAP_TILES/2;
                    actions.onSelect(centerX+dx,centerZ+dz);
                    performClick();
                } else if(viewRect.contains(downX,downY)&&dragging) {
                    float difference=e.getX()-downX;
                    int hundredths=Math.max(-80,Math.min(80,Math.round(
                            difference/viewRect.width()*100)));
                    if(hundredths!=0)actions.onRotate(hundredths);
                    performClick();
                }
                return true;
            case MotionEvent.ACTION_CANCEL:return true;
            default:return true;
        }
    }
    @Override public boolean performClick() {
        super.performClick();
        return true;
    }
}
