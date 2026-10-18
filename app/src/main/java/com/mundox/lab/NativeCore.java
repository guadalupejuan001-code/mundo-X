package com.mundox.lab;

public final class NativeCore {
    static {
        System.loadLibrary("mundox");
    }

    private NativeCore() {}

    public static native String hello();
    public static native String teleport();
    public static native String tExperiment(int qubits, int tGates);
    public static native String tReducedExperiment(int qubits, int tGates);
    public static native String magicWitness();
    public static native String tTraceVariant(int qubits, int tGates);
    public static native String bench(int qubits, boolean includeDense);
    public static native String cluster(int rows, int cols, boolean includeDense);
    public static native String practice(int qubits, String script);
    public static native float[] quantumVisualStep(int touchQubit, boolean reset);
    public static native String labCircuit(int qubits, String script, int steps, int shots, long seed);
    public static native String labProtocol(int protocol, int value, int shots, long seed);
    public static native String labMemoryRun(int budgetMiB, int qubits, int family);
    public static native String labMemoryProgress();
    public static native void labMemoryCancel();
}
