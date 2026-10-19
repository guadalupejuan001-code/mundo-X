package com.mundox.solar;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.GestureDetector;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

/**
 * Interactive offline map of the existing SolarData model, seen from the north
 * ecliptic pole. Heliocentric and local moon distances are logarithmically
 * compressed in separate panels; marker sizes are enlarged for selection.
 * The listener requests a destination only: this view never owns or edits a core.
 */
public final class SolarMapView extends View {
    public interface OnBodySelectedListener { void onBodySelected(int id); }

    private static final int INK=0xff080f1c, PAPER=0xffe8eff8, MUTED=0xff8c9caf;
    private static final int TEAL=0xff65ddd0, GOLD=0xffeac98c, ORBIT_STEPS=80;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path=new Path();
    private final RectF chart=new RectF(), details=new RectF();
    private final RectF plus=new RectF(), minus=new RectF(), recenter=new RectF();
    private final RectF[] moonRows=new RectF[SolarData.BODIES.length];
    private final float[][] targets=new float[SolarData.BODIES.length][2];
    private final float[][] moonMarkers=new float[SolarData.BODIES.length][2];
    private final double[][] positions=new double[SolarData.BODIES.length][];
    private final float[][] orbitPoints=new float[SolarData.BODIES.length][];
    private final OnBodySelectedListener listener;
    private final ScaleGestureDetector scaleDetector;
    private final GestureDetector gestures;
    private final SimpleDateFormat dateFormat;
    private final float density;
    private double julianDate=SolarData.J2000;
    private int selectedBody=3, moonParent=3;
    private float zoom=1, panX, panY, chartScale, centerX, centerY;
    private boolean positionsDirty=true, localDirty=true, hadMultiplePointers;
    private String dateText="";
    private Shader background;

    public SolarMapView(Context context,OnBodySelectedListener listener) {
        super(context);
        if(listener==null)throw new IllegalArgumentException("Falta el receptor del mapa");
        this.listener=listener;density=getResources().getDisplayMetrics().density;
        setMinimumHeight(Math.round(dp(300)));setFocusable(true);setClickable(true);
        setContentDescription("Mapa orbital: toca un planeta o una luna para seleccionarlo. "
            +"Pellizca para ampliar, arrastra para desplazar y usa Recentrar para volver al sistema.");
        dateFormat=new SimpleDateFormat("dd MMM yyyy · HH:mm 'UTC'",new Locale("es"));
        dateFormat.setTimeZone(TimeZone.getTimeZone("UTC"));
        for(int id=0;id<moonRows.length;id++){
            moonRows[id]=new RectF();Arrays.fill(moonMarkers[id],Float.NaN);
        }
        scaleDetector=new ScaleGestureDetector(context,new ScaleGestureDetector.SimpleOnScaleGestureListener(){
            @Override public boolean onScale(ScaleGestureDetector detector){
                if(chart.contains(detector.getFocusX(),detector.getFocusY()))
                    zoomAround(detector.getScaleFactor(),detector.getFocusX(),detector.getFocusY());
                return true;
            }
        });
        gestures=new GestureDetector(context,new GestureDetector.SimpleOnGestureListener(){
            @Override public boolean onDown(MotionEvent e){return true;}
            @Override public boolean onScroll(MotionEvent first,MotionEvent next,float dx,float dy){
                if(!scaleDetector.isInProgress()&&first!=null&&chart.contains(first.getX(),first.getY())){
                    panX-=dx;panY-=dy;invalidate();
                }
                return true;
            }
            @Override public boolean onSingleTapUp(MotionEvent event){
                if(hadMultiplePointers)return true;
                float x=event.getX(),y=event.getY();
                if(plus.contains(x,y))zoomAround(1.35f,chart.centerX(),chart.centerY());
                else if(minus.contains(x,y))zoomAround(1f/1.35f,chart.centerX(),chart.centerY());
                else if(recenter.contains(x,y))resetView();
                else selectAt(x,y);
                performClick();return true;
            }
            @Override public boolean onDoubleTap(MotionEvent event){
                if(chart.contains(event.getX(),event.getY()))zoomAround(1.6f,event.getX(),event.getY());
                return true;
            }
        });
    }

    public void setJulianDate(double jd) {
        if(!Double.isFinite(jd))throw new IllegalArgumentException("Fecha no finita");
        double bounded=Math.max(SolarData.MIN_JD,Math.min(SolarData.MAX_JD,jd));
        if(bounded!=julianDate){julianDate=bounded;positionsDirty=true;localDirty=true;invalidate();}
    }

