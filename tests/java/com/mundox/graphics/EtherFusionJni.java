package com.mundox.graphics;

import com.mundox.solar.QuantumSolarBridge;
import java.lang.reflect.Field;
import java.util.Arrays;

/** Runs the shipping Java wrappers and JNI, without Android UI or GLES mocks. */
public final class EtherFusionJni {
    private static int checks;
    private interface Action {void run();}
    private static void check(boolean value,String message){
        if(!value)throw new AssertionError(message);
        checks++;
    }
    private static void rejects(Action operation,String message){
        boolean failed=false;
        try{operation.run();}catch(IllegalArgumentException|IllegalStateException expected){failed=true;}
        check(failed,message);
    }
    private static long handle(NativeCore core) throws Exception {
        Field field=NativeCore.class.getDeclaredField("handle");field.setAccessible(true);
        return field.getLong(core);
    }
    private static native long share(long handle);

    private static void lifecycle() throws Exception {
        rejects(()->new NativeCore(0),"zero finite size accepted");
        rejects(()->new NativeCore(1001),"finite size beyond compatibility budget accepted");
        rejects(()->new NativeCore((NativeCore.Snapshot)null),"null snapshot accepted");
        rejects(()->NativeCore.adoptInfiniteHandle(0),"null shared owner accepted");
        try(NativeCore legacy=new NativeCore(100)){
            legacy.apply(NativeCore.H,99,0);legacy.apply(NativeCore.CX,99,0);
            char[] p=new char[100];Arrays.fill(p,'I');p[0]='X';p[99]='X';
            String[] query={new String(p)};
            check(legacy.query(query)[0]==1,"finite 100-qubit compatibility lost");
            try(NativeCore restored=new NativeCore(legacy.snapshot())){
                check(restored.qubits==100 && restored.query(query)[0]==1,"finite snapshot changed");
            }
            rejects(legacy::prepareEtherCluster,"finite compatibility silently allocated an Ether sidecar");
        }
        NativeCore owner=NativeCore.etherCluster();
        NativeCore shared=NativeCore.adoptInfiniteHandle(share(handle(owner)));
        try{
            String[] query=EtherCoreNative.visualProgram("000");
            check(Arrays.equals(owner.query(query),new float[]{1,1,0,0,0}),"q0 boundary probe failed");
            check(Arrays.equals(owner.query(query),shared.etherQuery(query)),"owners read different initial states");
            String before=owner.snapshot().infiniteSnapshot;
            long revision=owner.version(),epoch=owner.epoch();
            owner.etherStats();owner.etherVisualProgram();shared.query(query);
            check(before.equals(owner.snapshot().infiniteSnapshot),"observation prepared a global layer");
            shared.apply(NativeCore.Z,"0",null);
            check(owner.query(query)[0]==-1 && shared.etherQuery(query)[0]==-1,"local mutation was not shared");
            check(owner.version()==revision+1 && owner.epoch()==epoch+1,"shared state stamp did not advance");
            check(owner.version()==shared.version() && owner.epoch()==shared.epoch(),"owners have different state stamps");
            owner.close();owner.close();
            check(shared.query(query)[0]==-1,"closing one owner destroyed the shared session");
            rejects(()->owner.query(query),"closed owner remained callable");
            String saved=shared.snapshot().infiniteSnapshot;
            check(saved.startsWith("MXI2:"),"Ether frame missing from snapshot");
            try(NativeCore restored=new NativeCore(shared.snapshot())){
                check(Arrays.equals(restored.query(query),shared.query(query)),"local state and global frame restored separately");
                check(restored.etherStats().rules==2,"restored frame is missing");
            }
            rejects(()->shared.executeInfinite("h_all\nz 1\nt 0\n"),"unsupported operation was silently routed elsewhere");
            check(saved.equals(shared.snapshot().infiniteSnapshot),"failed program committed a partial frame or local state");
            rejects(()->shared.query(new String[]{"X:0 Z:00"}),"duplicate canonical site accepted");
            check(saved.equals(shared.snapshot().infiniteSnapshot),"invalid query changed the state");
            shared.etherCzNeighbors();
            check(shared.etherStats().rules==1,"equal adjacent Ether layers did not cancel");
            shared.etherHAll();
            check(shared.etherStats().rules==0 && shared.snapshot().infiniteSnapshot.startsWith("MXI1:"),
                    "empty frame does not preserve legacy snapshot encoding");
            shared.restoreInfinite(saved);
            check(shared.etherStats().rules==2 && shared.query(query)[0]==-1,"restore did not recover the frame");
            char[] zeros=new char[1000];Arrays.fill(zeros,'0');String far="1"+new String(zeros);
            String[] distant=EtherCoreNative.visualProgram(far);
            check(shared.query(distant)[0]==1,"distant cluster relation lost");
            shared.apply(NativeCore.Z,far,null);
            check(shared.query(distant)[0]==-1,"distant local defect did not affect the same Ether state");
            check(shared.infiniteStats().activeQubits<16,"distant address materialized the intervening domain");
        }finally{owner.close();shared.close();}
        try(EtherCoreNative standalone=new EtherCoreNative()){
            standalone.prepareCluster();
            check(standalone.query(EtherCoreNative.visualProgram("0"))[0]==1,"standalone adapter lost q0");
            standalone.close();rejects(standalone::stats,"closed convenience owner remained callable");
        }
    }

