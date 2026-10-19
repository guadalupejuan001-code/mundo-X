package com.mundox.lab;

/** JNI adapter for the original eight-bit CompactCore experiment. */
public final class BasisVmNative {
    static { System.loadLibrary("mundox_basis"); }
    private BasisVmNative() { }
    public static native String basisVmRun(String source, int maxSteps);
    public static native String basisVmSelfTest();
    public static native String basisVmExample(int index);
}
