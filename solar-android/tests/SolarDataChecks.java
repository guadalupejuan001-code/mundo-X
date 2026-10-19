package com.mundox.solar;

/** Plain-Java checks of physical invariants, coordinate conventions and model bounds. */
public final class SolarDataChecks {
    private static int checks;

    public static void main(String[] args) {
        catalogue();
        orbitGeometryAndMotion();
        satelliteMotion();
        dipolePhysics();
        boundaries();
        System.out.println("SolarDataChecks PASS: " + checks + " checks; 23 bodies; Kepler, moon and dipole invariants.");
    }

    private static void catalogue() {
        check(SolarData.BODIES.length==23,"catalogue body count");
        for (int id=0;id<SolarData.BODIES.length;id++) {
            SolarData.Body b=SolarData.BODIES[id];
            check(b.id==id && b.parent<id,"stable body identity and parent ordering");
            check(b.radiusKm>0 && b.color.length==4,"positive physical radius and RGBA colour");
            check(b.ringOuter==0 || (b.ringInner>1 && b.ringOuter>b.ringInner),"rings exterior to body");
        }
        check(SolarData.BODIES[2].fieldType==SolarData.FIELD_INDUCED,"Venus induced, not intrinsic");
        check(SolarData.BODIES[4].fieldType==SolarData.FIELD_INDUCED_CRUSTAL,"Mars crustal and induced");
        check(SolarData.BODIES[12].fieldType==SolarData.FIELD_INTRINSIC,"Ganymede intrinsic field");
        near(SolarData.AU_KM,149597870.7,0,"IAU astronomical unit");
        near(SolarData.EARTH_MOON_MASS_RATIO,81.3005682,1e-6,"DE440 Earth/Moon mass ratio");
        for(int id=0;id<=8;id++) near(norm(SolarData.spinAxis(id)),1,1e-14,"unit spin pole");
        check(SolarData.spinAxis(2)[1]<0 && SolarData.spinAxis(7)[1]<0,"Venus and Uranus angular momentum retrograde");
    }

    private static void orbitGeometryAndMotion() {
        double jd=SolarData.J2000;
        near(norm(SolarData.position(0,jd)),0,0,"heliocentric Sun origin");
        // Independently tabulated perihelion/aphelion distances from NASA planet factsheets.
        near(norm(SolarData.orbitPosition(1,0,jd))*SolarData.AU_KM,46000000,100000,"Mercury perihelion");
        near(norm(SolarData.orbitPosition(1,.5,jd))*SolarData.AU_KM,69818000,100000,"Mercury aphelion");
        near(norm(SolarData.orbitPosition(4,0,jd))*SolarData.AU_KM,206650000,300000,"Mars perihelion");
        near(norm(SolarData.orbitPosition(4,.5,jd))*SolarData.AU_KM,249261000,300000,"Mars aphelion");
        near(norm(SolarData.position(3,jd)),.9833,.001,"Earth near perihelion in early January");
        double[] mercury=SolarData.position(1,jd);
        check(mercury[0]<0 && mercury[1]<0 && mercury[2]>0,"J2000 Mercury octant in Y-up frame");
        for(int id=1;id<=8;id++) {
            double[] p=SolarData.position(id,jd),v=velocity(id,jd);
            check(cross(p,v)[1]>0,"planet prograde around ecliptic north");
            near(distance(SolarData.orbitPosition(id,0,jd),SolarData.orbitPosition(id,1,jd)),0,1e-13,"closed orbit path");
        }
        // Kepler's second law: approximately constant areal velocity, including high-e Mercury.
        // JPL element rates create tiny secular changes, so this is not asserted bit-for-bit.
        double base=norm(cross(SolarData.position(1,jd),velocity(1,jd)));
        for(int k=1;k<=16;k++) {
            double t=jd+k*SolarData.BODIES[1].orbitalPeriodDays/16.0;
            double h=norm(cross(SolarData.position(1,t),velocity(1,t)));
            near(h/base,1,2e-5,"Mercury constant areal velocity");
        }
    }

