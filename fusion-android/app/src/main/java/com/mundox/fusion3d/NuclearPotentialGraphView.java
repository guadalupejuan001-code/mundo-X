package com.mundox.fusion3d;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.View;

import com.cosmicx.pc.FusionPhysics;

import java.util.Locale;

/**
 * Plots REAL values of a clearly-labelled phenomenological potential.
 * It does NOT claim these Woods-Saxon parameters are experimentally fitted.
 */
public final class NuclearPotentialGraphView extends View {
    private static final int BG=Color.rgb(11,21,33);
    private static final int CYAN=Color.rgb(101,233,206);
    private static final int COULOMB=Color.rgb(255,186,114);
    private static final int TOTAL=Color.rgb(190,162,250);
    private static final int WAVE=Color.rgb(235,245,250);
    private static final int MUTED=Color.rgb(159,184,199);
    private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path curve=new Path();
    private NuclearInteractionModel.Result model;
    private boolean detailedBarrier=true;
    private final float density,scale;

    public NuclearPotentialGraphView(Context context) {
        super(context);
        density=getResources().getDisplayMetrics().density;
        scale=getResources().getDisplayMetrics().scaledDensity;
        setContentDescription("Gráfica numérica de potencial nuclear fenomenológico "+
                "y Coulomb más energía. Zona sub-barrera sombreada; no sección eficaz.");
    }

    public void setResult(NuclearInteractionModel.Result result){
        model=result;
        invalidate();
    }
    public void setBarrierZoom(boolean zoom){
        detailedBarrier=zoom;
        invalidate();
    }
    public boolean isBarrierZoom(){return detailedBarrier;}

    private float dp(double val){return (float)(val*density);}
    private void text(Canvas c,String value,float x,float y,int color,float sp){
        p.setStyle(Paint.Style.FILL);
        p.setColor(color);
        p.setTextSize(sp*scale);
        c.drawText(value,x,y,p);
    }
    private void stroke(int color,float dpWidth){
        p.setColor(color);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(dp(dpWidth));
        p.setPathEffect(null);
    }
    private static String fmt(double x){
        return String.format(Locale.US,Math.abs(x)<.1?"%.3g":"%.2f",x);
    }

    @Override protected void onDraw(Canvas c){
        super.onDraw(c);
        c.drawColor(BG);
        final float w=getWidth(),h=getHeight();
        if(w<dp(240)||h<dp(170))return;
        final float left=dp(42),right=w-dp(13);
        final float top=dp(38),bottom=h-dp(45);
        text(c,detailedBarrier?"INTERACCIÓN · DETALLE DE BARRERA":
                "INTERACCIÓN · POZO COMPLETO",
                dp(12),dp(18),WAVE,11);

        if(model==null||!model.hasNuclearModel){
            text(c,"Sin potencial nuclear para esta pareja.",left,h*.52f,MUTED,11);
            text(c,"No se inventan fuerzas ni productos.",left,h*.62f,MUTED,10);
            return;
        }
        FusionPhysics.Analysis pair=model.isotopes;
        final double contact=model.contactRadiusFm;
        final double rmin=detailedBarrier ? contact+2.2 : contact*.58;
        final double rmax=detailedBarrier ?
                Math.max(contact+17,model.barrierPeakRadiusFm+9.0):
                Math.max(contact+23,model.barrierPeakRadiusFm+13.0);

        // A zoom-in near the top shows the small fusion barrier separately
        // from the tens-of-MeV negative interior well.
        final double vmin=detailedBarrier ?
                Math.min(-1.0,NuclearInteractionModel.totalPotentialMeV(
                        pair,rmin,model.nuclearDepthMeV)*1.25) :
                -model.nuclearDepthMeV*1.12;
        final double vmax=detailedBarrier ?
                Math.max(.65,model.barrierPeakMeV*1.7) :
                Math.max(1.8,model.barrierPeakMeV*2.1);
        final double scaleY=(bottom-top)/(vmax-vmin);

        for(int i=0;i<=4;i++){
            final float yy=top+(bottom-top)*i/4f;
            stroke(Color.rgb(35,57,75),1);
            c.drawLine(left,yy,right,yy,p);
            double val=vmax-(vmax-vmin)*i/4.;
            text(c,fmt(val),dp(3),yy+dp(4),MUTED,10);
        }

        if(model.belowBarrier){
            double lower=Math.max(rmin,model.innerTurningFm);
            double upper=Math.min(rmax,model.outerTurningFm);
            if(upper>lower){
                float xL=(float)(left+(lower-rmin)/(rmax-rmin)*(right-left));
                float xR=(float)(left+(upper-rmin)/(rmax-rmin)*(right-left));
                p.setStyle(Paint.Style.FILL);
                p.setColor(Color.argb(40,255,182,90));
                c.drawRect(xL,top,xR,bottom,p);
            }
        }

        for(int which=0;which<3;which++){
            stroke(which==0?COULOMB:(which==1?CYAN:TOTAL),
                    which==2?dp(0.0f)+2.5f:1.9f);
            curve.reset();
            for(int i=0;i<=210;i++){
                double radius=rmin+(rmax-rmin)*i/210.;
                double potential=which==0 ?
                        NuclearInteractionModel.coulombPotentialMeV(pair,radius):
                        which==1 ?
                                NuclearInteractionModel.nuclearPotentialMeV(
                                        pair,radius,model.nuclearDepthMeV) :
                                NuclearInteractionModel.totalPotentialMeV(
                                        pair,radius,model.nuclearDepthMeV);
                float x=(float)(left+(right-left)*i/210.);
                float yy=(float)(bottom-(potential-vmin)*scaleY);
                yy=Math.max(top-dp(1),Math.min(bottom+dp(1),yy));
                if(i==0)curve.moveTo(x,yy);else curve.lineTo(x,yy);
            }
            c.drawPath(curve,p);
        }
        if(model.energyMeV>=vmin && model.energyMeV<=vmax) {
            final float energyY=(float)(bottom-(model.energyMeV-vmin)*scaleY);
            stroke(WAVE,1.4f);
            p.setPathEffect(new DashPathEffect(new float[]{dp(6),dp(4)},0));
            c.drawLine(left,energyY,right,energyY,p);
            p.setPathEffect(null);
            text(c,"E",right-dp(11),Math.max(top+dp(9),energyY-dp(4)),WAVE,10);
        }
        stroke(Color.rgb(82,107,124),1);
        c.drawLine(left,top,left,bottom,p);
        c.drawLine(left,bottom,right,bottom,p);

        text(c,"fm",right-dp(13),h-dp(8),MUTED,10);
        text(c,fmt(rmin),left-dp(6),bottom+dp(17),MUTED,10);
        text(c,fmt(rmax),right-dp(24),bottom+dp(17),MUTED,10);
        // Color legend and units are part of the model provenance.
        text(c,"C: Coulomb",left,bottom+dp(34),COULOMB,9);
        text(c,"N: Woods-Saxon",left+dp(77),bottom+dp(34),CYAN,9);
        text(c,"C+N",right-dp(35),bottom+dp(34),TOTAL,9);
    }
}
