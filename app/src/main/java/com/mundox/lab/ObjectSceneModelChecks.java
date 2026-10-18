package com.mundox.lab;

import java.util.Arrays;

/** Bounded pure-Java checks: runs on a desktop JVM without Android or OpenGL. */
public final class ObjectSceneModelChecks {
    private ObjectSceneModelChecks() { }

    public static final class Result {
        public final int passed;
        public final String summary;

        private Result(int passed) {
            this.passed = passed;
            this.summary = "ObjectSceneModel: " + passed + " checks passed";
        }

        @Override public String toString() { return summary; }
    }

    public static Result runAll() {
        Checks checks = new Checks();
        checkGolden(checks);
        checkDeterminismAndEdits(checks);
        checkSnapshotsAndPersistence(checks);
        checkLimits(checks);
        checkPickingAndBounds(checks);
        return new Result(checks.passed);
    }

    public static void main(String[] args) {
        System.out.println(runAll().summary);
    }

    private static void checkGolden(Checks c) {
        // Fixed unsigned vectors cross-checked with a separate Python uint32 calculation.
        c.that(ObjectSceneModel.hash(0, 0) == 0x01fce552, "hash golden 0");
        c.that(ObjectSceneModel.hash(1, 0) == 0xa4f7896c, "hash golden 1");
        c.that(ObjectSceneModel.hash(42, 123) == 0x865f053f, "hash golden seed");
        c.that(ObjectSceneModel.hash(65535, Integer.MIN_VALUE) == 0x6096bdc9, "hash signed seed");
        ObjectSceneModel model = new ObjectSceneModel(4, 0);
        float[] value = new float[] { 19, 19, 19, 19, 19, 19 };
        model.transform(0, value, 1);
        c.close(value[1], -1.1f, "golden x");
        c.close(value[2], -1.1f, "golden z");
        c.close(value[3], 1.340625f, "golden height");
        c.close(value[4], 0.8980392156862745f, "golden tone");
        c.that(value[0] == 19 && value[5] == 19, "write only requested four floats");
        c.that(model.logicalRuleBytes() == 32L, "unmodified logical record size");
        c.that(model.version() == 0, "initial version");
    }

    private static void checkDeterminismAndEdits(Checks c) {
        ObjectSceneModel a = new ObjectSceneModel(71, -137);
        ObjectSceneModel b = new ObjectSceneModel(71, -137);
        float[] baseline = a.snapshot().explicitTransforms();
        c.that(Arrays.equals(baseline, b.snapshot().explicitTransforms()), "repeatable generation");
        c.that(!Arrays.equals(baseline, new ObjectSceneModel(71, 138).snapshot().explicitTransforms()),
                "seed changes generated appearance");
        c.that(a.edit(5, 3), "new edit reports change");
        float[] changed = a.snapshot().explicitTransforms();
        for (int i = 0; i < baseline.length; i++) {
            if (i == 5 * 4 + 2) c.close(changed[i] - baseline[i], 1.05f, "height edit amount");
            else c.that(changed[i] == baseline[i], "isolated edit component " + i);
        }
        long version = a.version();
        c.that(!a.edit(5, 3) && a.version() == version, "idempotent edit preserves version");
        c.that(a.edit(5, 0), "restoring rule changes scene");
        c.that(Arrays.equals(baseline, a.snapshot().explicitTransforms()), "edit reversal restores scene");
        c.that(a.editCount() == 0 && a.logicalRuleBytes() == 32, "reversal removes sparse entry");
        for (int state = 1; state <= 7; state++) {
            c.that(a.cycleEdit(5) == state, "cycle state " + state);
        }
        float[] hidden = new float[4];
        a.transform(5, hidden, 0);
        c.that(hidden[2] == 0, "hidden object has zero height");
        c.that(hidden[0] == baseline[20] && hidden[1] == baseline[21]
                && hidden[3] == baseline[23], "hidden retains identity and tone");
        c.that(a.cycleEdit(5) == 0, "cycle returns to unmodified rule");
        c.that(Arrays.equals(baseline, a.snapshot().explicitTransforms()), "full edit cycle reversible");
        a.edit(9, 1); a.edit(2, 7); a.edit(20, 6);
        b.edit(20, 6); b.edit(2, 7); b.edit(9, 1);
        c.that(Arrays.equals(a.snapshot().explicitTransforms(), b.snapshot().explicitTransforms()),
                "independent edits commute");
        a.clearEdits();
        c.that(Arrays.equals(baseline, a.snapshot().explicitTransforms()), "clear restores rule");
        version = a.version();
        a.clearEdits();
        c.that(a.version() == version, "empty clear is idempotent");
    }

