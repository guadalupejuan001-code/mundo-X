package com.cosmicx.pc;

/** JNI boundary for the project's own x86 machine. All calls are serialized by MainActivity. */
public final class NativeMachine {
    static {
        System.loadLibrary("cosmic_pc");
    }

    private NativeMachine() { }

    public static native long create(int ramMiB);
    public static native void destroy(long machine);
    public static native String bootDemo(long machine);
    /** The native implementation duplicates fd before returning. */
    public static native String mount(long machine, int fd, long size, String mode);
    public static native String run(long machine, int budget);
    public static native String inspect(long machine);
    public static native String reset(long machine);
    public static native void key(long machine, int ascii, int scan);
    /** Actual text VRAM, 80 columns by 25 rows. */
    public static native String screen(long machine);
}
