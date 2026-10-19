package com.mundox.solar;

import com.mundox.graphics.NativeCore;
import java.util.Arrays;

/** The existing COSMIC X CompactCore owns the state. This adapter only prepares
 * explicit user presets and caches read-only Pauli observations for the observer.
 * No ephemeris, magnetic constant or physical force is inferred from these values. */
public final class QuantumSolarBridge implements AutoCloseable {
    public static final int QUBITS = 8;
    public static final String[] PRESETS = {"Base", "Superposición", "Entrelazado", "Fase"};
    private static final String[] OBSERVABLES = {
        "XIIIIIII", "YIIIIIII", "ZIIIIIII", "XXIIIIII",
        "YYIIIIII", "ZZIIIIII", "IIXXIIII", "IIZZIIII"
    };
    private NativeCore core;
    private volatile float[] values = new float[8];
    private volatile int preset;
    private volatile long revision, bytes;
    private boolean closed;

    public QuantumSolarBridge() { this(2); }
    public QuantumSolarBridge(int preset) { setPreset(preset); }

    public synchronized void setPreset(int value) {
        if (closed) throw new IllegalStateException("Núcleo cerrado");
        if (value < 0 || value >= PRESETS.length) throw new IllegalArgumentException("Preparación desconocida");
        NativeCore candidate = new NativeCore(QUBITS);
        try {
            if (value == 1) {
                for (int q=0; q<QUBITS; q++) candidate.apply(NativeCore.H,q,0);
            } else if (value >= 2) {
                for (int q=0; q<QUBITS; q+=2) {
                    candidate.apply(NativeCore.H,q,0);
                    candidate.apply(NativeCore.CX,q,q+1);
                }
                if (value==3) for (int q=0; q<QUBITS; q+=2) candidate.apply(NativeCore.S,q,0);
            }
            float[] result = candidate.query(OBSERVABLES);
            long actualBytes = candidate.stateBytes();
            if (result.length != 8 || actualBytes <= 0) throw new IllegalStateException("Respuesta incompleta del núcleo");
            for (float n: result) if (!Float.isFinite(n) || Math.abs(n)>1.00001f)
                throw new IllegalStateException("Observable fuera de rango");
            NativeCore previous = core;
            core = candidate; candidate = null;
            values = result; preset = value; bytes = actualBytes; revision++;
            if (previous != null) previous.close();
        } finally { if (candidate != null) candidate.close(); }
    }

    /** Immutable cached view; callers must not write into this array. */
    public float[] coefficients() { return values; }
    public long revision() { return revision; }
    /** Native state payload only, never the process RAM. */
    public long stateBytes() { return bytes; }
    public int preset() { return preset; }
    public String status() { return "Núcleo X · 8 qubits · " + PRESETS[preset]; }
    public synchronized long nativeRevision() { return core.version(); }
    public synchronized float[] verifyReadOnly() {
        long before = core.version();
        float[] out = core.query(OBSERVABLES);
        if (core.version()!=before) throw new IllegalStateException("La observación modificó el núcleo");
        return out;
    }
    @Override public synchronized void close() {
        if (closed) return;
        closed=true;
        if (core!=null) { core.close(); core=null; }
    }
}
