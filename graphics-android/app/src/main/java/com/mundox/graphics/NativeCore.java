package com.mundox.graphics;

import java.io.Serializable;
import java.util.ArrayList;
import java.util.Arrays;

/** Owner of one native session; UI operations and renderers borrow this same object. */
public final class NativeCore implements AutoCloseable {
    /** New worlds use this profile; imported and saved projects retain their size. */
    public static final int DEFAULT_QUBITS=1000, MAX_QUBITS=1000;
    public static final int H=0, S=1, X=2, Z=3, CX=4, CZ=5, Y=6;
    public static final int ZERO=0,BELL_PAIRS=1;
    private static final String[] GATES={"h","s","x","z","cx","cz","y"};
    private static final long MAX_PROJECT_REVISION=9_007_199_254_740_991L;
    static { System.loadLibrary("mx_graphics"); }

    /** Finite v1 replay or an opaque infinite v2 tableau + RNG snapshot. */
    public static final class Snapshot implements Serializable {
        private static final long serialVersionUID=1L;
        /** Finite size only. Infinite snapshots have no finite total qubit count. */
        public final int qubits;
        /** Infinite snapshots use -1; their atomic revision lives inside the opaque state. */
        public final long version;
        public final long[] gateHistory;
        public final boolean infinite;
        public final String infiniteSnapshot;
        private Snapshot(int qubits,long version,long[] gates) {
            this.qubits=qubits;this.version=version;this.gateHistory=gates;infinite=false;infiniteSnapshot=null;
        }
        private Snapshot(String text,long revision){
            qubits=0;version=revision;gateHistory=new long[0];infinite=true;infiniteSnapshot=text;
        }
        static Snapshot of(int qubits,long version,long[] gates){
            if(qubits<1||qubits>MAX_QUBITS||version<0||version>MAX_PROJECT_REVISION||gates==null||gates.length%3!=0||gates.length>300_000)
                throw new IllegalArgumentException("Estado del proyecto inválido");
            return new Snapshot(qubits,version,gates.clone());
        }
        static Snapshot infinite(String text){InfiniteProgram.snapshot(text);return new Snapshot(text,-1);}
    }

    public static final class Observation {
        public final float[] values;
        public final long revision,epoch;
        private Observation(float[] values,long revision,long epoch){this.values=values;this.revision=revision;this.epoch=epoch;}
    }
    /** Logical payload counters; tableauBytes does not include process/driver RAM. */
    public static final class InfiniteStats {
        public final long activeQubits,components,largestComponent,tableauBytes,addressBytes,revision,epoch;
        public final int background;
        private InfiniteStats(long[] values){
            if(values==null||values.length!=8)throw new IllegalStateException("Estadísticas nativas inválidas");
            activeQubits=values[0];components=values[1];largestComponent=values[2];tableauBytes=values[3];addressBytes=values[4];
            revision=values[5];epoch=values[6];background=(int)values[7];
        }
        public String backgroundName(){return background==BELL_PAIRS?"pares Bell":"cero";}
    }

    /** Finite size only; use isInfinite() and infiniteStats() for the infinite domain. */
    public final int qubits;
    private final boolean infinite;
    private final EtherCoreNative ether = new EtherCoreNative();
    private boolean etherPrepared;
    private long handle;

    public NativeCore(int qubits) {
        if(qubits<1||qubits>MAX_QUBITS)throw new IllegalArgumentException("El proyecto admite de 1 a "+MAX_QUBITS+" qubits");
        this.qubits=qubits;infinite=false;handle=nCreate(qubits);
        if(handle==0)throw new IllegalStateException("No se pudo crear el núcleo");
    }

    private NativeCore(int background,boolean unused){
        qubits=0;infinite=true;handle=nInfiniteCreate(background);
        if(handle==0)throw new IllegalStateException("No se pudo crear el núcleo infinito");
    }
    public static NativeCore infinite(int background){return new NativeCore(background,true);}
    private NativeCore(long ownedHandle){
        qubits=0;infinite=true;handle=ownedHandle;
        if(handle==0)throw new IllegalArgumentException("Falta el propietario infinito compartido");
    }
    /** Initialize JNI before another module allocates an owned handle for this class. */
    public static void ensureLoaded(){}
    /** Takes ownership of a mxi_* wrapper. Its shared session may also belong to the PC guest. */
    public static NativeCore adoptInfiniteHandle(long ownedHandle){
        try{return new NativeCore(ownedHandle);}
        catch(RuntimeException|Error error){if(ownedHandle!=0)nInfiniteDestroy(ownedHandle);throw error;}
    }

