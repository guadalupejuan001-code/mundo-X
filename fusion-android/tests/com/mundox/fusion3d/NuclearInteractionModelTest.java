package com.mundox.fusion3d;

import com.cosmicx.pc.FusionPhysics;

/**
 * Physics numerical regression: closed-form Coulomb WKB against independent
 * quadrature, reduced masses, root finding, model sensitivity and bounds.
 */
public final class NuclearInteractionModelTest {
    private NuclearInteractionModelTest() {}

    private static void require(boolean ok,String message){
        if(!ok)throw new AssertionError(message);
    }

    private static void close(double x,double y,double tol,String message){
        require(Double.isFinite(x)&&Double.isFinite(y) &&
                Math.abs(x-y)<tol,
                message+": actual="+x+" expected="+y);
    }

    public static void main(String[] args) {
        FusionPhysics.Analysis dt=FusionPhysics.analyze(1,2,1,3);
        NuclearInteractionModel.Result p20=
                NuclearInteractionModel.solve(dt,.020,35);
        require(p20.hasNuclearModel,"D+T in tabulated entrance channels");
        require(p20.belowBarrier,"D+T 20 keV has model barrier");
        require(p20.innerTurningFm>p20.contactRadiusFm,"inner turning outside nuclear contact");
        require(p20.barrierPeakRadiusFm>p20.innerTurningFm,"peak after first turning");
        require(p20.outerTurningFm>p20.barrierPeakRadiusFm,"outer turning after peak");
        require(p20.action>0&&p20.log10Wkb<0,"WKB suppression sign");
        close(Math.log10(p20.wkbFactor),p20.log10Wkb,2e-8,
                "WKB logarithm and real factor consistent");
        require(p20.barrierPeakMeV>.10&&p20.barrierPeakMeV<.35,
                "Phenomenological D+T barrier order of magnitude");
        require(p20.reducedMassMeV>1100&&p20.reducedMassMeV<1150,
                "reduced mass D+T from NIST isotope masses");

        // At fixed model potential, higher sub-barrier E means less
        // suppression; never mislabel this as reaction probability.
        NuclearInteractionModel.Result low=
                NuclearInteractionModel.solve(dt,.005,35);
        NuclearInteractionModel.Result high=
                NuclearInteractionModel.solve(dt,.060,35);
        require(low.belowBarrier&&high.belowBarrier,
                "comparison energies below barrier");
        require(low.log10Wkb<p20.log10Wkb &&
                p20.log10Wkb<high.log10Wkb,
                "WKB suppression responds monotonically to incident energy");
        require(Math.abs(p20.log10Wkb-p20.log10PureCoulomb)>.001,
                "adding attractive phenomenological potential changes WKB integral");
        require(p20.log10PureCoulomb>p20.log10Gamow,
                "finite-radius Coulomb suppression less than point Gamow suppression");

        // The nuclear potential is attractive and dies off outside the
        // interaction range; Coulomb dominates the far field.
        double near=NuclearInteractionModel.nuclearPotentialMeV(dt,p20.contactRadiusFm,35);
        close(near,-17.5,1e-10,"Woods-Saxon half-depth at nuclear contact");
        close(NuclearInteractionModel.nuclearPotentialMeV(dt,100,35),0,1e-24,
                "short-range potential vanishes");
        double r=9.;
        close(NuclearInteractionModel.totalPotentialMeV(dt,r,35),
                NuclearInteractionModel.coulombPotentialMeV(dt,r)+
                NuclearInteractionModel.nuclearPotentialMeV(dt,r,35),1e-12,
                "potential component identity");

        // Closed-form finite-cutoff Coulomb WKB has an independent numerical
        // quadrature. Check very different incident energies and channels.
        for(double energy:new double[]{.001,.005,.02,.1,.25}) {
            double analytic=NuclearInteractionModel.coulombFiniteRadiusLog10(
                    1,1,p20.reducedMassMeV,energy,p20.contactRadiusFm);
            double numeric=NuclearInteractionModel.coulombNumericLog10(
                    1,1,p20.reducedMassMeV,energy,p20.contactRadiusFm);
            close(numeric,analytic,1e-5,"Coulomb analytic vs Simpson "+energy);
        }

        NuclearInteractionModel.Result shallower=
                NuclearInteractionModel.solve(dt,.020,25);
        NuclearInteractionModel.Result deeper=
                NuclearInteractionModel.solve(dt,.020,45);
        require(shallower.belowBarrier&&deeper.belowBarrier,"depths bracket");
        require(deeper.log10Wkb>shallower.log10Wkb,
                "nuclear attraction decreases semiclassical suppression");
        require(Math.abs(deeper.log10Wkb-shallower.log10Wkb)>.01,
                "model parameter has visible impact");
        NuclearInteractionModel.Result over=
                NuclearInteractionModel.solve(dt,2.,35);
        require(!over.belowBarrier&&Double.isNaN(over.log10Wkb),
                "no false 100%-penetrability above the barrier");
        FusionPhysics.Analysis unknown=FusionPhysics.analyze(79,197,26,56);
        NuclearInteractionModel.Result unsupported=
                NuclearInteractionModel.solve(unknown,.02,35);
        require(!unsupported.hasNuclearModel &&
                Double.isNaN(unsupported.log10Wkb),
                "unknown nuclear pairs never receive made-up penetration");

        for(FusionPhysics.Analysis known:new FusionPhysics.Analysis[]{
                FusionPhysics.analyze(1,2,1,2),
                FusionPhysics.analyze(1,2,2,3),
                FusionPhysics.analyze(1,2,3,6)}){
            NuclearInteractionModel.Result supported=
                    NuclearInteractionModel.solve(known,.020,35);
            require(supported.hasNuclearModel,known.first.label()+" channel");
            require(supported.reducedMassMeV>0,known.second.label()+" reduced mass");
            require(supported.log10Gamow<0,"Gamow suppression at 20 keV");
        }
        for(double invalid:new double[]{Double.NaN,0.,Double.POSITIVE_INFINITY,-1.}){
            boolean rejected=false;
            try{NuclearInteractionModel.solve(dt,invalid,35);}
            catch(IllegalArgumentException ok){rejected=true;}
            require(rejected,"invalid energy must be rejected");
        }
        System.out.println("NUCLEAR_INTERACTION_PASS DT20keV"+
                " barrierMeV="+p20.barrierPeakMeV+
                " turningFm="+p20.innerTurningFm+","+p20.outerTurningFm+
                " log10WKB="+p20.log10Wkb+
                " CoulombAnalyticVsNumeric=PASS"+
                " energyTrend=PASS depthSensitivity=PASS"+
                " unsupported=bounded");
    }
}
