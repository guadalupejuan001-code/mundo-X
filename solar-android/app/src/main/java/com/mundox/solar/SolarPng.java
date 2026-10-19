package com.mundox.solar;

import android.graphics.Bitmap;
import java.io.IOException;
import java.io.OutputStream;

/** The 4K engine's row-wise RGBA export, adapted to the solar observer's capture. */
public final class SolarPng {
    private SolarPng() {}
    public static void write(SolarRenderer.Capture image,OutputStream output) throws IOException {
        if(image==null||output==null||image.width<1||image.height<1||image.rgba==null
                ||(long)image.width*image.height*4!=image.rgba.length)
            throw new IllegalArgumentException("Captura PNG inválida");
        Bitmap bitmap=Bitmap.createBitmap(image.width,image.height,Bitmap.Config.ARGB_8888);
        try{
            bitmap.setPremultiplied(false);int[] row=new int[image.width];
            for(int y=0;y<image.height;y++){
                int offset=(image.height-1-y)*image.width*4;
                for(int x=0;x<image.width;x++,offset+=4)
                    row[x]=((image.rgba[offset+3]&255)<<24)|((image.rgba[offset]&255)<<16)
                            |((image.rgba[offset+1]&255)<<8)|(image.rgba[offset+2]&255);
                bitmap.setPixels(row,0,image.width,0,y,image.width,1);
            }
            if(!bitmap.compress(Bitmap.CompressFormat.PNG,100,output))throw new IOException("No se pudo codificar PNG");
        }finally{bitmap.recycle();}
    }
}
