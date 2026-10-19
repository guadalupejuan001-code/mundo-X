package com.mundox.graphics;

/** Native pixel tiles, ordered as complete top-to-bottom PNG strips. No scaled pixels. */
public final class TilePlan {
    public static final int MAX_TILE_WIDTH=1024,MAX_STRIP_ROWS=64,MAX_DIMENSION=16384;
    public final RenderResolution.Size image;
    public final int tileWidth,stripRows,columns,bands,tileCount;
    /** Explicit band, readback, PNG buffers and estimated tile attachments only. */
    public final long workingBytes;

    public TilePlan(RenderResolution.Size image,RenderResolution.Limits limits,int tileWidth,int stripRows){
        if(image==null||limits==null)throw new IllegalArgumentException("Faltan dimensiones o límites GLES");
        if(image.width>MAX_DIMENSION||image.height>MAX_DIMENSION)
            throw new IllegalArgumentException("La exportación admite como máximo 16384 píxeles por eje");
        if(tileWidth<2||tileWidth>MAX_TILE_WIDTH||(tileWidth&1)!=0
                ||stripRows<2||stripRows>MAX_STRIP_ROWS||(stripRows&1)!=0)
            throw new IllegalArgumentException("Usa regiones pares de 2–1024 píxeles y franjas pares de 2–64 filas");
        this.image=image;
        this.tileWidth=aligned(Math.min(image.width,Math.min(tileWidth,Math.min(limits.texture,
                Math.min(limits.renderbuffer,limits.viewportWidth)))),image.width);
        this.stripRows=aligned(Math.min(image.height,Math.min(stripRows,Math.min(limits.texture,
                Math.min(limits.renderbuffer,limits.viewportHeight)))),image.height);
        limits.require(new RenderResolution.Size(this.tileWidth,this.stripRows));
        columns=(image.width+this.tileWidth-1)/this.tileWidth;
        bands=(image.height+this.stripRows-1)/this.stripRows;
        tileCount=Math.multiplyExact(columns,bands);
        workingBytes=(long)image.width*this.stripRows*4
                +(long)this.tileWidth*this.stripRows*12+(long)image.width*4+1+StreamPng.BUFFER_BYTES;
    }

    private static int aligned(int extent,int full){
        if(full==1)return 1;
        if(extent<2)throw new IllegalArgumentException("GLES no admite una región de dos píxeles");
        // Derivative quads must start on the same even global coordinates as one full image.
        return extent&~1;
    }

    public static final class Tile {
        /** x/y are bottom-left GL coordinates in the complete output image. */
        public final int x,y,width,height,topRow;
        private Tile(int x,int y,int width,int height,int topRow){
            this.x=x;this.y=y;this.width=width;this.height=height;this.topRow=topRow;
        }
    }

    public Tile tile(int index){
        if(index<0||index>=tileCount)throw new IllegalArgumentException("Región fuera de la imagen");
        int band=index/columns,column=index%columns;
        int x=column*tileWidth,y=(bands-1-band)*stripRows;
        int rows=Math.min(stripRows,image.height-y);
        return new Tile(x,y,Math.min(tileWidth,image.width-x),rows,image.height-y-rows);
    }
}