    private static void checkSnapshotsAndPersistence(Checks c) {
        ObjectSceneModel model = new ObjectSceneModel(37, Integer.MIN_VALUE);
        for (int state = 1; state <= 7; state++) model.edit(35 - state, state);
        ObjectSceneModel.Snapshot saved = model.snapshot();
        float[] expected = saved.explicitTransforms();
        String exported = saved.exportText();
        int[] ids = saved.editIds();
        int[] states = saved.editStates();
        c.that(saved.editCount() == 7, "snapshot edit count");
        for (int i = 1; i < ids.length; i++) c.that(ids[i - 1] < ids[i], "canonical ID order");
        ids[0] = 0; states[0] = 0;
        c.that(saved.editedId(0) == 28 && saved.editedState(0) == 7, "array getters cannot alter snapshot");
        model.clearEdits(); model.edit(0, 2);
        c.that(Arrays.equals(saved.explicitTransforms(), expected), "snapshot isolated from later edits");
        c.that(saved.exportText().equals(exported), "snapshot text remains immutable");
        ObjectSceneModel restored = ObjectSceneModel.importText(exported);
        c.that(restored.exportText().equals(exported), "canonical roundtrip is lossless");
        c.that(restored.count() == 37 && restored.seed() == Integer.MIN_VALUE
                && restored.version() == saved.version, "roundtrip metadata");
        c.that(Arrays.equals(restored.snapshot().explicitTransforms(), expected), "roundtrip visible scene");
        c.that(restored.logicalRuleBytes() == 88, "logical rule includes seven sparse edits");
        restored.edit(0, 4);
        c.that(saved.editState(0) == 0, "imported scene has independent edits");
        final String[] invalid = new String[] {
            "bad\ncount=1\nseed=0\nversion=0\nedits=0\n",
            "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=0\nedits=2\n0:1\n0:2\n",
            "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=0\nedits=2\n1:1\n0:2\n",
            "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=0\nedits=1\n0:0\n",
            "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=0\nedits=1\n2:1\n",
            "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=-1\nedits=0\n",
            "MUNDOX_OBJECTS_V1\ncount=1048577\nseed=0\nversion=0\nedits=0\n",
            "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=0\nedits=4097\n",
            "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=0\nedits=1\n",
            "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=0\nedits=0\nextra\n",
            "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=0\nedits=1\n0:1:2\n"
        };
        for (int i = 0; i < invalid.length; i++) {
            final String bad = invalid[i];
            c.reject(new Runnable() { public void run() { ObjectSceneModel.importText(bad); } },
                    IllegalArgumentException.class, "reject malformed import " + i);
        }
        c.reject(new Runnable() { public void run() {
            ObjectSceneModel.importText(new String(new char[ObjectSceneModel.MAX_EXPORT_CHARS + 1]));
        } }, IllegalArgumentException.class, "bound import allocation");
        c.reject(new Runnable() { public void run() { ObjectSceneModel.importText(null); } },
                IllegalArgumentException.class, "reject null import");
        c.that(saved.exportText().equals(exported), "failed imports do not mutate saved scene");
    }

    private static void checkLimits(Checks c) {
        final ObjectSceneModel full = new ObjectSceneModel(ObjectSceneModel.MAX_COUNT, 72);
        for (int id = 0; id < ObjectSceneModel.MAX_EDITS; id++) full.edit(id, 1 + id % 7);
        c.that(full.editCount() == ObjectSceneModel.MAX_EDITS, "edit limit is reachable");
        final String before = full.exportText();
        final long version = full.version();
        c.reject(new Runnable() { public void run() { full.edit(ObjectSceneModel.MAX_EDITS, 1); } },
                IllegalStateException.class, "reject overflow without eviction");
        c.that(full.version() == version && full.exportText().equals(before), "overflow is atomic");
        c.that(full.edit(0, 6) && full.editCount() == ObjectSceneModel.MAX_EDITS,
                "existing edit remains editable at limit");
        full.edit(0, 0); full.edit(ObjectSceneModel.MAX_EDITS, 2);
        c.that(full.editCount() == ObjectSceneModel.MAX_EDITS && full.editState(0) == 0
                && full.editState(ObjectSceneModel.MAX_EDITS) == 2, "freed slot can be reused");
        c.that(ObjectSceneModel.importText(full.exportText()).exportText().equals(full.exportText()),
                "maximum edit set roundtrips");
        c.that(full.logicalRuleBytes() == 32800L, "bounded logical record size");
        c.that(full.snapshot().explicitTransforms().length == ObjectSceneModel.MAX_COUNT * 4,
                "explicit reference has one transform per object");
        String atLimit = "MUNDOX_OBJECTS_V1\ncount=1\nseed=0\nversion=9223372036854775807\nedits=0\n";
        final ObjectSceneModel exhausted = ObjectSceneModel.importText(atLimit);
        c.reject(new Runnable() { public void run() { exhausted.edit(0, 1); } },
                IllegalStateException.class, "version overflow does not wrap");
        c.that(exhausted.editCount() == 0 && exhausted.version() == Long.MAX_VALUE,
                "version overflow preserves model");
    }

