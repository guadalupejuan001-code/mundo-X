package com.mundox.solar;

import com.mundox.graphics.NativeCore;
import java.util.Arrays;

/** One Ether/InfiniteSession owns the global rules and finite preparation.
 * This adapter prepares explicit presets and caches one atomic observation for
 * both groups of visual coefficients. Eight sites are prepared, not a finite
 * total domain of eight qubits.
 * No ephemeris, magnetic constant or physical force is inferred from these values. */
public final class QuantumSolarBridge implements AutoCloseable {
    public static final int PREPARATION_SITES = 8;
    public static final String[] PRESETS = {"Base", "Superposición", "Entrelazado", "Fase"};
    private static final String[] OBSERVABLES = {
        "X:0", "Y:0", "Z:0", "X:0 X:1",
        "Y:0 Y:1", "Z:0 Z:1", "X:2 X:3", "Z:2 Z:3"
    };
    // Probe the prepared region so the global-rule signals respond to its state.
    private static final String[] ETHER_OBSERVABLES = {
        "Z:0 X:1 Z:2", "X:0 Z:1", "X:1", "Z:1", "Z:0 Z:2"
    };
    private static final String[] ALL_OBSERVABLES = combinedObservables();
    private NativeCore core;
    /** One publication couples both coefficient groups to their exact epoch.
     * Arrays never escape; consumers can retain this immutable observation. */
    public static final class Observation {
        private final float[] values,etherValues;
        public final int preset;
        public final long revision,bytes,activeSites,observedRevision,observedEpoch,etherRules;
        private Observation(float[] values,float[] etherValues,int preset,long revision,
                            long bytes,long activeSites,long observedRevision,long observedEpoch,long etherRules){
            this.values=values;this.etherValues=etherValues;this.preset=preset;this.revision=revision;
            this.bytes=bytes;this.activeSites=activeSites;this.observedRevision=observedRevision;
            this.observedEpoch=observedEpoch;this.etherRules=etherRules;
        }
        public void copyCoefficients(float[] local,float[] global){
            if(local==null||local.length!=8||global==null||global.length<4||global.length>5)
                throw new IllegalArgumentException("Tamaño de observación inválido");
            System.arraycopy(values,0,local,0,8);System.arraycopy(etherValues,0,global,0,global.length);
        }
    }
    private volatile Observation view;
    private boolean closed;

    public QuantumSolarBridge() { this(2); }
    public QuantumSolarBridge(int preset) { setPreset(preset); }

    public synchronized void setPreset(int value) {
        if (closed) throw new IllegalStateException("Núcleo cerrado");
        if (value < 0 || value >= PRESETS.length) throw new IllegalArgumentException("Preparación desconocida");
        NativeCore candidate = NativeCore.infinite(NativeCore.ZERO);
        try {
            if (value == 1) {
                for (int q=0; q<PREPARATION_SITES; q++)
                    candidate.apply(NativeCore.H,Integer.toString(q),null);
            } else if (value >= 2) {
                for (int q=0; q<PREPARATION_SITES; q+=2) {
                    candidate.apply(NativeCore.H,Integer.toString(q),null);
                    candidate.apply(NativeCore.CX,Integer.toString(q),Integer.toString(q+1));
                }
                if (value==3) for (int q=0; q<PREPARATION_SITES; q+=2)
                    candidate.apply(NativeCore.S,Integer.toString(q),null);
            }
            // These rules act on the same preparation; observation never prepares it.
            candidate.prepareEtherCluster();
            NativeCore.Observation observation = candidate.observe(ALL_OBSERVABLES);
            long rules = candidate.etherStats().rules;
            long actualBytes = candidate.stateBytes();
            long actualSites = candidate.infiniteStats().activeQubits;
            if (observation.values.length != ALL_OBSERVABLES.length || rules != 2 || actualBytes < 0)
                throw new IllegalStateException("Respuesta incompleta del núcleo");
            for (float n: observation.values) if (!Float.isFinite(n) || Math.abs(n)>1.00001f)
                throw new IllegalStateException("Observable fuera de rango");
            float[] result = Arrays.copyOfRange(observation.values,0,OBSERVABLES.length);
            float[] ether = Arrays.copyOfRange(observation.values,OBSERVABLES.length,ALL_OBSERVABLES.length);
            NativeCore previous = core;
            core = candidate; candidate = null;
            view=new Observation(result,ether,value,view==null?1:view.revision+1,
                    actualBytes,actualSites,observation.revision,observation.epoch,rules);
            if (previous != null) previous.close();
        } finally { if (candidate != null) candidate.close(); }
    }

    public Observation observation() { return view; }
    /** Owned copy for compatibility; rendering uses the allocation-free observation. */
    public float[] coefficients() { return view.values.clone(); }
    /** Another view of the same atomic state observation; never ephemeris inputs. */
    public float[] etherCoefficients() { return view.etherValues.clone(); }
    public long etherRules() { return view.etherRules; }
    public long revision() { return view.revision; }
    /** Finite tableau payload only; excludes rule metadata and process RAM. */
    public long stateBytes() { return view.bytes; }
    public long activeSites() { return view.activeSites; }
    public long observationRevision() { return view.observedRevision; }
    public long observationEpoch() { return view.observedEpoch; }
    public int preset() { return view.preset; }
    public String status() { return "Ether Core ∞ · preparación en 8 sitios · " + PRESETS[view.preset]; }
    public synchronized long nativeRevision() { return core.version(); }
    public synchronized long nativeEpoch() { return core.epoch(); }
    public synchronized float[] verifyReadOnly() {
        long before = core.version(), epoch = core.epoch();
        String snapshot = core.snapshot().infiniteSnapshot;
        float[] out = core.query(OBSERVABLES);
        if (core.version()!=before || core.epoch()!=epoch
                || !snapshot.equals(core.snapshot().infiniteSnapshot))
            throw new IllegalStateException("La observación modificó el núcleo");
        return out;
    }
    public synchronized float[] verifyEtherReadOnly() {
        long before = core.version(), epoch = core.epoch(), rules = core.etherStats().rules;
        String snapshot = core.snapshot().infiniteSnapshot;
        float[] out = core.etherQuery(ETHER_OBSERVABLES);
        if(core.version()!=before || core.epoch()!=epoch || core.etherStats().rules!=rules
                || !snapshot.equals(core.snapshot().infiniteSnapshot))
            throw new IllegalStateException("La observación Ether modificó el núcleo");
        return out;
    }
    private static String[] combinedObservables() {
        String[] all = Arrays.copyOf(OBSERVABLES,OBSERVABLES.length+ETHER_OBSERVABLES.length);
        System.arraycopy(ETHER_OBSERVABLES,0,all,OBSERVABLES.length,ETHER_OBSERVABLES.length);
        return all;
    }
    @Override public synchronized void close() {
        if (closed) return;
        closed=true;
        if (core!=null) { core.close(); core=null; }
    }
}
