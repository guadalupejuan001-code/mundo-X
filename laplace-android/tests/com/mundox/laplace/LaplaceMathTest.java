package com.mundox.laplace;

import java.util.Locale;

/** Independent mathematical oracles, deliberately no use of Field.exact(). */
public final class LaplaceMathTest {
    private static int checks;
    private static void verify(boolean ok,String message) {
        checks++; if(!ok)throw new AssertionError(message);
    }
    private static double independentExact(LaplaceEngine.Field m,double x,double y){
        if(m==LaplaceEngine.Field.LINEAR)return x+2*y-1;
        if(m==LaplaceEngine.Field.SADDLE)return (x-y)*(x+y);
        if(m==LaplaceEngine.Field.CUBIC)return x*(x*x-3*y*y);
        return (x*x-y*y)*(x*x-y*y)-4*x*x*y*y;
    }
    public static void main(String[] args){
        double quartic17=0,quartic33=0,quartic65=0;
        for(LaplaceEngine.Field field:LaplaceEngine.Field.values()) {
            for(int n:new int[]{9,17,33,65}) {
                LaplaceEngine.Solution s=LaplaceEngine.solve(field,n);
                verify(s.converged,field+" grid="+n+" failed to converge");
                verify(s.iterations<25000,"solver iteration budget exceeded");
                verify(s.maxResidual<2e-8,"not a discrete Laplace solution: "+s.maxResidual);
                verify(s.maxBoundaryError==0,"boundary was modified");
                verify(s.maximumPrincipleViolation<1e-10,"maximum principle violated");
                verify(s.minBoundary<=s.maxBoundary,"invalid extrema");
                double h=1.0/(n-1),maxError=0;
                for(int j=0;j<n;j++)for(int i=0;i<n;i++) {
                    double target=independentExact(field,i*h,j*h);
                    maxError=Math.max(maxError,Math.abs(target-s.value(i,j)));
                }
                verify(Math.abs(maxError-s.maxError)<1e-12,"error calculation inconsistent");
                if(field!=LaplaceEngine.Field.QUARTIC)
                    verify(maxError<2e-9,"discrete harmonic polynomial differs from oracle: "+maxError);
                if(field==LaplaceEngine.Field.QUARTIC) {
                    if(n==17)quartic17=maxError;
                    if(n==33)quartic33=maxError;
                    if(n==65)quartic65=maxError;
                }
                System.out.printf(Locale.ROOT,"FIELD=%s N=%d ITER=%d RESIDUAL=%.3e ERROR=%.9g CENTER=%.9f%n",
                        field,n,s.iterations,s.maxResidual,maxError,s.center);
            }
        }
        verify(quartic17>quartic33 && quartic33>quartic65,"quartic truncation error not reducing");
        verify(quartic17/quartic33>3.5&&quartic33/quartic65>3.5,"not second-order convergence");
        LaplaceEngine.Solution fine=LaplaceEngine.solve(LaplaceEngine.Field.QUARTIC,129);
        verify(fine.converged&&fine.maxResidual<2e-7,"fine grid did not solve PDE");
        verify(quartic65/fine.maxError>3.5,"fine-grid improvement insufficient");
        verify(Math.abs(fine.center - independentExact(LaplaceEngine.Field.QUARTIC,0.5,0.5))<0.00005,"center not close to analytic solution");
        boolean rejected=false;
        try{LaplaceEngine.solve(LaplaceEngine.Field.LINEAR,130);}catch(IllegalArgumentException e){rejected=true;}
        verify(rejected,"invalid grid accepted");
        rejected=false;
        try{LaplaceEngine.solve(LaplaceEngine.Field.LINEAR,33,100,1e-20);}catch(IllegalArgumentException e){rejected=true;}
        verify(rejected,"invalid tolerance accepted");
        System.out.printf(Locale.ROOT,"LAPLACE_EQUATION_VERIFIED checks=%d quartic17=%.9g quartic33=%.9g quartic65=%.9g quartic129=%.9g rate17to33=%.5f rate33to65=%.5f rate65to129=%.5f\n",
           checks,quartic17,quartic33,quartic65,fine.maxError,quartic17/quartic33,quartic33/quartic65,quartic65/fine.maxError);
    }
}
