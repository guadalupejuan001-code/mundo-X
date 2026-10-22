package com.mundox.fusion3d;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;

import java.util.Locale;

/** Plot of measured (read-only) MIL Reduced amplitudes versus independent RK4. */
public final class MilProbabilityView extends View {
    private static final int BG=Color.rgb(11,22,35);
    private static final int TEXT=Color.rgb(233,246,251);
    private static final int MIL=Color.rgb(100,237,211);
    private static final int ORACLE=Color.rgb(245,173,105);
    private static final int GRID=Color.rgb(52,76,95);
    private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    private double[] mil,oracle;
    private final float density,scale;

    public MilProbabilityView(Context context){
        super(context);
        density=getResources().getDisplayMetrics().density;
        scale=getResources().getDisplayMetrics().scaledDensity;
        setContentDescription("mil-radial-population-chart");
    }

    public void setResult(MilRadialEvolver.Result result){
        mil=result==null?null:result.probabilities.clone();
        oracle=result==null?null:result.referenceProbabilities.clone();
        invalidate();
    }

    private float dp(double x){return (float)(x*density);}
    private void label(Canvas c,String text,float x,float y,int color,float size){
        p.setColor(color);p.setStyle(Paint.Style.FILL);p.setTextSize(size*scale);
        c.drawText(text,x,y,p);
    }

    @Override protected void onDraw(Canvas c){
        super.onDraw(c);c.drawColor(BG);
        float w=getWidth(),h=getHeight();
        if(w<dp(170)||h<dp(150))return;
        label(c,"EVOLUCIÓN: |amplitud|² por posición radial",dp(12),dp(22),TEXT,10);
        float left=dp(35),right=w-dp(12),top=dp(43),bottom=h-dp(42);
        if(mil==null||oracle==null){
            label(c,"Ejecuta MIL Core para ver las 8 posiciones.",left,h/2,
                    Color.rgb(163,186,201),11);
            return;
        }
        for(int k=0;k<=4;k++){
            float y=top+(bottom-top)*k/4f;
            p.setColor(GRID);p.setStrokeWidth(dp(.75f));
            c.drawLine(left,y,right,y,p);
            label(c,String.format(Locale.US,"%.2f",(4-k)/4.),
                    dp(2),y+dp(3),GRID,9);
        }
        int n=mil.length;
        float step=(right-left)/n;
        for(int i=0;i<n;i++){
            float x=left+i*step;
            float baseline=bottom;
            float ml=(float)Math.min(1.,Math.max(0.,mil[i]));
            float rl=(float)Math.min(1.,Math.max(0.,oracle[i]));
            p.setColor(MIL);p.setStyle(Paint.Style.FILL);
            c.drawRoundRect(new RectF(x+step*.10f,baseline-ml*(bottom-top),
                    x+step*.45f,baseline),dp(2),dp(2),p);
            p.setColor(ORACLE);
            c.drawRoundRect(new RectF(x+step*.49f,baseline-rl*(bottom-top),
                    x+step*.84f,baseline),dp(2),dp(2),p);
            label(c,Integer.toString(i),x+step*.36f,bottom+dp(17),TEXT,9);
        }
        label(c,"MIL (verde)  /  RK4 independiente (naranja)",
                left,bottom+dp(36),TEXT,10);
    }
}
