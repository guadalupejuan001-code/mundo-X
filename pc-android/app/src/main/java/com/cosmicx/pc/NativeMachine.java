package com.cosmicx.pc;

import java.nio.ByteBuffer;

/** JNI boundary for the project's own x86/x64 machine. Native state is protected by one lock. */
public final class NativeMachine {
    static {
        System.loadLibrary("cosmic_pc");
    }

    private NativeMachine() { }

    public static native long create(int ramMiB);
    /** Logical guest RAM and the separate budget for allocated guest pages, both in MiB. */
    public static native long createWithBudget(int logicalMiB, int residentMiB);
    public static native void destroy(long machine);
    public static native String bootDemo(long machine);
    public static native String bootLongModeDemo(long machine);
    /** Guest IN/OUT demonstration; longMode also performs the guest's x64 transition. */
    public static native String bootQuantumDemo(long machine, boolean longMode);
    public static native String bootQuantumPhaseDemo(long machine);
    /** Atomic ASCII .xi request, at most 65536 bytes; errors live in state.quantum.error. */
    public static native String quantumCommand(long machine, String program);
    public static native String quantumReset(long machine, String background);
    /** Exact decimal strings; visualizes XX, YY, ZZ, ZI and IZ on the same session. */
    public static native String quantumProbe(long machine, String first, String second);
    /** Native MXI1 state and RNG snapshot, bounded to 16 MiB. */
    public static native String quantumSnapshot(long machine);
    public static native String quantumRestore(long machine, String snapshot);
    /** New owned graphics handle; NativeCore.adoptInfiniteHandle must close it. */
    public static native long quantumGraphicsHandle(long machine);
    /** Boots original x86 guest code that selects VBE and writes actual framebuffer pixels. */
    public static native String bootGraphicsDemo(long machine, int width, int height);
    /** The native implementation duplicates fd before returning. */
    public static native String mount(long machine, int fd, long size, String mode);
    public static native String run(long machine, int budget);
    public static native String inspect(long machine);
    public static native String reset(long machine);
    public static native void key(long machine, int ascii, int scan);
    /** Actual text VRAM, 80 columns by 25 rows. */
    public static native String screen(long machine);
    /** Acquires immutable video bytes. Release the returned revision in a finally block. */
    public static native String acquireVideoFrame(long machine);
    /** Copies 4,000 VGA character/attribute bytes into a direct buffer. */
    public static native int copyVideoText(long revision, ByteBuffer target);
    /** Copies bounded rows of an immutable BGRX snapshot as RGBA8, top to bottom. */
    public static native int copyVideoRows(long revision, int firstRow, int rows, ByteBuffer target);
    public static native void releaseVideoFrame(long revision);
}
