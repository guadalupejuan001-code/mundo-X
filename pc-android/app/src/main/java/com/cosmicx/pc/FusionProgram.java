package com.cosmicx.pc;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Fusion Studio's bounded, inspectable graph of operations.
 *
 * A connection is an execution dependency: all XI operations target the same
 * persistent QuantumRuntime session. XASM operations target one fresh circuit.
 * The studio does not pretend that XASM and XI have a shared quantum state.
 *
 * This class has no Android or native dependencies, so generation can be
 * checked with ordinary Java tests before it reaches the native parsers.
 */
public final class FusionProgram {
    public enum Mode { XI, XASM_COMPACT, XASM_REDUCED }
    public enum Op {
        H, X, Z, S, CX, CZ, T,
        ETHER_H_ALL, ETHER_CZ_NEIGHBORS,
        MEM_STORE, MEM_BIND, MEM_READ,
        PROBZ, EXPECT_ZZ, STATS
    }

    public static final class Node {
        public final Op op;
        public final int a;
        public final int b;
        public Node(Op op, int a, int b) {
            if (op == null) throw new IllegalArgumentException("Missing operation");
            this.op = op;
            this.a = a;
            this.b = b;
        }
        public String label() {
            switch (op) {
                case ETHER_H_ALL: return "ETHER  ·  H ALL";
                case ETHER_CZ_NEIGHBORS: return "ETHER  ·  CZ NEIGHBORS";
                case MEM_STORE: return "MEM[" + a + "] ← " + b;
                case MEM_BIND: return "MEM[" + a + "] ↔ q" + b + "..q" + (b + 7);
                case MEM_READ: return "READ MEM[" + a + "]";
                case EXPECT_ZZ: return "OBSERVE ZZ(q" + a + ", q" + b + ")";
                case STATS: return "INSPECT STATE";
                case PROBZ: return "OBSERVE P(Z=1) q" + a;
                case CX: case CZ: return op.name() + " q" + a + " → q" + b;
                default: return op.name() + " q" + a;
            }
        }
    }

    public static final int MAX_NODES = 128;
    private Mode mode;
    private int qubits = 16;
    private final ArrayList<Node> nodes = new ArrayList<>();

    public FusionProgram(Mode mode) { setMode(mode); }
    public Mode mode() { return mode; }
    public int qubits() { return qubits; }
    public int size() { return nodes.size(); }
    public List<Node> nodes() { return Collections.unmodifiableList(nodes); }

    public void setMode(Mode newMode) {
        if (newMode == null) throw new IllegalArgumentException("Missing mode");
        mode = newMode;
        nodes.clear();
    }

    public void setQubits(int count) {
        if (count < 1 || count > 1000)
            throw new IllegalArgumentException("Los circuitos permiten 1 a 1000 qubits");
        for (Node node : nodes) check(node, count);
        qubits = count;
    }

    public void add(Op op, int a, int b) {
        if (nodes.size() >= MAX_NODES)
            throw new IllegalStateException("Fusion Studio admite hasta 128 bloques por proyecto");
        Node node = new Node(op, a, b);
        check(node, qubits);
        nodes.add(node);
    }

    public void replace(int index, Op op, int a, int b) {
        Node node = new Node(op, a, b);
        check(node, qubits);
        nodes.set(index, node);
    }

    public void remove(int index) {
        nodes.remove(index);
    }

    public void move(int index, int delta) {
        int target = index + delta;
        if (index < 0 || index >= nodes.size() || target < 0 || target >= nodes.size())
            return;
        Collections.swap(nodes, index, target);
    }

    public void clear() { nodes.clear(); }

    public static Op[] available(Mode mode) {
        if (mode == Mode.XI) return new Op[]{
                Op.H, Op.X, Op.Z, Op.S, Op.CX, Op.CZ,
                Op.ETHER_H_ALL, Op.ETHER_CZ_NEIGHBORS,
                Op.MEM_STORE, Op.MEM_BIND, Op.MEM_READ,
                Op.PROBZ, Op.EXPECT_ZZ, Op.STATS
        };
        if (mode == Mode.XASM_REDUCED)
            return new Op[]{Op.H, Op.X, Op.Z, Op.S, Op.CX, Op.CZ, Op.T};
        return new Op[]{Op.H, Op.X, Op.Z, Op.S, Op.CX, Op.CZ, Op.PROBZ};
    }