    private static void satelliteMotion() {
        double jd=SolarData.J2000+1234;
        for(int id=9;id<SolarData.BODIES.length;id++) {
            SolarData.Body b=SolarData.BODIES[id];
            double[] a=relative(id,jd),z=relative(id,jd+b.orbitalPeriodDays);
            near(distance(a,z),0,3e-10,"moon returns in one local orbital period");
            check(norm(a)>b.semiMajorAU*.90 && norm(a)<b.semiMajorAU*1.10,"moon stays on local physical orbit");
        }
        double[] tritonH=cross(relative(16,jd),relativeVelocity(16,jd));
        check(dot(tritonH,SolarData.spinAxis(8))<0,"Triton retrograde around Neptune");
        double[] titaniaH=cross(relative(20,jd),relativeVelocity(20,jd));
        check(dot(titaniaH,SolarData.spinAxis(7))>0,"Titania follows Uranus spin direction");
        double lunarDistance=norm(relative(9,jd))*SolarData.AU_KM;
        check(lunarDistance>363000 && lunarDistance<406000,"Moon geocentric distance range");
    }

    private static void dipolePhysics() {
        double[] axis={0,1,0};
        double[] equator=SolarData.dipoleField(new double[]{1,0,0},axis,35);
        near(equator[1],-35,1e-13,"equatorial vector points opposite magnetic moment");
        near(norm(SolarData.dipoleField(new double[]{0,1,0},axis,35)),70,1e-13,"dipole polar strength twice equator");
        near(norm(SolarData.dipoleField(new double[]{2,0,0},axis,35)),35.0/8,1e-13,"dipole inverse cube decay");
        near(distance(equator,SolarData.dipoleField(new double[]{1,0,0},new double[]{0,17,0},35)),0,1e-13,"axis magnitude does not alter field");
        near(norm(SolarData.dipoleField(new double[]{0,0,0},axis,35)),0,0,"interior excluded without singularity");
        double[] p={1.3,1.1,-.9};
        double divergence=0,step=1e-5;
        for(int k=0;k<3;k++) {
            double[] plus=p.clone(),minus=p.clone();
            plus[k]+=step; minus[k]-=step;
            divergence+=(SolarData.dipoleField(plus,axis,35)[k]-SolarData.dipoleField(minus,axis,35)[k])/(2*step);
        }
        near(divergence,0,2e-8,"divergence-free exterior dipole");
    }

    private static void boundaries() {
        double[] dates={SolarData.MIN_JD,SolarData.MIN_JD+45678,SolarData.J2000,2461332.5,SolarData.MAX_JD};
        for(double date:dates) for(int id=0;id<SolarData.BODIES.length;id++) {
            for(double value:SolarData.position(id,date)) check(Double.isFinite(value),"finite position through supported interval");
        }
        near(distance(SolarData.position(8,SolarData.MIN_JD-1),SolarData.position(8,SolarData.MIN_JD)),0,0,"date lower bound");
        near(distance(SolarData.position(1,SolarData.MAX_JD+1),SolarData.position(1,SolarData.MAX_JD)),0,0,"date upper bound");
        throwsArgument(() -> SolarData.position(-1,SolarData.J2000),"reject invalid body");
        throwsArgument(() -> SolarData.position(1,Double.NaN),"reject nonfinite date");
        throwsArgument(() -> SolarData.dipoleField(new double[]{1,0,0},new double[]{0,0,0},1),"reject zero dipole axis");
    }

    private static double[] velocity(int id,double jd) {
        return subtract(SolarData.position(id,jd+.001),SolarData.position(id,jd-.001));
    }
    private static double[] relative(int id,double jd) {
        return subtract(SolarData.position(id,jd),SolarData.position(SolarData.BODIES[id].parent,jd));
    }
    private static double[] relativeVelocity(int id,double jd) { return subtract(relative(id,jd+.001),relative(id,jd-.001)); }
    private static double[] subtract(double[] a,double[] b) { return new double[]{a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
    private static double[] cross(double[] a,double[] b) { return new double[]{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
    private static double dot(double[] a,double[] b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
    private static double norm(double[] a) { return Math.sqrt(dot(a,a)); }
    private static double distance(double[] a,double[] b) { return norm(subtract(a,b)); }
    private static void near(double actual,double expected,double tolerance,String name) {
        check(Double.isFinite(actual) && Math.abs(actual-expected)<=tolerance,name+": "+actual+" expected "+expected+" ± "+tolerance);
    }
    private static void check(boolean okay,String name) { checks++; if(!okay) throw new AssertionError(name); }
    private static void throwsArgument(Runnable action,String name) {
        boolean caught=false; try { action.run(); } catch(IllegalArgumentException expected) { caught=true; } check(caught,name);
    }
}
