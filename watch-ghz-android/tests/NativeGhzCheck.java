package com.mundox.ghzwatch;

public final class NativeGhzCheck {
    private static void has(String json, String field) {
        if (!json.contains(field)) throw new AssertionError("Missing " + field + " in " + json);
    }
    public static void main(String[] args) {
        has(NativeGhz.command(3, 0, 0), "\"active_qubits\":0");
        has(NativeGhz.command(0, 1000, 42), "\"x_all\":1");
        has(NativeGhz.command(1, 0, 0), "\"yx_all\":1");
        has(NativeGhz.command(2, 0, 0), "\"all_equal_verified\":true");
        has(NativeGhz.command(4, 0, 0), "\"collapsed\":true");
        String previous = NativeGhz.command(4, 0, 0);
        for (int[] bad : new int[][]{{0, -1}, {0, 1001}, {99, 3}, {1, 3}}) {
            boolean failed = false;
            try { NativeGhz.command(bad[0], bad[1], 0); }
            catch (IllegalStateException expected) { failed = true; }
            if (!failed || !previous.equals(NativeGhz.command(4, 0, 0)))
                throw new AssertionError("JNI error / transaction failure");
        }
        has(NativeGhz.command(3, 0, 0), "\"tableau_bytes\":0");
        System.out.println("PASS: real JNI, 1000-qubit state, phase, collapse, error mapping and reset");
    }
}
