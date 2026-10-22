package com.mundox.fusion3d;

import com.cosmicx.pc.FusionPhysics;

/**
 * Tests actual operator identities, Hermitian symmetry and the analytic
 * eigenvalue/eigenvector of the FREE particle Dirichlet finite-difference
 * Hamiltonian, without claiming nuclear dynamics or MIL execution.
 */
public final class RadialHamiltonianTest {
    private RadialHamiltonianTest() {}
    private static void require(boolean b,String m){if(!b)throw new AssertionError(m);}
    private static void close(double x,double y,double tolerance,String label){
        require(Double.isFinite(x)&&Double.isFinite(y) && Math.abs(x-y)<=tolerance,
                label+" "+x+" != "+y);
    }
    public static void main(String[] args){
        final int n=256;
        final double length=48., mu=1125.;
        RadialHamiltonian.Grid free=RadialHamiltonian.free(n,length,mu);
        require(free.sites==n && !free.hasNuclearPotential,"free model");
        close(free.stepFm,length/(n+1),1e-15,"grid spacing");
        // A real symmetric tridiagonal Dirichlet Laplacian has the exact
        // sampled sine eigenvector sin(j*pi/(N+1)) for j=1,...,N.
        double[] sine=new double[n];
        for(int j=0;j<n;j++)sine[j]=Math.sin((j+1)*Math.PI/(n+1));
        double expected=2.*free.kineticMeV*(1.-Math.cos(Math.PI/(n+1)));
        close(free.rayleighMeV(sine),expected,1e-10,"Dirichlet sine eigenvalue");
        double[] hs=free.apply(sine);
        double maxResidual=0.;
        for(int j=0;j<n;j++)
            maxResidual=Math.max(maxResidual,Math.abs(hs[j]-expected*sine[j]));
        require(maxResidual<1e-9,"free eigenstate residual="+maxResidual);

        FusionPhysics.Analysis dt=FusionPhysics.analyze(1,2,1,3);
        NuclearInteractionModel.Result model=NuclearInteractionModel.solve(dt,.020,35.);
        RadialHamiltonian.Grid h=RadialHamiltonian.from(model);
        require(h.hasNuclearPotential && h.sites==256,"modeled H grid");
        require(h.vnMeV[0]<0 && h.vcMeV[0]>0,"nuclear attraction and Coulomb repulsion");
        require(h.csv().contains("i,r_fm,vc_mev,vn_mev"),"CSV schema");
        require(h.csv().contains("MIL Core no la ejecuta todavia"),"honest model provenance");
        for(int i=0;i<n;i++){
            close(h.diagonalMeV[i]-2.*h.kineticMeV,
                    h.totalMeV[i],1e-9,"Hamiltonian diagonal decomposition");
            require(Double.isFinite(h.diagonalMeV[i]),"finite matrix component");
        }
        double[] a=new double[n],b=new double[n];
        for(int i=0;i<n;i++){
            a[i]=Math.sin(1.32*(i+1));
            b[i]=Math.cos(.64*(i+1));
        }
        double[] ha=h.apply(a),hb=h.apply(b);
        double aHb=0.,haB=0.;
        for(int i=0;i<n;i++){aHb+=a[i]*hb[i];haB+=ha[i]*b[i];}
        close(aHb,haB,1e-8,"Hermitian bilinear identity");
        boolean reject=false;
        try {h.apply(new double[3]);}catch(IllegalArgumentException e){reject=true;}
        require(reject,"reject wrong-size state");
        System.out.println("RADIAL_HAMILTONIAN_PASS n="+n+
                " sparse_symmetric=true free_sine_eigenvalue="+expected+
                " maxResidual="+maxResidual+" CSV=PASS");
    }
}
