package com.mundox.solar;

/**
 * Android adapter of EtherRenderSpatial. One UINT32 bitfield per screen tile.
 * Never renders a CPU framebuffer; the GPU is responsible for shading, textures,
 * depth, rings, atmospheric scattering and presentation.
 *
 * All calls are from the active GL thread. The caller owns and reuses masks[].
 */
public final class EtherVisibilityNative {
    private EtherVisibilityNative() {}
    static { System.loadLibrary("mx_graphics"); }
    public static native void plan(int width,int height,int tile,float tanHalfFov,
                                   float[] forward,float[] right,float[] up,
                                   float[] bodies,float[] appearance,float[] atmosphere,
                                   int count,int[] masks);
}
