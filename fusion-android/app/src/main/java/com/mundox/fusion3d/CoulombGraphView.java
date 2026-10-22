package com.mundox.fusion3d;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;

import java.util.Locale;

/** Plot of a measured-dimension Coulomb formula, not a measured cross section. */
public final class CoulombGraphView extends View {
    private static final int BG=Color.rgb(13,24,36);
    private static final int WHITE=Color.rgb(225,244,247);
    private static final int MUTED=Color.rgb(135,166,190);
    private static final int LINE=Color.rgb(113,231,209);
    private static final int ENERGY=Color.rgb(246,176,107);
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path curve=new Path();
    private CollisionModel.Result data;
    private float density,scale;

    public CoulombGraphView(Context context) {
        super(context);
        density=getResources().getDisplayMetrics().density;
        scale=getResources().getDisplayMetrics().scaledDensity;
        setContentDescription("Gráfica V(r) de Coulomb con línea de energía de colisión: modelo electrostático, no sección eficaz");
    }

    public void setData(CollisionModel.Result updated){
        data=updated;
        invalidate();
    }
    private float dp(float value){return density*value;}
    private void stroke(int color,float width){
        paint.setColor(color);
        paint.setStrokeWidth(width* density);
        paint.setStyle(Paint.Style.STROKE);
    }
    private void text(Canvas c,String s,float x,float y,int color,float size){
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(color);
        paint.setTextSize(size*scale);
        c.drawText(s,x,y,paint);
    }
    private static String fmt(double d){
        return String.format(Locale.US,d>999?"%.0f":d>9?"%.2f":"%.3f",d);
    }
    @Override protected void onDraw(Canvas canvas){
        super.onDraw(canvas);
        canvas.drawColor(BG);
        float w=getWidth(),h=getHeight();
        if(w<dp(230)||h<dp(135))return;
        float left=dp(46),right=w-dp(18),top=dp(26),bottom=h-dp(40);
        stroke(Color.rgb(48,68,86),1);
        for(int i=0;i<=4;i++){
            float y=top+i*(bottom-top)/4;
            canvas.drawLine(left,y,right,y,paint);
        }
        canvas.drawLine(left,top,left,bottom,paint);
        canvas.drawLine(left,bottom,right,bottom,paint);
        text(canvas,"V(r) [MeV]",dp(12),dp(17),WHITE,11);
        text(canvas,"r [fm]",right-dp(36),h-dp(11),MUTED,10);
        if(data==null){text(canvas,"Elige isótopos para ver la barrera.",left+dp(8),h*.5f,MUTED,11);return;}

        // Show the external region starting AT the nuclear-contact estimate.
        // Nuclear force at r < R is deliberately not plotted: Coulomb-only
        // modelling there would present an unphysical picture.
        final double r0=data.rContactFm;
        final double rmax=8.0*r0;
        final double vmax=data.coulombContactMeV*1.12;
        final double yscale=(bottom-top)/vmax;

        curve.reset();
        stroke(LINE,2);
        for(int i=0;i<=160;i++){
            double radius=r0+(rmax-r0)*i/160.;
            double V=CollisionModel.potentialMeV(data.isotopes,radius);
            float x=left+(right-left)*i/160f;
            float y=(float)(bottom - V*yscale);
            if(i==0)curve.moveTo(x,y);else curve.lineTo(x,y);
        }
        canvas.drawPath(curve,paint);

        final double requested=data.eCmMeV;
        if(requested<=vmax){
            float eY=(float)(bottom-requested*yscale);
            stroke(ENERGY,1.8f);
            paint.setPathEffect(new android.graphics.DashPathEffect(new float[]{dp(5),dp(5)},0));
            canvas.drawLine(left,eY,right,eY,paint);
            paint.setPathEffect(null);
            text(canvas,"Ecm",right-dp(36),Math.max(top+dp(12),eY-dp(7)),ENERGY,10);
        }else{
            text(canvas,"Ecm por encima de la escala",left+dp(7),top+dp(14),ENERGY,10);
        }

        // r_min from the purely classical HEAD-ON two point-charge model.
        if(data.classicalTurningFm>=r0&&data.classicalTurningFm<=rmax){
            float turnX=(float)(left+(right-left)*(data.classicalTurningFm-r0)/(rmax-r0));
            stroke(ENERGY,1);
            canvas.drawLine(turnX,top,turnX,bottom,paint);
            text(canvas,"r_min",Math.max(left+dp(1),turnX-dp(16)),bottom+dp(14),ENERGY,10);
        }
        text(canvas,fmt(vmax),dp(5),top+dp(3),MUTED,10);
        text(canvas,"0",dp(30),bottom+dp(2),MUTED,10);
        text(canvas,fmt(r0),left-dp(5),bottom+dp(24),MUTED,10);
        text(canvas,fmt(rmax),right-dp(20),bottom+dp(24),MUTED,10);
        text(canvas,"Contact",left+dp(5),top+dp(13),MUTED,9);
    }
}
