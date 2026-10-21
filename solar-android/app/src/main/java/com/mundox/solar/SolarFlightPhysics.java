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
        if(environment==null||state==null||body<0||body>=SolarData.BODIES.length||environment.id!=body
                ||!Double.isFinite(state.altitudeKm)||!Double.isFinite(state.radialVelocityKmS)||!Double.isFinite(thrustMs2)
                ||!Double.isFinite(dtSeconds)||dtSeconds<0||dtSeconds>.25)
            throw new IllegalArgumentException("Paso físico inválido");
        if(!(SolarData.BODIES[body].radiusKm+state.altitudeKm>0))
            throw new IllegalArgumentException("Radio físico inválido");
        if(dtSeconds==0)return;
        int steps=Math.max(1,(int)Math.ceil(dtSeconds/.01));
        double h=dtSeconds/steps;
        double altitude=state.altitudeKm,velocity=state.radialVelocityKmS;
        for(int i=0;i<steps;i++){
            double a0=thrustMs2/1000.0-SolarData.gravityMs2(body,altitude)/1000.0;
            double next=altitude+velocity*h+.5*a0*h*h;
            next=environment.clampAltitudeKm(next);
            double a1=thrustMs2/1000.0-SolarData.gravityMs2(body,next)/1000.0;
            velocity+=.5*(a0+a1)*h;
            altitude=next;
            if((next<=environment.minAltitudeKm+1e-12
                    || next>=environment.maxAltitudeKm-1e-12)
                    && ((next<=environment.minAltitudeKm&&velocity<0)
                    || (next>=environment.maxAltitudeKm&&velocity>0)))
                velocity=0;
            if(!Double.isFinite(velocity))throw new IllegalArgumentException("Velocidad fuera de rango");
        }
        state.altitudeKm=altitude;state.radialVelocityKmS=velocity;
    }

    /** Specific two-body mechanical energy, km^2/s^2, for regression checks. */
    public static double specificEnergy(int body,State state){
        double r=SolarData.BODIES[body].radiusKm+state.altitudeKm;
        if(!(r>0))throw new IllegalArgumentException("Radio físico inválido");
        return .5*state.radialVelocityKmS*state.radialVelocityKmS-SolarData.gmKm3S2(body)/r;
    }
}
