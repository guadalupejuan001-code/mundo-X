package com.mundox.lab;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.view.View;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.Locale;

/** Bloch-vector projections and all 16 probabilities, directly from the native state. */
final class MicroStateView extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private JSONObject state = new JSONObject();
    private int selected;
    MicroStateView(Context context) { super(context); setContentDescription("Cuatro vectores de Bloch y las dieciséis probabilidades"); }
    void update(JSONObject value,int target) { state=value; selected=target; invalidate(); }
    private float dp(float n){return n*getResources().getDisplayMetrics().density;}
    private void label(Canvas c,String text,float x,float y,int color,float size){
        paint.setStyle(Paint.Style.FILL);paint.setColor(color);paint.setTextSize(dp(size));paint.setTypeface(Typeface.create("sans-serif",Typeface.NORMAL));c.drawText(text,x,y,paint);
    }
    @Override protected void onDraw(Canvas c) {
        super.onDraw(c); float slot=getWidth()/4f,cy=dp(42),r=Math.min(dp(24),slot*.29f);
        JSONArray qs=state.optJSONArray("qubits"),basis=state.optJSONArray("basis");
        for(int q=0;q<4;q++){
            JSONObject v=qs==null?null:qs.optJSONObject(q);float cx=slot*(q+.5f);
            paint.setColor(q==selected?0xffbfe87b:0xff3a535b);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(1));
            c.drawCircle(cx,cy,r,paint);c.drawOval(cx-r,cy-r*.32f,cx+r,cy+r*.32f,paint);c.drawLine(cx,cy-r,cx,cy+r,paint);
            if(v!=null){
                float x=(float)v.optDouble("x"),y=(float)v.optDouble("y"),z=(float)v.optDouble("z");
                float px=cx+r*(.85f*x+.38f*y),py=cy-r*(z-.25f*y);
                paint.setColor(0xffbfe87b);paint.setStrokeWidth(dp(2));c.drawLine(cx,cy,px,py,paint);
                paint.setStyle(Paint.Style.FILL);c.drawCircle(px,py,dp(2.5f),paint);
                label(c,String.format(Locale.US,"S %.2f",v.optDouble("entropy")),cx-dp(18),cy+r+dp(15),0xff9bb1b6,10);
            }
            label(c,"q"+q,cx-dp(7),dp(11),q==selected?0xffbfe87b:0xffdce7df,11);
        }
        float top=dp(103),bottom=dp(156),bar=getWidth()/16f;
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(1));paint.setColor(0xff34484e);c.drawLine(0,bottom,getWidth(),bottom,paint);
        for(int i=0;i<16;i++){
            JSONObject b=basis==null?null:basis.optJSONObject(i);double p=b==null?0:b.optDouble("p"),phase=b==null?0:b.optDouble("phase");
            paint.setStyle(Paint.Style.FILL);paint.setColor(Color.rgb((int)(255*(.55+.45*Math.cos(phase))),
                    (int)(255*(.55+.45*Math.cos(phase-2.0944))),(int)(255*(.55+.45*Math.cos(phase+2.0944)))));
            c.drawRoundRect(i*bar+dp(2),bottom-(float)p*(bottom-top), (i+1)*bar-dp(2),bottom,dp(2),dp(2),paint);
            label(c,Integer.toHexString(i).toUpperCase(Locale.US),i*bar+bar*.24f,bottom+dp(13),0xff9bb1b6,9);
        }
        label(c,"P(estado) · 0–F en hexadecimal · altura 0–100%",0,dp(186),0xff9bb1b6,10);
    }
}
