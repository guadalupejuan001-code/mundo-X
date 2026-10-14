package com.mundox.lab;

public final class NativeCore {
    static {
        System.loadLibrary("mundox");
    }

    private NativeCore() {}

    public static native String hello();
    public static native String bench(int qubits, boolean includeDense);
    public static native String cluster(int rows, int cols, boolean includeDense);
    public static native String practice(int qubits, String script);
}
