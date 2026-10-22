package com.cosmicx.pc;

/**
 * Native, independent Mundo X quantum computer session.
 *
 * Unlike the x86 guest, this talks directly to QuantumRuntime:
 * InfiniteCore + Inverse Ether Memory. Programs use XI (.xi) commands.
 */
public final class QuantumWorkstation {
    static {
        System.loadLibrary("cosmic_pc");
    }

    private QuantumWorkstation() { }

    public static native long create();
    public static native void close(long workstation);
    public static native String execute(long workstation, String xiProgram);
    /** Standalone XASM Reduced/Compact execution; does not mutate the XI session. */
    public static native String executeXasm(String xasmProgram);
    public static native String status(long workstation);
    public static native void reset(long workstation);
    /** Atomic export pair: [InfiniteCore MXI snapshot, Inverse Ether Memory snapshot]. */
    public static native String[] snapshot(long workstation);
    /** Restores both snapshots transactionally without changing an invalid session. */
    public static native void restore(long workstation, String core, String memory);
}
