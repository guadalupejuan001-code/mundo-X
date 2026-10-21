package android.opengl;
/** Host float matrix adapter. Its output is independently compared with Android
 * input hashes; it does not replace any production source or render an image. */
public final class Matrix {
    private static float length(float x,float y,float z){return (float)Math.sqrt(x*x+y*y+z*z);}
    public static void setLookAtM(float[] m,int o,float ex,float ey,float ez,float cx,float cy,float cz,float ux,float uy,float uz){
        float fx=cx-ex,fy=cy-ey,fz=cz-ez;
        float rlf=1.0f/length(fx,fy,fz);fx*=rlf;fy*=rlf;fz*=rlf;
        float sx=fy*uz-fz*uy,sy=fz*ux-fx*uz,sz=fx*uy-fy*ux;
        float rls=1.0f/length(sx,sy,sz);sx*=rls;sy*=rls;sz*=rls;
        ux=sy*fz-sz*fy;uy=sz*fx-sx*fz;uz=sx*fy-sy*fx;
        m[o]=sx;m[o+1]=ux;m[o+2]=-fx;m[o+3]=0;
        m[o+4]=sy;m[o+5]=uy;m[o+6]=-fy;m[o+7]=0;
        m[o+8]=sz;m[o+9]=uz;m[o+10]=-fz;m[o+11]=0;
        m[o+12]=0;m[o+13]=0;m[o+14]=0;m[o+15]=1;
        for(int i=0;i<4;i++)m[o+12+i]+=m[o+i]*-ex+m[o+4+i]*-ey+m[o+8+i]*-ez;
    }
    public static void perspectiveM(float[] m,int o,float fovy,float aspect,float near,float far){
        float f=1.0f/(float)Math.tan(fovy*(Math.PI/360.0));
        float range=1.0f/(near-far);
        java.util.Arrays.fill(m,o,o+16,0);
        m[o]=f/aspect;m[o+5]=f;m[o+10]=(far+near)*range;m[o+11]=-1;
        m[o+14]=2.0f*far*near*range;
    }
    public static void multiplyMM(float[] out,int oo,float[] a,int ao,float[] b,int bo){
        float[] t=new float[16];
        for(int c=0;c<4;c++)for(int r=0;r<4;r++)
            t[c*4+r]=a[ao+r]*b[bo+c*4]+a[ao+4+r]*b[bo+c*4+1]+a[ao+8+r]*b[bo+c*4+2]+a[ao+12+r]*b[bo+c*4+3];
        System.arraycopy(t,0,out,oo,16);
    }
}
