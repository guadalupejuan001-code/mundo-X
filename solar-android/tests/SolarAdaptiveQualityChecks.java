package com.mundox.solar;

public final class SolarAdaptiveQualityChecks {
    private static int checks;
    private static void require(boolean yes,String why){checks++;if(!yes)throw new AssertionError(why);}
    private static void step(SolarAdaptiveQuality a,long ns,int count){
        for(int n=0;n<count;n++)a.sample(ns);
    }
    public static void main(String[] args) {
        SolarAdaptiveQuality a = new SolarAdaptiveQuality(960);
        require(a.dimension()==960,"initial 960");
        require(a.sample(0)==-1 && a.dimension()==960,"first frame ignored");
        step(a,50_000_000L,5);
        require(a.dimension()==960,"no premature shrink");
        require(a.sample(50_000_000L)==800,"sustained slow workload must shrink");
        step(a,50_000_000L,6);
        require(a.dimension()==640,"second sustained shrink");
        step(a,50_000_000L,6);
        require(a.dimension()==480,"bounded minimum");
        step(a,50_000_000L,60);
        require(a.dimension()==480,"minimum stable under pressure");
        a.reset(960);
        require(a.sample(180_000_000L)==-1,"single severe frame no shrink");
        require(a.sample(180_000_000L)==720,"two severe intervals shrink quickly");
        a.reset(720);
        step(a,10_000_000L,89);
        require(a.dimension()==720,"no premature increase");
        require(a.sample(10_000_000L)==800,"stable fast workload increases");
        step(a,10_000_000L,90*10);
        require(a.dimension()==1280,"never exceeds upper preview bound");
        a.reset(960);
        step(a,40_000_000L,5);
        require(a.sample(5_000_000_000L)==-1,"suspended gap not a performance sample");
        require(a.sample(40_000_000L)==-1 && a.dimension()==960,
                "suspend reset slow streak");
        a.reset(960);
        step(a,50_000_000L,5);
        a.clearSamples(); // capture / explicit UHD / paused frame
        require(a.sample(50_000_000L)==-1 && a.dimension()==960,
                "excluded frame should invalidate slow streak");
        a.reset(1440);
        require(a.dimension()==1280,"clamp large starting dimensions");
        a.reset(360);
        require(a.dimension()==480,"clamp small starting dimensions");
        a.reset(960);
        step(a,40_000_000L,3);
        step(a,17_000_000L,3);
        require(a.dimension()==960,"mid-band hysteresis");
        System.out.println("SolarAdaptiveQualityChecks: "+checks+" checks passed");
    }
}
