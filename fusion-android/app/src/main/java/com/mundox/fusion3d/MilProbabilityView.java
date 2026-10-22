package com.mundox.fusion3d;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;

import java.util.Locale;

/**
 * Displays actual MIL qubit-state populations versus independent RK4.
 *
 * For 4/8/16 grid sites: one bar per grid site. For 32..256 sites:
 * sum neighboring disjoint groups into up to 16 bars. No interpolation,
 * invented values, or hiding normalization failures.
 */
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
        super.onDraw(c);
        c.drawColor(BG);
        float w=getWidth(),h=getHeight();
        if(w<dp(170)||h<dp(150))return;
        label(c,"EVOLUCIÓN: probabilidad |amplitud|² por región radial",
                dp(9),dp(19),TEXT,10);
        float left=dp(36),right=w-dp(10),top=dp(44),bottom=h-dp(45);
        if(mil==null||oracle==null||mil.length!=oracle.length){
            label(c,"Pulsa Ejecutar para comparar MIL y RK4.",
                    left,h/2,Color.rgb(163,186,201),10);
            return;
        }
        final int n=mil.length;
        final int bins=n<=16?n:16;
        final int sitesPerBin=n/bins;
        double[] groupedMil=new double[bins];
        double[] groupedRef=new double[bins];
        double max=.04;
        for(int bin=0;bin<bins;bin++){
            for(int site=bin*sitesPerBin;site<(bin+1)*sitesPerBin;site++){
                groupedMil[bin]+=mil[site];
                groupedRef[bin]+=oracle[site];
            }
            max=Math.max(max,Math.max(groupedMil[bin],groupedRef[bin]));
        }
        final double maxY=Math.min(1.000001,max*1.10);
        for(int k=0;k<=4;k++){
            float y=top+(bottom-top)*k/4f;
            p.setColor(GRID);p.setStrokeWidth(dp(.75f));
            c.drawLine(left,y,right,y,p);
            label(c,String.format(Locale.US,"%.3f",maxY*(4-k)/4.),
                    dp(1),y+dp(3),GRID,9);
        }
        final float width=(right-left)/bins;
        for(int i=0;i<bins;i++){
            final float x=left+i*width;
            float milHeight=(float)Math.min(1.,Math.max(0.,groupedMil[i])/maxY);
            float refHeight=(float)Math.min(1.,Math.max(0.,groupedRef[i])/maxY);
            p.setStyle(Paint.Style.FILL);p.setColor(MIL);
            c.drawRoundRect(new RectF(x+width*.08f,
                    bottom-milHeight*(bottom-top),
                    x+width*.45f,bottom),dp(2),dp(2),p);
            p.setColor(ORACLE);
            c.drawRoundRect(new RectF(x+width*.49f,
                    bottom-refHeight*(bottom-top),
                    x+width*.86f,bottom),dp(2),dp(2),p);
            if(bins<=8||i%2==0){
                String caption=sitesPerBin==1?Integer.toString(i):
                        Integer.toString(i*sitesPerBin);
                label(c,caption,x+width*.18f,bottom+dp(15),TEXT,8);
            }
        }
        label(c,sitesPerBin==1?
                n+" posiciones (una barra por sitio)":
                n+" sitios · "+bins+" grupos · "+sitesPerBin+" por grupo",
                left,bottom+dp(28),TEXT,9);
        label(c,"Verde: MIL  ·  Naranja: RK4 independiente",
                left,bottom+dp(43),TEXT,10);
    }
}
