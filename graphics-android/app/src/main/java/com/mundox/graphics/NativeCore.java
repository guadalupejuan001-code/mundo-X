package com.mundox.graphics;

import java.io.Serializable;
import java.util.ArrayList;
import java.util.Arrays;

/** Owner of one native CompactCore; the renderer only borrows this object. */
public final class NativeCore implements AutoCloseable {
    public static final int H=0, S=1, X=2, Z=3, CX=4, CZ=5, Y=6;
    private static final long MAX_PROJECT_REVISION=9_007_199_254_740_991L;
    static { System.loadLibrary("mx_graphics"); }

    /** Exact replay from |0...0>; observation never adds gates to this history. */
    public static final class Snapshot implements Serializable {
        private static final long serialVersionUID=1L;
        public final int qubits;
        public final long version;
        public final long[] gateHistory;
        private Snapshot(int qubits,long version,long[] gates) {
            this.qubits=qubits;this.version=version;this.gateHistory=gates;
        }
        static Snapshot of(int qubits,long version,long[] gates){
            if(qubits<1||qubits>256||version<0||version>MAX_PROJECT_REVISION||gates==null||gates.length%3!=0||gates.length>300_000)
                throw new IllegalArgumentException("Estado del proyecto inválido");
            return new Snapshot(qubits,version,gates.clone());
        }
    }

    public final int qubits;
    private long handle;

    public NativeCore(int qubits) {
        if(qubits<1||qubits>256)throw new IllegalArgumentException("El proyecto admite de 1 a 256 qubits");
        this.qubits=qubits;handle=nCreate(qubits);
        if(handle==0)throw new IllegalStateException("No se pudo crear el núcleo");
    }

    public NativeCore(Snapshot state) {
        this(snapshotQubits(state));
        boolean restored=false;
        try {
            if(state.version<0||state.version>MAX_PROJECT_REVISION||state.gateHistory==null||state.gateHistory.length%3!=0||state.gateHistory.length>300_000)
                throw new IllegalArgumentException("Estado guardado del núcleo inválido");
            long[] gates=state.gateHistory.clone();
            // Native validation is transactional: an invalid batch applies no gates.
            nRestore(handle,gates,state.version);restored=true;
        } finally { if(!restored)close(); }
    }

    private static int snapshotQubits(Snapshot state) {
        if(state==null)throw new IllegalArgumentException("Falta el estado guardado del núcleo");
        return state.qubits;
    }
    private void live() { if(handle==0)throw new IllegalStateException("Núcleo cerrado"); }
    public synchronized long version() { live();return nVersion(handle); }
    public synchronized long epoch() { live();return nEpoch(handle); }
    public synchronized long stateBytes() { live();return nStateBytes(handle); }

    public synchronized void apply(int op,int a,int b) {
        live();
        if(nVersion(handle)>=MAX_PROJECT_REVISION)throw new IllegalStateException("El proyecto alcanzó su revisión máxima");
        if(nHistoryCount(handle)>=100_000)throw new IllegalStateException("El proyecto alcanzó 100.000 operaciones");
        long[] gate={op,a,b};
        nApply(handle,gate);
    }

    public synchronized float[] query(String[] paulis) { live();return nQuery(handle,paulis); }
    public synchronized String[] generators() {
        live();String[] out=new String[qubits];
        for(int q=0;q<qubits;q++)out[q]=nGenerator(handle,q,qubits);
        return out;
    }
    public synchronized String[] visualProgram() {
        String[] out=new String[qubits*3];String[] generators=generators();
        System.arraycopy(generators,0,out,0,qubits);
        for(int axis=0;axis<2;axis++)for(int q=0;q<qubits;q++) {
            char[] p=new char[qubits];Arrays.fill(p,'I');p[q]=axis==0?'X':'Z';
            out[qubits*(axis+1)+q]=new String(p);
        }
        return out;
    }

    /** Explicit user/demo preparation. The renderer constructor never calls it. */
    public synchronized void resetGraph() {
        live();ArrayList<Long> rows=new ArrayList<>();
        for(int q=0;q<qubits;q++)add(rows,H,q,0);
        for(int q=0;q<qubits-1;q++)add(rows,CZ,q,q+1);
        for(int q=0;q<qubits-3;q+=2)add(rows,CZ,q,q+3);
        for(int q=1;q<qubits;q+=3)add(rows,S,q,0);
        long[] gates=new long[rows.size()];for(int i=0;i<gates.length;i++)gates[i]=rows.get(i);
        long version=nVersion(handle);
        if(version>=MAX_PROJECT_REVISION)throw new IllegalStateException("El proyecto alcanzó su revisión máxima");
        nRestore(handle,gates,version+1);
    }
    private static void add(ArrayList<Long> rows,long op,long a,long b) {
        rows.add(op);rows.add(a);rows.add(b);
    }

    public synchronized Snapshot snapshot() {
        live();long[] saved=nSnapshot(handle);
        if(saved.length<1||(saved.length-1)%3!=0)throw new IllegalStateException("Snapshot nativo inválido");
        return new Snapshot(qubits,saved[0],Arrays.copyOfRange(saved,1,saved.length));
    }
    @Override public synchronized void close() {
        if(handle!=0){nDestroy(handle);handle=0;}
    }
    private static native long nCreate(int n);
    private static native void nDestroy(long h);
    private static native void nApply(long h,long[] gates);
    private static native float[] nQuery(long h,String[] paulis);
    private static native String nGenerator(long h,int row,int n);
    private static native long nStateBytes(long h);
    private static native long nVersion(long h);
    private static native long nEpoch(long h);
    private static native long nHistoryCount(long h);
    private static native long[] nSnapshot(long h);
    private static native void nRestore(long h,long[] gates,long revision);
}