    public static boolean hasSecond(Op op) {
        switch(op) {
            case CX: case CZ: case MEM_STORE: case MEM_BIND: case EXPECT_ZZ: return true;
            default: return false;
        }
    }

    public static String fieldA(Op op) {
        if (op == Op.MEM_STORE || op == Op.MEM_BIND || op == Op.MEM_READ) return "Dirección lógica";
        return "Qubit A";
    }

    public static String fieldB(Op op) {
        if (op == Op.MEM_STORE) return "Valor byte (0 a 255)";
        if (op == Op.MEM_BIND) return "Qubit base para 8 bits";
        return "Qubit B";
    }

    private void check(Node n, int q) {
        boolean supported = false;
        for (Op allowed : available(mode)) if (allowed == n.op) supported = true;
        if (!supported)
            throw new IllegalArgumentException(n.op + " no se admite en modo " + mode);
        switch (n.op) {
            case ETHER_H_ALL: case ETHER_CZ_NEIGHBORS: case STATS:
                return;
            case MEM_STORE:
                if (n.a < 0 || n.b < 0 || n.b > 255)
                    throw new IllegalArgumentException("Memoria: dirección >=0 y byte de 0 a 255");
                return;
            case MEM_READ:
                if (n.a < 0) throw new IllegalArgumentException("Dirección de memoria inválida");
                return;
            case MEM_BIND:
                if (n.a < 0 || n.b < 0 || n.b > 999 - 7)
                    throw new IllegalArgumentException("Memoria: qbase necesita 8 direcciones entre q0 y q999");
                return;
            default:
                int width = mode == Mode.XI ? 1000 : q;
                if (n.a < 0 || n.a >= width)
                    throw new IllegalArgumentException("Qubit A fuera del dominio del compositor");
                if (hasSecond(n.op)) {
                    if (n.b < 0 || n.b >= width)
                        throw new IllegalArgumentException("Qubit B fuera del dominio del compositor");
                    if ((n.op == Op.CX || n.op == Op.CZ) && n.a == n.b)
                        throw new IllegalArgumentException("Una puerta de dos qubits requiere dos direcciones distintas");
                }
        }
    }

    public String source() {
        StringBuilder out = new StringBuilder();
        out.append("# MUNDO X / FUSION STUDIO\n");
        if (mode == Mode.XI) {
            out.append("# Un estado: Ether + InfiniteCore + Inverse Ether Memory\n");
            out.append("# Sesión XI persistente; no contiene comandos del sistema operativo.\n");
        } else {
            out.append("# Circuito XASM independiente de la sesión XI\n")
                    .append("backend ")
                    .append(mode == Mode.XASM_REDUCED ? "reduced" : "compact")
                    .append("\nqubits ").append(qubits)
                    .append("\nseed 42\n");
        }
        for (Node n : nodes) {
            switch (n.op) {
                case ETHER_H_ALL: out.append("h_all\n"); break;
                case ETHER_CZ_NEIGHBORS: out.append("cz_neighbors\n"); break;
                case MEM_STORE: out.append("mem_store ").append(n.a).append(' ').append(n.b).append('\n'); break;
                case MEM_BIND: out.append("mem_bind ").append(n.a).append(' ').append(n.b).append('\n'); break;
                case MEM_READ: out.append("mem_read ").append(n.a).append('\n'); break;
                case EXPECT_ZZ: out.append("expect Z:").append(n.a).append(" Z:").append(n.b).append('\n'); break;
                case STATS: out.append("stats\n"); break;
                case PROBZ: out.append("probz ").append(n.a).append('\n'); break;
                case CX: case CZ: out.append(n.op == Op.CX ? "cx " : "cz ")
                        .append(n.a).append(' ').append(n.b).append('\n'); break;
                default: out.append(n.op.name().toLowerCase(java.util.Locale.ROOT))
                        .append(' ').append(n.a).append('\n'); break;
            }
        }
        if (mode == Mode.XI) out.append("stats\nmem_stats\n");
        else out.append("show\nbytes\n");
        return out.toString();
    }
}
