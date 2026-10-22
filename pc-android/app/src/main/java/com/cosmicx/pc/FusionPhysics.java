package com.cosmicx.pc;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * Educational nuclear-fusion calculator. Does not simulate nuclear dynamics.
 * Exactly tabulated reaction Q values use neutral atomic isotope masses (NIST),
 * so electron numbers are balanced in every supported channel.
 * Unknown channels deliberately return NO Q value and NO claim of synthesis.
 * No network, shell, radioactive source, or physical-machine interfaces.
 */
public final class FusionPhysics {
    private FusionPhysics() {}

    public static final String[] SYMBOLS = "H He Li Be B C N O F Ne Na Mg Al Si P S Cl Ar K Ca Sc Ti V Cr Mn Fe Co Ni Cu Zn Ga Ge As Se Br Kr Rb Sr Y Zr Nb Mo Tc Ru Rh Pd Ag Cd In Sn Sb Te I Xe Cs Ba La Ce Pr Nd Pm Sm Eu Gd Tb Dy Ho Er Tm Yb Lu Hf Ta W Re Os Ir Pt Au Hg Tl Pb Bi Po At Rn Fr Ra Ac Th Pa U Np Pu Am Cm Bk Cf Es Fm Md No Lr Rf Db Sg Bh Hs Mt Ds Rg Cn Nh Fl Mc Lv Ts Og".split(" ");
    public static final String[] NAMES = "Hidrógeno|Helio|Litio|Berilio|Boro|Carbono|Nitrógeno|Oxígeno|Flúor|Neón|Sodio|Magnesio|Aluminio|Silicio|Fósforo|Azufre|Cloro|Argón|Potasio|Calcio|Escandio|Titanio|Vanadio|Cromo|Manganeso|Hierro|Cobalto|Níquel|Cobre|Zinc|Galio|Germanio|Arsénico|Selenio|Bromo|Criptón|Rubidio|Estroncio|Itrio|Circonio|Niobio|Molibdeno|Tecnecio|Rutenio|Rodio|Paladio|Plata|Cadmio|Indio|Estaño|Antimonio|Telurio|Yodo|Xenón|Cesio|Bario|Lantano|Cerio|Praseodimio|Neodimio|Prometio|Samario|Europio|Gadolinio|Terbio|Disprosio|Holmio|Erbio|Tulio|Iterbio|Lutecio|Hafnio|Tántalo|Wolframio|Renio|Osmio|Iridio|Platino|Oro|Mercurio|Talio|Plomo|Bismuto|Polonio|Astato|Radón|Francio|Radio|Actinio|Torio|Protactinio|Uranio|Neptunio|Plutonio|Americio|Curio|Berkelio|Californio|Einstenio|Fermio|Mendelevio|Nobelio|Lawrencio|Rutherfordio|Dubnio|Seaborgio|Bohrio|Hassio|Meitnerio|Darmstadtio|Roentgenio|Copernicio|Nihonio|Flerovio|Moscovio|Livermorio|Teneso|Oganesón".split("\\|");
    // Convenient illustrative isotope A defaults. Radioactive elements may
    // have many isotopes; choosing A is NOT evidence that an isotope exists.
    public static final int[] SUGGESTED_A = {1, 4, 7, 9, 11, 12, 14, 16, 19, 20, 23, 24, 27, 28, 31, 32, 35, 40, 39, 40, 45, 48, 51, 52, 55, 56, 59, 58, 63, 64, 69, 74, 75, 80, 79, 84, 85, 88, 89, 90, 93, 98, 99, 102, 103, 106, 107, 114, 115, 120, 121, 130, 127, 132, 133, 138, 139, 140, 141, 142, 145, 152, 153, 158, 159, 164, 165, 166, 169, 174, 175, 180, 181, 184, 187, 192, 193, 195, 197, 202, 205, 208, 209, 209, 210, 222, 223, 226, 227, 232, 231, 238, 237, 244, 243, 247, 247, 251, 252, 257, 258, 259, 266, 267, 268, 269, 270, 277, 278, 281, 282, 285, 286, 289, 290, 293, 294, 294};

