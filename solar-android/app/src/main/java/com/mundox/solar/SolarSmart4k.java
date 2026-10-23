package com.mundox.solar;

import com.mundox.graphics.RenderResolution;

/**
 * Honest 4K PHOTO / adaptive LIVE viewport policy.
 *
 * Live pixels are never labelled 4K: the preview is 16:9, bounded to
 * 360x640 / 540x960 / 810x1440 (or landscape equivalents), and is scaled to
 * the surface. Full-resolution 2160x3840 / 3840x2160 is generated ONLY for
 * an explicitly requested PNG capture by the unchanged scene shader.
 *
 * Policy works with frame cadence rather than an invented GPU speedup.
 * 2 consecutive slow/fast observations and a 5s resize cooldown prevent
 * reallocation thrashing and false FPS claims.
 */
public final class SolarSmart4k {
    public static final int MIN_PREVIEW_LONG=640;
    public static final int DEFAULT_PREVIEW_LONG=960;
    public static final int MAX_PREVIEW_LONG=1440;
    private static final long SAMPLE_NS=2_500_000_000L;
    private static final long COOLDOWN_NS=5_000_000_000L;

    private SolarSmart4k(){}

    public static RenderResolution.Size preview(int surfaceWidth,int surfaceHeight,
                                                  int longerEdge,
                                                  RenderResolution.Limits limits) {
        if(surfaceWidth<1||surfaceHeight<1||limits==null||
           longerEdge!=640&&longerEdge!=960&&longerEdge!=1440)
            throw new IllegalArgumentException("4K Fluido: tamaño de previsualización inválido");
        int shorterEdge=longerEdge*9/16;
        RenderResolution.Size result=surfaceWidth>=surfaceHeight?
                new RenderResolution.Size(longerEdge,shorterEdge):
                new RenderResolution.Size(shorterEdge,longerEdge);
        limits.require(result);
        return result;
    }

    public static final class Governor {
        private int limit=DEFAULT_PREVIEW_LONG;
        private long lastSample,changedAt;
        private int proposed,consecutive;

        public int limit(){return limit;}

        public void reset(){
            limit=DEFAULT_PREVIEW_LONG;
            lastSample=changedAt=0;
            proposed=consecutive=0;
        }

        public int observe(float measuredFps,long monotonicNanos) {
            if(!Float.isFinite(measuredFps)||measuredFps<=0||monotonicNanos<=0)
                return limit;
            if(lastSample==0){lastSample=monotonicNanos;return limit;}
            if(monotonicNanos<lastSample||monotonicNanos-lastSample<SAMPLE_NS)
                return limit;
            lastSample=monotonicNanos;
            if(changedAt!=0 && monotonicNanos-changedAt<COOLDOWN_NS)
                return limit;

            int target=limit;
            if(limit==1440){
                if(measuredFps<18)target=960;
            } else if(limit==960){
                if(measuredFps<15)target=640;
                else if(measuredFps>48)target=1440;
            } else {
                if(measuredFps>40)target=960;
            }
            if(target==limit){proposed=consecutive=0;return limit;}
            if(proposed!=target){proposed=target;consecutive=1;}
            else consecutive++;
            if(consecutive>=2){
                limit=target;
                changedAt=monotonicNanos;
                proposed=consecutive=0;
            }
            return limit;
        }
    }
}
