package com.mundox.solar;

import com.mundox.graphics.StreamPng;
import java.io.IOException;
import java.io.OutputStream;

/** Shared lossless engine encoder; no second full image allocation for export. */
public final class SolarPng {
    private SolarPng() {}
    public static void write(SolarRenderer.Capture image,OutputStream output) throws IOException {
        if(image==null||output==null||image.width<1||image.height<1||image.rgba==null
                ||(long)image.width*image.height*4!=image.rgba.length)
            throw new IllegalArgumentException("Captura PNG inválida");
        try(StreamPng png=new StreamPng(output,image.width,image.height)){
            for(int y=image.height-1;y>=0;y--)png.writeRow(image.rgba,y*image.width*4);
            png.finish();
        }
    }
}