    private static void checkPickingAndBounds(Checks c) {
        final ObjectSceneModel model = new ObjectSceneModel(37, 9);
        c.that(model.side() == 7, "partial final row grid");
        c.that(new ObjectSceneModel(1, 9).side() == 1
                && new ObjectSceneModel(ObjectSceneModel.MAX_COUNT, 9).side() == 1024,
                "count boundary grid sizes");
        float[] value = new float[4];
        for (int id = 0; id < model.count(); id++) {
            model.transform(id, value, 0);
            c.that(model.pickGrid(value[0], value[1]) == id, "pick center " + id);
            c.that(model.pickGrid(value[0] + 0.5f, value[1] - 0.5f) == id, "pick cell interior " + id);
        }
        c.that(model.pickGrid(0, 6.6f) == -1, "reject unpopulated last-row cell");
        c.that(model.pickGrid(-1000, 0) == -1 && model.pickGrid(1000, 0) == -1,
                "reject outside world");
        c.that(model.pickGrid(Float.NaN, 0) == -1
                && model.pickGrid(0, Float.POSITIVE_INFINITY) == -1, "reject nonfinite pick");
        model.edit(3, 7); model.transform(3, value, 0);
        c.that(model.pickGrid(value[0], value[1]) == 3, "hidden cell can be selected to restore it");
        c.reject(new Runnable() { public void run() { new ObjectSceneModel(0, 0); } },
                IllegalArgumentException.class, "reject zero objects");
        c.reject(new Runnable() { public void run() { new ObjectSceneModel(-1, 0); } },
                IllegalArgumentException.class, "reject negative objects");
        c.reject(new Runnable() { public void run() { new ObjectSceneModel(ObjectSceneModel.MAX_COUNT + 1, 0); } },
                IllegalArgumentException.class, "reject excessive objects");
        final String before = model.exportText();
        c.reject(new Runnable() { public void run() { model.edit(-1, 1); } },
                IllegalArgumentException.class, "reject negative ID");
        c.reject(new Runnable() { public void run() { model.edit(37, 1); } },
                IllegalArgumentException.class, "reject ID at count");
        c.reject(new Runnable() { public void run() { model.edit(0, -1); } },
                IllegalArgumentException.class, "reject negative state");
        c.reject(new Runnable() { public void run() { model.edit(0, 8); } },
                IllegalArgumentException.class, "reject state outside cycle");
        c.that(model.exportText().equals(before), "invalid edits have no partial effect");
        c.reject(new Runnable() { public void run() { model.transform(0, null, 0); } },
                IllegalArgumentException.class, "reject missing output");
        c.reject(new Runnable() { public void run() { model.transform(0, new float[3], 0); } },
                IllegalArgumentException.class, "reject short output");
        c.reject(new Runnable() { public void run() { model.transform(0, new float[4], -1); } },
                IllegalArgumentException.class, "reject negative output offset");
        c.reject(new Runnable() { public void run() { model.transform(0, new float[4], Integer.MAX_VALUE); } },
                IllegalArgumentException.class, "reject overflowing output offset");
        c.reject(new Runnable() { public void run() { model.transform(37, new float[4], 0); } },
                IllegalArgumentException.class, "reject transform ID outside scene");
    }

    private static final class Checks {
        int passed;

        void that(boolean condition, String label) {
            if (!condition) throw new IllegalStateException("FAILED: " + label);
            passed++;
        }

        void close(float actual, float expected, String label) {
            that(Math.abs(actual - expected) <= 0.000001f, label + " (" + actual + " != " + expected + ")");
        }

        void reject(Runnable operation, Class<? extends RuntimeException> type, String label) {
            try { operation.run(); }
            catch (RuntimeException error) {
                that(type.isInstance(error), label + ": wrong exception " + error.getClass().getName());
                return;
            }
            throw new IllegalStateException("FAILED: " + label + ": accepted invalid input");
        }
    }
}
