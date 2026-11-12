package com.mundox.ghzwatch;

public final class NativeGhz {
    static { System.loadLibrary("mundox_ghz"); }
    private NativeGhz() { }
    // Serialized internally by the native owner. 0 prepare, 1 S, 2 Z,
    // 3 release quantum payload, 4 read the current report without mutation.
    public static native String command(int operation, int qubits, long seed);
}
