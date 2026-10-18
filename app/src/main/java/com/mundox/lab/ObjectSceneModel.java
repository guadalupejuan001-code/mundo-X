package com.mundox.lab;

import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;

/**
 * A finite object scene represented by a generation rule and sparse edits.
 * Coordinates are world units; the rule does not model collision or physics.
 * The model never retains a transform array proportional to object count.
 */
public final class ObjectSceneModel {
    public static final int MAX_COUNT = 1048576;
    public static final int MAX_EDITS = 4096;
    public static final int STRIDE_FLOATS = 4;
    public static final float GRID_SPACING = 2.2f;
    public static final int MAX_EXPORT_CHARS = 65536;
    private static final String HEADER = "MUNDOX_OBJECTS_V1";

    private final int count;
    private final int seed;
    private final int side;
    private final TreeMap<Integer, Integer> edits = new TreeMap<Integer, Integer>();
    private long version;

    public ObjectSceneModel(int count, int seed) {
        if (count < 1 || count > MAX_COUNT) {
            throw new IllegalArgumentException("Object count must be 1.." + MAX_COUNT);
        }
        this.count = count;
        this.seed = seed;
        this.side = (int) Math.ceil(Math.sqrt(count));
    }

    public int count() { return count; }
    public int seed() { return seed; }
    public int side() { return side; }
    public synchronized long version() { return version; }
    public synchronized int editCount() { return edits.size(); }

    /** Logical format size, excluding Java containers, driver memory and meshes. */
    public synchronized long logicalRuleBytes() {
        return 32L + 8L * edits.size();
    }

    public synchronized int editState(int id) {
        checkId(id, count);
        Integer value = edits.get(id);
        return value == null ? 0 : value.intValue();
    }

    /** State 0 restores the rule; 1..6 raise an object; 7 hides it. */
    public synchronized boolean edit(int id, int state) {
        checkId(id, count);
        checkState(state);
        Integer old = edits.get(id);
        int previous = old == null ? 0 : old.intValue();
        if (previous == state) return false;
        if (state != 0 && old == null && edits.size() >= MAX_EDITS) {
            throw new IllegalStateException("Sparse edit limit reached: " + MAX_EDITS);
        }
        if (version == Long.MAX_VALUE) {
            throw new IllegalStateException("Scene version exhausted");
        }
        if (state == 0) edits.remove(id);
        else edits.put(id, state);
        version++;
        return true;
    }

    public synchronized int cycleEdit(int id) {
        int next = (editState(id) + 1) & 7;
        edit(id, next);
        return next;
    }

    public synchronized void clearEdits() {
        if (edits.isEmpty()) return;
        if (version == Long.MAX_VALUE) {
            throw new IllegalStateException("Scene version exhausted");
        }
        edits.clear();
        version++;
    }

    /** Writes x, z, height and tone into out[offset..offset+3]. */
    public synchronized void transform(int id, float[] out, int offset) {
        Integer state = edits.get(id);
        writeTransform(count, seed, side, id, state == null ? 0 : state.intValue(), out, offset);
    }

    /** Returns a cell ID, including hidden objects, or -1 outside the finite grid. */
    public int pickGrid(float x, float z) {
        if (Float.isNaN(x) || Float.isInfinite(x)
                || Float.isNaN(z) || Float.isInfinite(z)) return -1;
        double center = (side - 1) * 0.5;
        double colValue = Math.floor(x / (double) GRID_SPACING + center + 0.5);
        double rowValue = Math.floor(z / (double) GRID_SPACING + center + 0.5);
        if (colValue < 0 || colValue >= side || rowValue < 0 || rowValue >= side) return -1;
        int id = (int) rowValue * side + (int) colValue;
        return id < count ? id : -1;
    }

    public synchronized Snapshot snapshot() {
        int[] ids = new int[edits.size()];
        int[] states = new int[edits.size()];
        int index = 0;
        for (Map.Entry<Integer, Integer> entry : edits.entrySet()) {
            ids[index] = entry.getKey().intValue();
            states[index] = entry.getValue().intValue();
            index++;
        }
        return new Snapshot(count, seed, side, version, ids, states);
    }

    public String exportText() { return snapshot().exportText(); }

    /**
     * Imports a bounded, versioned text format. Parsing finishes before a scene
     * is returned; malformed files cannot partly modify an existing model.
     */
    public static ObjectSceneModel importText(String text) {
        if (text == null || text.length() > MAX_EXPORT_CHARS) {
            throw new IllegalArgumentException("Invalid scene text size");
        }
        String[] lines = text.split("\n", -1);
        int used = lines.length;
        if (used > 0 && lines[used - 1].length() == 0) used--;
        if (used < 5 || !HEADER.equals(lines[0])) {
            throw new IllegalArgumentException("Unsupported scene format");
        }
        int count = readInt(lines[1], "count=");
        int seed = readInt(lines[2], "seed=");
        long version = readLong(lines[3], "version=");
        int size = readInt(lines[4], "edits=");
        if (version < 0 || size < 0 || size > MAX_EDITS || used != 5 + size) {
            throw new IllegalArgumentException("Invalid scene metadata");
        }
        ObjectSceneModel result = new ObjectSceneModel(count, seed);
        int previous = -1;
        for (int i = 0; i < size; i++) {
            String line = lines[5 + i];
            int colon = line.indexOf(':');
            if (colon < 1 || colon != line.lastIndexOf(':') || colon == line.length() - 1) {
                throw new IllegalArgumentException("Invalid edit at line " + (6 + i));
            }
            int id = parseInt(line.substring(0, colon));
            int state = parseInt(line.substring(colon + 1));
            checkId(id, count);
            if (id <= previous || state < 1 || state > 7) {
                throw new IllegalArgumentException("Edits must be sorted, unique and in states 1..7");
            }
            result.edits.put(id, state);
            previous = id;
        }
        result.version = version;
        return result;
    }

