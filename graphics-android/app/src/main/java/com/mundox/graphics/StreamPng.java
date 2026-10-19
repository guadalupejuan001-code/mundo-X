package com.mundox.graphics;

import java.io.IOException;
import java.io.OutputStream;
import java.util.zip.CRC32;
import java.util.zip.Deflater;

/** Lossless RGBA8 PNG encoder. One scanline plus a fixed compressed buffer, no Bitmap. */
public final class StreamPng implements AutoCloseable {
    public static final int BUFFER_BYTES=32768;
    private static final byte[] SIGNATURE={(byte)137,80,78,71,13,10,26,10};
    private static final byte[] IHDR={73,72,68,82},IDAT={73,68,65,84},IEND={73,69,78,68};
    private final OutputStream output;
    private final int height,rowBytes;
    private final byte[] row,compressed;
    private final CRC32 crc=new CRC32();
    private final Deflater deflater;
    private int rows;
    private boolean closed,finished;

    /** The caller retains ownership of output, including on cancellation or failure. */
    public StreamPng(OutputStream output,int width,int height) throws IOException {
        if(output==null||width<1||height<1||width>TilePlan.MAX_DIMENSION||height>TilePlan.MAX_DIMENSION)
            throw new IllegalArgumentException("Destino o dimensiones PNG inválidos");
        this.output=output;this.height=height;this.rowBytes=width*4;
        row=new byte[rowBytes+1];compressed=new byte[BUFFER_BYTES];
        deflater=new Deflater(Deflater.BEST_SPEED);
        try{
            output.write(SIGNATURE);
            byte[] header=new byte[13];integer(header,0,width);integer(header,4,height);
            header[8]=8;header[9]=6; // Eight bits per channel, color type RGBA.
            chunk(IHDR,header,header.length);
        }catch(IOException|RuntimeException|Error failure){close();throw failure;}
    }

    /** One unpremultiplied RGBA row, in image top-to-bottom order. */
    public void writeRow(byte[] rgba,int offset) throws IOException {
        active();
        if(rows>=height)throw new IOException("El PNG ya contiene todas sus filas");
        if(rgba==null||offset<0||(long)offset+rowBytes>rgba.length)
            throw new IllegalArgumentException("Fila PNG incompleta");
        row[0]=1; // PNG Sub filter, preserving every channel bit exactly.
        for(int x=0;x<rowBytes;x++)row[x+1]=(byte)((rgba[offset+x]&255)-(x<4?0:rgba[offset+x-4]&255));
        deflater.setInput(row);
        while(!deflater.needsInput()){
            int count=deflater.deflate(compressed);
            if(count>0)chunk(IDAT,compressed,count);
        }
        rows++;
    }

    public void finish() throws IOException {
        active();
        if(rows!=height)throw new IOException("El PNG está incompleto: "+rows+" de "+height+" filas");
        deflater.finish();
        while(!deflater.finished()){
            int count=deflater.deflate(compressed);
            if(count==0)throw new IOException("El compresor PNG no pudo finalizar");
            chunk(IDAT,compressed,count);
        }
        chunk(IEND,compressed,0);finished=true;close();
    }

    private void active() throws IOException {
        if(closed||finished)throw new IOException("El codificador PNG está cerrado");
    }
    private static void integer(byte[] bytes,int at,int value){
        bytes[at]=(byte)(value>>>24);bytes[at+1]=(byte)(value>>>16);bytes[at+2]=(byte)(value>>>8);bytes[at+3]=(byte)value;
    }
    private void number(int value) throws IOException {
        output.write(value>>>24);output.write(value>>>16);output.write(value>>>8);output.write(value);
    }
    private void chunk(byte[] type,byte[] data,int length) throws IOException {
        number(length);output.write(type);output.write(data,0,length);
        crc.reset();crc.update(type,0,4);crc.update(data,0,length);number((int)crc.getValue());
    }

    /** Releases compression state; deliberately never finishes or closes a partial output. */
    @Override public void close(){if(!closed){closed=true;deflater.end();}}
}
