package com.mundox.fusion3d;

import com.cosmicx.pc.FusionPhysics;

import java.util.Locale;

/**
 * BOUNDED phenomenological, one-dimensional radial entrance-channel model.
 *
 * Model potential: V(r) = V_C(r) - V0/(1+exp((r-R)/a)), l=0.
 * V_C is the Coulomb potential for uniformly charged spheres inside R and
 * point-charge Coulomb outside R. The Woods-Saxon nuclear depth is freely
 * adjustable: it is NOT fitted to D-T, D-D, D-He3, or D-Li6 data.
 *
 * Below the barrier, semiclassical WKB estimates only the suppression factor
 * for reaching the nuclear interior. It is not a fusion probability, S-factor,
 * reaction cross section, microscopic NN interaction, or a solution to the
 * quantum many-body problem. No MIL Core evolution is claimed.
 *
 * CODATA (hbar*c, u*c^2) and NIST isotopic atomic masses are declared here.
 */
public final class NuclearInteractionModel {
    private NuclearInteractionModel() {}

    public static final double HBAR_C_MEV_FM = 197.3269804;
    public static final double AMU_MEV = 931.49410242;
    public static final double COULOMB_MEV_FM = CollisionModel.COULOMB_MEV_FM;
    public static final double SURFACE_DIFFUSENESS_FM = 0.65;
    public static final double DEFAULT_DEPTH_MEV = 35.0;

    public static final class Result {
        public final FusionPhysics.Analysis isotopes;
        public final double energyMeV, nuclearDepthMeV, contactRadiusFm;
        public final boolean hasNuclearModel, belowBarrier;
        public final double reducedMassMeV, barrierPeakMeV, barrierPeakRadiusFm;
        public final double innerTurningFm, outerTurningFm, action;
        public final double log10Wkb, log10PureCoulomb, log10Gamow;
        public final double wkbFactor;

