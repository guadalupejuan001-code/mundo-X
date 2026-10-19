package com.mundox.cosmic;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.View;
import java.util.Locale;

/** Expectation values from the active observer; drawing this view never measures the core. */
final class CoreSignalsView extends View {
    private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    private float x,z,probabilityZ;
    private String address="0",xLabel="⟨X⟩",zLabel="⟨Z⟩";
    CoreSignalsView(Context context){super(context);setTag("core_signals");}
    void update(int q,float vx,float vz){
        address=String.valueOf(q);x=vx;z=vz;probabilityZ=vz;xLabel="⟨X⟩";zLabel="⟨Z⟩";
        setContentDescription(String.format(Locale.US,"Qubit %d: expectativa X %.3f, expectativa Z %.3f, probabilidad de uno %.1f por ciento",q,x,z,(1-z)*50));invalidate();
    }
    void updateInfinite(String q,float xx,float zz,float localZ){
        address=com.mundox.graphics.InfiniteProgram.shortAddress(q);x=xx;z=zz;probabilityZ=localZ;xLabel="⟨XX⟩ · A,B";zLabel="⟨ZZ⟩ · A,B";
        setContentDescription(String.format(Locale.US,"Dirección %s: correlación XX %.3f, correlación ZZ %.3f, probabilidad de uno %.1f por ciento",q,xx,zz,(1-localZ)*50));invalidate();
    }
    private float dp(float v){return v*getResources().getDisplayMetrics().density;}
    private float sp(float v){return v*getResources().getDisplayMetrics().scaledDensity;}
    @Override protected void onDraw(Canvas c){
        super.onDraw(c);float w=getWidth(),h=getHeight(),cy=h*.45f,r=Math.min(h*.32f,w*.15f),cx=w*.19f;
        p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(dp(5));p.setColor(CosmicUi.LINE);c.drawCircle(cx,cy,r,p);
        p.setColor(CosmicUi.VIOLET);c.drawArc(new RectF(cx-r,cy-r,cx+r,cy+r),-90,Math.max(0,Math.min(360,(1-probabilityZ)*180)),false,p);
        p.setStyle(Paint.Style.FILL);p.setTypeface(Typeface.create("sans-serif-medium",0));p.setTextSize(sp(19));p.setTextAlign(Paint.Align.CENTER);p.setColor(CosmicUi.TEXT);c.drawText(String.format(Locale.US,"%.0f%%",(1-probabilityZ)*50),cx,cy+dp(5),p);
        p.setTextSize(sp(10));p.setColor(CosmicUi.MUTED);c.drawText("P(1) · "+address,cx,cy+r+dp(19),p);
        float left=w*.4f,right=w-dp(6),center=(left+right)/2;
        drawBar(c,xLabel,x,CosmicUi.CYAN,left,right,center,h*.27f);
        drawBar(c,zLabel,z,CosmicUi.VIOLET,left,right,center,h*.64f);
        p.setTextSize(sp(9));p.setColor(CosmicUi.MUTED);p.setTextAlign(Paint.Align.LEFT);c.drawText("−1",left,h-dp(3),p);p.setTextAlign(Paint.Align.CENTER);c.drawText("0",center,h-dp(3),p);p.setTextAlign(Paint.Align.RIGHT);c.drawText("+1",right,h-dp(3),p);
    }
    private void drawBar(Canvas c,String label,float value,int color,float left,float right,float center,float y){
        p.setColor(CosmicUi.MUTED);p.setTextSize(sp(11));p.setTextAlign(Paint.Align.LEFT);c.drawText(label,left,y-dp(9),p);
        p.setTextAlign(Paint.Align.RIGHT);p.setColor(CosmicUi.TEXT);c.drawText(String.format(Locale.US,"%+.2f",value),right,y-dp(9),p);
        p.setColor(CosmicUi.LINE);c.drawRoundRect(left,y,right,y+dp(5),dp(3),dp(3),p);
        p.setColor(color);float end=center+Math.max(-1,Math.min(1,value))*(right-left)/2;
        c.drawRoundRect(Math.min(center,end),y,Math.max(center,end)+dp(1),y+dp(5),dp(2),dp(2),p);
        p.setColor(CosmicUi.MUTED);c.drawRect(center-dp(.5f),y-dp(2),center+dp(.5f),y+dp(7),p);
    }
}
