package com.mundox.solar;

import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import javax.imageio.ImageIO;

/** Host ImageIO is independent of both Android Bitmap and the shared encoder. */
public final class SolarPngChecks {
    static byte pixel(int index){return (byte)((index*31)^(index>>>3)^(index>>>11));}
    static byte[] pixels(int w,int h){byte[] b=new byte[w*h*4];for(int i=0;i<b.length;i++)b[i]=pixel(i);return b;}
    public static void main(String[] args)throws Exception{
        long checked=0;
        for(int[] size:new int[][]{{1,1},{2,3},{257,129},{961,541}}){
            int w=size[0],h=size[1];byte[] bytes=pixels(w,h);ByteArrayOutputStream out=new ByteArrayOutputStream();
            SolarPng.write(new SolarRenderer.Capture(w,h,bytes),out);
            BufferedImage decoded=ImageIO.read(new ByteArrayInputStream(out.toByteArray()));
            if(decoded.getWidth()!=w||decoded.getHeight()!=h)throw new AssertionError("PNG geometry changed");
            for(int y=0;y<h;y++)for(int x=0;x<w;x++){
                int p=((h-1-y)*w+x)*4,rgba=decoded.getRGB(x,y);
                int expected=((bytes[p+3]&255)<<24)|((bytes[p]&255)<<16)|((bytes[p+1]&255)<<8)|(bytes[p+2]&255);
                if(rgba!=expected)throw new AssertionError("PNG channel/orientation changed");checked++;
            }
        }
        final boolean[] closed={false};boolean failed=false;
        OutputStream broken=new OutputStream(){public void write(int b)throws IOException{throw new IOException("test failure");}public void close(){closed[0]=true;}};
        try{SolarPng.write(new SolarRenderer.Capture(1,1,pixels(1,1)),broken);}catch(IOException expected){failed=true;}
        if(!failed||closed[0])throw new AssertionError("Output ownership/error propagation changed");
        System.out.println("SolarPngChecks PASS: "+checked+" exact independently decoded pixels; output failure preserved");
        if(args.length>0){
            int w=7680,h=4320;byte[] bytes=pixels(w,h);
            com.sun.management.ThreadMXBean bean=(com.sun.management.ThreadMXBean)java.lang.management.ManagementFactory.getThreadMXBean();
            long allocated=bean.getThreadAllocatedBytes(Thread.currentThread().getId()),start=System.nanoTime();
            try(FileOutputStream out=new FileOutputStream(args[0])){SolarPng.write(new SolarRenderer.Capture(w,h,bytes),out);}
            System.out.println("{\"width\":"+w+",\"height\":"+h+",\"ms\":"+(System.nanoTime()-start)/1e6+",\"extraJavaAllocatedBytes\":"+(bean.getThreadAllocatedBytes(Thread.currentThread().getId())-allocated)+",\"inputBytes\":"+bytes.length+"}");
        }
    }
}
