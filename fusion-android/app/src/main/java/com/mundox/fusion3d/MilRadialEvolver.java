package com.mundox.fusion3d;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.Locale;

/**
 * Runs actual gates in the repository's MIL ReducedExactTRepresentation engine.
 *
 * The 4/8-site pilot remains untouched. The structured Pauli scaling backend
 * also supports 16/32/64/128/256 sites, with explicit gate/component budgets.
 * The 256-site radial operator is now evolved by 8 logical qubits of the
 * SOFTWARE MIL Reduced backend. This is NOT a many-body nuclear solver.
 * RK4 is independent validation only, never the MIL execution engine.
 */
public final class MilRadialEvolver {
    static { System.loadLibrary("mxfusionrender"); }
    private MilRadialEvolver() {}

    private static native String evolveNative(
            double[] diagonalMeV,double offDiagonalMeV,int initialSite,
            double tauInverseMeV,int strangSteps);

    public static final class Result {
        public final int qubits,sites,initialSite,steps,pauliTerms,gates,peakComponents;
        public final double tauInverseMev,physicalSeconds,norm,referenceNorm;
        public final double fidelity,probabilityL1,initialProbability;
        public final double executionMs,pauliPreparationMs,referenceMs;
        public final double[] probabilities,referenceProbabilities;
        public final String backend,method,oracle,pauliDecomposition;

        private Result(JSONObject state) throws JSONException {
            backend=state.getString("backend");
            method=state.getString("method");
            oracle=state.getString("oracle");
            pauliDecomposition=state.optString("pauli_decomposition","legacy");
            qubits=state.getInt("qubits");
            sites=state.getInt("sites");
            initialSite=state.getInt("initial_site");
            steps=state.getInt("steps");
            pauliTerms=state.getInt("pauli_terms");
            gates=state.getInt("gates");
            peakComponents=state.getInt("peak_components");
            tauInverseMev=state.getDouble("tau_inverse_mev");
            physicalSeconds=state.getDouble("physical_seconds");
            norm=state.getDouble("norm");
            referenceNorm=state.getDouble("reference_norm");
            fidelity=state.getDouble("fidelity");
            probabilityL1=state.getDouble("probability_l1");
            initialProbability=state.getDouble("initial_probability");
            executionMs=state.optDouble("execution_ms",0.);
            pauliPreparationMs=state.optDouble("pauli_preparation_ms",0.);
            referenceMs=state.optDouble("reference_ms",0.);
            probabilities=asArray(state.getJSONArray("probabilities"));
            referenceProbabilities=asArray(state.getJSONArray("reference_probabilities"));
            if(!"MIL ReducedExactTRepresentation".equals(backend) || qubits<2||
                    qubits>8 || sites!=(1<<qubits) ||
                    peakComponents<1 || peakComponents>1024 ||
                    gates<1 || gates>200000 ||
                    probabilities.length!=sites ||
                    referenceProbabilities.length!=sites ||
                    !Double.isFinite(norm)||!Double.isFinite(fidelity) ||
                    !Double.isFinite(probabilityL1))
                throw new JSONException("Resultado MIL incompleto o inválido");
        }

        private static double[] asArray(JSONArray array) throws JSONException {
            double[] values=new double[array.length()];
            for(int i=0;i<values.length;i++){
                values[i]=array.getDouble(i);
                if(!Double.isFinite(values[i])||values[i]<0||values[i]>1.0000001)
                    throw new JSONException("Población radial fuera de dominio");
            }
            return values;
        }

        public String summary(){
            return String.format(Locale.US,
                    "MOTOR: %s\n"+
                    "MÉTODO: %s\n"+
                    "Malla: %d posiciones radiales codificadas en %d qubits.\n"+
                    "Estado inicial localizado: sitio %d (de 0 a %d).\n"+
                    "Tiempo τ=t/ħ = %.6g MeV^-1 (t≈%.3e s).\n"+
                    "Evolución: %d pasos Strang, %d términos Pauli, %d puertas MIL.\n"+
                    "Descomposición Pauli: %s; MIL puertas: %.3f ms.\n"+
                    "Preparación Pauli: %.3f ms; RK4 independiente: %.3f ms.\n"+
                    "Componentes del motor (máximo): %d.\n"+
                    "Norma MIL = %.10f · norma referencia = %.10f.\n"+
                    "Fidelidad MIL vs. RK4 independiente = %.9f.\n"+
                    "Distancia L1 entre probabilidades = %.6g.\n"+
                    "Probabilidad de seguir en sitio %d = %.6f.\n"+
                    "Distribución calculada (MIL): %s\n"+
                    "Cálculo independiente (RK4): %s\n"+
                    "ADVERTENCIA: fidelidad = comparación numérica del mismo "+
                    "Hamiltoniano discretizado, no validación de parámetros "+
                    "nucleares ni probabilidad de fusión. La malla de 256 sitios "+
                    "solo se evoluciona al seleccionar explícitamente 8 qubits. "+
                    "No se calculan secciones eficaces ni dinámica de muchos cuerpos.",
                    backend,method,sites,qubits,initialSite,sites-1,
                    tauInverseMev,physicalSeconds,steps,pauliTerms,gates,
                    pauliDecomposition,executionMs,pauliPreparationMs,referenceMs,
                    peakComponents,norm,referenceNorm,fidelity,probabilityL1,
                    initialSite,initialProbability,display(probabilities),
                    display(referenceProbabilities));
        }

        private static String display(double[] values){
            StringBuilder b=new StringBuilder("[");
            int n=values.length;
            if(n<=32){
                for(int i=0;i<n;i++){
                    if(i>0)b.append(", ");
                    b.append(String.format(Locale.US,"%.5f",values[i]));
                }
            }else{
                for(int i=0;i<16;i++){
                    if(i>0)b.append(", ");
                    b.append(String.format(Locale.US,"%.5f",values[i]));
                }
                b.append(", … ").append(n-24).append(" sitios más … ");
                for(int i=n-8;i<n;i++){
                    if(i>n-8)b.append(", ");
                    b.append(String.format(Locale.US,"%.5f",values[i]));
                }
            }
            return b.append(']').toString();
        }
    }

    public static Result execute(RadialHamiltonian.Grid grid,int initialSite,
                                 double tauInverseMev,int steps) throws JSONException {
        if(grid==null || !grid.hasNuclearPotential ||
                grid.sites<4 || grid.sites>256 ||
                (grid.sites&(grid.sites-1))!=0 ||
                initialSite<0 || initialSite>=grid.sites)
            throw new IllegalArgumentException("Modelo radial reducido no disponible");
        if(steps<1||steps>(grid.sites>8?12:24)||tauInverseMev<0||tauInverseMev>.12||
                !Double.isFinite(tauInverseMev))
            throw new IllegalArgumentException("Parámetros MIL fuera de presupuesto");
        String output=evolveNative(grid.diagonalMeV,grid.offDiagonalMeV,
                initialSite,tauInverseMev,steps);
        if(output==null)throw new IllegalStateException("MIL Core no devolvió resultado");
        return new Result(new JSONObject(output));
    }
}
