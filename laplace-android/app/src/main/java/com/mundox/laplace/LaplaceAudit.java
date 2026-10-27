package com.mundox.laplace;

import java.util.Locale;

/** Audits independent algebraic oracles against the numerical PDE solver. */
public final class LaplaceAudit {
    private LaplaceAudit() {}
    public static final class Report {
        public final int checks;
        public final int[] dimensions;
        public final double[] quarticErrors,quarticResiduals;
        public final int[] quarticIterations;
        public final double maxObservedResidual,quadraticGain1,quadraticGain2,quadraticGain3;
        public final boolean passed;
        Report(int checks,int[] dimensions,double[] errors,double[] residuals,int[] iterations,
               double maxObservedResidual,boolean passed) {
            this.checks=checks;this.dimensions=dimensions;this.quarticErrors=errors;
            this.quarticResiduals=residuals;this.quarticIterations=iterations;
            this.maxObservedResidual=maxObservedResidual;this.passed=passed;
            quadraticGain1=errors[0]/errors[1];
            quadraticGain2=errors[1]/errors[2];
            quadraticGain3=errors[2]/errors[3];
        }
        public String asCsv(){
            StringBuilder csv=new StringBuilder("dimension,error_maximo,residuo_laplaciano,iteraciones\n");
            for(int k=0;k<dimensions.length;k++)
                csv.append(String.format(Locale.ROOT,"%d,%.14g,%.14g,%d\n",dimensions[k],quarticErrors[k],quarticResiduals[k],quarticIterations[k]));
            return csv.toString();
        }
    }
    private static final class AuditCounter {
        int checked;
        void require(boolean expression,String description){
            checked++;
            if(!expression) throw new IllegalStateException("Falló la auditoría: "+description);
        }
    }
    private static double oracle(LaplaceEngine.Field field,double x,double y) {
        switch(field) {
            case LINEAR:return (x-1)+2*y;
            case SADDLE:return (x+y)*(x-y);
            case CUBIC:return x*(x*x-3*y*y);
            case QUARTIC:return (x*x-y*y)*(x*x-y*y)-4*x*x*y*y;
            default:throw new IllegalArgumentException("unknown case");
        }
    }
    public static Report evaluate() {
        final int[] ns={17,33,65,129};
        double[] errors=new double[ns.length],residuals=new double[ns.length];
        int[] iterations=new int[ns.length];
        AuditCounter c=new AuditCounter();
        double worstResidual=0;
        for(LaplaceEngine.Field field:LaplaceEngine.Field.values()) {
            for(int k=0;k<ns.length;k++) {
                int n=ns[k];
                LaplaceEngine.Solution sol=LaplaceEngine.solve(field,n);
                c.require(sol.converged,field+" no convergió con "+n);
                c.require(sol.iterations>0&&sol.iterations<25000,field+" iteraciones");
                c.require(sol.maxBoundaryError==0,field+" fronteras");
                c.require(sol.maxResidual<2e-7,field+" residuo numérico: "+sol.maxResidual);
                c.require(sol.maximumPrincipleViolation<1e-10,field+" principio del máximo");
                double maxError=0.0,h=1.0/(n-1);
                for(int y=0;y<n;y++)for(int x=0;x<n;x++)
                    maxError=Math.max(maxError,Math.abs(sol.value(x,y)-oracle(field,x*h,y*h)));
                c.require(Math.abs(maxError-sol.maxError)<1e-10,field+" inconsistencia de error");
                if(field!=LaplaceEngine.Field.QUARTIC)c.require(maxError<2e-9,field+" valor analítico");
                else {
                    errors[k]=maxError;residuals[k]=sol.maxResidual;iterations[k]=sol.iterations;
                }
                worstResidual=Math.max(worstResidual,sol.maxResidual);
            }
        }
        for(int k=0;k<errors.length-1;k++){
            c.require(errors[k]>errors[k+1],"error debe disminuir con resolución");
            c.require(errors[k]/errors[k+1]>3.5&&errors[k]/errors[k+1]<4.5,"convergencia de orden 2");
        }
        return new Report(c.checked,ns,errors,residuals,iterations,worstResidual,true);
    }
}
