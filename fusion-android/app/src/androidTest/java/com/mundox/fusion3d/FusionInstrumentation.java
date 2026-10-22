package com.mundox.fusion3d;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;

import com.cosmicx.pc.FusionPhysics;

/** Instrumentation of the native 3D viewer and scientific calculation on Android. */
public final class FusionInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle args) {
        super.onCreate(args);
        start();
    }
    @Override public void onStart() {
        super.onStart();
        Bundle result=new Bundle();
        try {
            FusionPhysics.Analysis dt=FusionPhysics.analyze(1,2,1,3);
            if(dt.documentedChannels.size()!=1)throw new AssertionError("D-T channel absent");
            double q=dt.documentedChannels.get(0).qMeV;
            if(Math.abs(q-17.5893)>.005)throw new AssertionError("D-T Q invalid: "+q);
            CollisionModel.Result model=CollisionModel.calculate(dt,.020);
            if(!(model.rContactFm>0&&model.coulombContactMeV>.02&&
                    model.classicalTurningFm>model.rContactFm))
                throw new AssertionError("Coulomb model invalid");
            Intent intent=new Intent();
            intent.setClassName("com.mundox.fusion3d","com.mundox.fusion3d.FusionActivity");
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            Activity activity=startActivitySync(intent);
            waitForIdleSync();
            EtherRenderView viewer=findViewer(activity.getWindow().getDecorView());
            if(viewer==null)throw new AssertionError("3D native Ether view missing");
            long deadline=SystemClock.elapsedRealtime()+15000;
            while(SystemClock.elapsedRealtime()<deadline && !viewer.hasNativeFrame()){
                SystemClock.sleep(150);
            }
            if(!viewer.hasNativeFrame())
                throw new AssertionError("native EtherRenderer failed to generate a frame");
            String provenance=viewer.provenance();
            if(!provenance.contains("EtherRenderer")||!provenance.contains("escena 344x224"))
                throw new AssertionError("noncanonical renderer: "+provenance);
            // Real, non-default orbit should redraw an actual raytraced image.
            runOnMainSync(viewer::resetCamera);
            waitForIdleSync();
            result.putString("stream","FUSION_3D_ANDROID_PASS Q="+q+
                    " MeV; native "+provenance+"\n");
            runOnMainSync(activity::finish);
        } catch(Throwable fail) {
            result.putString("stream","FUSION_3D_ANDROID_FAIL "+fail+"\n");
            finish(Activity.RESULT_CANCELED,result);
            return;
        }
        finish(Activity.RESULT_OK,result);
    }

    private EtherRenderView findViewer(View root){
        if(root instanceof EtherRenderView)return (EtherRenderView)root;
        if(root instanceof ViewGroup){
            ViewGroup group=(ViewGroup)root;
            for(int i=0;i<group.getChildCount();i++){
                EtherRenderView found=findViewer(group.getChildAt(i));
                if(found!=null)return found;
            }
        }
        return null;
    }
}
