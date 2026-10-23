package com.mundox.ghzwatch;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Random;

/** Descriptive sensor statistics only. No quantum or significance inference. */
final class ProbeAnalysis {
    static final long MS = 1_000_000L;
    static final class Sample {
        final long timeNs, receivedNs;
        final int type, accuracy;
        final float[] values;
        Sample(long timeNs, long receivedNs, int type, int accuracy, float[] values) {
            this.timeNs = timeNs; this.receivedNs = receivedNs;
            this.type = type; this.accuracy = accuracy; this.values = values.clone();
        }
    }
    static final class Window {
        int count;
        long first = Long.MAX_VALUE, last = Long.MIN_VALUE, largestGap;
        final double[] mean = new double[3];
        double m2;
        void add(Sample s) {
            if (count > 0) largestGap = Math.max(largestGap, s.timeNs - last);
            first = Math.min(first, s.timeNs); last = s.timeNs; count++;
            for (int i = 0; i < 3; i++) {
                double delta = s.values[i] - mean[i];
                mean[i] += delta / count;
                m2 += delta * (s.values[i] - mean[i]);
            }
        }
        double rms() { return count == 0 ? Double.NaN : Math.sqrt(Math.max(0, m2 / count)); }
        boolean covers(long from, long to) {
            return count >= 4 && first <= from + 200 * MS && last >= to - 200 * MS
                && largestGap <= 250 * MS;
        }
    }
    static List<Boolean> order(Random random) {
        List<Boolean> order = new ArrayList<>();
        Collections.addAll(order, true, true, false, false);
        Collections.shuffle(order, random);
        return order;
    }
    static boolean valid(float[] values) {
        if (values == null || values.length < 3) return false;
        for (float value : values) if (Float.isNaN(value) || Float.isInfinite(value)) return false;
        return true;
    }
    static Window window(List<Sample> samples, int type, long from, long to) {
        Window result = new Window();
        // Arrival order is not assumed to equal event timestamp order.
        List<Sample> ordered = new ArrayList<>();
        for (Sample s : samples)
            if (s.type == type && s.timeNs >= from && s.timeNs < to && valid(s.values)) ordered.add(s);
        ordered.sort((a, b) -> Long.compare(a.timeNs, b.timeNs));
        long previous = Long.MIN_VALUE;
        for (Sample s : ordered) if (s.timeNs != previous) { result.add(s); previous = s.timeNs; }
        return result;
    }
    static double delta(Window before, Window after) {
        if (before.count == 0 || after.count == 0) return Double.NaN;
        double sum = 0;
        for (int i = 0; i < 3; i++) {
            double d = after.mean[i] - before.mean[i]; sum += d*d;
        }
        return Math.sqrt(sum);
    }
}
