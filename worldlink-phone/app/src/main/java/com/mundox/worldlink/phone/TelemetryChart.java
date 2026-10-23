package com.mundox.worldlink.phone;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.View;
import java.util.Locale;

/** Live sensor sparkline. Samples are real watch measurements, never generated values. */
final class TelemetryChart extends View {
    private static final int SIZE = 90;
    private final float[] samples = new float[SIZE];
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private int count, next;
    private String quantity = "MAGNETISMO";
    private String unit = "µT";
    private int lineColor = 0xffc2ef65;
    private float last;

    TelemetryChart(Context context) {
        super(context);
        setMinimumHeight(Math.round(155 * getResources().getDisplayMetrics().density));
        setLayerType(View.LAYER_TYPE_SOFTWARE,null);
    }

    void setSignal(String name, String unit, int lineColor) {
        this.quantity = name;
        this.unit = unit;
        this.lineColor = lineColor;
        count = 0;
        next = 0;
        invalidate();
    }

    void append(float value) {
        if (!Float.isFinite(value) || value < 0) return;
        samples[next] = value;
        next = (next + 1) % SIZE;
        count = Math.min(SIZE,count + 1);
        last = value;
        invalidate();
    }

    @Override protected void onDraw(Canvas c) {
        super.onDraw(c);
        float w = getWidth(), h = getHeight(), d = getResources().getDisplayMetrics().density;
        c.drawColor(0xff192822);
        paint.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        paint.setTextSize(11f*d);
        paint.setColor(0xffaabdb0);
        c.drawText(quantity + " · SEÑAL REAL",14*d,20*d,paint);
        paint.setTextSize(15f*d);
        paint.setColor(0xffeaf3ed);
        String number = count == 0 ? "—" : String.format(Locale.US,"%.1f %s",last,unit);
        c.drawText(number,14*d,42*d,paint);
        float l=14*d,r=w-14*d,t=58*d,b=h-13*d;
        paint.setStrokeWidth(d);
        paint.setColor(0xff32463a);
        for (int i=0;i<=3;i++) {
            float y=t+(b-t)*i/3f;
            c.drawLine(l,y,r,y,paint);
        }
        if (count<2) {
            paint.setTextSize(12*d);
            paint.setColor(0xffaabdb0);
            c.drawText("Inicia una sesión en el reloj",l,t+(b-t)*.55f,paint);
            return;
        }
        float min=Float.POSITIVE_INFINITY,max=Float.NEGATIVE_INFINITY;
        for (int i=0;i<count;i++) {
            int idx=(next-count+i+SIZE)%SIZE;
            min=Math.min(min,samples[idx]);
            max=Math.max(max,samples[idx]);
        }
        float range=Math.max(1f,max-min);
        float pad=range*.12f;
        min-=pad;max+=pad;
        paint.setColor(lineColor);
        paint.setStrokeWidth(2.6f*d);
        paint.setStrokeCap(Paint.Cap.ROUND);
        for (int i=1;i<count;i++) {
            float prev=samples[(next-count+i-1+SIZE)%SIZE];
            float cur=samples[(next-count+i+SIZE)%SIZE];
            float x1=l+(r-l)*(i-1)/(SIZE-1);
            float x2=l+(r-l)*i/(SIZE-1);
            float y1=b-(prev-min)/(max-min)*(b-t);
            float y2=b-(cur-min)/(max-min)*(b-t);
            c.drawLine(x1,y1,x2,y2,paint);
        }
    }
}