package com.mundox.etherworld;

import org.json.JSONArray;
import org.json.JSONObject;
import java.util.HashMap;
import java.util.Map;

/**
 * Sparse, deterministic, procedurally generated world.
 * Infinite mathematical coordinate plane, finite local CPU/RAM budget.
 * Coordinates are virtual; this is NOT a scan of the user's room.
 */
public final class EtherWorld {
    public static final long SEED=0x5845544845524cL;
    public static final int VIEW_RADIUS=8;
    private final Map<Long,Integer> changes=new HashMap<>();
    private long steps;
    private double x,z,yaw;

    public double x(){return x;}
    public double z(){return z;}
    public double yaw(){return yaw;}
    public long steps(){return steps;}
    public int edits(){return changes.size();}

    public void turn(double radians){
        if(!Double.isFinite(radians))return;
        yaw=wrap(yaw+Math.max(-.5,Math.min(.5,radians)));
    }
    public void move(double amount){
        if(!Double.isFinite(amount))return;
        double dist=Math.max(-2,Math.min(2,amount));
        x+=Math.sin(yaw)*dist;
        z-=Math.cos(yaw)*dist;
        if(Math.abs(x)>1e7 || Math.abs(z)>1e7) {
            x=0;z=0;
        }
    }
    public void step(){steps++;}
    public void edit(boolean place) {
        if(changes.size()>=4096 && !changes.containsKey(key((int)Math.round(x+Math.sin(yaw)*3),
               (int)Math.round(z-Math.cos(yaw)*3))))return;
        int gx=(int)Math.round(x+Math.sin(yaw)*3);
        int gz=(int)Math.round(z-Math.cos(yaw)*3);
        changes.put(key(gx,gz),place?1:-1);
    }
    public int value(int gx,int gz) {
        Integer override=changes.get(key(gx,gz));
        if(override!=null)return override;
        long h=hash(gx,gz);
        int chance=(int)((h>>>1)%100);
        return chance<16?1:0;
    }
    public float[] visible() {
        float[] out=new float[64*6];
        int n=0;
        int cx=(int)Math.round(x),cz=(int)Math.round(z);
        // Prioritize nearest cells to keep EtherRenderer's 64-body budget bounded.
        for(int radius=0;radius<=VIEW_RADIUS && n<64;radius++) {
            for(int dz=-radius;dz<=radius && n<64;dz++) {
                for(int dx=-radius;dx<=radius && n<64;dx++) {
                    if(Math.max(Math.abs(dx),Math.abs(dz))!=radius)continue;
                    int gx=cx+dx,gz=cz+dz;
                    if(value(gx,gz)!=1)continue;
                    long h=hash(gx,gz);
                    double phase=((h>>>11)&1023)/1024.0*6.283185307179586;
                    double height=0.9+0.22*Math.sin(steps*.045+phase);
                    out[n*6]=gx;
                    out[n*6+1]=(float)height;
                    out[n*6+2]=gz;
                    out[n*6+3]=.40f+(float)((h&7)*.065);
                    out[n*6+4]=(float)(((h>>>14)&255)/255.0);
                    out[n*6+5]=(float)(.45+.4*((h>>>24)&255)/255.);
                    n++;
                }
            }
        }
        float[] actual=new float[n*6];
        System.arraycopy(out,0,actual,0,actual.length);
        return actual;
    }
    public String save() throws Exception {
        JSONObject root=new JSONObject();
        root.put("version",1);
        root.put("seed",SEED);
        root.put("x",x);root.put("z",z);root.put("yaw",yaw);root.put("steps",steps);
        JSONArray edits=new JSONArray();
        for(Map.Entry<Long,Integer> e:changes.entrySet()) {
            JSONArray item=new JSONArray();
            item.put((int)(e.getKey()>>32));
            item.put((int)(long)e.getKey());
            item.put(e.getValue());
            edits.put(item);
        }
        root.put("edits",edits);
        return root.toString();
    }
    public static EtherWorld restore(String json) throws Exception {
        JSONObject root=new JSONObject(json);
        if(root.getInt("version")!=1 || root.getLong("seed")!=SEED)
            throw new IllegalArgumentException("Versión o semilla incompatible");
        EtherWorld w=new EtherWorld();
        w.x=root.getDouble("x");w.z=root.getDouble("z");w.yaw=root.getDouble("yaw");
        w.steps=root.getLong("steps");
        if(!Double.isFinite(w.x)||!Double.isFinite(w.z)||!Double.isFinite(w.yaw)
             ||Math.abs(w.x)>1e7||Math.abs(w.z)>1e7||w.steps<0)
            throw new IllegalArgumentException("Estado espacial inválido");
        JSONArray edits=root.getJSONArray("edits");
        if(edits.length()>4096)throw new IllegalArgumentException("Demasiadas modificaciones");
        for(int i=0;i<edits.length();i++){
            JSONArray row=edits.getJSONArray(i);
            int value=row.getInt(2);
            if(value!=1 && value!=-1)throw new IllegalArgumentException("Edición inválida");
            w.changes.put(key(row.getInt(0),row.getInt(1)),value);
        }
        return w;
    }
    static long key(int x,int z){return ((long)x<<32)|(z&0xffffffffL);}
    static long hash(int x,int z){
        long n=SEED ^ ((long)x*0x9e3779b97f4a7c15L) ^ ((long)z*0xc2b2ae3d27d4eb4fL);
        n^=n>>>30;n*=0xbf58476d1ce4e5b9L;
        n^=n>>>27;n*=0x94d049bb133111ebL;
        return n^(n>>>31);
    }
    private static double wrap(double x){
        double tau=2*Math.PI;
        x%=tau;
        return x<0?x+tau:x;
    }
}
