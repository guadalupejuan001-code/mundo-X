package com.mundox.solar;

import com.mundox.graphics.RenderResolution;

/**
 * Pure, deterministic policy for Solar UHD interaction preview.
 *
 * The real 4K/8K framebuffer remains the reference for image capture and
 * still frames. While dragging, the renderer may draw into a smaller separate
 * color/depth target and upscale ONLY the screen presentation. It never
 * changes SolarData positions, UVs, camera angles or output PNG dimensions.
 */
public final class SolarRenderPolicy {
    public static final int PREVIEW_LONG_EDGE=960;
    private SolarRenderPolicy() {}

    public static boolean shouldPreview(boolean enabled,boolean interacting,
                                        boolean uhd,boolean capturePending) {
        return enabled && interacting && uhd && !capturePending;
    }

    /**
     * Fixed 16:9 or 9:16 aspect follows the FULL framebuffer, not the
     * phone screen aspect; otherwise pinching would stretch the solar world.
     */
    public static RenderResolution.Size preview(RenderResolution.Size full) {
        if(full==null)throw new IllegalArgumentException("Full target missing");
        int longEdge=Math.max(full.width,full.height);
        if(longEdge<=PREVIEW_LONG_EDGE)return full;
        double scale=(double)PREVIEW_LONG_EDGE/longEdge;
        int shortEdge=Math.max(1,(int)Math.round(Math.min(full.width,full.height)*scale));
        return full.width>=full.height
                ?new RenderResolution.Size(PREVIEW_LONG_EDGE,shortEdge)
                :new RenderResolution.Size(shortEdge,PREVIEW_LONG_EDGE);
    }

    public static long expectedPreviewBytes(RenderResolution.Size full){
        return preview(full).attachmentBytes();
    }

    public static double approximatePixelReduction(RenderResolution.Size full){
        return (double)full.pixels()/preview(full).pixels();
    }
}
