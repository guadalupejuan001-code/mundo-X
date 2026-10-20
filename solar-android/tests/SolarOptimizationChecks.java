package com.mundox.solar;

import java.lang.management.ManagementFactory;
import java.util.Arrays;
import java.util.Random;

/** Baseline positions remain an independent per-body path. Broad-phase tests
 * use surface-to-Sun rays directly, without projected/capsule bounds. */
public final class SolarOptimizationChecks {
    static long checks;static volatile double sink;
    static void check(boolean ok,String name){checks++;if(!ok)throw new AssertionError(name);}
    static boolean same(double a,double b){return Double.doubleToRawLongBits(a)==Double.doubleToRawLongBits(b);}
    static void positions(){
        SolarData.Ephemeris e=new SolarData.Ephemeris();double[][] out=new double[23][3];
        for(int i=0;i<4096;i++){
            double jd=SolarData.MIN_JD-1+(SolarData.MAX_JD-SolarData.MIN_JD+2)*i/4095.0;
            e.positions(jd,out);
            for(int id=0;id<23;id++){double[] ref=SolarData.position(id,jd);for(int k=0;k<3;k++)check(same(out[id][k],ref[k]),"ephemeris changed bits");}
        }
    }
    static void visibility(){
        SolarVisualEther.Workspace w=new SolarVisualEther.Workspace();Random random=new Random(91);
        float[] forward={0,0,1},right={1,0,0},up={0,1,0},b=new float[23*4],a=new float[23*4],at=new float[23*4];
        int[] storage=null;
        for(int scene=0;scene<300;scene++){
            for(int i=0;i<23;i++){b[4*i]=(random.nextFloat()-.5f)*100;b[4*i+1]=(random.nextFloat()-.5f)*100;b[4*i+2]=random.nextFloat()*120-20;b[4*i+3]=.1f+random.nextFloat()*10;a[4*i+3]=i%6==0?2.4f:0;at[4*i]=i%4==0?.2f:0;}
            int width=scene<100?360:scene<200?961:7680,height=scene<100?203:scene<200?541:4320;
            SolarVisualEther.Plan ref=SolarVisualEther.plan(width,height,128,.43f,(float)width/height,forward,right,up,b,a,at,23);
            SolarVisualEther.Plan got=w.update(width,height,128,.43f,(float)width/height,forward,right,up,b,a,at,23);
            check(Arrays.equals(ref.masks,got.masks),"workspace candidate mismatch");
            if(scene%100!=0)check(storage==got.masks,"workspace reallocated fixed dimensions");storage=got.masks;
        }
    }
    static void eclipses(){
        Random random=new Random(20261020);float[] b=new float[23*4];int[] mask=new int[23];
        long omitted=0;
        for(int scene=0;scene<700;scene++){
            for(int i=0;i<23;i++){b[4*i]=(random.nextFloat()-.5f)*200;b[4*i+1]=(random.nextFloat()-.5f)*200;b[4*i+2]=(random.nextFloat()-.5f)*200;b[4*i+3]=.1f+random.nextFloat()*5;}
            if(scene%4==0)for(int i=1;i<23;i++){b[4*i]=i*5;b[4*i+1]=0;b[4*i+2]=0;} // aligned eclipses
            SolarOcclusion.masks(b,23,mask);
            for(int target=1;target<23;target++)for(int sample=0;sample<32;sample++){
                double x=random.nextDouble()*2-1,y=random.nextDouble()*2-1,z=random.nextDouble()*2-1,inv=1/Math.sqrt(x*x+y*y+z*z);
                x=b[target*4]+x*inv*b[target*4+3];y=b[target*4+1]+y*inv*b[target*4+3];z=b[target*4+2]+z*inv*b[target*4+3];
                double dx=b[0]-x,dy=b[1]-y,dz=b[2]-z,length=Math.sqrt(dx*dx+dy*dy+dz*dz);dx/=length;dy/=length;dz/=length;
                for(int j=1;j<23;j++)if(j!=target&&(mask[target]&(1<<j))==0){
                    double cx=b[j*4]-x,cy=b[j*4+1]-y,cz=b[j*4+2]-z,along=cx*dx+cy*dy+cz*dz;
                    boolean blocks=false;
                    if(along>0&&along<length){cx-=dx*along;cy-=dy*along;cz-=dz*along;double radial=Math.sqrt(cx*cx+cy*cy+cz*cz),penumbra=b[3]*along/length;blocks=radial<b[j*4+3]+Math.max(penumbra,.00001);}
                    check(!blocks,"pruned a possible eclipse");omitted++;
                }
            }
        }
        System.out.println("eclipse_omission_checks="+omitted);
    }
    static void physics(){
        SolarFlightPhysics.State state=new SolarFlightPhysics.State(35,0);double altitude=state.altitudeKm;
        boolean rejected=false;try{SolarFlightPhysics.step(3,SolarEnvironment.environment(2),state,0,.1);}catch(IllegalArgumentException e){rejected=true;}
        check(rejected&&same(state.altitudeKm,altitude)&&same(state.radialVelocityKmS,0),"environment mismatch changed state");
        state.radialVelocityKmS=Double.NaN;rejected=false;
        try{SolarFlightPhysics.step(3,SolarEnvironment.environment(3),state,0,.1);}catch(IllegalArgumentException e){rejected=true;}
        check(rejected&&same(state.altitudeKm,altitude),"nonfinite public state accepted");
    }
    static void bench(){
        com.sun.management.ThreadMXBean bean=(com.sun.management.ThreadMXBean)ManagementFactory.getThreadMXBean();
        SolarData.Ephemeris e=new SolarData.Ephemeris();double[][] out=new double[23][3];
        for(int run=0;run<6;run++)for(int order=0;order<2;order++){
            boolean batch=(run+order)%2==0;double sum=0;long bytes=bean.getThreadAllocatedBytes(Thread.currentThread().getId()),start=System.nanoTime();
            for(int i=0;i<12000;i++){
                double date=2461331+i*.03125;
                if(batch)e.positions(date,out);else for(int id=0;id<23;id++){double[] p=SolarData.position(id,date);System.arraycopy(p,0,out[id],0,3);}
                for(int id=0;id<23;id++)sum+=out[id][0]+out[id][1]+out[id][2];
            }
            long elapsed=System.nanoTime()-start,allocated=bean.getThreadAllocatedBytes(Thread.currentThread().getId())-bytes;sink=sum;
            System.out.println("{\"run\":"+run+",\"batch\":"+batch+",\"ms\":"+elapsed/1e6+",\"allocatedBytes\":"+allocated+",\"checksum\":"+sum+"}");
        }
    }
    public static void main(String[] args){
        if(args.length>0&&args[0].equals("bench")){bench();return;}
        positions();visibility();eclipses();physics();System.out.println("SolarOptimizationChecks PASS: "+checks);
    }
}
