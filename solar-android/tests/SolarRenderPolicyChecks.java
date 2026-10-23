package com.mundox.solar;

import com.mundox.graphics.RenderResolution;

/** Host-only validation of the UI preview scaling contract (no Android/GLES). */
public final class SolarRenderPolicyChecks {
    private static int checks;
    private static void yes(boolean condition,String name){
        checks++;if(!condition)throw new AssertionError(name);
    }
    public static void main(String[] args) {
        for(boolean enabled:new boolean[]{false,true})
            for(boolean drag:new boolean[]{false,true})
                for(boolean uhd:new boolean[]{false,true})
                    for(boolean screenshot:new boolean[]{false,true}){
                        yes(SolarRenderPolicy.shouldPreview(enabled,drag,uhd,screenshot)
                                ==(enabled&&drag&&uhd&&!screenshot),
                                "Preview consent, UHD and capture interlock");
                    }
        for(int[] spec:new int[][]{
                {3840,2160,960,540,16},
                {2160,3840,540,960,16},
                {7680,4320,960,540,64},
                {4320,7680,540,960,64},
                {1280,720,960,540,2},
                {720,1280,540,960,2},
                {960,540,960,540,1}
        }){
            RenderResolution.Size full=new RenderResolution.Size(spec[0],spec[1]);
            RenderResolution.Size smaller=SolarRenderPolicy.preview(full);
            yes(smaller.width==spec[2]&&smaller.height==spec[3],
                "Preview dimensions "+spec[0]+"x"+spec[1]);
            double expected=(double)spec[0]*spec[1]/(spec[2]*spec[3]);
            yes(Math.abs(SolarRenderPolicy.approximatePixelReduction(full)-expected)<1e-10,
                "Preview pixel cost "+spec[0]);
            yes(smaller.attachmentBytes()<=8*960L*960,
                "Preview remains below 8 MiB of two logical surfaces");
            yes((long)smaller.width*full.height==(long)smaller.height*full.width,
                "No aspect distortion");
            if(full.isUhd4k()||full.isUhd8k()){
                yes(full.pixels()>smaller.pixels(),"UHD draws fewer preview pixels");
                RenderResolution.requireAllocation(smaller,full);
            }
        }
        boolean bad=false;
        try{SolarRenderPolicy.preview(null);}catch(IllegalArgumentException expected){bad=true;}
        yes(bad,"No null framebuffer policy");
        System.out.println("SOLAR_RENDER_POLICY_PASS checks="+checks
                +" 4K=16x_pixels 8K=64x_pixels capture_remains_exact");
    }
}
