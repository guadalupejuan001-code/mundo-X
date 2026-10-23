package com.mundox.worldlink.phone;

/** The physical model uses classical bytes in the original Ether memory; no simulated qubits. */
final class NativeFieldRule implements FieldExperiment.RuleStore {
    static {System.loadLibrary("worldlink_quantum");}
    private static native void nativeStore(double[] parameters);
    private static native double[] nativeLoad();
    public void save(double[] parameters){nativeStore(parameters);}
    public double[] load(){return nativeLoad();}
    public String name(){return "canonical_InverseMemory_COW256_classical_48_bytes";}
}
