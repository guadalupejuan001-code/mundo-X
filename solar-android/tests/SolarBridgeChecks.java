package com.mundox.solar;

import java.util.Arrays;
import java.util.concurrent.atomic.AtomicReference;

/** Real shared JNI bridge and native session, not a Java core substitute. */
public final class SolarBridgeChecks {
    static long checks;
    static void check(boolean ok,String name){checks++;if(!ok)throw new AssertionError(name);}
    public static void main(String[] args)throws Exception{
        float[][] local=new float[4][],global=new float[4][];
        for(int preset=0;preset<4;preset++)try(QuantumSolarBridge bridge=new QuantumSolarBridge(preset)){
            local[preset]=bridge.verifyReadOnly();global[preset]=bridge.verifyEtherReadOnly();
            check(Arrays.equals(local[preset],bridge.coefficients()),"native/local disagreement");
            check(Arrays.equals(global[preset],bridge.etherCoefficients()),"native/global disagreement");
            float[] copy=bridge.coefficients();copy[0]=99;check(Arrays.equals(bridge.coefficients(),local[preset]),"mutable state escaped");
        }
        try(QuantumSolarBridge bridge=new QuantumSolarBridge(0)){
            QuantumSolarBridge.Observation original=bridge.observation();
            AtomicReference<Throwable> error=new AtomicReference<>();
            Thread writer=new Thread(()->{try{for(int i=0;i<1200;i++)bridge.setPreset(i%4);}catch(Throwable e){error.set(e);}});
            writer.start();long previous=0;float[] a=new float[8],b=new float[5];
            for(int i=0;i<100000;i++){
                QuantumSolarBridge.Observation o=bridge.observation();o.copyCoefficients(a,b);
                check(o.revision>=previous,"revision moved backwards");previous=o.revision;
                check(Arrays.equals(a,local[o.preset])&&Arrays.equals(b,global[o.preset]),"torn observation across presets");
                check(o.etherRules==2&&o.activeSites==(o.preset==0?0:8),"torn observation metadata");
            }
            writer.join();if(error.get()!=null)throw new AssertionError(error.get());
            original.copyCoefficients(a,b);check(Arrays.equals(a,local[0])&&Arrays.equals(b,global[0]),"retained observation changed");
            check(bridge.observationRevision()==bridge.nativeRevision()&&bridge.observationEpoch()==bridge.nativeEpoch(),"published native revision mismatch");
        }
        for(int i=0;i<1000;i++)try(QuantumSolarBridge bridge=new QuantumSolarBridge(i%4)){
            check(Arrays.equals(bridge.verifyReadOnly(),local[i%4]),"session lifetime changed state");
        }
        System.out.println("SolarBridgeChecks PASS: "+checks+"; 2205 session constructions, 100000 concurrent observations");
    }
}
