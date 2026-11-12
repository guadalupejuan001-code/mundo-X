package com.mundox.worldlink.phone;

/** Pure numeric waveform model, shared with JVM verification. */
public final class AcousticWave {
    private AcousticWave() {}
    public static final int SAMPLE_RATE=22050;
    public static final int DURATION_TONE_MS=4500;
    public static final int DURATION_SWEEP_MS=10000;
    public static final double SWEEP_START_HZ=180.0;
    public static final double SWEEP_END_HZ=1200.0;

    public static boolean validFrequency(double hz) {
        return Double.isFinite(hz) && hz>=120 && hz<=1200;
    }
    public static double sweepFrequency(double progress) {
        double x=Math.max(0,Math.min(1,progress));
        return SWEEP_START_HZ + (SWEEP_END_HZ-SWEEP_START_HZ)*x;
    }
    public static double fade(long sample,long total) {
        if(total<1)return 0;
        double fadeSamples=SAMPLE_RATE*.045;
        double a=Math.min(1,sample/fadeSamples);
        double b=Math.min(1,(total-sample)/fadeSamples);
        return Math.max(0,Math.min(a,b));
    }
    /** Return PCM16 normalized value; conservative amplitude to protect hearing. */
    public static short pcm(double phase,double envelope) {
        double x=Math.sin(phase)*0.23*Math.max(0,Math.min(1,envelope));
        return (short)Math.round(x*32767);
    }
}