        private Result(FusionPhysics.Analysis pair,double energy,double depth) {
            isotopes=pair;
            energyMeV=energy;
            nuclearDepthMeV=depth;
            contactRadiusFm=CollisionModel.R0_FM*(
                    Math.cbrt(pair.first.a)+Math.cbrt(pair.second.a));
            hasNuclearModel=pair.hasDocumentedChannel();
            if(!hasNuclearModel){
                reducedMassMeV=Double.NaN;
                barrierPeakMeV=Double.NaN;
                barrierPeakRadiusFm=Double.NaN;
                belowBarrier=false;
                innerTurningFm=Double.NaN;
                outerTurningFm=Double.NaN;
                action=Double.NaN;
                log10Wkb=Double.NaN;
                log10PureCoulomb=Double.NaN;
                log10Gamow=Double.NaN;
                wkbFactor=Double.NaN;
                return;
            }
            double m1=atomicMassU(pair.first);
            double m2=atomicMassU(pair.second);
            reducedMassMeV=(m1*m2/(m1+m2))*AMU_MEV;
            log10PureCoulomb=coulombFiniteRadiusLog10(
                    pair.first.z,pair.second.z,reducedMassMeV,
                    energyMeV,contactRadiusFm);
            log10Gamow=gamowLog10(pair.first.z,pair.second.z,reducedMassMeV,energyMeV);
            // Search the near-surface barrier maximum; nuclear Woods-Saxon
            // terms vanish exponentially at large radius, so the far tail is
            // monotonically Coulomb-dominated.
            final double highR=contactRadiusFm+40.;
            double maxV=Double.NEGATIVE_INFINITY;
            double maxR=contactRadiusFm;
            final int samples=4000;
            for(int i=0;i<=samples;i++){
                double r=contactRadiusFm+(highR-contactRadiusFm)*i/samples;
                double v=totalPotentialMeV(pair,r,depth);
                if(v>maxV){maxV=v;maxR=r;}
            }
            barrierPeakMeV=maxV;
            barrierPeakRadiusFm=maxR;
            if(energyMeV>=maxV || maxR<=contactRadiusFm) {
                // WKB is inappropriate at/above the top of the model barrier.
                belowBarrier=false;
                innerTurningFm=Double.NaN;
                outerTurningFm=Double.NaN;
                action=Double.NaN;
                log10Wkb=Double.NaN;
                wkbFactor=Double.NaN;
                return;
            }
            if(totalPotentialMeV(pair,contactRadiusFm,depth)>=energyMeV) {
                // A barrier already present at contact needs a different
                // inner-boundary model. Never invent an inner turning point.
                belowBarrier=false;
                innerTurningFm=Double.NaN;
                outerTurningFm=Double.NaN;
                action=Double.NaN;
                log10Wkb=Double.NaN;
                wkbFactor=Double.NaN;
                return;
            }
            double inner=bisect(pair,energyMeV,depth,
                    contactRadiusFm,maxR);
            double charge=COULOMB_MEV_FM*pair.first.z*pair.second.z;
            double right=Math.max(maxR+2.,1.25*charge/energyMeV+4.);
            if(right>1e6)throw new IllegalArgumentException("radio WKB fuera de presupuesto");
            if(totalPotentialMeV(pair,right,depth)>=energyMeV)
                throw new IllegalStateException("No se encontró punto de retorno exterior");
            double outer=bisect(pair,energyMeV,depth,maxR,right);
            double s2048=simpsonAction(pair,energyMeV,depth,
                    reducedMassMeV,inner,outer,2048);
            double s4096=simpsonAction(pair,energyMeV,depth,
                    reducedMassMeV,inner,outer,4096);
            double integral=s4096;
            double tolerance=Math.max(1e-6,2e-5*integral);
            if(Math.abs(s2048-s4096)>tolerance){
                double refined=simpsonAction(pair,energyMeV,depth,
                        reducedMassMeV,inner,outer,8192);
                if(Math.abs(s4096-refined)>Math.max(1e-6,2e-5*refined))
                    throw new IllegalStateException("No convergió la integral WKB");
                integral=refined;
            }
            if(!Double.isFinite(integral) || integral<0)
                throw new IllegalStateException("Acción WKB no finita");
            belowBarrier=true;
            innerTurningFm=inner;
            outerTurningFm=outer;
            action=integral;
            log10Wkb=-2.*action/Math.log(10.);
            wkbFactor=Math.exp(-2.*action);
        }

        public String explain(){
            if(!hasNuclearModel){
                return "INTERACCIÓN NUCLEAR: SIN MODELO VALIDADO PARA ESTA PAREJA\n"
                       +"No se asignan valores de potencial fuerte ni penetración WKB.\n"
                       +"La composición Z/A sigue siendo solo una hipótesis formal.";
            }
            StringBuilder b=new StringBuilder();
            b.append("INTERACCIÓN NUCLEAR · MODELO FENOMENOLÓGICO\n")
             .append("Núcleos: ").append(isotopes.first.label()).append(" + ")
             .append(isotopes.second.label()).append('\n')
             .append("Ecm = ").append(f(energyMeV*1000)).append(" keV\n")
             .append("Masa reducida μc² = ").append(f(reducedMassMeV)).append(" MeV\n")
             .append("V_C = cargas uniformes para r < R, Coulomb 1/r para r >= R\n")
             .append("V_N(r) = -V0 / (1 + exp((r-R)/a))\n")
             .append("R = ").append(f(contactRadiusFm)).append(" fm; a = ")
             .append(f(SURFACE_DIFFUSENESS_FM)).append(" fm; V0 = ")
             .append(f(nuclearDepthMeV)).append(" MeV (valor de prueba NO ajustado)\n")
             .append("Pico de barrera aproximado: ")
             .append(f(barrierPeakMeV)).append(" MeV a r=")
             .append(f(barrierPeakRadiusFm)).append(" fm\n");
            if(belowBarrier){
                b.append("Dos puntos de retorno r1=")
                 .append(f(innerTurningFm)).append(" fm; r2=")
                 .append(f(outerTurningFm)).append(" fm\n")
                 .append("WKB s-wave: S = ∫ sqrt(2μc²[V(r)-E])/(ħc) dr = ")
                 .append(f(action)).append('\n')
                 .append("Supresión WKB exp(-2S): log10 = ")
                 .append(f(log10Wkb)).append('\n')
                 .append("Coulomb a radio finito: log10 = ")
                 .append(f(log10PureCoulomb)).append('\n')
                 .append("Gamow puntual: log10 = ").append(f(log10Gamow)).append('\n')
                 .append("INTERPRETACIÓN: factor semiclasico de penetración, NO porcentaje de fusión.\n");
            } else {
                b.append("Ecm está en o sobre el máximo / no hay dos puntos de retorno.\n")
                 .append("WKB sub-barrera: NO APLICABLE. No se asigna penetración=100%.\n");
            }
            b.append("Modelo radial unidimensional, l=0. Ignora espín, canales acoplados,\n")
             .append("fuerza tensorial, absorción, incertidumbre del potencial y S(E).\n")
             .append("No se calculan secciones eficaces, tasas de fusión ni materiales reales.\n")
             .append("No se ha ejecutado evolución Hamiltoniana en MIL Core.\n");
            return b.toString();
        }
    }

