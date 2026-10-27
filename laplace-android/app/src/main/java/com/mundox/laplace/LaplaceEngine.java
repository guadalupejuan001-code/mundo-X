package com.mundox.laplace;

/**
 * Independent, CPU-native numerical solver for the genuine 2D Laplace equation:
 *     d2u/dx2 + d2u/dy2 = 0 on [0,1]^2.
 *
 * Dirichlet conditions are taken from known harmonic polynomials and are held
 * fixed. The interior is solved with 5-point finite differences and SOR.
 * Nothing here predicts securities prices, time evolution, or the future.
 * No backend in REVERSE-PC or BBY Q is touched.
 */
public final class LaplaceEngine {
    public enum Field {
        LINEAR("Gradiente", "u(x,y) = x + 2y - 1", "Campo lineal"),
        SADDLE("Silla", "u(x,y) = x² - y²", "Campo cuadrático"),
        CUBIC("Cúbico", "u(x,y) = x³ - 3xy²", "Campo multipolar"),
        QUARTIC("Cuártico", "u(x,y) = x⁴ - 6x²y² + y⁴", "Prueba de discretización");
        public final String display, expression, description;
        Field(String display, String expression, String description) {
            this.display = display;
            this.expression = expression;
            this.description = description;
        }
        public double exact(double x, double y) {
            switch (this) {
                case LINEAR: return x + 2.0*y - 1.0;
                case SADDLE: return x*x - y*y;
                case CUBIC: return x*x*x - 3.0*x*y*y;
                case QUARTIC: return x*x*x*x - 6.0*x*x*y*y + y*y*y*y;
                default: throw new IllegalStateException("unknown polynomial");
            }
        }
    }

    public static final class Solution {
        public final Field mode;
        public final int points, iterations;
        public final double h, omega, maxResidual, maxError, center,
                minBoundary, maxBoundary, maxBoundaryError, maximumPrincipleViolation;
        public final boolean converged;
        public final long durationNanos;
        private final double[][] samples;
        Solution(Field mode,int points,int iterations,double h,double omega,
                 double maxResidual,double maxError,double center,
                 double minBoundary,double maxBoundary,double maxBoundaryError,
                 double maximumPrincipleViolation, boolean converged,long durationNanos,
                 double[][] samples) {
            this.mode=mode;this.points=points;this.iterations=iterations;
            this.h=h;this.omega=omega;this.maxResidual=maxResidual;this.maxError=maxError;
            this.center=center;this.minBoundary=minBoundary;this.maxBoundary=maxBoundary;
            this.maxBoundaryError=maxBoundaryError;
            this.maximumPrincipleViolation=maximumPrincipleViolation;
            this.converged=converged;this.durationNanos=durationNanos;
            this.samples=samples;
        }
        public double value(int i, int j) {return samples[i][j];}
        public double[] flattened() {
            double[] z=new double[points*points];int k=0;
            for(int j=0;j<points;j++)for(int i=0;i<points;i++)z[k++]=samples[i][j];
            return z;
        }
    }

    private LaplaceEngine() {}

    public static Solution solve(Field mode, int points) {
        return solve(mode,points,25000,1e-13);
    }
    public static Solution solve(Field mode,int points,int maxIterations,double relativeTolerance) {
        if(mode==null)throw new IllegalArgumentException("A field is required");
        if(points<9||points>129||((points-1)&(points-2))!=0)
            throw new IllegalArgumentException("Use a grid of 9, 17, 33, 65, or 129 points");
        if(maxIterations<1||maxIterations>200000||!(relativeTolerance>0)||relativeTolerance<1e-15||relativeTolerance>1e-4)
            throw new IllegalArgumentException("Solver budgets outside supported limits");
        long start=System.nanoTime();
        double h=1.0/(points-1),omega=2.0/(1.0+Math.sin(Math.PI/(points-1)));
        double[][] u=new double[points][points];
        double min=Double.POSITIVE_INFINITY,max=Double.NEGATIVE_INFINITY;
        for(int j=0;j<points;j++)for(int i=0;i<points;i++) {
            if(i==0||j==0||i==points-1||j==points-1){
                double value=mode.exact(i*h,j*h);
                if(!Double.isFinite(value))throw new ArithmeticException("Invalid boundary");
                u[i][j]=value;
                if(value<min)min=value;
                if(value>max)max=value;
            }
        }
        final double stop=relativeTolerance*Math.max(1.0,Math.max(Math.abs(min),Math.abs(max)));
        boolean converged=false;
        int iter=0;
        for(;iter<maxIterations;iter++) {
            double delta=0;
            for(int j=1;j<points-1;j++)for(int i=1;i<points-1;i++) {
                final double average=0.25*(u[i-1][j]+u[i+1][j]+u[i][j-1]+u[i][j+1]);
                double change=omega*(average-u[i][j]);
                u[i][j]+=change;
                if(!Double.isFinite(u[i][j]))throw new ArithmeticException("Solver diverged");
                delta=Math.max(delta,Math.abs(change));
            }
            if(delta<stop){converged=true;iter++;break;}
        }
        double residual=0,error=0,boundaryError=0,violation=0;
        double invH2=1.0/(h*h);
        for(int j=0;j<points;j++)for(int i=0;i<points;i++) {
            double exact=mode.exact(i*h,j*h);
            if(i==0||j==0||i==points-1||j==points-1) {
                boundaryError=Math.max(boundaryError,Math.abs(u[i][j]-exact));
            } else {
                double laplacian=(u[i-1][j]+u[i+1][j]+u[i][j-1]+u[i][j+1]-4*u[i][j])*invH2;
                residual=Math.max(residual,Math.abs(laplacian));
                error=Math.max(error,Math.abs(u[i][j]-exact));
            }
            violation=Math.max(violation,Math.max(min-u[i][j],u[i][j]-max));
        }
        return new Solution(mode,points,iter,h,omega,residual,error,u[(points-1)/2][(points-1)/2],
                min,max,boundaryError,violation,converged,System.nanoTime()-start,u);
    }
}
