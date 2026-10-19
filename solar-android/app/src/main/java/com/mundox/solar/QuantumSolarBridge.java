package com.mundox.solar;

import com.mundox.graphics.InfiniteProgram;
import com.mundox.graphics.NativeCore;

/**
 * Solar now owns the same rules-based InfiniteCore route used by the rest of
 * COSMIC X. The eight addresses below are only the finite support touched by
 * the visual presets; the register itself has no final address.
 *
 * No ephemeris, magnetic constant or physical force is inferred from these
 * values. The core only modulates illustrative field/wind traces.
 */
public final class QuantumSolarBridge implements AutoCloseable {
    public static final int PRESET_ADDRESSES = 8;
    /** Compatibility name for old reports: this is active preset support, not total qubits. */
    public static final int QUBITS = PRESET_ADDRESSES;
    public static final String[] PRESETS = {"Base", "Superposición", "Entrelazado", "Fase"};
    private static final String[] OBSERVABLES = {
        "X:0", "Y:0", "Z:0", "X:0 X:1",
        "Y:0 Y:1", "Z:0 Z:1", "X:2 X:3", "Z:2 Z:3"
    };

    private NativeCore core;
    private volatile float[] values = new float[8];
    private volatile int preset;
    private volatile long revision, bytes, activeQubits;
    private boolean closed;

    public QuantumSolarBridge() { this(2); }
    public QuantumSolarBridge(int preset) { setPreset(preset); }

    public synchronized void setPreset(int value) {
        if (closed) throw new IllegalStateException("Núcleo cerrado");
        if (value < 0 || value >= PRESETS.length) throw new IllegalArgumentException("Preparación desconocida");
        NativeCore candidate = NativeCore.infinite(NativeCore.ZERO);
        try {
            if (value == 1) {
                for (int q=0; q<PRESET_ADDRESSES; q++)
                    candidate.apply(NativeCore.H, Integer.toString(q), "");
            } else if (value >= 2) {
                for (int q=0; q<PRESET_ADDRESSES; q+=2) {
                    String a=Integer.toString(q), b=Integer.toString(q+1);
                    candidate.apply(NativeCore.H,a,"");
                    candidate.apply(NativeCore.CX,a,b);
                }
                if (value==3) for (int q=0; q<PRESET_ADDRESSES; q+=2)
                    candidate.apply(NativeCore.S,Integer.toString(q),"");
            }
            for (String observable: OBSERVABLES) InfiniteProgram.validate(observable);
            NativeCore.Observation observation = candidate.observe(OBSERVABLES);
            float[] result = observation.values;
            NativeCore.InfiniteStats stats = candidate.infiniteStats();
            if (result.length != 8) throw new IllegalStateException("Respuesta incompleta del núcleo infinito");
            for (float n: result) if (!Float.isFinite(n) || Math.abs(n)>1.00001f)
                throw new IllegalStateException("Observable fuera de rango");
            NativeCore previous = core;
            core = candidate; candidate = null;
            values = result; preset = value; bytes = stats.tableauBytes;
            activeQubits = stats.activeQubits; revision++;
            if (previous != null) previous.close();
        } finally { if (candidate != null) candidate.close(); }
    }

    /** Immutable cached view; callers must not write into this array. */
    public float[] coefficients() { return values; }
    public long revision() { return revision; }
    /** Native tableau payload only. Rule-defined untouched infinity can be zero bytes. */
    public long stateBytes() { return bytes; }
    public long activeQubits() { return activeQubits; }
    public boolean isInfinite() { return true; }
    public int preset() { return preset; }
    public String status() {
        return "Núcleo X · ∞ · "+activeQubits+" activos · "+PRESETS[preset];
    }
    public synchronized long nativeRevision() { return core.version(); }
    public synchronized float[] verifyReadOnly() {
        long before = core.version();
        float[] out = core.observe(OBSERVABLES).values;
        if (core.version()!=before) throw new IllegalStateException("La observación modificó el núcleo infinito");
        return out;
    }
    @Override public synchronized void close() {
        if (closed) return;
        closed=true;
        if (core!=null) { core.close(); core=null; }
    }
}
