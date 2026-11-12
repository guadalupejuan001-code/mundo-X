package com.mundox.ghzwatch;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Random;

/** Synthetic fixtures exercise statistics, not a physical sensor claim. */
public final class ProbeAnalysisCheck {
    private static int checks;
    private static void check(boolean value, String message) {
        checks++;
        if (!value) throw new AssertionError(message);
    }
    private static ProbeAnalysis.Sample sample(long ms, int type, float x, float y, float z) {
        return new ProbeAnalysis.Sample(ms*ProbeAnalysis.MS, (ms+15)*ProbeAnalysis.MS,
            type, 3, new float[]{x,y,z});
    }
    public static void main(String[] args) {
        List<ProbeAnalysis.Sample> data = new ArrayList<>();
        for (int t=0; t<700; t+=50) data.add(sample(t, 1, 1, 2, 3));
        for (int t=1000; t<1700; t+=50) data.add(sample(t, 1, 4, 6, 3));
        data.add(sample(200, 2, 1000, 1000, 1000)); // Different sensor must be excluded.
        data.add(sample(700, 1, 1000, 1000, 1000)); // Right endpoint is exclusive.
        data.add(sample(0, 1, 1, 2, 3)); // Duplicate timestamp does not add coverage.
        Collections.shuffle(data, new Random(9)); // Delivery may arrive out of order.
        ProbeAnalysis.Window before = ProbeAnalysis.window(data,1,0,700*ProbeAnalysis.MS);
        ProbeAnalysis.Window after = ProbeAnalysis.window(data,1,1000*ProbeAnalysis.MS,1700*ProbeAnalysis.MS);
        check(before.count==14, "sample selection and deduplication");
        check(before.covers(0,700*ProbeAnalysis.MS), "continuous coverage");
        check(before.rms()==0, "constant vector has no variation");
        check(ProbeAnalysis.delta(before,after)==5, "3-4-5 vector mean displacement");
        List<ProbeAnalysis.Sample> oscillation = new ArrayList<>();
        for (int t=0; t<700; t+=50) oscillation.add(sample(t,1,t%100==0 ? -2:2,0,10));
        ProbeAnalysis.Window wave = ProbeAnalysis.window(oscillation,1,0,700*ProbeAnalysis.MS);
        check(wave.rms()==2, "RMS about the vector mean, excluding constant gravity");
        List<ProbeAnalysis.Sample> gap = new ArrayList<>();
        for (int t : new int[]{0,50,100,600,650}) gap.add(sample(t,1,0,0,0));
        check(!ProbeAnalysis.window(gap,1,0,700*ProbeAnalysis.MS).covers(0,700*ProbeAnalysis.MS), "long gap rejected");
        check(!ProbeAnalysis.window(data,1,3000*ProbeAnalysis.MS,3700*ProbeAnalysis.MS).covers(3000*ProbeAnalysis.MS,3700*ProbeAnalysis.MS), "empty window rejected");
        check(Double.isNaN(new ProbeAnalysis.Window().rms()), "empty is unknown, not zero");
        check(!ProbeAnalysis.valid(new float[]{1,2}), "short sample rejected");
        check(!ProbeAnalysis.valid(new float[]{Float.NaN,2,3}), "NaN rejected");
        check(!ProbeAnalysis.valid(new float[]{1,2,3,Float.POSITIVE_INFINITY}), "nonfinite bias rejected");
        float[] original = {1,2,3};
        ProbeAnalysis.Sample retained = new ProbeAnalysis.Sample(0,0,1,3,original);
        original[0]=99;
        check(retained.values[0]==1, "Android reused value buffer is copied");
        boolean sawDifferentOrder = false;
        List<Boolean> first = ProbeAnalysis.order(new Random(0));
        for (int i=0;i<40;i++) {
            List<Boolean> order = ProbeAnalysis.order(new Random(i));
            check(Collections.frequency(order,true)==2 && Collections.frequency(order,false)==2, "balanced pulse/control order");
            sawDifferentOrder |= !first.equals(order);
        }
        check(sawDifferentOrder, "order actually changes");
        System.out.println("Hardware probe analysis: " + checks + " checks passed (synthetic fixtures).");
    }
}
