package com.mundox.solar;

import android.content.Context;
import java.io.*;
import java.lang.reflect.*;
import java.nio.FloatBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;

/** Calls the unchanged shipping renderer's Java preparation without invoking GL.
 * Replays fixture controls 0..7 because entryAltitude survives focus(). */
public final class AndroidInputFixture {
    private static final String[] INPUTS={"packedBodies","appearance","colors","axes","poleBasis","atmosphere","atmosphereColors","layers","quantum","ether","forward","right","up","vp","orbitCache","cameraEye","jd","clockSeconds","entryAltitude","near","far","aspect","lines","points","paused","fields","wind","orbits","entryMode"};
    private static Field field(String n)throws Exception{Field f=SolarRenderer.class.getDeclaredField(n);f.setAccessible(true);return f;}
    private static Object get(SolarRenderer r,String n)throws Exception{return field(n).get(r);}
    private static void set(SolarRenderer r,String n,Object v)throws Exception{field(n).set(r,v);}
    private static void invoke(SolarRenderer r,String n)throws Exception{Method m=SolarRenderer.class.getDeclaredMethod(n);m.setAccessible(true);m.invoke(r);}
    private static void camera(SolarRenderer r)throws Exception{Method m=SolarRenderer.class.getDeclaredMethod("camera",double.class);m.setAccessible(true);m.invoke(r,0.0);}
    private static double[] cross(double[] a,double[] b){return new double[]{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
    private static void normalize(double[] v){double n=Math.sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(int i=0;i<3;i++)v[i]/=n;}
    private static void fixedMoonPoles(SolarRenderer r)throws Exception{
        float[] axes=(float[])get(r,"axes");double[][] basis=(double[][])get(r,"poleBasis");
        for(SolarData.Body body:SolarData.BODIES)if(body.id>8){
            double[] a=SolarData.orbitPosition(body.id,0,2461331.0),b=SolarData.orbitPosition(body.id,.0001,2461331.0),parent=SolarData.position(body.parent,2461331.0);
            for(int i=0;i<3;i++){a[i]-=parent[i];b[i]-=parent[i];}
            double[] pole=cross(a,b);normalize(pole);
            double[] reference=Math.abs(pole[2])>.98?new double[]{0,1,0}:new double[]{0,0,1};
            double[] bx=cross(pole,reference);normalize(bx);double[] bz=cross(bx,pole);
            for(int i=0;i<3;i++){axes[body.id*3+i]=(float)pole[i];basis[body.id][i]=bx[i];basis[body.id][3+i]=pole[i];basis[body.id][6+i]=bz[i];}
        }
    }
    private static void bits(Object value,StringBuilder out){
        if(value==null){out.append("null;");return;}
        if(value instanceof FloatBuffer){FloatBuffer b=(FloatBuffer)value;for(int i=0;i<b.limit();i++)bits(b.get(i),out);}
        else if(value.getClass().isArray()){out.append('[');for(int i=0;i<Array.getLength(value);i++)bits(Array.get(value,i),out);out.append(']');}
        else if(value instanceof Float)out.append(Integer.toHexString(Float.floatToIntBits((Float)value))).append(';');
        else if(value instanceof Double)out.append(Long.toHexString(Double.doubleToLongBits((Double)value))).append(';');
        else out.append(value).append(';');
    }
    private static String hash(Object value)throws Exception{
        StringBuilder data=new StringBuilder();bits(value,data);
        byte[] digest=MessageDigest.getInstance("SHA-256").digest(data.toString().getBytes(StandardCharsets.UTF_8));
        StringBuilder out=new StringBuilder();for(byte b:digest)out.append(String.format(Locale.ROOT,"%02x",b&255));return out.toString();
    }
    private static void json(PrintWriter out,Object v){
        if(v==null){out.print("null");return;}
        if(v instanceof String){out.print('"');out.print(((String)v).replace("\\","\\\\").replace("\"","\\\"").replace("\n","\\n"));out.print('"');}
        else if(v instanceof FloatBuffer){FloatBuffer b=(FloatBuffer)v;out.print('[');for(int i=0;i<b.limit();i++){if(i>0)out.print(',');json(out,b.get(i));}out.print(']');}
        else if(v.getClass().isArray()){out.print('[');for(int i=0;i<Array.getLength(v);i++){if(i>0)out.print(',');json(out,Array.get(v,i));}out.print(']');}
        else if(v instanceof Map){out.print('{');boolean first=true;for(Object raw:((Map<?,?>)v).entrySet()){Map.Entry<?,?> e=(Map.Entry<?,?>)raw;if(!first)out.print(',');first=false;json(out,e.getKey());out.print(':');json(out,e.getValue());}out.print('}');}
        else out.print(v);
    }
    private static void write(File f,Object value)throws Exception{try(PrintWriter out=new PrintWriter(f,"UTF-8")){json(out,value);out.println();}}
    public static void main(String[] args)throws Exception{
        File out=new File(args[1]);out.mkdirs();
        try(QuantumSolarBridge bridge=new QuantumSolarBridge(2)){
            SolarRenderer r=new SolarRenderer(new Context(args[0]),bridge,new SolarRenderer.Listener(){public void status(String v){}public void selected(int id){}});
            r.setPaused(true);r.setQuality(360);fixedMoonPoles(r);
            set(r,"width",360);set(r,"height",203);set(r,"viewWidth",360);set(r,"viewHeight",203);set(r,"aspect",360f/203f);
            int[] layers=(int[])get(r,"layers");int layer=0;
            for(SolarData.Body b:SolarData.BODIES)if(b.texture!=null&&!b.texture.isEmpty())layers[b.id]=layer++;
            int[] bodies={3,3,6,0,3,5,9,7};
            for(int k=0;k<8;k++){
                r.setJulianDate(2461331.0+k*13.125);r.setRealScale(k==6);r.focus(bodies[k]);r.setAnimationTimeForTest(7.25f);
                r.setFields(k==1);r.setWind(true);r.setOrbits(true);r.setAtmosphereEnabled(true);bridge.setPreset(k%4);
                if(k==4||k==5)r.enterAtmosphere(bodies[k]);r.rotate(13*k,-3*k);
                invoke(r,"syncWorld");invoke(r,"syncQuantum");camera(r);
                FloatBuffer lines=(FloatBuffer)get(r,"lines"),points=(FloatBuffer)get(r,"points");lines.clear();points.clear();
                if(!r.isEntryMode()){invoke(r,"orbitLines");invoke(r,"fieldLines");invoke(r,"tracerParticles");if(lines.position()>0)lines.flip();if(points.position()>0)points.flip();}
                Map<String,Object> hashes=new LinkedHashMap<>(),values=new LinkedHashMap<>();
                for(String n:INPUTS){Object value=get(r,n);hashes.put(n,hash(value));values.put(n,value);}
                write(new File(out,"case-"+k+"-input-hashes.json"),hashes);
                if(k!=7)continue;
                write(new File(out,"case-7-input-values.json"),values);
                int count=SolarData.BODIES.length;float tan=(float)field("TAN_HALF_FOV").get(null);
                SolarVisualEther.Plan plan=SolarVisualEther.plan(360,203,SolarVisualEther.DEFAULT_TILE,tan,(float)get(r,"aspect"),(float[])get(r,"forward"),(float[])get(r,"right"),(float[])get(r,"up"),(float[])get(r,"packedBodies"),(float[])get(r,"appearance"),(float[])get(r,"atmosphere"),count);
                int[] eclipses=new int[count];SolarOcclusion.masks((float[])get(r,"packedBodies"),count,eclipses);
                Map<String,Object> f=new LinkedHashMap<>();
                f.put("scene",7);f.put("selected",7);f.put("width",360);f.put("height",203);f.put("near",get(r,"near"));f.put("far",get(r,"far"));f.put("aspect",get(r,"aspect"));f.put("tan",tan);f.put("entry",r.isEntryMode());f.put("altitude",get(r,"entryAltitude"));f.put("radiusKm",SolarData.BODIES[7].radiusKm);f.put("tilesX",plan.tilesX);f.put("tilesY",plan.tilesY);f.put("tileSize",plan.tileSize);
                String[][] mappings={{"bodies","packedBodies"},{"appearance","appearance"},{"colors","colors"},{"axes","axes"},{"atmosphere","atmosphere"},{"atmoColors","atmosphereColors"},{"forward","forward"},{"right","right"},{"up","up"},{"layers","layers"},{"quantum","quantum"},{"ether","ether"}};
                for(String[] m:mappings)f.put(m[0],get(r,m[1]));
                f.put("tiles",plan.masks);f.put("eclipses",eclipses);f.put("time",7.25f);f.put("atmosphereEnabled",true);f.put("coupling",1);f.put("mapOrder",new String[]{"earth.png","jupiter.jpg","moon.jpg"});f.put("nativeRevision",bridge.nativeRevision());f.put("nativeEpoch",bridge.nativeEpoch());f.put("activeSites",bridge.activeSites());
                write(new File(out,"fixture-android-case-7.json"),new Object[]{f});
                System.out.println("tiles="+Arrays.toString(plan.masks));System.out.println("hashes="+hashes);
            }
        }
    }
}
