package com.mundox.fusion3d;

import com.cosmicx.pc.FusionPhysics;

import java.util.Locale;

/** Classical Coulomb potential for charged NUCLEI outside the contact radius.
 *
 * This calculation is real and dimensioned. It is NOT a reaction probability,
 * collision solution with nuclear forces, cross section, tunnelling
 * coefficient, or full quantum many-body simulation.
 */
public final class CollisionModel {
    private CollisionModel() {}

    // e^2 / (4*pi*epsilon0) in MeV*fm, conventional rounded constant.
    public static final double COULOMB_MEV_FM = 1.4399764;
    // Effective sharp-surface estimate of nuclear radius, in femtometres.
    public static final double R0_FM = 1.2;

    public static final class Result {
        public final FusionPhysics.Analysis isotopes;
        public final double eCmMeV;
        public final double rContactFm;
        public final double coulombContactMeV;
        public final double classicalTurningFm;
        public final boolean classicalContact;
        public final double energyRatio;

        private Result(FusionPhysics.Analysis selected,double energy){
            if(selected==null||!Double.isFinite(energy)||energy<=0||energy>10000)
                throw new IllegalArgumentException("Ecm no es una energía válida");
            isotopes=selected;
            eCmMeV=energy;
            rContactFm=R0_FM*(Math.cbrt(selected.first.a)+Math.cbrt(selected.second.a));
            coulombContactMeV=COULOMB_MEV_FM*selected.first.z*selected.second.z/rContactFm;
            classicalTurningFm=COULOMB_MEV_FM*selected.first.z*selected.second.z/eCmMeV;
            classicalContact=eCmMeV>=coulombContactMeV;
            energyRatio=eCmMeV/coulombContactMeV;
        }

        public String summary(){
            StringBuilder result=new StringBuilder();
            result.append("1. NÚCLEOS SELECCIONADOS\n");
            result.append("   ").append(isotopes.first.label()).append(": ")
                    .append(isotopes.first.z).append(" protones, ")
                    .append(isotopes.first.a-isotopes.first.z).append(" neutrones\n");
            result.append("   ").append(isotopes.second.label()).append(": ")
                    .append(isotopes.second.z).append(" protones, ")
                    .append(isotopes.second.a-isotopes.second.z).append(" neutrones\n\n");
            result.append("2. ENERGÍA DE COLISIÓN (CENTRO DE MASAS)\n");
            result.append("   Ecm = ").append(f(eCmMeV*1000)).append(" keV\n");
            result.append("   Esta energía NO es la energía liberada por fusión.\n\n");
            result.append("3. APROXIMACIÓN COULOMBIANA\n");
            result.append("   V(r) ≈ 1.4399764 · Z1 · Z2 / r   [MeV si r está en fm]\n");
            result.append("   Radio de contacto estimado = ").append(f(rContactFm)).append(" fm\n");
            result.append("   Energía electrostática en contacto = ")
                    .append(f(coulombContactMeV)).append(" MeV\n");
            result.append("   Distancia de retorno clásica de dos cargas puntuales = ")
                    .append(f(classicalTurningFm)).append(" fm\n");
            result.append(classicalContact
                    ? "   Ecm supera la estimación de Coulomb en contacto. Esto no garantiza reacción.\n"
                    : "   Ecm no supera la barrera clásica. El túnel cuántico puede permitir reacciones.\n");
            result.append("   Límite: sin interacción nuclear fuerte, secciones eficaces, ni túnel calculado.\n\n");
            result.append("4. CANALES DOCUMENTADOS / ESTADO DE LA EVIDENCIA\n");
            result.append(isotopes.explanation()).append("\n");
            result.append("\nMUNDO X: estado de investigación. Valores Q calculados desde masas NIST; "+
                          "barrera es una aproximación clásica; render Ether X muestra geometría esquemática.\n");
            return result.toString();
        }
        private static String f(double n) {return String.format(Locale.US,"%.5g",n);}
    }

    public static Result calculate(FusionPhysics.Analysis isotopes,double energyMeV) {
        return new Result(isotopes,energyMeV);
    }

    public static double potentialMeV(FusionPhysics.Analysis isotopes,double separationFm) {
        if(isotopes==null||!Double.isFinite(separationFm)||separationFm<=0)
            throw new IllegalArgumentException("Separación nuclear inválida");
        return COULOMB_MEV_FM*isotopes.first.z*isotopes.second.z/separationFm;
    }

    /** Logarithmic energy control from 0.1 to 10,000 keV. */
    public static double sliderToMeV(int position) {
        int p=Math.max(0,Math.min(1000,position));
        return Math.pow(10.0,-4.0 + p*5.0/1000.0); // 0.0001..10 MeV
    }

    public static int energyToSlider(double mev) {
        if(mev<=0||!Double.isFinite(mev))return 0;
        return Math.max(0,Math.min(1000,(int)Math.round(
                (Math.log10(mev)+4.0)*1000.0/5.0)));
    }
}