    public void setSelectedBody(int id) {
        validateBody(id);
        selectedBody=id;int parent=id>8?SolarData.BODIES[id].parent:id;
        if(parent!=moonParent){moonParent=parent;localDirty=true;}
        invalidate();
    }

    public void resetView() { zoom=1;panX=panY=0;invalidate(); }

    /** Local coordinates of a visible selectable target, or null if it is hidden. */
    public float[] bodyScreenPosition(int id) {
        validateBody(id);if(getWidth()<1||getHeight()<1)return null;
        prepare();
        return Float.isNaN(targets[id][0])?null:targets[id].clone();
    }

    public int selectedBody() { return selectedBody; }

    private static void validateBody(int id){
        if(id<0||id>=SolarData.BODIES.length)throw new IllegalArgumentException("Cuerpo desconocido: "+id);
    }

    private float dp(float value){return value*density;}

    private void zoomAround(float factor,float x,float y){
        if(!Float.isFinite(factor)||factor<=0)return;
        float next=Math.max(.65f,Math.min(8f,zoom*factor)),ratio=next/zoom;
        panX=(panX+chart.centerX()-x)*ratio+x-chart.centerX();
        panY=(panY+chart.centerY()-y)*ratio+y-chart.centerY();
        zoom=next;invalidate();
    }

    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh){
        super.onSizeChanged(w,h,oldw,oldh);
        background=new LinearGradient(0,0,w,h,new int[]{0xff0d2033,INK,0xff07151d},null,Shader.TileMode.CLAMP);
    }

    private static float radial(double au){return (float)(Math.log1p(au/.18)/Math.log1p(32/.18));}

    private static void project(double x,double z,float[] out,int offset){
        double distance=Math.hypot(x,z);
        float r=radial(distance);
        out[offset]=distance==0?0:(float)(x/distance*r);
        out[offset+1]=distance==0?0:(float)(z/distance*r);
    }

    private void refreshPositions(){
        if(positionsDirty){
            for(int id=0;id<positions.length;id++)positions[id]=SolarData.position(id,julianDate);
            for(int id=1;id<=8;id++){
                float[] points=orbitPoints[id];
                if(points==null)points=orbitPoints[id]=new float[(ORBIT_STEPS+1)*2];
                for(int i=0;i<=ORBIT_STEPS;i++){
                    double[] p=SolarData.orbitPosition(id,(double)i/ORBIT_STEPS,julianDate);
                    project(p[0],p[2],points,i*2);
                }
            }
            dateText=dateFormat.format(new Date(Math.round((julianDate-2440587.5)*86400000.0)));
            positionsDirty=false;
        }
        if(localDirty){
            double[] parent=positions[moonParent];
            for(int id=9;id<positions.length;id++)if(SolarData.BODIES[id].parent==moonParent){
                float[] points=orbitPoints[id];
                if(points==null)points=orbitPoints[id]=new float[(ORBIT_STEPS+1)*2];
                for(int i=0;i<=ORBIT_STEPS;i++){
                    double[] p=SolarData.orbitPosition(id,(double)i/ORBIT_STEPS,julianDate);
                    points[i*2]=(float)((p[0]-parent[0])*SolarData.AU_KM);
                    points[i*2+1]=(float)((p[2]-parent[2])*SolarData.AU_KM);
                }
            }
            localDirty=false;
        }
    }

    private int moonCount(){
        int count=0;for(int id=9;id<SolarData.BODIES.length;id++)if(SolarData.BODIES[id].parent==moonParent)count++;
        return count;
    }

    private void layoutMap(){
        int moons=moonCount();float w=getWidth(),h=getHeight(),edge=dp(12),top=dp(70),bottom=h-dp(12);
        boolean wide=w>h*1.35f;
        if(wide){
            float divide=w*.57f;chart.set(edge,top,divide-dp(10),bottom);
            details.set(divide,top,w-edge,bottom);
        }else{
            float panel=dp(moons>0?Math.min(178,70+moons*27):86);
            panel=Math.min(panel,Math.max(dp(70),h*.42f));
            details.set(edge,bottom-panel,w-edge,bottom);
            chart.set(edge,top,w-edge,details.top-dp(12));
        }
        chartScale=Math.max(dp(12),Math.min(chart.width(),chart.height()-dp(24))*.41f);
        centerX=chart.centerX()+panX;centerY=chart.centerY()-dp(12)+panY;
        float by=chart.bottom-dp(35);
        minus.set(chart.left+dp(4),by,chart.left+dp(40),by+dp(32));
        plus.set(minus.right+dp(6),by,minus.right+dp(42),by+dp(32));
        recenter.set(plus.right+dp(6),by,Math.min(chart.right-dp(2),plus.right+dp(98)),by+dp(32));
    }

    private void prepare(){
        refreshPositions();layoutMap();
        for(int id=0;id<targets.length;id++){Arrays.fill(targets[id],Float.NaN);moonRows[id].setEmpty();}
        float[] point=new float[2];
        for(int id=0;id<=8;id++){
            project(positions[id][0],positions[id][2],point,0);
            float x=centerX+point[0]*chartScale*zoom,y=centerY+point[1]*chartScale*zoom;
            if(chart.contains(x,y)&&!plus.contains(x,y)&&!minus.contains(x,y)&&!recenter.contains(x,y)){
                targets[id][0]=x;targets[id][1]=y;
            }
        }
        int count=moonCount(),row=0;
        if(count>0){
            float listLeft=details.left+Math.min(details.width()*.42f,dp(134));
            float rowsTop=details.top+dp(48),rowHeight=Math.min(dp(30),(details.bottom-dp(10)-rowsTop)/count);
            for(int id=9;id<targets.length;id++)if(SolarData.BODIES[id].parent==moonParent){
                RectF r=moonRows[id];r.set(listLeft,rowsTop+row*rowHeight,details.right-dp(10),rowsTop+(row+1)*rowHeight-dp(2));
                targets[id][0]=r.centerX();targets[id][1]=r.centerY();row++;
            }
        }
    }

    @Override protected void onDraw(Canvas canvas){
        super.onDraw(canvas);prepare();
        for(float[] marker:moonMarkers)Arrays.fill(marker,Float.NaN);
        paint.setStyle(Paint.Style.FILL);paint.setShader(background);canvas.drawRect(0,0,getWidth(),getHeight(),paint);paint.setShader(null);
        for(int i=0;i<88;i++){
            float x=((i*179+37)%997)/997f*getWidth(),y=((i*283+91)%991)/991f*getHeight();
            paint.setColor(i%3==0?0x435b7899:0x213b546e);canvas.drawCircle(x,y,dp(i%9==0?1f:.6f),paint);
        }
        text(canvas,"MAPA ORBITAL",dp(16),dp(24),15,PAPER,true);
        text(canvas,"Plano eclíptico · distancias comprimidas",dp(16),dp(43),10.5f,MUTED,false);
        text(canvas,dateText,dp(16),dp(59),10,GOLD,false);
        canvas.save();canvas.clipRect(chart);
        drawChart(canvas);
        canvas.restore();
        drawButton(canvas,minus,"−");drawButton(canvas,plus,"+");drawButton(canvas,recenter,"Recentrar");
        drawDetails(canvas);
    }

    private void drawChart(Canvas canvas){
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(.65f));paint.setColor(0x244b7897);
        canvas.drawLine(chart.left,centerY,chart.right,centerY,paint);canvas.drawLine(centerX,chart.top,centerX,chart.bottom,paint);
        for(double radius:new double[]{1,5,10,30}){
            float r=radial(radius)*chartScale*zoom;paint.setColor(0x28477993);canvas.drawCircle(centerX,centerY,r,paint);
            if(r>dp(25))text(canvas,(int)radius+" UA",centerX+dp(4),centerY-r-dp(4),8.5f,0xff587b95,false);
            paint.setStyle(Paint.Style.STROKE);
        }
        for(int id=1;id<=8;id++){
            float[] p=orbitPoints[id];path.reset();
            for(int i=0;i<p.length;i+=2){
                float x=centerX+p[i]*chartScale*zoom,y=centerY+p[i+1]*chartScale*zoom;
                if(i==0)path.moveTo(x,y);else path.lineTo(x,y);
            }
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(id==moonParent?1.2f:.75f));
            paint.setColor(id==moonParent?0x9865ddd0:0x6644607c);canvas.drawPath(path,paint);
        }
        for(int id=0;id<=8;id++)if(!Float.isNaN(targets[id][0])){
            float x=targets[id][0],y=targets[id][1];float radius=dp(id==0?7.5f:id==5||id==6?6f:4.6f);
            drawMarker(canvas,id,x,y,radius);
            String name=SolarData.BODIES[id].name;
            paint.setTextSize(dp(11));float tw=paint.measureText(name);
            float tx=x+radius+dp(6);if(tx+tw>chart.right-dp(5))tx=x-radius-dp(6)-tw;
            float ty=y-dp(5);if(ty<chart.top+dp(12))ty=y+dp(15);
            text(canvas,name,tx,ty,11,id==selectedBody?TEAL:id==0?GOLD:PAPER,id==selectedBody);
        }
        text(canvas,"Escala radial logarítmica · tamaños ampliados",chart.left+dp(4),chart.top+dp(11),8.5f,MUTED,false);
    }

    private void drawDetails(Canvas canvas){
        paint.setStyle(Paint.Style.FILL);paint.setColor(0xf0132435);canvas.drawRoundRect(details,dp(14),dp(14),paint);
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(1));paint.setColor(0xff284757);canvas.drawRoundRect(details,dp(14),dp(14),paint);
        int count=moonCount();String parent=SolarData.BODIES[moonParent].name;
        text(canvas,count>0?"SISTEMA DE "+parent.toUpperCase(new Locale("es")):SolarData.BODIES[selectedBody].name,
            details.left+dp(12),details.top+dp(22),11.5f,PAPER,true);
        if(count==0){
            text(canvas,SolarEnvironment.body(selectedBody).label,details.left+dp(12),details.top+dp(43),10,MUTED,false);
            text(canvas,"Toca otro destino para explorar sus lunas",details.left+dp(12),details.top+dp(63),9.5f,TEAL,false);return;
        }
        text(canvas,"Órbitas locales logarítmicas · escala ampliada",details.left+dp(12),details.top+dp(38),8.8f,MUTED,false);
        float plotWidth=Math.min(details.width()*.42f,dp(134));
        float cx=details.left+plotWidth*.5f,cy=(details.top+dp(47)+details.bottom-dp(10))*.5f;
        float radius=Math.max(dp(9),Math.min(plotWidth*.37f,(details.bottom-details.top-dp(65))*.43f));
        double smallest=Double.POSITIVE_INFINITY,largest=0;
        for(int id=9;id<positions.length;id++)if(SolarData.BODIES[id].parent==moonParent){
            double a=SolarData.BODIES[id].semiMajorAU*SolarData.AU_KM;
            smallest=Math.min(smallest,a);largest=Math.max(largest,a*1.12);
        }
        double unit=smallest*.6,divisor=Math.log1p(largest/unit);
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(.65f));
        for(int id=9;id<positions.length;id++)if(SolarData.BODIES[id].parent==moonParent){
            float[] p=orbitPoints[id];path.reset();
            for(int i=0;i<p.length;i+=2){
                double d=Math.hypot(p[i],p[i+1]);float rr=(float)(Math.log1p(d/unit)/divisor)*radius;
                float x=cx+(d==0?0:(float)(p[i]/d)*rr),y=cy+(d==0?0:(float)(p[i+1]/d)*rr);
                if(i==0)path.moveTo(x,y);else path.lineTo(x,y);
            }
            paint.setStyle(Paint.Style.STROKE);paint.setColor(id==selectedBody?0xff65ddd0:0xff375772);canvas.drawPath(path,paint);
            double x=(positions[id][0]-positions[moonParent][0])*SolarData.AU_KM;
            double z=(positions[id][2]-positions[moonParent][2])*SolarData.AU_KM;
            double d=Math.hypot(x,z);float rr=(float)(Math.log1p(d/unit)/divisor)*radius;
            moonMarkers[id][0]=cx+(d==0?0:(float)(x/d)*rr);
            moonMarkers[id][1]=cy+(d==0?0:(float)(z/d)*rr);
            drawMarker(canvas,id,moonMarkers[id][0],moonMarkers[id][1],dp(2.8f));
            RectF row=moonRows[id];paint.setStyle(Paint.Style.FILL);paint.setColor(id==selectedBody?0xff1b4c54:0xff172e41);
            canvas.drawRoundRect(row,dp(6),dp(6),paint);
            paint.setColor(bodyColor(id));canvas.drawCircle(row.left+dp(10),row.centerY(),dp(2.8f),paint);
            text(canvas,SolarData.BODIES[id].name,row.left+dp(20),row.centerY()+dp(3.5f),10.5f,id==selectedBody?TEAL:PAPER,id==selectedBody);
        }
        drawMarker(canvas,moonParent,cx,cy,dp(5));
    }

    private int bodyColor(int id){
        float[] c=SolarData.BODIES[id].color;
        return Color.rgb(Math.round(c[0]*255),Math.round(c[1]*255),Math.round(c[2]*255));
    }

    private void drawMarker(Canvas canvas,int id,float x,float y,float radius){
        paint.setStyle(Paint.Style.FILL);paint.setColor(id==selectedBody?0x3065ddd0:id==0?0x30eac98c:0x153e9cb5);
        canvas.drawCircle(x,y,radius+dp(5),paint);
        if(id==selectedBody){paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(1.2f));paint.setColor(TEAL);canvas.drawCircle(x,y,radius+dp(3.5f),paint);}
        paint.setStyle(Paint.Style.FILL);paint.setColor(bodyColor(id));canvas.drawCircle(x,y,radius,paint);
        paint.setColor(0x55ffffff);canvas.drawCircle(x-radius*.27f,y-radius*.27f,radius*.36f,paint);
    }

    private void drawButton(Canvas canvas,RectF rect,String label){
        paint.setStyle(Paint.Style.FILL);paint.setColor(0xf0193045);canvas.drawRoundRect(rect,dp(8),dp(8),paint);
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(.8f));paint.setColor(0xff385a70);canvas.drawRoundRect(rect,dp(8),dp(8),paint);
        float size=label.length()==1?20:10;paint.setTextSize(dp(size));paint.setTypeface(Typeface.DEFAULT);
        text(canvas,label,rect.centerX()-paint.measureText(label)*.5f,rect.centerY()+dp(size*.34f),size,PAPER,false);
    }

    private void text(Canvas canvas,String value,float x,float y,float size,int color,boolean bold){
        paint.setStyle(Paint.Style.FILL);paint.setColor(color);paint.setTextSize(dp(size));
        paint.setTypeface(bold?Typeface.DEFAULT_BOLD:Typeface.DEFAULT);canvas.drawText(value,x,y,paint);
    }

    private void selectAt(float x,float y){
        prepare();
        for(int id=9;id<moonRows.length;id++)if(moonRows[id].contains(x,y)){choose(id);return;}
        if(details.contains(x,y)){
            int nearest=-1;double radius=dp(14)*dp(14);
            for(int id=9;id<moonMarkers.length;id++)if(SolarData.BODIES[id].parent==moonParent&&!Float.isNaN(moonMarkers[id][0])){
                double dx=x-moonMarkers[id][0],dy=y-moonMarkers[id][1],distance=dx*dx+dy*dy;
                if(distance<radius){nearest=id;radius=distance;}
            }
            if(nearest>=0)choose(nearest);
            return;
        }
        if(!chart.contains(x,y))return;
        int best=-1;double distance=dp(23)*dp(23);
        for(int id=0;id<=8;id++)if(!Float.isNaN(targets[id][0])){
            double dx=x-targets[id][0],dy=y-targets[id][1],d=dx*dx+dy*dy;
            if(d<distance){best=id;distance=d;}
        }
        if(best>=0)choose(best);
    }

    private void choose(int id){setSelectedBody(id);announceForAccessibility(SolarData.BODIES[id].name);listener.onBodySelected(id);}

    @Override public boolean onTouchEvent(MotionEvent event){
        if(event.getActionMasked()==MotionEvent.ACTION_DOWN){
            hadMultiplePointers=false;if(getParent()!=null)getParent().requestDisallowInterceptTouchEvent(true);
        }
        if(event.getPointerCount()>1)hadMultiplePointers=true;
        scaleDetector.onTouchEvent(event);gestures.onTouchEvent(event);
        if(event.getActionMasked()==MotionEvent.ACTION_UP||event.getActionMasked()==MotionEvent.ACTION_CANCEL)
            if(getParent()!=null)getParent().requestDisallowInterceptTouchEvent(false);
        return true;
    }

    @Override public boolean performClick(){super.performClick();return true;}

    @Override public boolean onKeyDown(int keyCode,KeyEvent event){
        if(keyCode==KeyEvent.KEYCODE_DPAD_RIGHT||keyCode==KeyEvent.KEYCODE_DPAD_DOWN){choose((selectedBody+1)%targets.length);return true;}
        if(keyCode==KeyEvent.KEYCODE_DPAD_LEFT||keyCode==KeyEvent.KEYCODE_DPAD_UP){choose((selectedBody+targets.length-1)%targets.length);return true;}
        return super.onKeyDown(keyCode,event);
    }

    @Override public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info){
        super.onInitializeAccessibilityNodeInfo(info);info.setScrollable(true);
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD);
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD);
    }

    @Override public boolean performAccessibilityAction(int action,Bundle args){
        if(action==AccessibilityNodeInfo.ACTION_SCROLL_FORWARD){choose((selectedBody+1)%targets.length);return true;}
        if(action==AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD){choose((selectedBody+targets.length-1)%targets.length);return true;}
        return super.performAccessibilityAction(action,args);
    }
}