    private static final double U_TO_MEV = 931.49410242;
    private static final double MEV_TO_J = 1.602176634e-13;
    // Neutral isotope masses from NIST (u). Neutron mass is for a free neutron.
    private static final double H1 = 1.00782503223;
    private static final double D2 = 2.01410177812;
    private static final double T3 = 3.0160492779;
    private static final double HE3 = 3.0160293201;
    private static final double HE4 = 4.00260325413;
    private static final double LI6 = 6.0151228874;
    private static final double NEUTRON = 1.00866491595;

    public static final class Isotope {
        public final int z, a;
        public Isotope(int protons, int massNumber) {
            if (protons < 1 || protons > 118)
                throw new IllegalArgumentException("El número atómico debe estar entre 1 y 118");
            if (massNumber < protons || massNumber > 320)
                throw new IllegalArgumentException("A debe ser >= Z y <= 320");
            z = protons;
            a = massNumber;
        }

        public String label() { return SYMBOLS[z - 1] + "-" + a; }
        public String description() { return NAMES[z - 1] + "-" + a + " (Z=" + z + ")"; }
    }

    public static final class Channel {
        public final String label;
        public final String products;
        public final double qMeV;
        private final int reactantsZ, reactantsA, productsZ, productsA;

        Channel(String name, String products, double reactantMassU, double productMassU,
                int inputZ, int inputA, int outputZ, int outputA) {
            this.label = name;
            this.products = products;
            this.qMeV = (reactantMassU - productMassU) * U_TO_MEV;
            this.reactantsZ = inputZ;
            this.reactantsA = inputA;
            this.productsZ = outputZ;
            this.productsA = outputA;
            if (inputZ != outputZ || inputA != outputA || !Double.isFinite(qMeV))
                throw new IllegalArgumentException("Canal nuclear no conserva Z y A");
        }

        public boolean conserved() {
            return reactantsZ == productsZ && reactantsA == productsA;
        }
        public double energyJoules() { return qMeV * MEV_TO_J; }
    }

    public static final class Analysis {
        public final Isotope first, second;
        public final int totalZ, totalA;
        public final String combinedSymbol, combinedName;
        public final double coulombBarrierMeV;
        public final List<Channel> documentedChannels;

        Analysis(Isotope a, Isotope b) {
            first = a;
            second = b;
            totalZ = a.z + b.z;
            totalA = a.a + b.a;
            combinedSymbol = totalZ <= 118 ? SYMBOLS[totalZ - 1] : "Z=" + totalZ;
            combinedName = totalZ <= 118 ? NAMES[totalZ - 1] : "Fuera de los elementos actualmente nombrados";
            // Approximate touching-sphere electrostatic scale. NOT required
            // threshold: quantum tunnelling and the nuclear potential matter.
            double radiusFm = 1.2 * (Math.cbrt(a.a) + Math.cbrt(b.a));
            coulombBarrierMeV = 1.4399764 * a.z * b.z / radiusFm;
            documentedChannels = Collections.unmodifiableList(matchedChannels(a, b));
        }

        public boolean hasDocumentedChannel() {
            return !documentedChannels.isEmpty();
        }

        public String explanation() {
            StringBuilder out = new StringBuilder();
            out.append("ENTRADAS\n  ").append(first.description())
               .append(" + ").append(second.description()).append("\n\n");
            out.append("COMPOSICIÓN HIPOTÉTICA (NO SÍNTESIS DEMOSTRADA)\n");
            out.append("  Z total = ").append(totalZ)
               .append(" protones\n  A total = ").append(totalA)
               .append(" nucleones\n  Núcleo compuesto formal: ")
               .append(combinedSymbol).append("-").append(totalA)
               .append(" (").append(combinedName).append(")\n\n");
            out.append("ESCALA ELECTROSTÁTICA ORIENTATIVA\n  ")
               .append(String.format(Locale.US, "%.3f", coulombBarrierMeV))
               .append(" MeV (esferas en contacto; NO umbral necesario ni predicción de tasa)\n\n");
            if (hasDocumentedChannel()) {
                out.append("CANALES NUCLEARES TABULADOS\n");
                for (Channel channel : documentedChannels) {
                    out.append("  ").append(channel.label).append("\n")
                       .append("  → ").append(channel.products).append("\n")
                       .append("  Q = ").append(String.format(Locale.US, "%+.3f", channel.qMeV))
                       .append(" MeV, ")
                       .append(String.format(Locale.US, "%.4g", channel.energyJoules()))
                       .append(" J por evento\n\n");
                }
                out.append("Q proviene de masas isotópicas, no de un resultado de MIL Core.\n");
            } else {
                out.append("REACCIÓN NO DETERMINADA\n");
                out.append("  No hay canal validado en el catálogo local.\n");
                out.append("  Q = no disponible. Ni la suma Z+A ni la colisión gráfica prueban fusión.\n");
            }
            out.append("\nMODELO: calculadora nuclear educativa sin dinámica de muchos cuerpos. ");
            out.append("Seleccionar núcleos no crea elementos físicos.\n");
            out.append("FUENTES: NIST (masas isotópicas); IAEA (canal D+T).");
            return out.toString();
        }
    }

