package com.mundox.solar;

/**
 * Demand-driven render resolution policy, deliberately independent of GLES.
 * Samples elapsed time between frame starts, which captures the effective
 * drawing cadence (including display/GPU throttling), not just Java CPU time.
 *
 * The policy changes ONLY the display framebuffer size. The Solar clock,
 * ephemerides, native Ether state, observable preparation, and capture formats
 * stay owned by the existing engines.
 */
public final class SolarAdaptiveQuality {
    public static final int MIN_DIMENSION = 480;
    public static final int MAX_DIMENSION = 1280;
    private static final long SLOW_NS = 29_000_000L;
    private static final long VERY_SLOW_NS = 100_000_000L;
    private static final long FAST_NS = 18_000_000L;
    private static final long SUSPENDED_NS = 1_000_000_000L;
    private int dimension,slow,verySlow,fast;

    public SolarAdaptiveQuality(int initialDimension) {
        reset(initialDimension);
    }

    public int dimension() { return dimension; }

    /** Reset streaks across resize, capture, user modes and resume. */
    public void reset(int initialDimension) {
        dimension = Math.max(MIN_DIMENSION, Math.min(MAX_DIMENSION, initialDimension));
        clearSamples();
    }

    public void clearSamples() { slow=verySlow=fast=0; }

    /**
     * Returns -1 for unchanged size, or a new maximum framebuffer dimension.
     * Paused, exported or explicit UHD frames must not call sample.
     */
    public int sample(long frameIntervalNanos) {
        if(frameIntervalNanos <= 0 || frameIntervalNanos >= SUSPENDED_NS) {
            clearSamples();
            return -1;
        }
        if(frameIntervalNanos >= SLOW_NS) {
            fast=0;slow++;
            verySlow = frameIntervalNanos>=VERY_SLOW_NS ? verySlow+1 : 0;
            if(slow >= 6 || verySlow >= 2) {
                int next = Math.max(MIN_DIMENSION,dimension-(verySlow>=2?240:160));
                clearSamples();
                if(next != dimension) { dimension=next; return next; }
            }
        }else if(frameIntervalNanos <= FAST_NS){
            slow=verySlow=0;
            if(++fast >= 90){
                int next = Math.min(MAX_DIMENSION,dimension+80);
                clearSamples();
                if(next != dimension){dimension=next;return next;}
            }
        }else clearSamples();
        return -1;
    }
}
