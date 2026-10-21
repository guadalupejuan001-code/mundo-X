package com.mundox.solar;

public final class SolarVisualEtherChecks {
    private static int checks;
    private static void check(boolean ok,String name){checks++;if(!ok)throw new AssertionError(name);}
    public static void main(String[] args){
        final int width=7680,height=4320,tile=128,count=23;
        float[] f={0,0,-1},r={1,0,0},u={0,1,0};
        float[] bodies=new float[count*4],appearance=new float[count*4],atmosphere=new float[count*4];
        for(int i=0;i<count;i++){
            bodies[i*4]=(i-11)*8f;bodies[i*4+1]=(i%5-2)*3f;bodies[i*4+2]=-80f-i*7f;bodies[i*4+3]=1.2f+(i%3)*.3f;
        }
        bodies[0]=0;bodies[1]=0;bodies[2]=-10;bodies[3]=2;
        appearance[6*4+3]=2.2f;atmosphere[3*4]=.12f;
        SolarVisualEther.Plan plan=SolarVisualEther.plan(width,height,tile,(float)Math.tan(Math.toRadians(47/2.0)),
                (float)width/height,f,r,u,bodies,appearance,atmosphere,count);
        check(plan.tilesX==60&&plan.tilesY==34,"8K tile geometry");
        check((SolarVisualEther.maskAt(plan,width/2,height/2)&1)!=0,"central body candidate");
        check(plan.candidateReferences<plan.bruteForceReferences,"planner reduces candidate references");
        check(plan.maxCandidates<=count,"candidate upper bound");

        long state=0x1234abcdL;
        for(int sample=0;sample<20000;sample++){
            state=state*6364136223846793005L+1442695040888963407L;
            double nx=((state>>>11)&0xffff)/32767.5-1;
            state=state*6364136223846793005L+1442695040888963407L;
            double ny=((state>>>11)&0xffff)/32767.5-1;
            double tx=nx*((double)width/height)*Math.tan(Math.toRadians(47/2.0));
            double ty=ny*Math.tan(Math.toRadians(47/2.0));
            double dz=-1,inv=1/Math.sqrt(tx*tx+ty*ty+1);
            double dx=tx*inv,dy=ty*inv;dz*=inv;
            int px=Math.max(0,Math.min(width-1,(int)((nx*.5+.5)*width)));
            int py=Math.max(0,Math.min(height-1,(int)((ny*.5+.5)*height)));
            int mask=SolarVisualEther.maskAt(plan,px,py);
            for(int id=0;id<count;id++){
                int p=id*4;double extent=Math.max(1,Math.max(appearance[p+3],1+atmosphere[p]));
                double cx=bodies[p],cy=bodies[p+1],cz=bodies[p+2],rad=bodies[p+3]*extent;
                double along=cx*dx+cy*dy+cz*dz;
                if(along<=0)continue;
                double d2=cx*cx+cy*cy+cz*cz-along*along;
                if(d2<=rad*rad)check((mask&(1<<id))!=0,"planner missed an exact ray/sphere candidate");
            }
        }
        adversarial();
        System.out.println("SolarVisualEtherChecks PASS: "+checks+" checks; candidate ratio="+plan.referenceRatio()
                +"; max candidates="+plan.maxCandidates+"; tiles="+plan.masks.length);
    }

    private static void adversarial(){
        float[] f={0,0,1},r={1,0,0},u={0,1,0};
        float[] a=new float[4],at=new float[4];
        float[] body={5.7694357f,0,2.9868784f,1.3490111f};
        float tan=3.9869144f;
        SolarVisualEther.Plan p=SolarVisualEther.plan(7680,4320,128,tan,1,f,r,u,body,a,at,1);
        ray(p,7680,4320,0.832957819,0,tan,1,body);
        java.util.Random random=new java.util.Random(42);
        for(int scene=0;scene<1200;scene++){
            int w=scene%2==0?7680:1920,h=scene%2==0?4320:1080;
            float aspect=(float)w/h;
            tan=(float)(.2+random.nextDouble()*2.8);
            double z=.5+random.nextDouble()*10;
            body[0]=(float)((random.nextDouble()*8-4)*z);
            body[1]=(float)((random.nextDouble()*4-2)*z);
            body[2]=(float)z;body[3]=(float)(z*(.01+random.nextDouble()*1.1));
            p=SolarVisualEther.plan(w,h,128,tan,aspect,f,r,u,body,a,at,1);
            for(int sample=0;sample<512;sample++)
                ray(p,w,h,random.nextDouble()*2-1,random.nextDouble()*2-1,tan,aspect,body);
        }
        boolean rejected=false;
        try{SolarVisualEther.plan(7680,4320,128,Float.POSITIVE_INFINITY,1,f,r,u,body,a,at,1);}
        catch(IllegalArgumentException e){rejected=true;}
        check(rejected,"infinite projection accepted");
        f[0]=Float.NaN;rejected=false;
        try{SolarVisualEther.plan(7680,4320,128,1,1,f,r,u,body,a,at,1);}
        catch(IllegalArgumentException e){rejected=true;}
        check(rejected,"NaN camera accepted");
    }

    private static void ray(SolarVisualEther.Plan p,int w,int h,double nx,double ny,
                            float tan,float aspect,float[] body){
        // Independent ray/sphere intersection; no projected-bound calculation.
        double x=nx*tan*aspect,y=ny*tan,inv=1/Math.sqrt(1+x*x+y*y);
        x*=inv;y*=inv;double z=inv;
        double along=body[0]*x+body[1]*y+body[2]*z;
        double d2=(double)body[0]*body[0]+(double)body[1]*body[1]+(double)body[2]*body[2]-along*along;
        if(along>0&&d2<=(double)body[3]*body[3]){
            int px=Math.max(0,Math.min(w-1,(int)((nx*.5+.5)*w)));
            int py=Math.max(0,Math.min(h-1,(int)((ny*.5+.5)*h)));
            check((SolarVisualEther.maskAt(p,px,py)&1)!=0,"off-axis ray omitted from candidate mask");
        }
    }
}
