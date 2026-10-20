package com.mundox.graphics;

import java.util.Arrays;

/**
 * Convenience owner over the same fused session used by NativeCore.
 * It does not allocate an independent native Ether state alongside that core.
 */
public final class EtherCoreNative implements AutoCloseable {
    private final NativeCore core;

    public static final class Stats {
        public final long rules,lastQuerySites,lastQueryPeakSites,lastQueryAddressBytes;
        Stats(long[] v){
            if(v==null||v.length!=4)throw new IllegalStateException("Estadísticas EtherCore inválidas");
            rules=v[0];lastQuerySites=v[1];lastQueryPeakSites=v[2];lastQueryAddressBytes=v[3];
        }
    }

    public EtherCoreNative(){core=NativeCore.infinite(NativeCore.ZERO);}
    public synchronized void hAll(){core.etherHAll();}
    public synchronized void czNeighbors(){core.etherCzNeighbors();}
    public synchronized void prepareCluster(){core.prepareEtherCluster();}
    public synchronized float[] query(String[] observables){
        if(observables==null||observables.length<1||observables.length>4096)
            throw new IllegalArgumentException("Se requieren de 1 a 4096 observables");
        String[] copy=observables.clone();
        for(String p:copy)InfiniteProgram.validate(p);
        float[] out=core.etherQuery(copy);
        if(out==null||out.length!=copy.length)throw new IllegalStateException("Respuesta EtherCore incompleta");
        return out;
    }
    public synchronized Stats stats(){return core.etherStats();}
    public synchronized long ruleCount(){return stats().rules;}

    /** Five exact observables useful as a visual/read-only probe. */
    public static String[] visualProgram(String center){
        String q=InfiniteProgram.address(center);
        if(q.equals("0"))return new String[]{"X:0 Z:1","Z:0 X:1 Z:2","X:0","Z:0","Z:1"};
        String prev=previous(q),next=next(q);
        return new String[]{
            "Z:"+prev+" X:"+q+" Z:"+next,
            "X:0 Z:1",
            "X:"+q,
            "Z:"+q,
            "Z:"+prev+" Z:"+next
        };
    }
    private static String next(String value){
        char[] d=InfiniteProgram.address(value).toCharArray();
        for(int i=d.length-1;i>=0;i--){
            if(d[i]<'9'){d[i]++;return new String(d);}
            d[i]='0';
        }
        char[] out=new char[d.length+1];out[0]='1';System.arraycopy(d,0,out,1,d.length);return new String(out);
    }
    private static String previous(String value){
        char[] d=InfiniteProgram.address(value).toCharArray();
        boolean zero=true;for(char c:d)if(c!='0'){zero=false;break;}
        if(zero)return "0";
        for(int i=d.length-1;i>=0;i--){
            if(d[i]>'0'){d[i]--;break;}
            d[i]='9';
        }
        int start=0;while(start+1<d.length&&d[start]=='0')start++;
        return new String(Arrays.copyOfRange(d,start,d.length));
    }
    @Override public synchronized void close(){core.close();}
}
