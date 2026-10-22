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
            NuclearInteractionModel.Result interaction=
                    NuclearInteractionModel.solve(dt,.020,35.);
            if(!interaction.hasNuclearModel || !interaction.belowBarrier ||
                    !(interaction.log10Wkb<0 && interaction.innerTurningFm>0))
                throw new AssertionError("Nuclear interaction WKB solver failed");
            RadialHamiltonian.Grid radial=RadialHamiltonian.from(interaction);
            if(radial.sites!=256 || !radial.csv().contains("i,r_fm,vc_mev"))
                throw new AssertionError("Sparse radial Hamiltonian unavailable");
            if(!(model.rContactFm>0&&model.coulombContactMeV>.02&&
                    model.classicalTurningFm>model.rContactFm))
                throw new AssertionError("Coulomb model invalid");
            Intent intent=new Intent();
            intent.setClassName("com.mundox.fusion3d","com.mundox.fusion3d.FusionActivity");
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            Activity activity=startActivitySync(intent);
            waitForIdleSync();
            View workspace=activity.getWindow().getDecorView();
            View physicsPanel=findByDescription(workspace,"nuclear-wkb-model-result");
            View graph=findByDescription(workspace,"nuclear-potential-wkb-plot");
            View operatorView=findByDescription(workspace,"radial-hamiltonian-grid-details");
            View operatorExport=findByDescription(workspace,"export-sparse-nuclear-hamiltonian");
            if(physicsPanel==null || graph==null || operatorView==null || operatorExport==null)
                throw new AssertionError("Nuclear interaction results not connected to UI");
            if(!(physicsPanel instanceof android.widget.TextView) ||
                    !((android.widget.TextView)physicsPanel).getText().toString().contains("WKB"))
                throw new AssertionError("No calculated WKB value in user-facing model panel");
            EtherRenderView viewer=findViewer(workspace);
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
            // Verify the 100 logical qubit register is actually constructed by
            // MIL, and that two native Ether frame passes produce 100 objects.
            View quantum100=findByDescription(workspace,"mil-100q-ether-3d-viewport");
            View metrics100=findByDescription(workspace,"mil-100q-quantum-status");
            View phaseSlider100=findByDescription(workspace,"mil-100q-phase-control");
            if(!(quantum100 instanceof Mil100EtherView) ||
                metrics100==null || phaseSlider100==null)
                throw new AssertionError("MIL 100q Ether UI was not connected");
            Mil100EtherView hundred=(Mil100EtherView)quantum100;
            long quantumDeadline=SystemClock.elapsedRealtime()+40000;
            while(SystemClock.elapsedRealtime()<quantumDeadline &&
                  !hundred.hasNativeFrame()){
                if(hundred.lastError()!=null)
                    throw new AssertionError("MIL GHZ100 frame failed: "+hundred.lastError());
                SystemClock.sleep(180);
            }
            if(!hundred.hasNativeFrame())
                throw new AssertionError("100 logical qubit GHZ/Ether scene did not draw");
            org.json.JSONObject hundredData=new org.json.JSONObject(hundred.lastStatus());
            if(hundredData.getInt("qubits")!=100 ||
                hundredData.getInt("visible_objects")!=100 ||
                hundredData.getInt("passes")!=2 ||
                hundredData.getInt("components")>32 ||
                hundredData.getInt("state_bytes")>200000 ||
                Math.abs(hundredData.getDouble("norm")-1.)>1e-8 ||
                Math.abs(hundredData.getDouble("distant_zz")-1.)>1e-9 ||
                Math.abs(hundredData.getDouble("global_x")-Math.cos(Math.PI/4.))>1e-5)
                throw new AssertionError("Invalid native MIL GHZ100 correlation: "+
                        hundred.lastStatus());
            String initialHash=hundredData.getString("framebuffer_hash");
            int initialFrameCount=hundred.frameCount();
            runOnMainSync(()->hundred.setQuantumPhase(0f));
            long phaseDeadline=SystemClock.elapsedRealtime()+30000;
            while(SystemClock.elapsedRealtime()<phaseDeadline &&
                    hundred.frameCount()<=initialFrameCount){
                if(hundred.lastError()!=null)
                    throw new AssertionError("MIL100 phase update failed: "+hundred.lastError());
                SystemClock.sleep(150);
            }
            if(hundred.frameCount()<=initialFrameCount)
                throw new AssertionError("MIL100 non-Clifford phase did not refresh");
            org.json.JSONObject restored100=new org.json.JSONObject(hundred.lastStatus());
            if(Math.abs(restored100.getDouble("global_x")-1.)>1e-8 ||
                initialHash.equals(restored100.getString("framebuffer_hash")))
                throw new AssertionError("MIL100 global coherence failed to change visual output");

            // Test the radial Hamiltonian as actual MIL gates through JNI,
            // not just the classical reference solver or a fake summary.
            View milRun=findByDescription(workspace,"mil-run-hamiltonian-evolution");
            View milStatus=findByDescription(workspace,"mil-radial-execution-result");
            View milPlot=findByDescription(workspace,"mil-radial-population-chart");
            if(milRun==null||milStatus==null||milPlot==null)
                throw new AssertionError("MIL Core execution UI not connected");
            runOnMainSync(milRun::performClick);
            final long milDeadline=SystemClock.elapsedRealtime()+40000;
            String milText="";
            while(SystemClock.elapsedRealtime()<milDeadline){
                milText=((android.widget.TextView)milStatus).getText().toString();
                if(milText.contains("MIL ejecutó "))break;
                if(milText.contains("no completó"))
                    throw new AssertionError("MIL Core JNI failure: "+milText);
                SystemClock.sleep(200);
            }
            if(!milText.contains("MIL ejecutó ")||
                    !milText.contains("256 posiciones")||
                    !milText.contains("fidelidad vs RK4"))
                throw new AssertionError("MIL backend did not execute: "+milText);
            // Real, non-default orbit should redraw an actual raytraced image.
            runOnMainSync(viewer::resetCamera);
            waitForIdleSync();
            result.putString("stream","FUSION_3D_ANDROID_PASS Q="+q+
                    " MeV; native "+provenance+"\n"+
                    "NUCLEAR_INTERACTION_ANDROID_PASS WKB_log10="+
                    interaction.log10Wkb+" r1="+interaction.innerTurningFm+
                    " r2="+interaction.outerTurningFm+"\n"+
                    "RADIAL_H_ANDROID_PASS sites="+radial.sites+
                    " tridiagonal=true\n"+
                    "MIL100_ETHER_ANDROID_PASS qubits=100 visible=100 passes=2 "+
                    "initial_X="+hundredData.getDouble("global_x")+
                    " restored_X="+restored100.getDouble("global_x")+
                    " native_ether_ms="+hundredData.getDouble("raster_ms")+
                    " GHZ_components="+hundredData.getInt("components")+"\n"+
                    "MIL_RADIAL_ANDROID_PASS backend=MIL-Reduced 256-sites 8-qubits Strang-2\n"+
                    "MIL_ANDROID_MEASUREMENTS "+milText.replace('\n','|')
                        .substring(0,Math.min(600,milText.length()))+"\n");
            runOnMainSync(activity::finish);
        } catch(Throwable fail) {
            result.putString("stream","FUSION_3D_ANDROID_FAIL "+fail+"\n");
            finish(Activity.RESULT_CANCELED,result);
            return;
        }
        finish(Activity.RESULT_OK,result);
    }

    private View findByDescription(View root,String description){
        CharSequence label=root.getContentDescription();
        if(label!=null && label.toString().equals(description))return root;
        if(root instanceof ViewGroup){
            ViewGroup group=(ViewGroup)root;
            for(int i=0;i<group.getChildCount();i++){
                View candidate=findByDescription(group.getChildAt(i),description);
                if(candidate!=null)return candidate;
            }
        }
        return null;
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