    public static Result solve(FusionPhysics.Analysis pair,double energyMeV,double nuclearDepthMeV){
        if(pair==null || !Double.isFinite(energyMeV) || energyMeV<1e-4 || energyMeV>10)
            throw new IllegalArgumentException("Ecm debe estar entre 0.1 y 10,000 keV");
        if(!Double.isFinite(nuclearDepthMeV) || nuclearDepthMeV<20 || nuclearDepthMeV>60)
            throw new IllegalArgumentException("Profundidad nuclear V0 fuera de 20..60 MeV");
        return new Result(pair,energyMeV,nuclearDepthMeV);
    }

    public static double nuclearPotentialMeV(FusionPhysics.Analysis pair,double radiusFm,double depthMeV){
        requirePairAndRadius(pair,radiusFm);
        if(!Double.isFinite(depthMeV) || depthMeV<0 || depthMeV>60)
            throw new IllegalArgumentException("Profundidad nuclear inválida");
        if(!pair.hasDocumentedChannel()) return Double.NaN;
        double R=CollisionModel.R0_FM*(Math.cbrt(pair.first.a)+Math.cbrt(pair.second.a));
        double x=(radiusFm-R)/SURFACE_DIFFUSENESS_FM;
        if(x>60.)return 0.;
        if(x< -60.)return -depthMeV;
        return -depthMeV/(1.+Math.exp(x));
    }
    public static double coulombPotentialMeV(FusionPhysics.Analysis pair,double radiusFm){
        requirePairAndRadius(pair,radiusFm);
        double R=CollisionModel.R0_FM*(Math.cbrt(pair.first.a)+Math.cbrt(pair.second.a));
        double factor=COULOMB_MEV_FM*pair.first.z*pair.second.z;
        if(radiusFm>=R)return factor/radiusFm;
        double x=radiusFm/R;
        return factor*(3.-x*x)/(2.*R);
    }
    public static double totalPotentialMeV(FusionPhysics.Analysis pair,double radiusFm,double depthMeV){
        return coulombPotentialMeV(pair,radiusFm)+nuclearPotentialMeV(pair,radiusFm,depthMeV);
    }

    private static void requirePairAndRadius(FusionPhysics.Analysis pair,double radiusFm){
        if(pair==null||!Double.isFinite(radiusFm)||radiusFm<=0||radiusFm>1e7)
            throw new IllegalArgumentException("Radio nuclear fuera de dominio");
    }

    private static double atomicMassU(FusionPhysics.Isotope iso){
        if(iso.z==1){
            if(iso.a==2)return 2.01410177812;
            if(iso.a==3)return 3.0160492779;
        }
        if(iso.z==2 && iso.a==3)return 3.0160293201;
        if(iso.z==3 && iso.a==6)return 6.0151228874;
        throw new IllegalArgumentException("No hay masa tabulada para el isótopo seleccionado");
    }

