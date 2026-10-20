package com.mundox.solar;

/**
 * Rule-based visibility planner for the solar renderer.
 *
 * The visual domain is conceptually unbounded; no pixel/object visibility list
 * is stored persistently. For the current observation only, conservative body
 * masks are materialized per screen tile. A mask may contain false positives
 * but must never omit a body whose bounding sphere can touch that tile.
 */
public final class SolarVisualEther {
    public static final int DEFAULT_TILE=128;
    private SolarVisualEther(){}

    public static final class Plan {
        public final int tilesX,tilesY,tileSize;
        public final int[] masks;
        public final long candidateReferences,bruteForceReferences;
        public final int maxCandidates,nonEmptyTiles;
        private Plan(int tx,int ty,int tile,int[] m,long refs,long brute,int max,int nonEmpty){
            tilesX=tx;tilesY=ty;tileSize=tile;masks=m;
            candidateReferences=refs;bruteForceReferences=brute;
            maxCandidates=max;nonEmptyTiles=nonEmpty;
        }
        public double referenceRatio(){
            return bruteForceReferences==0?0:(double)candidateReferences/bruteForceReferences;
        }
    }

    public static Plan plan(int width,int height,int tileSize,float tanHalfFov,float aspect,
                            float[] forward,float[] right,float[] up,
                            float[] bodies,float[] appearance,float[] atmosphere,int count){
        if(width<1||height<1||tileSize<16||tileSize>1024||!(tanHalfFov>0)||!(aspect>0)
                ||forward==null||right==null||up==null||forward.length<3||right.length<3||up.length<3
                ||bodies==null||appearance==null||atmosphere==null||count<0||count>24
                ||bodies.length<count*4||appearance.length<count*4||atmosphere.length<count*4)
            throw new IllegalArgumentException("Visual Ether input invalid");
        if(!Float.isFinite(tanHalfFov)||!Float.isFinite(aspect))
            throw new IllegalArgumentException("Visual Ether projection must be finite");
        for(float[] axis:new float[][]{forward,right,up}){
            double norm=0;
            for(int i=0;i<3;i++){
                if(!Float.isFinite(axis[i]))throw new IllegalArgumentException("Visual Ether camera must be finite");
                norm+=(double)axis[i]*axis[i];
            }
            if(Math.abs(norm-1)>1e-4)throw new IllegalArgumentException("Visual Ether camera axes must be unit vectors");
        }
        int tx=1+(width-1)/tileSize,ty=1+(height-1)/tileSize;
        int[] masks=new int[Math.multiplyExact(tx,ty)];
        for(int id=0;id<count;id++){
            int p=id*4;
            double cx=bodies[p],cy=bodies[p+1],cz=bodies[p+2],base=bodies[p+3];
            if(!finite(cx)||!finite(cy)||!finite(cz)||!finite(base)||base<=0)continue;
            double extent=1.0;
            double ring=appearance[p+3];
            double atmosphereTop=atmosphere[p];
            if(finite(ring)&&ring>extent)extent=ring;
            if(finite(atmosphereTop)&&1.0+atmosphereTop>extent)extent=1.0+atmosphereTop;
            double radius=base*extent;
            double x=cx*right[0]+cy*right[1]+cz*right[2];
            double y=cx*up[0]+cy*up[1]+cz*up[2];
            double z=cx*forward[0]+cy*forward[1]+cz*forward[2];
            if(z+radius<=0)continue;
            if(z<=radius){
                fill(masks,tx,ty,id,0,tx-1,0,ty-1);
                continue;
            }
            // Project the enclosing camera-space box using interval division.
            // center +/- r/(z-r) misses the displacement of an off-axis center
            // with depth. All four ratios bound every point of the sphere.
            double near=z-radius,far=z+radius;
            double x0n=(x-radius)/near,x0f=(x-radius)/far;
            double x1n=(x+radius)/near,x1f=(x+radius)/far;
            double y0n=(y-radius)/near,y0f=(y-radius)/far;
            double y1n=(y+radius)/near,y1f=(y+radius)/far;
            double sx=(double)tanHalfFov*aspect,sy=tanHalfFov;
            double marginX=2.0*tileSize/width,marginY=2.0*tileSize/height;
            double left=Math.min(Math.min(x0n,x0f),Math.min(x1n,x1f))/sx-marginX;
            double rightNdc=Math.max(Math.max(x0n,x0f),Math.max(x1n,x1f))/sx+marginX;
            double bottom=Math.min(Math.min(y0n,y0f),Math.min(y1n,y1f))/sy-marginY;
            double top=Math.max(Math.max(y0n,y0f),Math.max(y1n,y1f))/sy+marginY;
            if(rightNdc<-1||left>1||top<-1||bottom>1)continue;
            int x0=tileFloor((Math.max(-1,left)*.5+.5)*width,tileSize,tx);
            int x1=tileFloor((Math.min(1,rightNdc)*.5+.5)*Math.max(0,width-1),tileSize,tx);
            int y0=tileFloor((Math.max(-1,bottom)*.5+.5)*height,tileSize,ty);
            int y1=tileFloor((Math.min(1,top)*.5+.5)*Math.max(0,height-1),tileSize,ty);
            fill(masks,tx,ty,id,x0,x1,y0,y1);
        }
        long refs=0;int max=0,nonEmpty=0;
        for(int mask:masks){
            int n=Integer.bitCount(mask);refs+=n;if(n>max)max=n;if(mask!=0)nonEmpty++;
        }
        return new Plan(tx,ty,tileSize,masks,refs,(long)masks.length*count,max,nonEmpty);
    }

    public static int maskAt(Plan plan,int x,int y){
        if(plan==null||x<0||y<0)throw new IllegalArgumentException("Tile query invalid");
        int tx=Math.min(plan.tilesX-1,x/plan.tileSize);
        int ty=Math.min(plan.tilesY-1,y/plan.tileSize);
        return plan.masks[ty*plan.tilesX+tx];
    }

    private static int tileFloor(double pixel,int tile,int count){
        if(!Double.isFinite(pixel))return 0;
        return Math.max(0,Math.min(count-1,(int)Math.floor(pixel/tile)));
    }
    private static void fill(int[] masks,int tx,int ty,int id,int x0,int x1,int y0,int y1){
        int bit=1<<id;
        for(int y=Math.max(0,y0);y<=Math.min(ty-1,y1);y++)
            for(int x=Math.max(0,x0);x<=Math.min(tx-1,x1);x++)
                masks[y*tx+x]|=bit;
    }
    private static boolean finite(double n){return !Double.isNaN(n)&&!Double.isInfinite(n);}
}
