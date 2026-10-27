package com.mundox.laplace;

import android.app.Instrumentation;
import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.util.Arrays;

/** Real device/emulator test for the standalone Android application. */
public final class LaplaceInstrumentation extends Instrumentation {
    private static void need(boolean ok,String message){if(!ok)throw new AssertionError(message);}
    private Button findButton(View root,String text){
        if(root instanceof Button&&((Button)root).getText().toString().contains(text))return (Button)root;
        if(root instanceof ViewGroup){ViewGroup group=(ViewGroup)root;
            for(int i=0;i<group.getChildCount();i++){
                Button b=findButton(group.getChildAt(i),text);if(b!=null)return b;
            }
        }return null;
    }
    private boolean contains(View root,String term){
        if(root instanceof TextView&&((TextView)root).getText().toString().contains(term))return true;
        if(root instanceof ViewGroup){ViewGroup g=(ViewGroup)root;
            for(int i=0;i<g.getChildCount();i++)if(contains(g.getChildAt(i),term))return true;
        }return false;
    }
    private void click(MainActivity activity,String text){
        runOnMainSync(()->{
            Button b=findButton(activity.getWindow().getDecorView(),text);
            need(b!=null,"Missing button: "+text);
            need(b.isEnabled(),"Button disabled: "+text);
            b.performClick();
        });
    }
    private void waitUntil(java.util.function.BooleanSupplier condition,String reason)throws Exception{
        for(int i=0;i<130;i++){
            if(condition.getAsBoolean())return;
            Thread.sleep(130);
        }
        throw new AssertionError("Timed out: "+reason);
    }
    private void screenshot(String name)throws Exception{
        Bitmap image=getUiAutomation().takeScreenshot();
        need(image!=null,"Screenshot missing: "+name);
        File dir=new File(getTargetContext().getExternalFilesDir(null),"laplace-proof");
        need(dir.isDirectory()||dir.mkdirs(),"Cannot save visual evidence");
        try(FileOutputStream out=new FileOutputStream(new File(dir,name+".png"))){
            need(image.compress(Bitmap.CompressFormat.PNG,100,out),"Image could not be saved");
        }finally{image.recycle();}
    }
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    @Override public void onStart(){
        super.onStart();Bundle output=new Bundle();
        try{
            PackageInfo packageInfo=getTargetContext().getPackageManager().getPackageInfo("com.mundox.laplace",PackageManager.GET_PERMISSIONS);
            need(packageInfo.requestedPermissions==null||
                 !Arrays.asList(packageInfo.requestedPermissions).contains("android.permission.INTERNET"),
                 "Standalone Laplace must not request INTERNET");
            Intent app=getTargetContext().getPackageManager().getLaunchIntentForPackage("com.mundox.laplace");
            need(app!=null,"Standalone launcher not found");
            app.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            Activity launched=startActivitySync(app);
            need(launched instanceof MainActivity,"Launcher opened unexpected Activity");
            MainActivity activity=(MainActivity)launched;
            need(activity.isLabStandalone(),"This is not the independent lab");
            need(activity.currentTabForTesting().equals("CAMPO"),"Initial equation screen missing");
            click(activity,"Resolver ecuación");
            waitUntil(activity::isMeasuredFieldReadyForTesting,"calculated harmonic field");
            need(activity.measuredResidualForTesting()<2e-7,"Field does not solve discrete Laplace");
            need(contains(activity.getWindow().getDecorView(),"RESIDUO MÁXIMO"),"Measured residual not displayed");
            screenshot("campo");
            runOnMainSync(()->activity.openTabForTesting(1));
            need(contains(activity.getWindow().getDecorView(),"Ganancia de precisión"),"Convergence screen missing");
            click(activity,"Ejecutar auditoría independiente");
            waitUntil(activity::auditPassedForTesting,"on-device independent audit");
            need(contains(activity.getWindow().getDecorView(),"GANANCIA EN PRECISIÓN"),"No measured gain displayed");
            screenshot("convergencia");
            runOnMainSync(()->activity.openTabForTesting(2));
            need(contains(activity.getWindow().getDecorView(),"Demonio de Laplace"),"Determinism screen missing");
            need(contains(activity.getWindow().getDecorView(),"250 ¢"),"Unknown event not accounted for");
            screenshot("determinismo");
            runOnMainSync(()->activity.openTabForTesting(3));
            need(contains(activity.getWindow().getDecorView(),"APROBADA"),"Audit not shown");
            need(findButton(activity.getWindow().getDecorView(),"Exportar CSV")!=null,"CSV export not available");
            screenshot("resultados");
            output.putString("stream","LAPLACE_X_ANDROID_VERIFIED equation=2D_Laplace screens=4 numerical_field=true measured_error=true audit=true standalone=true internet=false\n");
            output.putBoolean("passed",true);
            finish(Activity.RESULT_OK,output);
        }catch(Throwable failure){
            output.putString("stream","LAPLACE_X_ANDROID_FAILED: "+failure+"\n");
            output.putString("error",failure.toString());
            finish(Activity.RESULT_CANCELED,output);
        }
    }
}