    /** Unsigned 32-bit mixing; Java overflow matches the GLSL uint operations. */
    public static int hash(int id, int seed) {
        int value = id ^ seed ^ 0x9e3779b9;
        value ^= value >>> 16;
        value *= 0x7feb352d;
        value ^= value >>> 15;
        value *= 0x846ca68b;
        value ^= value >>> 16;
        return value;
    }

    public static String hashGLSL() {
        return "uint mxHash(uint id, uint seed) {\n"
                + "  uint x = id ^ seed ^ 0x9e3779b9u;\n"
                + "  x ^= x >> 16u; x *= 0x7feb352du;\n"
                + "  x ^= x >> 15u; x *= 0x846ca68bu;\n"
                + "  x ^= x >> 16u; return x;\n"
                + "}\n";
    }

    private static void writeTransform(int count, int seed, int side, int id, int state,
            float[] out, int offset) {
        checkId(id, count);
        if (out == null || offset < 0 || offset > out.length - STRIDE_FLOATS) {
            throw new IllegalArgumentException("Transform output requires four floats");
        }
        int mixed = hash(id, seed);
        float center = (side - 1) * 0.5f;
        out[offset] = ((id % side) - center) * GRID_SPACING;
        out[offset + 1] = ((id / side) - center) * GRID_SPACING;
        float height = 0.7f + (mixed & 255) / 128.0f;
        out[offset + 2] = state == 7 ? 0.0f : height + state * 0.35f;
        out[offset + 3] = ((mixed >>> 8) & 255) / 255.0f;
    }

    private static void checkId(int id, int count) {
        if (id < 0 || id >= count) throw new IllegalArgumentException("Object ID outside scene");
    }

    private static void checkState(int state) {
        if (state < 0 || state > 7) throw new IllegalArgumentException("Edit state must be 0..7");
    }

    private static int readInt(String line, String prefix) {
        if (!line.startsWith(prefix)) throw new IllegalArgumentException("Missing " + prefix);
        return parseInt(line.substring(prefix.length()));
    }

    private static long readLong(String line, String prefix) {
        if (!line.startsWith(prefix)) throw new IllegalArgumentException("Missing " + prefix);
        String number = line.substring(prefix.length());
        requireDecimal(number);
        try { return Long.parseLong(number); }
        catch (NumberFormatException error) {
            throw new IllegalArgumentException("Invalid integer", error);
        }
    }

    private static int parseInt(String number) {
        requireDecimal(number);
        try { return Integer.parseInt(number); }
        catch (NumberFormatException error) {
            throw new IllegalArgumentException("Invalid integer", error);
        }
    }

    private static void requireDecimal(String number) {
        int begin = number.startsWith("-") ? 1 : 0;
        if (begin == number.length()) throw new IllegalArgumentException("Missing integer");
        for (int i = begin; i < number.length(); i++) {
            char c = number.charAt(i);
            if (c < '0' || c > '9') throw new IllegalArgumentException("Invalid integer");
        }
    }

    /** Immutable scene value. Array accessors return copies rather than aliases. */
    public static final class Snapshot {
        public final int count;
        public final int seed;
        public final int side;
        public final long version;
        private final int[] editIds;
        private final int[] editStates;

        private Snapshot(int count, int seed, int side, long version, int[] editIds, int[] editStates) {
            this.count = count;
            this.seed = seed;
            this.side = side;
            this.version = version;
            this.editIds = editIds;
            this.editStates = editStates;
        }

        public int editCount() { return editIds.length; }
        public int editedId(int index) { return editIds[index]; }
        public int editedState(int index) { return editStates[index]; }
        public int[] editIds() { return editIds.clone(); }
        public int[] editStates() { return editStates.clone(); }
        public long logicalRuleBytes() { return 32L + 8L * editIds.length; }

        public int editState(int id) {
            checkId(id, count);
            int index = Arrays.binarySearch(editIds, id);
            return index < 0 ? 0 : editStates[index];
        }

        public void transform(int id, float[] out, int offset) {
            writeTransform(count, seed, side, id, editState(id), out, offset);
        }

        /** Reference path: materializes the same finite scene as interleaved floats. */
        public float[] explicitTransforms() {
            float[] result = new float[count * STRIDE_FLOATS];
            for (int id = 0; id < count; id++) transform(id, result, id * STRIDE_FLOATS);
            return result;
        }

        public String exportText() {
            StringBuilder text = new StringBuilder(96 + editIds.length * 8);
            text.append(HEADER).append('\n');
            text.append("count=").append(count).append('\n');
            text.append("seed=").append(seed).append('\n');
            text.append("version=").append(version).append('\n');
            text.append("edits=").append(editIds.length).append('\n');
            for (int i = 0; i < editIds.length; i++) {
                text.append(editIds[i]).append(':').append(editStates[i]).append('\n');
            }
            return text.toString();
        }
    }
}
