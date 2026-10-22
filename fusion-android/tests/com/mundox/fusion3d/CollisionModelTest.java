package com.mundox.fusion3d;

import com.cosmicx.pc.FusionPhysics;

/** Pure Java mathematical oracle (no Android dependencies). */
public final class CollisionModelTest {
    private static void assertYes(boolean value,String label){
        if(!value) throw new AssertionError(label);
    }
    private static void close(double a,double b,double eps,String label){
        assertYes(Double.isFinite(a)&&Math.abs(a-b)<eps,label+": "+a+" vs "+b);
    }
    public static void main(String[] args){
        FusionPhysics.Analysis dt=FusionPhysics.analyze(1,2,1,3);
        CollisionModel.Result collision=CollisionModel.calculate(dt,.020);
        close(collision.eCmMeV,.020,1e-13,"Ecm=20keV");
        close(collision.rContactFm,1.2*(Math.cbrt(2)+Math.cbrt(3)),1e-13,"radius fm");
        close(collision.coulombContactMeV,
                1.4399764/(1.2*(Math.cbrt(2)+Math.cbrt(3))),1e-13,"V contact");
        close(collision.classicalTurningFm,1.4399764/.02,1e-10,"turning 20keV");
        assertYes(!collision.classicalContact,"tunnelling required for below-barrier collision");
        assertYes(CollisionModel.calculate(dt,3).classicalContact,"above-barrier not rate");
        close(CollisionModel.potentialMeV(dt,10),.14399764,1e-9,"1/r potential");
        close(CollisionModel.sliderToMeV(0),.0001,1e-12,"slider min");
        close(CollisionModel.sliderToMeV(1000),10,1e-10,"slider max");
        for(int p=0;p<=1000;p+=25)
            assertYes(Math.abs(CollisionModel.energyToSlider(
                    CollisionModel.sliderToMeV(p))-p)<=1,"slider reversible");
        FusionPhysics.Analysis feAu=FusionPhysics.analyze(26,56,79,197);
        CollisionModel.Result heavy=CollisionModel.calculate(feAu,.1);
        assertYes(heavy.coulombContactMeV>collision.coulombContactMeV,"heavy Z barrier");
        assertYes(!heavy.isotopes.hasDocumentedChannel(),"no fabricated heavy Q");
        boolean rejected=false;
        try {CollisionModel.calculate(dt,Double.NaN);}catch(IllegalArgumentException ok){rejected=true;}
        assertYes(rejected,"NaN energy blocked");
        System.out.println("COLLISION_MODEL_PASS ecm=20keV contactRadiusFm="+collision.rContactFm);
    }
}
