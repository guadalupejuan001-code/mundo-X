package com.mundox.solar;

/** The host runner extracts LegacySolarFlightPhysics from the pinned baseline. */
public final class SolarPhysicsComparison {
    public static void main(String[] args){
        java.util.Random random=new java.util.Random(2026);long checks=0;
        for(int body=0;body<23;body++)for(int scenario=0;scenario<32;scenario++){
            SolarEnvironment e=SolarEnvironment.environment(body);
            double altitude=e.minAltitudeKm+random.nextDouble()*(e.maxAltitudeKm-e.minAltitudeKm),velocity=(random.nextDouble()-.5)*2;
            SolarFlightPhysics.State a=new SolarFlightPhysics.State(altitude,velocity);
            LegacySolarFlightPhysics.State b=new LegacySolarFlightPhysics.State(altitude,velocity);
            for(int step=0;step<400;step++){
                double thrust=(random.nextDouble()-.5)*20,dt=random.nextDouble()*.25;
                SolarFlightPhysics.step(body,e,a,thrust,dt);LegacySolarFlightPhysics.step(body,e,b,thrust,dt);
                if(Double.doubleToRawLongBits(a.altitudeKm)!=Double.doubleToRawLongBits(b.altitudeKm)||Double.doubleToRawLongBits(a.radialVelocityKmS)!=Double.doubleToRawLongBits(b.radialVelocityKmS))throw new AssertionError("Flight changed bits");checks+=2;
            }
        }
        SolarFlightPhysics.State s=new SolarFlightPhysics.State(Double.MAX_VALUE,Double.MAX_VALUE);boolean rejected=false;
        try{SolarFlightPhysics.step(3,SolarEnvironment.environment(3),s,0,.25);}catch(IllegalArgumentException e){rejected=true;}
        if(!rejected||s.altitudeKm!=Double.MAX_VALUE||s.radialVelocityKmS!=Double.MAX_VALUE)throw new AssertionError("Invalid step was not atomic");
        System.out.println("SolarPhysicsComparison PASS: "+checks+" bitwise state checks; overflow rollback passed");
    }
}
