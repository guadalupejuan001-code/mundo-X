package com.mundox.cosmic;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Small platform-only visual vocabulary for the new Android host. */
final class CosmicUi {
    static final int BG=0xff080d18, PANEL=0xff121c2e, RAISED=0xff1a2940;
    static final int TEXT=0xfff1f4fc, MUTED=0xff98a9c5, LINE=0xff25354d;
    static final int CYAN=0xff75e5df, VIOLET=0xffb3a3ff, GOLD=0xffffcf8b;
    final Context context;
    CosmicUi(Context context){this.context=context;}
    int dp(float value){return Math.round(value*context.getResources().getDisplayMetrics().density);}
    TextView text(String value,float size,int color,boolean bold){
        TextView view=new TextView(context);view.setText(value);view.setTextSize(size);view.setTextColor(color);
        view.setTypeface(Typeface.create(bold?"sans-serif-medium":"sans-serif",Typeface.NORMAL));
        view.setIncludeFontPadding(false);return view;
    }
    TextView eyebrow(String value,int color){TextView t=text(value,10,color,true);t.setLetterSpacing(.14f);return t;}
    LinearLayout column(){LinearLayout box=new LinearLayout(context);box.setOrientation(LinearLayout.VERTICAL);return box;}
    LinearLayout row(){LinearLayout box=new LinearLayout(context);box.setGravity(Gravity.CENTER_VERTICAL);return box;}
    GradientDrawable shape(int color,int radius,int stroke){
        GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(radius));
        if(stroke!=0)d.setStroke(dp(1),stroke);return d;
    }
    void clickable(View view,int color,int radius){
        view.setBackground(new RippleDrawable(ColorStateList.valueOf(0x3375e5df),shape(color,radius,LINE),shape(0xffffffff,radius,0)));
    }
    Button button(String label,String tag,boolean primary,Runnable action){
        Button b=new Button(context);b.setText(label);b.setTag(tag);b.setAllCaps(false);b.setTextSize(12);
        b.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));b.setTextColor(primary?BG:TEXT);
        b.setMinWidth(0);b.setMinimumWidth(0);b.setMinHeight(dp(46));b.setMinimumHeight(dp(46));b.setPadding(dp(12),dp(8),dp(12),dp(8));
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(primary?0x33080d18:0x3375e5df),
            shape(primary?CYAN:RAISED,13,primary?0:LINE),shape(0xffffffff,13,0)));
        b.setStateListAnimator(null);b.setOnClickListener(v->action.run());return b;
    }
    LinearLayout.LayoutParams full(){return new LinearLayout.LayoutParams(-1,-2);}
    LinearLayout.LayoutParams weight(){return new LinearLayout.LayoutParams(0,-2,1);}
    View space(int height){View v=new View(context);v.setLayoutParams(new LinearLayout.LayoutParams(1,dp(height)));return v;}
    void weightedButton(LinearLayout row,Button b,boolean first){LinearLayout.LayoutParams p=weight();p.height=dp(46);if(!first)p.leftMargin=dp(8);row.addView(b,p);}

    /** Original vector glyphs, drawn at device resolution instead of bundled bitmap mockups. */
    static final class Glyph extends View {
        private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        private final String kind;
        private int color;
        Glyph(Context context,String kind,int color){super(context);this.kind=kind;this.color=color;setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
        void color(int value){color=value;invalidate();}
        @Override protected void onDraw(Canvas canvas){
            super.onDraw(canvas);float s=Math.min(getWidth(),getHeight());
            canvas.save();canvas.translate((getWidth()-s)/2,(getHeight()-s)/2);canvas.scale(s/32,s/32);
            paint.setColor(color);paint.setStrokeWidth(1.7f);paint.setStyle(Paint.Style.STROKE);paint.setStrokeCap(Paint.Cap.ROUND);paint.setStrokeJoin(Paint.Join.ROUND);
            if(kind.equals("motor")||kind.equals("objetos")||kind.equals("gpu")||kind.equals("juego")){
                Path p=new Path();p.moveTo(16,5);p.lineTo(26,11);p.lineTo(26,22);p.lineTo(16,28);p.lineTo(6,22);p.lineTo(6,11);p.close();
                canvas.drawPath(p,paint);canvas.drawLine(6,11,16,17,paint);canvas.drawLine(26,11,16,17,paint);canvas.drawLine(16,17,16,28,paint);
            }else if(kind.equals("archivo")){
                Path p=new Path();p.moveTo(5,10);p.lineTo(5,25);p.lineTo(27,25);p.lineTo(27,9);p.lineTo(16,9);p.lineTo(12,5);p.lineTo(5,5);p.close();canvas.drawPath(p,paint);canvas.drawLine(11,16,22,16,paint);canvas.drawLine(11,20,18,20,paint);
            }else if(kind.equals("labs")||kind.equals("vm8")||kind.equals("clasico")){
                for(int j=0;j<2;j++)for(int i=0;i<2;i++)canvas.drawRoundRect(new RectF(5+i*13,5+j*13,14+i*13,14+j*13),2,2,paint);
            }else if(kind.equals("micromundo")){
                canvas.drawOval(new RectF(5,9,26,24),paint);canvas.drawCircle(12,14,2,paint);canvas.drawCircle(20,19,2,paint);
                for(int i=0;i<6;i++){double a=i*Math.PI/3;canvas.drawLine(16+(float)Math.cos(a)*11,16+(float)Math.sin(a)*9,16+(float)Math.cos(a)*14,16+(float)Math.sin(a)*12,paint);}
            }else if(kind.equals("ecos")||kind.equals("visual")||kind.equals("red")||kind.equals("sincronizada")){
                Path p=new Path();for(int i=3;i<=29;i++){float y=16+(float)Math.sin(i*.55)*7;if(i==3)p.moveTo(i,y);else p.lineTo(i,y);}canvas.drawPath(p,paint);canvas.drawLine(3,25,29,25,paint);
            }else{
                canvas.save();canvas.rotate(-30,16,16);canvas.drawOval(new RectF(3,10,29,22),paint);canvas.rotate(60,16,16);canvas.drawOval(new RectF(3,10,29,22),paint);canvas.restore();
                paint.setStyle(Paint.Style.FILL);canvas.drawCircle(16,16,2.4f,paint);canvas.drawCircle(26,9,2,paint);
            }
            canvas.restore();
        }
    }
}
