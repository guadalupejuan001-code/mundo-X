package com.mundox.graphics;

import java.io.Serializable;
import java.util.ArrayList;
import java.util.Arrays;

/** Owner of one native CompactCore; the renderer only borrows this object. */
public final class NativeCore implements AutoCloseable {
    public static final int H=0, S=1, X=2, Z=3, CX=4, CZ=5, Y=6;
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
    }

    public final int qubits;
    private long handle;
    private long version;
    private ArrayList<long[]> history=new ArrayList<>(1);
    private int historyValues;

    public NativeCore(int qubits) {
        if(qubits<1||qubits>Integer.MAX_VALUE/3)throw new IllegalArgumentException("Número de qubits inválido");
        this.qubits=qubits;handle=nCreate(qubits);
        if(handle==0)throw new IllegalStateException("No se pudo crear el núcleo");
    }

    public NativeCore(Snapshot state) {
        this(snapshotQubits(state));
        boolean restored=false;
        try {
            if(state.version<0||state.gateHistory==null||state.gateHistory.length%3!=0)
                throw new IllegalArgumentException("Estado guardado del núcleo inválido");
            long[] gates=state.gateHistory.clone();
            // Native validation is transactional: an invalid batch applies no gates.
            if(gates.length>0)nApply(handle,gates);
            history.add(gates);historyValues=gates.length;version=state.version;restored=true;
        } finally { if(!restored)close(); }
    }

    private static int snapshotQubits(Snapshot state) {
        if(state==null)throw new IllegalArgumentException("Falta el estado guardado del núcleo");
        return state.qubits;
    }
    private void live() { if(handle==0)throw new IllegalStateException("Núcleo cerrado"); }
    public synchronized long version() { live();return version; }
    public synchronized long stateBytes() { live();return nStateBytes(handle); }

    public synchronized void apply(int op,int a,int b) {
        live();
        if(historyValues>Integer.MAX_VALUE-3)throw new IllegalStateException("Historial de puertas demasiado grande");
        long[] gate={op,a,b};
        history.ensureCapacity(history.size()+1);
        nApply(handle,gate);
        history.add(gate);historyValues+=3;version++;
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
        ArrayList<long[]> replacementHistory=new ArrayList<>(1);replacementHistory.add(gates);
        long replacement=nCreate(qubits);
        try {
            nApply(replacement,gates);
            long previous=handle;handle=replacement;replacement=0;
            history=replacementHistory;historyValues=gates.length;version++;
            nDestroy(previous);
        } finally { if(replacement!=0)nDestroy(replacement); }
    }
    private static void add(ArrayList<Long> rows,long op,long a,long b) {
        rows.add(op);rows.add(a);rows.add(b);
    }

    public synchronized Snapshot snapshot() {
        live();long[] gates=new long[historyValues];int offset=0;
        for(long[] batch:history) {
            System.arraycopy(batch,0,gates,offset,batch.length);offset+=batch.length;
        }
        return new Snapshot(qubits,version,gates);
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
}
