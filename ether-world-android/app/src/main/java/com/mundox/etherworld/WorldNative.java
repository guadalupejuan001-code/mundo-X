package com.mundox.etherworld;

/**
 * Thin bridge to the user's canonical Ether/VisualScene/Ether Render X/RulePhysics.
 * No local Java recreation of the procedural world.
 * All native calls are serialized on one dedicated worker thread.
 */
final class WorldNative {
    static final int VIEW_WIDTH=320;
    static final int VIEW_HEIGHT=208;
    static final int MAP_WIDTH=180;
    static final int MAP_TILES=45;
    private static final boolean READY;
    private static final String PROBLEM;

    static {
        boolean loaded=false;
        String error="";
        try {
            System.loadLibrary("ether_world");
            loaded=true;
        } catch (LinkageError e) {
            error=e.getMessage()==null?"JNI library unavailable":e.getMessage();
        }
        READY=loaded;
        PROBLEM=error;
    }

    static boolean ready(){return READY;}
    static String problem(){return PROBLEM;}

    static native long nativeCreate(int seed);
    static native long nativeLoad(String saved);
    static native void nativeClose(long handle);
    static native String nativeAction(long handle,String command,int a,int b,int c);
    static native String nativeRender(long handle,int[] view,int[] map,boolean exactOracle);
    static native String nativeStatus(long handle);
    static native String nativeOptimize(long handle);
    static native String nativeSave(long handle);
    private WorldNative(){}
}
