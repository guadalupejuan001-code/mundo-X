package com.cosmicx.pc;

import java.util.HashSet;
import java.util.Set;

/** Standalone JVM physics and periodic-table regression, no Android SDK. */
public final class FusionPhysicsTest {
    private static void require(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }

    private static void near(double actual, double expected, double tolerance, String label) {
        require(Double.isFinite(actual) && Math.abs(actual - expected) <= tolerance,
                label + ": expected " + expected + ", got " + actual);
    }

    public static void main(String[] args) {
        require(FusionPhysics.SYMBOLS.length == 118, "118 symbols");
        require(FusionPhysics.NAMES.length == 118, "118 names");
        require(FusionPhysics.SUGGESTED_A.length == 118, "118 isotope suggestions");
        require("H".equals(FusionPhysics.SYMBOLS[0]), "H");
        require("Fe".equals(FusionPhysics.SYMBOLS[25]), "Fe");
        require("Au".equals(FusionPhysics.SYMBOLS[78]), "Au");
        require("Og".equals(FusionPhysics.SYMBOLS[117]), "Og");
        Set<Integer> occupied = new HashSet<>();
        for (int z = 1; z <= 118; z++) {
            int row = FusionPhysics.periodicRow(z);
            int col = FusionPhysics.periodicColumn(z);
            require(row >= 0 && row < 9 && col >= 0 && col < 18, "element coordinates " + z);
            require(occupied.add(row * 18 + col), "table overlap z=" + z);
            require(FusionPhysics.SUGGESTED_A[z - 1] >= z, "isotope A>=Z " + z);
        }

        FusionPhysics.Analysis dt = FusionPhysics.analyze(1, 2, 1, 3);
        require(dt.totalZ == 2 && dt.totalA == 5, "DT formal charge/mass");
        require("He".equals(dt.combinedSymbol), "formal compound nucleus is He-5");
        require(dt.documentedChannels.size() == 1, "DT one catalog channel");
        near(dt.documentedChannels.get(0).qMeV, 17.589, 0.003, "DT Q");
        require(dt.documentedChannels.get(0).conserved(), "DT Z and A conservation");
        near(dt.documentedChannels.get(0).energyJoules(), 17.589 * 1.602176634e-13,
                1e-15, "DT conversion to joules");

        FusionPhysics.Analysis reverse = FusionPhysics.analyze(1, 3, 1, 2);
        near(reverse.documentedChannels.get(0).qMeV, dt.documentedChannels.get(0).qMeV,
                1e-10, "reactant order symmetry");
        FusionPhysics.Analysis dd = FusionPhysics.analyze(1, 2, 1, 2);
        require(dd.documentedChannels.size() == 2, "DD two known channels");
        near(dd.documentedChannels.get(0).qMeV, 3.269, 0.005, "DD neutron Q");
        near(dd.documentedChannels.get(1).qMeV, 4.033, 0.005, "DD proton Q");
        FusionPhysics.Analysis dHe3 = FusionPhysics.analyze(1, 2, 2, 3);
        near(dHe3.documentedChannels.get(0).qMeV, 18.353, 0.005, "D-He3 Q");
        FusionPhysics.Analysis dLi6 = FusionPhysics.analyze(1, 2, 3, 6);
        near(dLi6.documentedChannels.get(0).qMeV, 22.373, 0.005, "D-Li6 Q");

        FusionPhysics.Analysis auFe = FusionPhysics.analyze(79, 197, 26, 56);
        require(auFe.totalZ == 105 && auFe.totalA == 253, "Au+Fe formal compound");
        require("Db".equals(auFe.combinedSymbol), "Au+Fe Z105 is Db symbol");
        require(!auFe.hasDocumentedChannel(), "Au+Fe must NOT claim a reaction");
        require(auFe.explanation().contains("Q = no disponible"),
                "Unknown channels do not invent Q");
        require(auFe.coulombBarrierMeV > 0, "electrostatic scale positive");
        FusionPhysics.Analysis heavy = FusionPhysics.analyze(118, 294, 118, 294);
        require("Z=236".equals(heavy.combinedSymbol), "Z>118 is unnamed");
        require(!heavy.hasDocumentedChannel(), "unknown heavy channel");

        for (int[] bad : new int[][]{{0,1,1,1},{1,0,1,1},{79,10,1,1},{1,2,119,300},{1,321,1,3}}) {
            boolean rejected = false;
            try { FusionPhysics.analyze(bad[0],bad[1],bad[2],bad[3]); }
            catch (IllegalArgumentException ok) { rejected = true; }
            require(rejected, "Bad isotope must be rejected");
        }
        System.out.println("FUSION_PHYSICS_PASS elements=118 channels=5 layouts=118 unknowns=bounded");
    }
}