    private static void measurement(){
        try(NativeCore core=NativeCore.etherCluster()){
            core.executeInfinite("seed 813\n");
            String saved=core.snapshot().infiniteSnapshot;
            String first=core.executeInfinite("measure 0\nmeasure 1\n");
            String projected=core.snapshot().infiniteSnapshot;
            core.restoreInfinite(saved);
            core.query(EtherCoreNative.visualProgram("1"));
            String second=core.executeInfinite("measure 0\nmeasure 1\n");
            check(first.equals(second) && projected.equals(core.snapshot().infiniteSnapshot),
                    "snapshot or observation changed the measurement RNG sequence");
            core.restoreInfinite(saved);
            core.executeInfinite("postselect 0 1\n");
            check(core.query(new String[]{"Z:0"})[0]==-1,"physical Z projection used the wrong frame/sign");
            String before=core.snapshot().infiniteSnapshot;
            rejects(()->core.executeInfinite("postselect 0 0\n"),"impossible branch accepted");
            check(before.equals(core.snapshot().infiniteSnapshot),"impossible branch consumed state or RNG");
        }
    }

    private static void solar(){
        float[][] local={{0,0,0,0,0,0,0,0},{0,0,1,0,0,1,0,1},
                {0,0,0,0,0,1,0,1},{0,-1,0,0,0,0,0,0}};
        float[][] ether={{1,1,0,0,0},{0,0,0,1,1},{0,0,0,0,0},{0,0,0,0,0}};
        try(QuantumSolarBridge bridge=new QuantumSolarBridge(0)){
            for(int i=0;i<4;i++){
                bridge.setPreset(i);
                check(Arrays.equals(bridge.coefficients(),local[i]),"Solar local view has incorrect physical values");
                check(Arrays.equals(bridge.etherCoefficients(),ether[i]),"Solar Ether view is still a constant sidecar");
                check(bridge.etherRules()==2 && bridge.activeSites()==(i==0?0:8),"Solar materialized the background");
                long revision=bridge.nativeRevision(),epoch=bridge.nativeEpoch();
                check(bridge.observationRevision()==revision && bridge.observationEpoch()==epoch,
                        "Solar coefficients do not share one atomic observation");
                check(Arrays.equals(bridge.verifyReadOnly(),local[i]) && Arrays.equals(bridge.verifyEtherReadOnly(),ether[i]),
                        "Solar observation changed its values");
                check(bridge.nativeRevision()==revision && bridge.nativeEpoch()==epoch,"Solar observation mutated its state");
            }
            check(!Arrays.equals(ether[0],ether[1]),"Solar preparations cannot affect Ether observations");
        }
    }

    public static void main(String[] args) throws Exception {
        lifecycle();measurement();solar();
        System.out.println("{\"status\":\"passed\",\"checks\":"+checks
                +",\"scope\":\"production Java and JNI on host JVM; no Android UI or GLES\","
                +"\"sharedOwnerLifetime\":true,\"localGlobalCausality\":true,\"snapshotAndRng\":true,"
                +"\"solarSingleObservation\":true}");
    }
}
