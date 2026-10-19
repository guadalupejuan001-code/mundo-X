package com.mundox.graphics;

/** Pixel geometry shared by framebuffer allocation, FIT presentation and touch picking. */
public final class RenderResolution {
    public static final int ADAPTIVE=0,UHD_4K=1,UHD_8K=2;
    public static final int UHD_LONG=3840,UHD_SHORT=2160;
    public static final int UHD_8K_LONG=7680,UHD_8K_SHORT=4320;
    /** RGBA8 plus conservatively four bytes for D24; not a process RAM estimate. */
    public static final long ATTACHMENT_BYTES_PER_PIXEL=8;
    /** Both old and replacement attachments coexist during a transactional switch. */
    public static final long MAX_FRAMEBUFFER_PEAK_BYTES=512L*1024*1024;

    private RenderResolution(){}

    public static final class Size {
        public final int width,height;
        public Size(int width,int height){
            positive(width,height);this.width=width;this.height=height;
        }
        public long pixels(){return (long)width*height;}
        public boolean isUhd4k(){
            return width==UHD_LONG&&height==UHD_SHORT||width==UHD_SHORT&&height==UHD_LONG;
        }
        public boolean isUhd8k(){
            return width==UHD_8K_LONG&&height==UHD_8K_SHORT||width==UHD_8K_SHORT&&height==UHD_8K_LONG;
        }
        public long attachmentBytes(){return Math.multiplyExact(pixels(),ATTACHMENT_BYTES_PER_PIXEL);}
    }

    /** The limits are per context. Viewport limits may differ between the two axes. */
    public static final class Limits {
        public final int texture,renderbuffer,viewportWidth,viewportHeight;
        public Limits(int texture,int renderbuffer,int viewportWidth,int viewportHeight){
            positive(texture,renderbuffer);positive(viewportWidth,viewportHeight);
            this.texture=texture;this.renderbuffer=renderbuffer;
            this.viewportWidth=viewportWidth;this.viewportHeight=viewportHeight;
        }
        public void require(Size size){
            if(size.width>texture||size.height>texture)
                throw new IllegalArgumentException("GL_MAX_TEXTURE_SIZE="+texture+" no admite "+size.width+"×"+size.height);
            if(size.width>renderbuffer||size.height>renderbuffer)
                throw new IllegalArgumentException("GL_MAX_RENDERBUFFER_SIZE="+renderbuffer+" no admite "+size.width+"×"+size.height);
            if(size.width>viewportWidth||size.height>viewportHeight)
                throw new IllegalArgumentException("GL_MAX_VIEWPORT_DIMS="+viewportWidth+"×"+viewportHeight+" no admite "+size.width+"×"+size.height);
        }
    }

    /** Equal dimensions use the landscape convention; no scaling of UHD is permitted. */
    public static Size uhd(int viewWidth,int viewHeight){
        positive(viewWidth,viewHeight);
        return viewWidth>=viewHeight?new Size(UHD_LONG,UHD_SHORT):new Size(UHD_SHORT,UHD_LONG);
    }

    public static Size uhd8k(int viewWidth,int viewHeight){
        positive(viewWidth,viewHeight);
        return viewWidth>=viewHeight?new Size(UHD_8K_LONG,UHD_8K_SHORT):new Size(UHD_8K_SHORT,UHD_8K_LONG);
    }

    public static boolean validMode(int mode){return mode==ADAPTIVE||mode==UHD_4K||mode==UHD_8K;}

    public static Size fixed(int mode,int viewWidth,int viewHeight){
        if(mode==UHD_4K)return uhd(viewWidth,viewHeight);
        if(mode==UHD_8K)return uhd8k(viewWidth,viewHeight);
        throw new IllegalArgumentException("El modo no tiene resolución fija");
    }

    public static void requireAllocation(Size next,Size existing){
        long peak=Math.addExact(next.attachmentBytes(),existing==null?0:existing.attachmentBytes());
        if(peak>MAX_FRAMEBUFFER_PEAK_BYTES)
            throw new IllegalArgumentException("La reserva simultánea de color y profundidad supera el presupuesto de 512 MiB; usa PNG 8K por regiones");
    }

    /** Original screen-relative mode, now respecting the independent viewport limits too. */
    public static Size adaptive(int viewWidth,int viewHeight,int limit,Limits limits){
        positive(viewWidth,viewHeight);
        if(limit<64||limit>4096)throw new IllegalArgumentException("Resolución fuera de rango");
        int maximum=Math.min(limit,Math.min(limits.texture,limits.renderbuffer));
        double scale=Math.min(1.0,(double)maximum/Math.max(viewWidth,viewHeight));
        scale=Math.min(scale,Math.min((double)limits.viewportWidth/viewWidth,(double)limits.viewportHeight/viewHeight));
        Size result=new Size(Math.max(1,(int)Math.round(viewWidth*scale)),Math.max(1,(int)Math.round(viewHeight*scale)));
        limits.require(result);return result;
    }

    /** All rectangle coordinates and incoming touch coordinates have a top-left origin. */
    public static final class Fit {
        public final int left,top,width,height;
        public final int framebufferWidth,framebufferHeight;
        private Fit(int left,int top,int width,int height,Size framebuffer){
            this.left=left;this.top=top;this.width=width;this.height=height;
            framebufferWidth=framebuffer.width;framebufferHeight=framebuffer.height;
        }
        public boolean contains(int x,int y){
            return x>=left&&y>=top&&(long)x<((long)left+width)&&(long)y<((long)top+height);
        }
        /** Source sample nearest the displayed pixel centre, or -1 outside this axis. */
        public int framebufferX(int x){return pixel(x,left,width,framebufferWidth);}
        /** Top-to-bottom framebuffer row; OpenGL readback uses framebufferHeight-1-row. */
        public int framebufferY(int y){return pixel(y,top,height,framebufferHeight);}
        private static int pixel(int coordinate,int origin,int span,int sourceSpan){
            long local=(long)coordinate-origin;
            if(local<0||local>=span)return -1;
            return (int)Math.min(sourceSpan-1,((2*local+1)*sourceSpan)/(2L*span));
        }
    }

    /** Centres the complete image; integer viewport edges round by at most one pixel. */
    public static Fit fit(int viewWidth,int viewHeight,Size framebuffer){
        positive(viewWidth,viewHeight);
        double scale=Math.min((double)viewWidth/framebuffer.width,(double)viewHeight/framebuffer.height);
        int width=Math.max(1,Math.min(viewWidth,(int)Math.round(framebuffer.width*scale)));
        int height=Math.max(1,Math.min(viewHeight,(int)Math.round(framebuffer.height*scale)));
        return new Fit((viewWidth-width)/2,(viewHeight-height)/2,width,height,framebuffer);
    }

    private static void positive(int width,int height){
        if(width<1||height<1)throw new IllegalArgumentException("Dimensiones de imagen inválidas");
    }
}
