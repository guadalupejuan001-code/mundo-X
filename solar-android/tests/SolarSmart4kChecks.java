package com.mundox.solar;

import com.mundox.graphics.RenderResolution;

/** Deterministic CPU-only regression of the GPU resolution governor. */
public final class SolarSmart4kChecks {
    private static void verify(boolean condition,String why){
        if(!condition)throw new AssertionError(why);
    }
    private static void eq(int a,int b,String why){
        if(a!=b)throw new AssertionError(why+": "+a+" != "+b);
    }
    public static void main(String[] args){
        RenderResolution.Limits caps=new RenderResolution.Limits(8192,8192,8192,8192);
        for(int size:new int[]{640,960,1440}){
            RenderResolution.Size portrait=SolarSmart4k.preview(1080,2400,size,caps);
            RenderResolution.Size landscape=SolarSmart4k.preview(2400,1080,size,caps);
            eq(portrait.height,size,"portrait long edge");
            eq(portrait.width,size*9/16,"portrait short edge");
            eq(landscape.width,size,"landscape long edge");
            eq(landscape.height,size*9/16,"landscape short edge");
            eq((int)portrait.pixels(),(int)landscape.pixels(),"rotation stable");
            verify(!portrait.isUhd4k(),"a preview must NEVER claim to be full 4K");
            verify(portrait.pixels() < RenderResolution.uhd(1080,2400).pixels(),
                   "live preview reduces fragment count");
        }
        verify(SolarSmart4k.preview(1080,2400,960,caps).pixels()==518400,
               "960 preview should process 518400 real pixels");
        verify(RenderResolution.uhd(1080,2400).pixels()==8294400L,
               "full 4K capture is always 8294400 pixels");
        verify(RenderResolution.uhd(1080,2400).pixels()/
                    SolarSmart4k.preview(1080,2400,960,caps).pixels()==16,
               "16x fewer fragment invocations by resolution, not 16x guaranteed FPS");
        RenderResolution.Limits narrow=new RenderResolution.Limits(720,720,720,720);
        boolean rejected=false;
        try{SolarSmart4k.preview(1080,2400,1440,narrow);}
        catch(IllegalArgumentException ok){rejected=true;}
        verify(rejected,"GPU caps must block unsupported preview");
        for(int bad:new int[]{0,500,1000,2160}){
            rejected=false;
            try{SolarSmart4k.preview(1080,2400,bad,caps);}
            catch(IllegalArgumentException ok){rejected=true;}
            verify(rejected,"unsupported resolution must be rejected");
        }

        SolarSmart4k.Governor gov=new SolarSmart4k.Governor();
        eq(gov.limit(),960,"initial preview is balanced");
        long now=1_000_000_000L;
        eq(gov.observe(2f,now),960,"first fps sample does not resize");
        now+=2_600_000_000L;
        eq(gov.observe(2f,now),960,"single bad sample should not resize");
        now+=2_600_000_000L;
        eq(gov.observe(2f,now),640,"two slow observations select 640");
        now+=2_600_000_000L;
        eq(gov.observe(60f,now),640,"resize cooldown prevents oscillation");
        now+=2_600_000_000L;
        eq(gov.observe(60f,now),640,"first fast sample does not resize");
        now+=2_600_000_000L;
        eq(gov.observe(60f,now),960,"two fast samples restore 960");
        now+=2_600_000_000L;
        eq(gov.observe(61f,now),960,"cooldown 2");
        now+=2_600_000_000L;
        eq(gov.observe(61f,now),960,"upward check 1");
        now+=2_600_000_000L;
        eq(gov.observe(61f,now),1440,"two fast samples promote 1440");
        now+=5_200_000_000L;
        eq(gov.observe(11f,now),1440,"slow check one");
        now+=2_600_000_000L;
        eq(gov.observe(11f,now),960,"downshift from 1440 to 960");
        gov.reset();
        eq(gov.limit(),960,"reset preview");

        System.out.println("SOLAR_SMART_4K_PASS preview=540x960 full=2160x3840 "
                +"pixels_ratio=16 quality_steps=640,960,1440 "
                +"hysteresis=PASS cooldown=PASS bounds=PASS");
    }
}