    /** Closed form Coulomb-only WKB, starting at finite nuclear contact R. */
    public static double coulombFiniteRadiusLog10(int za,int zb,double muMeV,double eMeV,double cutoffFm) {
        if(za<=0||zb<=0||!Double.isFinite(muMeV)||muMeV<=0||
           !Double.isFinite(eMeV)||eMeV<=0||
           !Double.isFinite(cutoffFm)||cutoffFm<=0)
            throw new IllegalArgumentException("Parámetros Coulomb inválidos");
        double charge=COULOMB_MEV_FM*za*zb;
        double ratio=eMeV*cutoffFm/charge;
        if(ratio>=1.)return 0.;
        double eta=charge/HBAR_C_MEV_FM*Math.sqrt(muMeV/(2.*eMeV));
        double x=Math.max(0.,ratio);
        double correction=1.-2./Math.PI*(
                Math.asin(Math.sqrt(x))+Math.sqrt(x*(1.-x)));
        return -2.*Math.PI*eta*correction/Math.log(10.);
    }

    public static double gamowLog10(int za,int zb,double muMeV,double eMeV) {
        if(za<=0||zb<=0||!Double.isFinite(muMeV)||muMeV<=0||
           !Double.isFinite(eMeV)||eMeV<=0)
            throw new IllegalArgumentException("Parámetros Gamow inválidos");
        double eta=COULOMB_MEV_FM*za*zb/HBAR_C_MEV_FM*Math.sqrt(muMeV/(2.*eMeV));
        return -2.*Math.PI*eta/Math.log(10.);
    }

    /** Independent quadrature against the analytic Coulomb solution (for tests). */
    public static double coulombNumericLog10(int za,int zb,double muMeV,double eMeV,double cutoffFm){
        double charge=COULOMB_MEV_FM*za*zb;
        double outer=charge/eMeV;
        if(cutoffFm>=outer)return 0.;
        double s=integrateWkb(muMeV,cutoffFm,outer,4096,
                r->charge/r-eMeV);
        return -2.*s/Math.log(10.);
    }

    private interface EnergyGap { double at(double radiusFm); }

    private static double integrateWkb(double mu,double start,double end,int panels,EnergyGap gap){
        if(panels<2||(panels&1)==1||end<=start)
            throw new IllegalArgumentException("Integral WKB sin dominio");
        double h=1./panels;
        double total=0.;
        for(int i=1;i<panels;i++){
            double t=i*h;
            double sin=Math.sin(Math.PI*t*.5);
            double r=start+(end-start)*sin*sin;
            double drdt=(end-start)*Math.PI*.5*Math.sin(Math.PI*t);
            double dE=Math.max(0.,gap.at(r));
            double k=Math.sqrt(2.*mu*dE)/HBAR_C_MEV_FM;
            total+=(i%2==0?2.:4.)*k*drdt;
        }
        return total*h/3.;
    }

    private static double simpsonAction(FusionPhysics.Analysis pair,double energy,double depth,
                                        double reducedMass,double r1,double r2,int panels){
        return integrateWkb(reducedMass,r1,r2,panels,
                r->totalPotentialMeV(pair,r,depth)-energy);
    }

    private static double bisect(FusionPhysics.Analysis pair,double energy,double depth,
                                 double a,double b){
        double fa=totalPotentialMeV(pair,a,depth)-energy;
        double fb=totalPotentialMeV(pair,b,depth)-energy;
        if(fa*fb>0) throw new IllegalStateException("No se delimita punto de retorno");
        for(int i=0;i<75;i++){
            double m=(a+b)*0.5;
            double fm=totalPotentialMeV(pair,m,depth)-energy;
            if(fm==0.)return m;
            if(Math.signum(fm)==Math.signum(fa)){a=m;fa=fm;}
            else {b=m;fb=fm;}
        }
        return (a+b)*.5;
    }

    private static String f(double number) {
        return String.format(Locale.US,"%.6g",number);
    }
}
