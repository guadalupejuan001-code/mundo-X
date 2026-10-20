package com.mundox.solar;

/**
 * Deterministic local radial flight integrator using the catalogue body's JPL GM.
 *
 * It is intentionally a local two-body model: spherical gravity only. It does
 * not pretend to include lift, weather, J2, tides or N-body perturbations.
 * Altitude is measured from the same mean-radius reference as SolarRenderer.
 */
public final class SolarFlightPhysics {
    private SolarFlightPhysics(){}

    public static final class State {
        public double altitudeKm;
        public double radialVelocityKmS;
        public State(double altitudeKm,double radialVelocityKmS){
            if(!Double.isFinite(altitudeKm)||!Double.isFinite(radialVelocityKmS))
                throw new IllegalArgumentException("Estado de vuelo no finito");
            this.altitudeKm=altitudeKm;this.radialVelocityKmS=radialVelocityKmS;
        }
        public State copy(){return new State(altitudeKm,radialVelocityKmS);}
    }

    /**
     * Velocity-Verlet integration. thrustMs2 is radial: positive points away
     * from the body. dt is bounded and internally substepped for stable mobile
     * frame times.
     */
    public static void step(int body,SolarEnvironment environment,State state,
                            double thrustMs2,double dtSeconds){
        if(environment==null||state==null||!Double.isFinite(thrustMs2)
                ||!Double.isFinite(dtSeconds)||dtSeconds<0||dtSeconds>.25)
            throw new IllegalArgumentException("Paso físico inválido");
        if(dtSeconds==0)return;
        int steps=Math.max(1,(int)Math.ceil(dtSeconds/.01));
        double h=dtSeconds/steps;
        for(int i=0;i<steps;i++){
            double a0=thrustMs2/1000.0-SolarData.gravityMs2(body,state.altitudeKm)/1000.0;
            double next=state.altitudeKm+state.radialVelocityKmS*h+.5*a0*h*h;
            next=environment.clampAltitudeKm(next);
            double a1=thrustMs2/1000.0-SolarData.gravityMs2(body,next)/1000.0;
            state.radialVelocityKmS+=.5*(a0+a1)*h;
            state.altitudeKm=next;
            if((next<=environment.minAltitudeKm+1e-12
                    || next>=environment.maxAltitudeKm-1e-12)
                    && ((next<=environment.minAltitudeKm&&state.radialVelocityKmS<0)
                    || (next>=environment.maxAltitudeKm&&state.radialVelocityKmS>0)))
                state.radialVelocityKmS=0;
        }
    }

    /** Specific two-body mechanical energy, km^2/s^2, for regression checks. */
    public static double specificEnergy(int body,State state){
        double r=SolarData.BODIES[body].radiusKm+state.altitudeKm;
        if(!(r>0))throw new IllegalArgumentException("Radio físico inválido");
        return .5*state.radialVelocityKmS*state.radialVelocityKmS-SolarData.gmKm3S2(body)/r;
    }
}
