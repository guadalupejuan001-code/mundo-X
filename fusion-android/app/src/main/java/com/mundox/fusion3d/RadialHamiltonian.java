package com.mundox.fusion3d;

import java.util.Locale;

/**
 * Matrix representation of a 1D l=0 radial Schrödinger Hamiltonian on a
 * finite Dirichlet grid (u(0)=u(L)=0). This does NOT propagate wavefunctions.
 *
 * H = -[(hbar*c)^2/(2 mu*c^2)] d²/dr² + V_C(r) + V_N(r)
 *
 * It is a REAL SYMMETRIC TRIDIAGONAL matrix, represented in O(N) memory.
 * This is an explicit numerical operator for future verified MIL backends,
 * not a claim that MIL Core already solves nuclear Hamiltonians.
 */
public final class RadialHamiltonian {
    private RadialHamiltonian(){}

    public static final int DEFAULT_SITES=256;
    public static final double DEFAULT_LENGTH_FM=96.;

    public static final class Grid {
        public final int sites;
        public final double domainFm,stepFm,reducedMassMeV,kineticMeV;
        public final boolean hasNuclearPotential;
        public final double[] radiusFm,vcMeV,vnMeV,totalMeV,diagonalMeV;
        public final double offDiagonalMeV;

        private Grid(int n,double domain,double mu,NuclearInteractionModel.Result model) {
            if(n<64||n>512||!Double.isFinite(domain)||domain<12||domain>256||
                    !Double.isFinite(mu)||mu<=0)
                throw new IllegalArgumentException("Malla radial fuera de presupuesto");
            sites=n;domainFm=domain;
            stepFm=domain/(n+1);
            reducedMassMeV=mu;
            kineticMeV=NuclearInteractionModel.HBAR_C_MEV_FM*
                    NuclearInteractionModel.HBAR_C_MEV_FM/(2.*mu*stepFm*stepFm);
            offDiagonalMeV=-kineticMeV;
            hasNuclearPotential=model!=null;
            radiusFm=new double[n];
            vcMeV=new double[n];
            vnMeV=new double[n];
            totalMeV=new double[n];
            diagonalMeV=new double[n];
            for(int i=0;i<n;i++){
                double r=(i+1)*stepFm;
                radiusFm[i]=r;
                double coulomb=model==null ? 0. :
                        NuclearInteractionModel.coulombPotentialMeV(model.isotopes,r);
                double attraction=model==null ? 0. :
                        NuclearInteractionModel.nuclearPotentialMeV(
                                model.isotopes,r,model.nuclearDepthMeV);
                vcMeV[i]=coulomb;
                vnMeV[i]=attraction;
                totalMeV[i]=coulomb+attraction;
                diagonalMeV[i]=2.*kineticMeV+totalMeV[i];
                if(!Double.isFinite(diagonalMeV[i]))
                    throw new IllegalStateException("Hamiltoniano no finito");
            }
        }

        /** Matrix-vector application without constructing an N×N matrix. */
        public double[] apply(double[] state) {
            if(state==null||state.length!=sites)
                throw new IllegalArgumentException("vector con dimensión distinta al Hamiltoniano");
            double[] out=new double[sites];
            for(int i=0;i<sites;i++){
                if(!Double.isFinite(state[i]))
                    throw new IllegalArgumentException("amplitudes no finitas");
            }
            for(int i=0;i<sites;i++){
                out[i]=diagonalMeV[i]*state[i]+
                        (i>0?offDiagonalMeV*state[i-1]:0.)+
                        (i+1<sites?offDiagonalMeV*state[i+1]:0.);
            }
            return out;
        }

        public double rayleighMeV(double[] state) {
            double[] h=apply(state);
            double num=0.,den=0.;
            for(int i=0;i<sites;i++){num+=state[i]*h[i];den+=state[i]*state[i];}
            if(!(den>0.)||!Double.isFinite(num)||!Double.isFinite(den))
                throw new IllegalArgumentException("estado no normalizable");
            return num/den;
        }

        public String summary(){
            return String.format(Locale.US,
                    "H radial l=0: %d puntos, L=%.3f fm, Δr=%.5f fm\n"+
                    "Hii = 2t + Vc(ri) + Vn(ri); H(i,i+1) = -t\n"+
                    "t=(ħc)²/(2μc² Δr²) = %.6g MeV\n"+
                    "Matriz REAL SIMÉTRICA tridiagonal; frontera u(0)=u(L)=0.\n"+
                    "Solo operador discretizado: SIN evolución temporal ni diagonalización en MIL Core.\n",
                    sites,domainFm,stepFm,kineticMeV);
        }

        /** Reproducible CSV with a comment-only header, never network I/O. */
        public String csv(){
            StringBuilder b=new StringBuilder(sites*100+450);
            b.append("# Mundo X / Fusion 3D / H radial 1D\n")
             .append("# Parametros y potencial nuclear fenomenologicos, NO ajustados ni validados para prediccion de fusiones.\n")
             .append("# Solo matriz tridiagonal l=0; MIL Core no la ejecuta todavia.\n")
             .append("# Fronteras Dirichlet; L_fm=").append(domainFm)
             .append("; puntos=").append(sites)
             .append("; paso_fm=").append(stepFm).append("\n")
             .append("# ").append(summary().replace("\n","\n# "))
             .append("i,r_fm,vc_mev,vn_mev,vtotal_mev,diagonal_mev,offdiag_to_next_mev\n");
            for(int i=0;i<sites;i++){
                b.append(String.format(Locale.US,
                        "%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                        i,radiusFm[i],vcMeV[i],vnMeV[i],totalMeV[i],
                        diagonalMeV[i],i+1<sites?offDiagonalMeV:0.));
            }
            return b.toString();
        }
    }

    /** The only modelled isotope entrance channels permit H construction. */
    public static Grid from(NuclearInteractionModel.Result result) {
        if(result==null||!result.hasNuclearModel)
            throw new IllegalArgumentException("No existe modelo nuclear para esta pareja");
        return new Grid(DEFAULT_SITES,DEFAULT_LENGTH_FM,result.reducedMassMeV,result);
    }

    /** Pure free-particle reference Hamiltonian, used as an analytic oracle. */
    public static Grid free(int n,double lengthFm,double reducedMassMeV) {
        return new Grid(n,lengthFm,reducedMassMeV,null);
    }
}
