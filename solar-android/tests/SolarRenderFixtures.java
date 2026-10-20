package com.mundox.solar;

import java.io.PrintWriter;
import java.util.Locale;

/** Deterministic catalogue/camera inputs for the production GLES shader.
 * This host fixture is not an Android or physical-device FPS measurement. */
public final class SolarRenderFixtures {
    static void floats(PrintWriter out,String name,float[] a){
        out.print("\""+name+"\":[");for(int i=0;i<a.length;i++){if(i>0)out.print(',');out.print(Float.toString(a[i]));}out.print(']');
    }
    static void ints(PrintWriter out,String name,int[] a){
        out.print("\""+name+"\":[");for(int i=0;i<a.length;i++){if(i>0)out.print(',');out.print(a[i]);}out.print(']');
    }
    static double length(double[] a){return Math.sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
    static float[] unit(double x,double y,double z){double n=Math.sqrt(x*x+y*y+z*z);return new float[]{(float)(x/n),(float)(y/n),(float)(z/n)};}
    static double[] cross(double[] a,double[] b){return new double[]{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
    public static void main(String[] args)throws Exception{
        int width=Integer.parseInt(args[0]),height=Integer.parseInt(args[1]);
        PrintWriter out=new PrintWriter(args[2],"UTF-8");out.println('[');
        for(int scene=0;scene<32;scene++){
            if(scene>0)out.println(',');
            int selected=scene<24?Math.max(0,scene-1):new int[]{3,2,5,6,9,14,7,8}[scene-24];
            boolean entry=scene>=24;
            double date=2461331.0+scene*13.125;
            int n=SolarData.BODIES.length;
            double[][] positions=new double[n][3],physical=new double[n][];
            double[] radii=new double[n];
            float[] bodies=new float[n*4],appearance=new float[n*4],colors=new float[n*4],axes=new float[n*3],atmosphere=new float[n*4],atmoColors=new float[n*3];
            int[] layers=new int[n];java.util.Arrays.fill(layers,-1);layers[3]=0;layers[9]=1;layers[5]=2;
            for(SolarData.Body b:SolarData.BODIES){physical[b.id]=SolarData.position(b.id,date);radii[b.id]=b.id==0?5.2:Math.max(.17,.92*Math.pow(b.radiusKm/6371.0,.49));}
            for(SolarData.Body b:SolarData.BODIES){
                int id=b.id,p=id*4;
                if(b.parent<=0){double d=length(physical[id]),s=d<1e-14?0:25*Math.log1p(d*2)/d;for(int k=0;k<3;k++)positions[id][k]=physical[id][k]*s;}
                else{double rr=b.semiMajorAU*SolarData.AU_KM/SolarData.BODIES[b.parent].radiusKm;
                    double scale=radii[b.parent]*(2.1+Math.log1p(Math.max(1,rr))*1.1)/Math.max(1e-12,b.semiMajorAU);
                    for(int k=0;k<3;k++)positions[id][k]=positions[b.parent][k]+(physical[id][k]-physical[b.parent][k])*scale;}
                double period=Math.abs(b.rotationHours)/24.0;
                appearance[p]=(float)(((date-SolarData.J2000)%period)/period*Math.PI*2);appearance[p+1]=(float)Math.toRadians(b.obliquityDeg);
                appearance[p+2]=(float)b.ringInner;appearance[p+3]=(float)b.ringOuter;
                System.arraycopy(b.color,0,colors,p,4);
                double[] pole;
                if(id<=8)pole=SolarData.spinAxis(id);
                else{double[] a=SolarData.orbitPosition(id,0,date),c=SolarData.orbitPosition(id,.0001,date),parent=physical[b.parent];for(int k=0;k<3;k++){a[k]-=parent[k];c[k]-=parent[k];}pole=cross(a,c);double d=length(pole);for(int k=0;k<3;k++)pole[k]/=d;}
                for(int k=0;k<3;k++)axes[id*3+k]=(float)pole[k];
                SolarEnvironment e=SolarEnvironment.environment(id);
                atmosphere[p]=(float)(e.atmosphereTopKm/b.radiusKm);atmosphere[p+1]=(float)(e.scaleHeightKm/b.radiusKm);atmosphere[p+2]=e.opacity;atmosphere[p+3]=e.type;
                System.arraycopy(e.color,0,atmoColors,id*3,3);
            }
            double yaw=.63+scene*.09,pitch=.21,distance=radii[selected]*5.5;
            double[] target=positions[selected].clone();if(scene==0){target=new double[3];distance=650;}
            double[] eye={target[0]+distance*Math.sin(yaw)*Math.cos(pitch),target[1]+distance*Math.sin(pitch),target[2]+distance*Math.cos(yaw)*Math.cos(pitch)};
            float[] f=unit(target[0]-eye[0],target[1]-eye[1],target[2]-eye[2]);
            float[] r=unit(-f[2],0,f[0]);float[] up=unit(r[1]*f[2]-r[2]*f[1],r[2]*f[0]-r[0]*f[2],r[0]*f[1]-r[1]*f[0]);
            float near=.001f,far=2000;double scale=1,altitude=35;
            if(entry){
                altitude=SolarEnvironment.environment(selected).entryAltitudeKm;
                double rr=radii[selected];eye=new double[]{target[0],target[1]+rr*(1+altitude/SolarData.BODIES[selected].radiusKm),target[2]};
                f=unit(0,-.25,-1);r=new float[]{1,0,0};up=unit(0,-f[2],f[1]);scale=1/rr;near=1e-6f;far=20000;
            }
            for(int i=0;i<n;i++){for(int k=0;k<3;k++)bodies[i*4+k]=(float)((positions[i][k]-eye[k])*scale);bodies[i*4+3]=(float)(radii[i]*scale);}
            float tan=(float)Math.tan(Math.toRadians(47/2.0)),aspect=(float)width/height;
            SolarVisualEther.Plan plan=SolarVisualEther.plan(width,height,128,tan,aspect,f,r,up,bodies,appearance,atmosphere,n);
            int[] masks=new int[n];SolarOcclusion.masks(bodies,n,masks);
            out.printf(Locale.ROOT,"{\"scene\":%d,\"selected\":%d,\"width\":%d,\"height\":%d,\"near\":%s,\"far\":%s,\"aspect\":%s,\"tan\":%s,\"entry\":%s,\"altitude\":%s,\"radiusKm\":%s,\"tilesX\":%d,\"tilesY\":%d,",scene,selected,width,height,near,far,aspect,tan,entry,altitude,SolarData.BODIES[selected].radiusKm,plan.tilesX,plan.tilesY);
            floats(out,"bodies",bodies);out.print(',');floats(out,"appearance",appearance);out.print(',');floats(out,"colors",colors);out.print(',');floats(out,"axes",axes);out.print(',');floats(out,"atmosphere",atmosphere);out.print(',');floats(out,"atmoColors",atmoColors);out.print(',');floats(out,"forward",f);out.print(',');floats(out,"right",r);out.print(',');floats(out,"up",up);out.print(',');ints(out,"layers",layers);out.print(',');ints(out,"tiles",plan.masks);out.print(',');ints(out,"eclipses",masks);out.print('}');
        }
        out.println("]");out.close();
    }
}