    public static Analysis analyze(int z1, int a1, int z2, int a2) {
        return new Analysis(new Isotope(z1, a1), new Isotope(z2, a2));
    }

    private static boolean samePair(Isotope a, Isotope b,
                                    int firstZ, int firstA, int secondZ, int secondA) {
        return (a.z == firstZ && a.a == firstA && b.z == secondZ && b.a == secondA)
                || (a.z == secondZ && a.a == secondA && b.z == firstZ && b.a == firstA);
    }

    private static List<Channel> matchedChannels(Isotope a, Isotope b) {
        ArrayList<Channel> channels = new ArrayList<>();
        // D + T -> alpha + n (mass, baryon and charge balanced).
        if (samePair(a,b, 1,2, 1,3))
            channels.add(new Channel("Deuterio + tritio", "He-4 + neutrón",
                    D2 + T3, HE4 + NEUTRON, 2,5, 2,5));
        // D + D has at least two common primary channels.
        if (samePair(a,b, 1,2, 1,2)) {
            channels.add(new Channel("Deuterio + deuterio (rama n)", "He-3 + neutrón",
                    2 * D2, HE3 + NEUTRON, 2,4, 2,4));
            channels.add(new Channel("Deuterio + deuterio (rama p)", "T-3 + H-1",
                    2 * D2, T3 + H1, 2,4, 2,4));
        }
        if (samePair(a,b, 1,2, 2,3))
            channels.add(new Channel("Deuterio + helio-3", "He-4 + H-1",
                    D2 + HE3, HE4 + H1, 3,5, 3,5));
        if (samePair(a,b, 1,2, 3,6))
            channels.add(new Channel("Deuterio + litio-6", "He-4 + He-4",
                    D2 + LI6, HE4 * 2, 4,8, 4,8));
        return channels;
    }

    public static int periodicRow(int z) {
        if (z == 1 || z == 2) return 0;
        if (z <= 10) return 1;
        if (z <= 18) return 2;
        if (z <= 36) return 3;
        if (z <= 54) return 4;
        if (z >= 58 && z <= 71) return 7;
        if (z >= 90 && z <= 103) return 8;
        if (z <= 86) return 5;
        return 6;
    }

    public static int periodicColumn(int z) {
        if (z == 1) return 0;
        if (z == 2) return 17;
        if (z >= 58 && z <= 71) return z - 55; // Ce-Lu on row 8, columns 4..17
        if (z >= 90 && z <= 103) return z - 87; // Th-Lr on row 9, columns 4..17
        if (z == 3 || z == 11 || z == 19 || z == 37 || z == 55 || z == 87) return 0;
        if (z == 4 || z == 12 || z == 20 || z == 38 || z == 56 || z == 88) return 1;
        if (z <= 10) return z + 7; // B-Ne -> col 13-18
        if (z <= 18) return z - 6; // Al-Ar -> col 13-18
        if (z <= 36) return z - 19; // Sc-Kr -> col 3-18
        if (z <= 54) return z - 37; // Y-Xe -> col 3-18
        if (z <= 86) return z == 57 ? 2 : z - 69; // La, Hf-Rn
        return z == 89 ? 2 : z - 101; // Ac, Rf-Og
    }

    static {
        if (SYMBOLS.length != 118 || NAMES.length != 118 || SUGGESTED_A.length != 118)
            throw new IllegalStateException("La tabla periódica debe contener 118 elementos");
        for (int z = 1; z <= 118; ++z) {
            if (SUGGESTED_A[z - 1] < z)
                throw new IllegalStateException("A sugerido inválido en Z=" + z);
            int row = periodicRow(z), col = periodicColumn(z);
            if (row < 0 || row > 8 || col < 0 || col > 17)
                throw new IllegalStateException("Posición periódica inválida en Z=" + z);
        }
    }
}