    public NativeCore(Snapshot state) {
        if(state==null)throw new IllegalArgumentException("Falta el estado guardado del núcleo");
        infinite=state.infinite;qubits=infinite?0:state.qubits;
        if(!infinite&&(qubits<1||qubits>MAX_QUBITS))throw new IllegalArgumentException("Tamaño finito del proyecto inválido");
        handle=infinite?nInfiniteCreate(ZERO):nCreate(qubits);
        if(handle==0)throw new IllegalStateException("No se pudo crear el núcleo");
        boolean restored=false;
        try {
            if(infinite){InfiniteProgram.snapshot(state.infiniteSnapshot);nInfiniteRestore(handle,state.infiniteSnapshot);}
            else {
                if(state.version<0||state.version>MAX_PROJECT_REVISION||state.gateHistory==null||state.gateHistory.length%3!=0||state.gateHistory.length>300_000)
                    throw new IllegalArgumentException("Estado guardado del núcleo inválido");
                nRestore(handle,state.gateHistory.clone(),state.version);
            }
            restored=true;
        } finally { if(!restored)close(); }
    }
    private void live() { if(handle==0)throw new IllegalStateException("Núcleo cerrado"); }
    private void requireInfinite(){live();if(!infinite)throw new IllegalStateException("Esta operación requiere un núcleo infinito");}
    private void requireFinite(){live();if(infinite)throw new IllegalStateException("Usa direcciones decimales para el núcleo infinito");}
    public boolean isInfinite(){return infinite;}
    public String domainLabel(){return infinite?"Infinito":qubits+" qubits";}
    /** Shared symbolic infinite-domain sidecar. Preparation is idempotent per owner. */
    public synchronized void prepareEtherCluster(){live();if(!etherPrepared){ether.prepareCluster();etherPrepared=true;}}
    public synchronized float[] etherQuery(String[] paulis){live();prepareEtherCluster();return ether.query(paulis);}
    public synchronized EtherCoreNative.Stats etherStats(){live();prepareEtherCluster();return ether.stats();}
    public synchronized String[] etherVisualProgram(){live();prepareEtherCluster();return EtherCoreNative.visualProgram(InfiniteProgram.FAR_ADDRESS);}
    public synchronized InfiniteStats infiniteStats(){requireInfinite();return new InfiniteStats(nInfiniteStats(handle));}
    public synchronized long version() { live();return infinite?infiniteStats().revision:nVersion(handle); }
    public synchronized long epoch() { live();return infinite?infiniteStats().epoch:nEpoch(handle); }
    public synchronized long stateBytes() { live();return infinite?infiniteStats().tableauBytes:nStateBytes(handle); }

    public synchronized void apply(int op,int a,int b) {
        requireFinite();
        if(nVersion(handle)>=MAX_PROJECT_REVISION)throw new IllegalStateException("El proyecto alcanzó su revisión máxima");
        if(nHistoryCount(handle)>=100_000)throw new IllegalStateException("El proyecto alcanzó 100.000 operaciones");
        long[] gate={op,a,b};
        nApply(handle,gate);
    }

    public synchronized void apply(int op,String first,String second){
        requireInfinite();if(op<0||op>=GATES.length)throw new IllegalArgumentException("Puerta no admitida");
        String a=InfiniteProgram.address(first),command=GATES[op]+" "+a;
        if(op==CX||op==CZ){String b=InfiniteProgram.address(second);if(a.equals(b))throw new IllegalArgumentException("Las puertas de dos direcciones requieren destinos distintos");command+=" "+b;}
        nInfiniteExecute(handle,command+"\n");
    }
    public synchronized String executeInfinite(String script){
        requireInfinite();
        if(script==null||script.length()>16*1024*1024)throw new IllegalArgumentException("Programa infinito demasiado grande o nulo");
        for(int i=0;i<script.length();i++)if(script.charAt(i)==0||script.charAt(i)>127)throw new IllegalArgumentException("El programa requiere texto ASCII");
        return nInfiniteExecute(handle,script);
    }
    public synchronized void resetInfinite(int background){requireInfinite();nInfiniteReset(handle,background);}
    /** Restore this owner transactionally; even an equal revision invalidates observation caches. */
    public synchronized void restoreInfinite(String text){requireInfinite();InfiniteProgram.snapshot(text);nInfiniteRestore(handle,text);}
    public synchronized Observation observe(String[] paulis){
        live();
        if(infinite){long[] stamp=new long[2];float[] values=nInfiniteQuery(handle,paulis,stamp);return new Observation(values,stamp[0],stamp[1]);}
        return new Observation(nQuery(handle,paulis),nVersion(handle),nEpoch(handle));
    }
    public synchronized float[] query(String[] paulis) {live();return infinite?observe(paulis).values:nQuery(handle,paulis);}
    public synchronized String[] generators() {
        requireFinite();String[] out=new String[qubits];
        for(int q=0;q<qubits;q++)out[q]=nGenerator(handle,q,qubits);
        return out;
    }
    public synchronized String[] visualProgram() {
        live();if(infinite)return InfiniteProgram.pair("0","1");
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
        requireFinite();ArrayList<Long> rows=new ArrayList<>();
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
        live();
        if(infinite)return new Snapshot(nInfiniteSnapshot(handle),-1);
        long[] saved=nSnapshot(handle);
        if(saved.length<1||(saved.length-1)%3!=0)throw new IllegalStateException("Snapshot nativo inválido");
        return new Snapshot(qubits,saved[0],Arrays.copyOfRange(saved,1,saved.length));
    }
    @Override public synchronized void close() {
        if(handle!=0){if(infinite)nInfiniteDestroy(handle);else nDestroy(handle);handle=0;}
        ether.close();
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
    private static native long nInfiniteCreate(int background);
    private static native void nInfiniteDestroy(long h);
    private static native String nInfiniteExecute(long h,String script);
    private static native String nInfiniteSnapshot(long h);
    private static native void nInfiniteRestore(long h,String snapshot);
    private static native void nInfiniteReset(long h,int background);
    private static native float[] nInfiniteQuery(long h,String[] sparse,long[] stamp);
    private static native long[] nInfiniteStats(long h);
}
