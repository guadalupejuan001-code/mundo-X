package com.mundox.solar;

/** Experimental host-only broad phase; not enabled in the shipping APK after
 * paired GLES measurements failed to establish a consistent speed benefit.
 * Conservative broad phase for the existing soft eclipse function.
 * Every surface-to-Sun segment lies in a capsule around the body-centre-to-Sun
 * segment. Inflate that capsule by the surface, occluder and Sun radii. Only
 * bodies outside it can be removed. The fragment shader still evaluates the
 * original eclipse formula, in original catalogue order, for every candidate.
 */
public final class SolarOcclusion {
    private SolarOcclusion() {}

    public static void masks(float[] bodies, int count, int[] out) {
        if (count < 1 || count > 24 || bodies == null || bodies.length < count*4
                || out == null || out.length < count)
            throw new IllegalArgumentException("Invalid eclipse catalogue");
        double magnitude=1;
        for(int k=0;k<count*4;k++) {
            if(!Float.isFinite(bodies[k])) throw new IllegalArgumentException("Nonfinite body");
            magnitude=Math.max(magnitude,Math.abs(bodies[k]));
        }
        for(int i=0;i<count;i++) if(!(bodies[i*4+3]>0))
            throw new IllegalArgumentException("Invalid body radius");
        // Accommodate single-precision subtraction/projection in the shader,
        // including camera-relative positions at very large separation.
        double padding=64.0*Math.ulp((float)magnitude);
        int all=(1<<count)-2;
        out[0]=0;
        for(int i=1;i<count;i++) {
            int p=i*4,mask=0;
            double dx=(double)bodies[0]-bodies[p],dy=(double)bodies[1]-bodies[p+1],dz=(double)bodies[2]-bodies[p+2];
            double length2=dx*dx+dy*dy+dz*dz;
            if(length2<=((double)bodies[p+3]+padding)*((double)bodies[p+3]+padding)) {
                out[i]=all&~(1<<i);continue;
            }
            for(int j=1;j<count;j++) {
                if(i==j)continue;
                int q=j*4;
                double x=(double)bodies[q]-bodies[p],y=(double)bodies[q+1]-bodies[p+1],z=(double)bodies[q+2]-bodies[p+2];
                double t=Math.max(0,Math.min(1,(x*dx+y*dy+z*dz)/length2));
                x-=t*dx;y-=t*dy;z-=t*dz;
                double radius=(double)bodies[p+3]+bodies[q+3]+Math.max(bodies[3],.00001)+padding;
                if(x*x+y*y+z*z<=radius*radius)mask|=1<<j;
            }
            out[i]=mask;
        }
    }
}
