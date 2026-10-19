package com.mundox.graphics;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;
import org.json.JSONArray;
import org.json.JSONObject;

/** Device tests use the shipping JNI core, shaders, GL thread and activity lifecycle. */
public final class GraphicsInstrumentation extends Instrumentation {
    private static final String TAG = "MundoXGraphicsTests";
    private static final int EXPECTED_TESTS = 18;
    private static final long FRAME_TIMEOUT_MS = 45_000;
    private MainActivity activity;
    private File evidence;
    private final List<Result> results = new ArrayList<>();
    private final JSONObject metrics = new JSONObject();
    private final JSONArray images = new JSONArray();
    private final JSONArray projectExports = new JSONArray();
    private String portableName;

    private interface Checked { void run() throws Exception; }
    private static final class Result {
        final String name;
        final double seconds;
        final Throwable error;
        Result(String name, double seconds, Throwable error) {
            this.name = name;
            this.seconds = seconds;
            this.error = error;
        }
    }
    private static final class Pick {
        final int id, x, y, area;
        Pick(int id, int x, int y, int area) {
            this.id = id;
            this.x = x;
            this.y = y;
            this.area = area;
        }
    }
    private static final class LiveProject {
        final CosmicProject.Document document;
        final String[] generators;
        final float[] expectations;
        final long stateBytes;
        LiveProject(CosmicProject.Document document, String[] generators, float[] expectations, long stateBytes) {
            this.document = document;
            this.generators = generators;
            this.expectations = expectations;
            this.stateBytes = stateBytes;
        }
    }

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        start();
    }

    @Override public void onStart() {
        try {
            evidence = new File(getTargetContext().getFilesDir(), "graphics-evidence");
            expect(evidence.isDirectory() || evidence.mkdirs(), "Cannot create evidence directory");
            metrics.put("androidSdk", Build.VERSION.SDK_INT);
            metrics.put("androidRelease", Build.VERSION.RELEASE);
            metrics.put("abis", new JSONArray(Arrays.asList(Build.SUPPORTED_ABIS)));
            metrics.put("performanceScope", "Software GLES emulator; not phone performance measurements");

            Intent launch = new Intent(getTargetContext(), MainActivity.class);
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            activity = (MainActivity) startActivitySync(launch);
            waitUntil(() -> activity.resumed, 15_000, "Activity did not resume");

            runCase("default_1000_qubit_world", this::defaultThousand);
            runCase("native_bell_invariants", this::nativeBell);
            runCase("native_1000_qubit_invariants", this::nativeThousand);
            runCase("renderer_borrows_native_state", this::borrowedState);
            runCase("gles_one_million_objects", () -> largeScene(1_000_000, "million"));
            runCase("gles_maximum_object_limit", () -> largeScene(1_048_576, "maximum"));
            runCase("render_observes_without_mutating", this::observationIsReadOnly);
            runCase("gate_changes_coupled_image", this::coupledGate);
            runCase("gate_preserves_uncoupled_image", this::uncoupledGate);
            runCase("selection_and_individual_edits", this::selectionAndEdits);
            runCase("edit_restoration", this::editRestoration);
            runCase("pause_resume_preserves_state", this::pauseResume);

            // Record the active 1000-qubit profile before project migration tests
            // deliberately replace it with legacy and pedagogical projects.
            metrics.put("glRenderer", activity.renderer.device);
            metrics.put("qubits", activity.core.qubits);
            metrics.put("nativeStateBytes", activity.core.stateBytes());
            metrics.put("baselineMetricsScope", "1000-qubit default, before portable project replacement");

            runCase("portable_codec_strict_validation", this::portableCodec);
            runCase("portable_1000_and_legacy_expansion", this::portableThousand);
            runCase("portable_desktop_fixtures_import", this::portableFixtures);
            runCase("portable_mutation_export_reimport", this::portableRoundtrip);
            runCase("portable_invalid_import_preserves_session", this::portableInvalidImport);
            runCase("portable_activity_recreation", this::portableRecreation);

            metrics.put("portableFinalQubits", activity.core.qubits);
            metrics.put("portableFinalStateBytes", activity.core.stateBytes());
            saveScreen("activity.png");
        } catch (Throwable error) {
            results.add(new Result("instrumentation_setup_or_reporting", 0, error));
            Log.e(TAG, "Instrumentation setup/reporting failed", error);
        }

        int failures = failures();
        Bundle finish = new Bundle();
        try {
            writeReports();
        } catch (Throwable error) {
            failures++;
            finish.putString("report_error", trace(error));
            Log.e(TAG, "Cannot save reports", error);
        }
        finish.putInt("tests_run", results.size());
        finish.putInt("tests_failed", failures);
        boolean passed = failures == 0 && results.size() == EXPECTED_TESTS;
        finish.putString("stream", "\nMUNDOX_GRAPHICS_TESTS_" + (passed ? "PASSED" : "FAILED")
                + " tests=" + results.size() + " failures=" + failures + "\n");
        finish(passed ? Activity.RESULT_OK : Activity.RESULT_CANCELED, finish);
    }

    private void runCase(String name, Checked test) {
        Bundle status = new Bundle();
        status.putString("class", getClass().getName());
        status.putString("test", name);
        status.putInt("numtests", EXPECTED_TESTS);
        status.putInt("current", results.size() + 1);
        sendStatus(1, status);
        long start = SystemClock.elapsedRealtime();
        Throwable failure = null;
        try {
            test.run();
        } catch (Throwable error) {
            failure = error;
            status.putString("stack", trace(error));
            Log.e(TAG, name, error);
            try { saveScreen("failure-" + name + ".png"); } catch (Throwable ignored) { }
        }
        results.add(new Result(name, (SystemClock.elapsedRealtime() - start) / 1000.0, failure));
        sendStatus(failure == null ? 0 : -2, status);
    }

    private void defaultThousand() throws Exception {
        expect(NativeCore.DEFAULT_QUBITS == 1000 && NativeCore.MAX_QUBITS == 1000,
                "The shipping profile and project limit must both be 1000");
        expect(activity.core.qubits == 1000, "A fresh app did not start a 1000-qubit core");
        frame(false);
        QuantumRenderer.FrameStats stats = activity.renderer.frameStats();
        expect(stats != null && stats.qubits == 1000 && stats.observables == 3000,
                "The live renderer did not receive the complete 1000-qubit visual program");
        metrics.put("defaultQubits", activity.core.qubits);
        metrics.put("defaultObservables", stats.observables);
    }

    private static String pauli(int qubits, char axis, int... positions) {
        char[] out = new char[qubits]; Arrays.fill(out, 'I');
        for (int q : positions) out[q] = axis;
        return new String(out);
    }

    private static String uniformPauli(int qubits, char axis) {
        char[] out = new char[qubits]; Arrays.fill(out, axis); return new String(out);
    }

    private void nativeThousand() throws Exception {
        int[] high = {32, 53, 59, 64, 511, 999};
        try (NativeCore core = new NativeCore(1000)) {
            for (int q : high) {
                String x = pauli(1000, 'X', q), y = pauli(1000, 'Y', q), z = pauli(1000, 'Z', q);
                expect(Arrays.equals(core.query(new String[]{x, y, z}), new float[]{0, 0, 1}), "Fresh high qubit is not |0>: " + q);
                core.apply(NativeCore.H, q, 0);
                expect(core.query(new String[]{x})[0] == 1, "H lost high qubit " + q);
                core.apply(NativeCore.S, q, 0);
                expect(core.query(new String[]{y})[0] == 1, "S phase lost high qubit " + q);
                core.apply(NativeCore.Z, q, 0);
                expect(core.query(new String[]{y})[0] == -1, "Z phase lost high qubit " + q);
                core.apply(NativeCore.X, q, 0);
                core.apply(NativeCore.Y, q, 0);
                expect(core.query(new String[]{y})[0] == 1, "X/Y action lost high qubit " + q);
            }
        }
        try (NativeCore core = new NativeCore(1000)) {
            core.apply(NativeCore.H, 0, 0);
            for (int q = 1; q < 1000; q++) core.apply(NativeCore.CX, q - 1, q);
            String allX = uniformPauli(1000, 'X'), allZ = uniformPauli(1000, 'Z');
            String[] operators = {allX, allZ, pauli(1000, 'Z', 0, 999), pauli(1000, 'Z', 31, 32),
                    pauli(1000, 'Z', 63, 64), pauli(1000, 'Z', 53), pauli(1000, 'Z', 511), pauli(1000, 'Z', 999)};
            float[] ghz = {1, 1, 1, 1, 1, 0, 0, 0};
            expect(Arrays.equals(core.query(operators), ghz), "GHZ1000 global correlations or local mixed states are wrong");
            core.apply(NativeCore.Z, 999, 0);
            expect(core.query(new String[]{allX})[0] == -1, "Z999 did not reverse GHZ1000 phase");
            core.apply(NativeCore.Z, 999, 0);
            core.apply(NativeCore.CZ, 32, 999);
            expect(core.query(new String[]{allX})[0] == -1, "CZ32,999 did not reverse GHZ1000 phase");
            core.apply(NativeCore.CZ, 32, 999);
            core.apply(NativeCore.X, 53, 0);
            expect(Arrays.equals(core.query(new String[]{allX, allZ, pauli(1000, 'Z', 32, 53), pauli(1000, 'Z', 53, 999)}),
                    new float[]{1, -1, -1, -1}), "X53 aliased a different bit of GHZ1000");
            NativeCore.Snapshot before = core.snapshot();
            String[] generators = core.generators(); float[] values = core.query(operators);
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (ObjectOutputStream output = new ObjectOutputStream(bytes)) { output.writeObject(before); }
            NativeCore.Snapshot saved;
            try (ObjectInputStream input = new ObjectInputStream(new ByteArrayInputStream(bytes.toByteArray()))) {
                saved = (NativeCore.Snapshot) input.readObject();
            }
            try (NativeCore restored = new NativeCore(saved)) {
                sameCore(before, restored.snapshot(), "1000-qubit snapshot changed history or revision");
                expect(Arrays.equals(generators, restored.generators()) && Arrays.equals(values, restored.query(operators)),
                        "1000-qubit snapshot replay changed the native state");
            }
            JSONArray bits = new JSONArray(), expected = new JSONArray();
            for (int q : high) bits.put(q); for (float value : ghz) expected.put((double)value);
            metrics.put("native1000", new JSONObject().put("qubits", core.qubits).put("highQubits", bits)
                    .put("ghzExpectations", expected).put("snapshotRoundtrip", true)
                    .put("stateBytes", core.stateBytes()).put("gateCount", before.gateHistory.length / 3));
        }
        boolean rejected = false;
        try (NativeCore invalid = new NativeCore(1001)) { }
        catch (IllegalArgumentException expected) { rejected = true; }
        expect(rejected, "The declared 1000-qubit project limit was not enforced");
    }

    private void nativeBell() {
        try (NativeCore core = new NativeCore(2)) {
            core.apply(0, 0, 0); // H(0)
            core.apply(4, 0, 1); // CX(0,1)
            String[] operators = {"XX", "ZZ", "ZI", "IZ"};
            expect(Arrays.equals(core.query(operators), new float[]{1, 1, 0, 0}),
                    "Bell correlations or local expectations are wrong");
            long version = core.version();
            core.apply(3, 0, 0); // Z(0) reverses XX, retaining ZZ.
            expect(Arrays.equals(core.query(operators), new float[]{-1, 1, 0, 0}),
                    "Native Z gate did not reverse Bell phase correlation");
            expect(core.version() == version + 1, "Gate version must advance exactly once");
            expect(core.stateBytes() > 0, "Native state must report allocated bytes");
        }
    }

    private void borrowedState() throws Exception {
        try (NativeCore core = new NativeCore(5)) {
            core.apply(0, 0, 0);
            core.apply(4, 0, 1);
            core.apply(3, 1, 0);
            String[] operators = {"XXIII", "ZZIII", "ZIIII", "IIIIZ"};
            float[] before = core.query(operators);
            String[] generators = core.generators();
            long version = core.version(), bytes = core.stateBytes();
            new QuantumRenderer(getTargetContext(), core, text -> { });
            expect(version == core.version(), "Renderer constructor changed the borrowed core version");
            expect(bytes == core.stateBytes(), "Renderer constructor replaced the native state");
            expect(Arrays.equals(before, core.query(operators)), "Renderer constructor changed the state");
            expect(Arrays.equals(generators, core.generators()), "Renderer constructor replaced generators");
            ByteArrayOutputStream bytesOut = new ByteArrayOutputStream();
            try (ObjectOutputStream output = new ObjectOutputStream(bytesOut)) {
                output.writeObject(core.snapshot());
            }
            NativeCore.Snapshot restoredSnapshot;
            try (ObjectInputStream input = new ObjectInputStream(new ByteArrayInputStream(bytesOut.toByteArray()))) {
                restoredSnapshot = (NativeCore.Snapshot) input.readObject();
            }
            try (NativeCore restored = new NativeCore(restoredSnapshot)) {
                expect(restored.version() == version, "Serialized native snapshot lost gate version");
                expect(Arrays.equals(before, restored.query(operators)), "Native snapshot replay changed observations");
                expect(Arrays.equals(generators, restored.generators()), "Native snapshot replay changed generators");
            }
            core.apply(3, 1, 0);
            expect(core.version() == version + 1, "Borrowed core is not usable by its owner");
        }
    }

    private void largeScene(int count, String prefix) throws Exception {
        gl(() -> {
            activity.renderer.reset();
            activity.renderer.objectCount(count);
            activity.renderer.resolution(320);
            activity.renderer.setTime(0);
            activity.renderer.coupling(true);
            activity.renderer.testCamera(new float[]{0, 6000, 0}, new float[]{0, 0, 0});
        });
        QuantumRenderer.Capture color = frame(false);
        QuantumRenderer.Capture ids = frame(true);
        int hits = 0, minId = count, maxId = -1;
        Map<Integer, Integer> distinct = new HashMap<>();
        for (int y = 0; y < ids.height; y++) for (int x = 0; x < ids.width; x++) {
            int id = ids.idAt(x, y);
            if (id < 0) continue;
            expect(id < count, "Rendered ID exceeds requested scene size: " + id);
            hits++;
            minId = Math.min(minId, id);
            maxId = Math.max(maxId, id);
            distinct.put(id, 1);
        }
        expect(color.nonBackground() > 100, "Real GLES output is empty or almost empty");
        expect(hits > 100 && distinct.size() > 100, "ID pass does not contain a substantial scene");
        expect(minId < count / 10 && maxId > count * 9 / 10,
                "ID pass does not reach both ends of the full object population");
        expect(color.changed(frame(false)) == 0, "A frozen scene must produce identical pixels");
        save(prefix + "-color.png", color);
        save(prefix + "-ids.png", ids);
        metrics.put(prefix + "ObjectCount", count);
        metrics.put(prefix + "VisiblePixels", hits);
        metrics.put(prefix + "DistinctVisibleIds", distinct.size());
        metrics.put(prefix + "MinVisibleId", minId);
        metrics.put(prefix + "MaxVisibleId", maxId);
        metrics.put(prefix + "Width", color.width);
        metrics.put(prefix + "Height", color.height);
    }

    private void observationIsReadOnly() throws Exception {
        scene(64);
        String[] program = activity.core.visualProgram();
        String[] generators = activity.core.generators();
        float[] before = activity.core.query(program);
        long version = activity.core.version(), bytes = activity.core.stateBytes();
        frame(false);
        frame(true);
        frame(false);
        expect(activity.core.version() == version, "Drawing changed the native gate version");
        expect(activity.core.stateBytes() == bytes, "Drawing replaced native state storage");
        expect(Arrays.equals(generators, activity.core.generators()), "Drawing changed native generators");
        expect(Arrays.equals(before, activity.core.query(program)), "Drawing changed native observables");
    }

    private void coupledGate() throws Exception {
        scene(1);
        gl(() -> activity.renderer.coupling(true));
        String observable = activity.core.generators()[0];
        long version = activity.core.version();
        expect(activity.core.query(new String[]{observable})[0] == 1, "Prepared generator is not stabilized");
        QuantumRenderer.Capture before = frame(false);
        gl(() -> activity.renderer.gate(3, 0, 0));
        expect(activity.core.version() == version + 1, "Renderer gate did not reach the borrowed core");
        expect(activity.core.query(new String[]{observable})[0] == -1, "Z(0) did not reverse the control observable");
        QuantumRenderer.Capture after = frame(false);
        int changed = before.changed(after);
        expect(changed > 20, "Native gate has no visible effect with quantum coupling enabled");
        gl(() -> activity.renderer.gate(3, 0, 0));
        expect(before.changed(frame(false)) == 0, "Applying Z twice did not restore the exact image");
        save("coupling-on-before.png", before);
        save("coupling-on-after.png", after);
        metrics.put("coupledChangedPixels", changed);
    }

    private void uncoupledGate() throws Exception {
        scene(1);
        String observable = activity.core.generators()[0];
        long version = activity.core.version();
        QuantumRenderer.Capture before = frame(false);
        gl(() -> activity.renderer.gate(3, 0, 0));
        expect(activity.core.version() == version + 1, "Disabled visual coupling must still permit native gates");
        expect(activity.core.query(new String[]{observable})[0] == -1, "Native state did not change in control run");
        QuantumRenderer.Capture after = frame(false);
        int changed = before.changed(after);
        expect(changed == 0, "Native gate changed pixels while visual coupling was disabled");
        save("coupling-off-before.png", before);
        save("coupling-off-after.png", after);
        metrics.put("uncoupledChangedPixels", changed);
    }

    private void selectionAndEdits() throws Exception {
        scene(64);
        QuantumRenderer.Capture before = frame(false), ids = frame(true);
        Pick pick = choose(ids);
        long version = activity.core.version();
        select(pick, ids);
        QuantumRenderer.Capture selected = frame(false);
        expect(before.changed(selected) > 10, "Picking did not visibly highlight the requested object");
        expect(changedOutside(before, selected, ids, pick.id) == 0,
                "Selection modified pixels belonging to other objects");
        gl(() -> activity.renderer.editSelected(1));
        QuantumRenderer.Capture taller = frame(false), tallerIds = frame(true);
        expect(selected.changed(taller) > 10, "Individual height edit has no visible effect");
        expect(ids.changed(tallerIds) > 0, "Individual height edit did not change visible geometry");
        gl(() -> activity.renderer.editSelected(7));
        QuantumRenderer.Capture removed = frame(false), removedIds = frame(true);
        expect(taller.changed(removed) > 10, "Removing the selected object did not change the image");
        expect(area(removedIds, pick.id) == 0, "Removed object is still present in the ID pass");
        expect(activity.core.version() == version, "Visual selection/edit unexpectedly applied a native gate");
        save("selection-before.png", before);
        save("selection-highlight.png", selected);
        save("selection-taller.png", taller);
        save("selection-removed.png", removed);
        metrics.put("selectedId", pick.id);
        metrics.put("selectedOriginalPixels", pick.area);
        metrics.put("removedIdPixels", area(removedIds, pick.id));
    }

    private void editRestoration() throws Exception {
        scene(64);
        QuantumRenderer.Capture ids = frame(true);
        Pick pick = choose(ids);
        select(pick, ids);
        QuantumRenderer.Capture before = frame(false);
        gl(() -> activity.renderer.editSelected(2));
        expect(before.changed(frame(false)) > 10, "Test edit did not change the selected object");
        gl(() -> activity.renderer.editSelected(0));
        QuantumRenderer.Capture restored = frame(false);
        expect(before.changed(restored) == 0, "Restoring an individual edit did not recover exact scene pixels");
        save("edit-restored.png", restored);
    }

    private void pauseResume() throws Exception {
        scene(64);
        QuantumRenderer.Capture ids = frame(true);
        select(choose(ids), ids);
        gl(() -> {
            activity.renderer.editSelected(2);
            activity.renderer.coupling(true);
            activity.renderer.gate(3, 0, 0);
            activity.renderer.setTime(1.25f);
        });
        QuantumRenderer.Capture before = frame(false);
        NativeCore original = activity.core;
        long version = original.version();
        String[] generators = original.generators(), program = original.visualProgram();
        float[] observations = original.query(program);
        AtomicReference<Boolean> moved = new AtomicReference<>(false);
        runOnMainSync(() -> moved.set(activity.moveTaskToBack(true)));
        expect(Boolean.TRUE.equals(moved.get()), "Could not move activity to the background");
        try {
            waitUntil(() -> !activity.resumed, 15_000, "Real onPause callback was not observed");
            waitForIdleSync();
        } finally {
            Intent resume = new Intent(getTargetContext(), MainActivity.class);
            resume.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                    | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            getTargetContext().startActivity(resume);
        }
        waitUntil(() -> activity.resumed, 15_000, "Real onResume callback was not observed");
        waitForIdleSync();
        QuantumRenderer.Capture after = frame(false);
        expect(activity.core == original, "Pause/resume replaced the authoritative core object");
        expect(original.version() == version, "Pause/resume applied or lost native gates");
        expect(Arrays.equals(generators, original.generators()), "Pause/resume lost native state");
        expect(Arrays.equals(observations, original.query(program)), "Pause/resume changed observables");
        expect(before.changed(after) == 0, "Pause/resume lost camera, time, selection, edits or coupling");
        save("lifecycle-before.png", before);
        save("lifecycle-after.png", after);
        metrics.put("pauseResumeChangedPixels", before.changed(after));
        metrics.put("pauseResumeVersion", version);
    }

    private void portableCodec() throws Exception {
        NativeCore owner = activity.core;
        NativeCore.Snapshot before = owner.snapshot();
        JSONObject result = ProjectCodecChecks.run(getTargetContext());
        expect(result.getBoolean("passed") && result.getInt("checks") >= 99
                && result.getInt("rejectedInvalidDocuments") >= 83, "Strict codec checks incomplete");
        expect(!result.getBoolean("nativeOrGlCreated"), "Value codec must not create a rendering session");
        sameCore(before, owner.snapshot(), "Codec affected active native session");
        expect(activity.core == owner, "Codec replaced active native owner");
        metrics.put("projectCodec", result);
    }

    private CosmicProject.Document fixture(String name) throws Exception {
        try (InputStream input = getTargetContext().getAssets().open("projects/" + name)) {
            return CosmicProject.read(input);
        }
    }

    private void install(CosmicProject.Document project) throws Exception {
        AtomicReference<Throwable> error = new AtomicReference<>();
        runOnMainSync(() -> {
            try { activity.installProjectForTest(project); }
            catch (Throwable caught) { error.set(caught); }
        });
        if (error.get() instanceof Exception) throw (Exception) error.get();
        if (error.get() != null) throw new AssertionError("Project installation", error.get());
        portableName = project.name;
        waitForIdleSync();
        expect(activity.resumed, "Project installation paused the app");
        expectProjectHeading(project.name);
    }

    private LiveProject liveProject() throws Exception {
        AtomicReference<LiveProject> value = new AtomicReference<>();
        AtomicReference<Throwable> error = new AtomicReference<>();
        gl(() -> {
            try {
                NativeCore.Snapshot core = activity.core.snapshot();
                QuantumRenderer.Snapshot visual = activity.renderer.snapshot();
                CosmicProject.Document document = CosmicProject.fromSnapshots(portableName, core, visual);
                value.set(new LiveProject(document, activity.core.generators(),
                        activity.core.query(visual.observables), activity.core.stateBytes()));
            } catch (Throwable caught) { error.set(caught); }
        });
        if (error.get() != null) throw new AssertionError("Cannot export live project", error.get());
        expect(value.get() != null, "Missing live project export");
        return value.get();
    }

    private void portableThousand() throws Exception {
        CosmicProject.Document legacy20 = null;
        JSONArray sizes = new JSONArray();
        for (int oldSize : new int[]{8, 20, 60}) {
            try (NativeCore original = new NativeCore(oldSize)) {
                original.apply(NativeCore.H, oldSize - 1, 0);
                original.apply(NativeCore.CX, oldSize - 1, 0);
                original.apply(NativeCore.Z, oldSize - 1, 0);
                QuantumRenderer visual = new QuantumRenderer(getTargetContext(), original, text -> { });
                visual.objectCount(64); visual.resolution(320); visual.worldSeed(4_000_000_001L);
                CosmicProject.Document legacy = CosmicProject.fromSnapshots("Android · legado " + oldSize, original.snapshot(), visual.snapshot());
                legacy = CosmicProject.decode(CosmicProject.encode(legacy));
                expect(legacy.core.qubits == oldSize, "Import silently expanded a legacy project");
                byte[] before = CosmicProject.encode(legacy);
                CosmicProject.Document expanded = CosmicProject.expandQubits(legacy, 1000);
                CosmicProject.Document back = CosmicProject.decode(CosmicProject.encode(expanded));
                sameCore(expanded.core, back.core, "Portable expansion lost native history");
                sameGraphics(expanded.graphics, back.graphics, "Portable expansion lost graphics");
                expect(Arrays.equals(before, CosmicProject.encode(legacy)), "Expansion mutated its original project");
                try (NativeCore larger = new NativeCore(back.core)) {
                    expect(larger.qubits == 1000 && larger.version() == original.version(), "Expansion replaced the source revision");
                    expect(Arrays.equals(original.query(legacy.graphics.observables), larger.query(back.graphics.observables)),
                            "Expansion changed legacy quantum observations");
                    assertAddedZero(larger.generators(), oldSize);
                }
                if (oldSize == 20) {
                    JSONObject json = new JSONObject(new String(before, StandardCharsets.UTF_8));
                    json.getJSONObject("graphics").put("coupling", -.125);
                    legacy20 = CosmicProject.decode(json.toString().getBytes(StandardCharsets.UTF_8));
                }
                sizes.put(oldSize);
            }
        }
        expect(legacy20 != null, "Missing explicit 20-qubit migration fixture");
        install(legacy20);
        gl(() -> {
            activity.renderer.coupling(false); activity.renderer.orbit(11, -4); activity.renderer.setTime(1.25f);
            activity.renderer.testCamera(new float[]{13, 15, 19}, new float[]{0, 1, 0});
        });
        QuantumRenderer.Capture ids = frame(true); Pick selected = choose(ids); select(selected, ids);
        gl(() -> activity.renderer.editSelected(3));
        LiveProject before = liveProject();
        AtomicReference<QuantumRenderer.Snapshot> raw = new AtomicReference<>();
        gl(() -> raw.set(activity.renderer.snapshot()));
        QuantumRenderer.Snapshot beforeVisual = raw.get();
        QuantumRenderer.Capture image = frame(false);
        NativeCore previous = activity.core;
        runOnMainSync(() -> {
            View button = activity.getWindow().getDecorView().findViewWithTag("project_expand_qubits");
            expect(button != null && button.performClick(), "Missing production expansion action for the legacy project");
            // Deliver an already-resolved gate callback while candidate preparation is pending.
            // The UI commit cannot run until this main-thread callback returns.
            View gate = findText(activity.getWindow().getDecorView(), "H");
            expect(gate != null && gate.performClick(), "Missing gate control during expansion race check");
            CountDownLatch checked = new CountDownLatch(1);
            AtomicReference<Long> revision = new AtomicReference<>();
            activity.surface.queueEvent(() -> { revision.set(previous.version()); checked.countDown(); });
            try { expect(checked.await(10, TimeUnit.SECONDS), "Pending expansion gate check timed out"); }
            catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); throw new AssertionError(interrupted); }
            expect(revision.get() == before.document.core.version, "A gate callback mutated the source while expansion was pending");
        });
        waitUntil(() -> activity.core != previous && activity.core.qubits == 1000 && activity.renderer.hasFrame,
                FRAME_TIMEOUT_MS, "Production expansion did not install a 1000-qubit project");
        LiveProject after = liveProject();
        expect(after.document.core.version == before.document.core.version
                && Arrays.equals(after.document.core.gateHistory, before.document.core.gateHistory)
                && Arrays.equals(after.expectations, before.expectations), "Production expansion reset the legacy quantum state");
        CosmicProject.Document expected = CosmicProject.expandQubits(before.document, 1000);
        sameCore(expected.core, after.document.core, "Production expansion has the wrong native project");
        sameGraphics(expected.graphics, after.document.graphics, "Production expansion changed portable visual state");
        assertAddedZero(after.generators, 20);
        gl(() -> raw.set(activity.renderer.snapshot()));
        expect(raw.get().enabledQuantumStrength == beforeVisual.enabledQuantumStrength
                && raw.get().enabledQuantumStrength == -.125f && raw.get().yaw == beforeVisual.yaw
                && raw.get().pitch == beforeVisual.pitch && raw.get().zoom == beforeVisual.zoom
                && raw.get().overview == beforeVisual.overview, "Production expansion lost hidden session preferences");
        QuantumRenderer.Capture expandedImage = frame(false);
        expect(image.changed(expandedImage) == 0, "Production expansion changed the frozen framebuffer");
        runOnMainSync(() -> expect(activity.getWindow().getDecorView().findViewWithTag("project_expand_qubits") == null,
                "Expansion action remains enabled at the maximum profile"));
        install(CosmicProject.decode(CosmicProject.encode(after.document)));
        sameLive(after, liveProject(), "1000-qubit export/reimport changed the live portable project");
        expect(expandedImage.changed(frame(false)) == 0, "1000-qubit reimport changed the frozen framebuffer");
        metrics.put("legacyExpansionSourceQubits", sizes);
        metrics.put("expandedProjectQubits", activity.core.qubits);
        metrics.put("legacyExpansionChangedPixels", image.changed(expandedImage));
        metrics.put("legacyExpansionPreservedState", true);
        metrics.put("legacyExpansionAddedQubitsZero", true);
        metrics.put("legacyExpansionHiddenStatePreserved", true);
        metrics.put("legacyExpansionBlockedConcurrentGate", true);
        metrics.put("expandedProjectRoundtrip", true);
    }

    private static void assertAddedZero(String[] generators, int oldSize) {
        expect(generators.length == 1000, "Expanded core did not expose 1000 generators");
        for (int q = oldSize; q < generators.length; q++)
            expect(generators[q].equals("+" + pauli(generators.length, 'Z', q)),
                    "An added qubit was entangled or was not initialized to |0>: " + q);
    }

    private void portableFixtures() throws Exception {
        JSONArray imported = new JSONArray();
        for (String file : new String[]{"bell-observer.cosmic", "ghz-city.cosmic"}) {
            CosmicProject.Document source = fixture(file);
            install(source);
            LiveProject actual = liveProject();
            sameCore(source.core, actual.document.core, "Desktop fixture core/history changed on import");
            sameGraphics(source.graphics, actual.document.graphics, "Desktop fixture world/camera changed on import");
            try (NativeCore reference = new NativeCore(source.core)) {
                expect(Arrays.equals(reference.generators(), actual.generators), "Fixture native generators changed");
                expect(Arrays.equals(reference.query(source.graphics.observables), actual.expectations),
                        "Fixture observables are not driven by its imported core");
            }
            QuantumRenderer.Capture color = frame(false), ids = frame(true);
            expect(color.nonBackground() > 100, "Imported fixture produced no substantial GLES image");
            expectValidIds(ids, source.graphics.objectCount);
            sameLive(actual, liveProject(), "Rendering imported fixture changed its project");
            String prefix = file.startsWith("bell") ? "portable-bell" : "portable-ghz";
            save(prefix + "-color.png", color);
            save(prefix + "-ids.png", ids);
            exportEvidence(prefix + "-android.cosmic", file, true, actual);
            if (file.startsWith("ghz")) {
                expect(actual.document.graphics.seed == 4_000_000_001L,
                        "Unsigned 32-bit world seed was truncated by Android");
                expect(actual.document.graphics.quantumStrength == -.125f,
                        "Imported negative coupling was replaced by a default");
                expect(actual.document.graphics.observables.length == 11,
                        "Imported operators were replaced with a default 3*n program");
            }
            imported.put(new JSONObject().put("fixture", file).put("qubits", actual.document.core.qubits)
                    .put("objects", actual.document.graphics.objectCount).put("seed", actual.document.graphics.seed)
                    .put("coupling", actual.document.graphics.quantumStrength)
                    .put("observables", actual.document.graphics.observables.length)
                    .put("cameraMode", actual.document.graphics.overrideEye == null ? "orbit" : "lookAt")
                    .put("visiblePixels", color.nonBackground()).put("passed", true));
        }
        metrics.put("portableFixtures", imported);
    }

    private void portableRoundtrip() throws Exception {
        CosmicProject.Document source = fixture("bell-observer.cosmic");
        install(source);
        gl(() -> activity.renderer.resolution(320));
        Pick chosen = choose(frame(true));
        QuantumRenderer.Capture selectionIds = frame(true);
        select(chosen, selectionIds);
        LiveProject before = liveProject();
        QuantumRenderer.Capture unedited = frame(false);
        gl(() -> activity.renderer.editSelected(7));
        QuantumRenderer.Capture edited = frame(false);
        expect(unedited.changed(edited) > 10, "Portable roundtrip test did not visibly edit an object");
        expect(area(frame(true), chosen.id) == 0, "Portable edited ID remained visible");
        sameCore(before.document.core, activity.core.snapshot(), "Visual edit changed the native core");

        gl(() -> activity.renderer.gate(NativeCore.Y, 0, 0));
        LiveProject afterGate = liveProject();
        expect(afterGate.document.core.version == before.document.core.version + 1,
                "Portable Y gate did not advance native revision");
        expect(afterGate.document.core.gateHistory.length == before.document.core.gateHistory.length + 3,
                "Portable gate was not appended to authoritative native history");
        gl(() -> {
            activity.renderer.orbit(17, -6);
            activity.renderer.zoom(1.1f);
            activity.renderer.setTime(1.75f);
        });
        portableName = "Android · Bell editado";
        LiveProject exported = liveProject();
        sameCore(afterGate.document.core, exported.document.core, "Camera/time applied unexpected gates");
        expect(Arrays.equals(source.graphics.observables, exported.document.graphics.observables),
                "Original visual operators were regenerated after native mutation");
        expect(exported.document.graphics.overrideEye == null, "Camera gesture did not produce an orbital camera");
        QuantumRenderer.Capture first = frame(false);
        QuantumRenderer.Capture firstIds = frame(true);
        byte[] bytes = CosmicProject.encode(exported.document);
        CosmicProject.Document decoded = CosmicProject.decode(bytes);
        sameCore(exported.document.core, decoded.core, "Encoded project lost native history");
        sameGraphics(exported.document.graphics, decoded.graphics, "Encoded project lost graphics state");
        install(decoded);
        LiveProject imported = liveProject();
        sameLive(exported, imported, "Live portable roundtrip changed state or graphics");
        QuantumRenderer.Capture second = frame(false), secondIds = frame(true);
        expect(first.changed(second) == 0 && firstIds.changed(secondIds) == 0,
                "Reimport changed camera, edits, time, observables or the GLES image");
        save("portable-roundtrip-before.png", first);
        save("portable-roundtrip-after.png", second);
        exportEvidence("portable-mutated-android.cosmic", "bell-observer.cosmic", false, imported);
        metrics.put("portableRoundtripChangedPixels", first.changed(second));
        metrics.put("portableRoundtripIdDifferences", firstIds.changed(secondIds));
        metrics.put("portableEditedId", chosen.id);
        metrics.put("portableEditChangedPixels", unedited.changed(edited));
        metrics.put("portableCameraPreservedCore", true);
    }

    private void portableInvalidImport() throws Exception {
        LiveProject before = liveProject();
        NativeCore owner = activity.core;
        QuantumRenderer observer = activity.renderer;
        QuantumRenderer.Capture image = frame(false);
        int rejected = 0;
        JSONObject invalidJson = new JSONObject(new String(CosmicProject.encode(before.document), StandardCharsets.UTF_8));
        invalidJson.put("version", 2);
        try { CosmicProject.decode(invalidJson.toString().getBytes(StandardCharsets.UTF_8)); }
        catch (IllegalArgumentException expected) { rejected++; }

        // Document snapshots are detached arrays. The shipping install path
        // must still reject a damaged candidate before replacing the owner.
        CosmicProject.Document invalidGates = fixture("bell-observer.cosmic");
        invalidGates.core.gateHistory[0] = 99;
        try { install(invalidGates); }
        catch (IllegalArgumentException | IllegalStateException expected) { rejected++; }
        CosmicProject.Document invalidOperators = fixture("bell-observer.cosmic");
        invalidOperators.graphics.observables[0] = "BAD";
        try { install(invalidOperators); }
        catch (IllegalArgumentException | IllegalStateException expected) { rejected++; }
        expect(rejected == 3, "Invalid JSON or native/observer candidate was accepted");

        int refusedRevisionOperations = 0;
        NativeCore.Snapshot maximum = NativeCore.Snapshot.of(before.document.core.qubits,
                9_007_199_254_740_991L, before.document.core.gateHistory);
        try (NativeCore limited = new NativeCore(maximum)) {
            try { limited.apply(NativeCore.H, 0, 0); }
            catch (IllegalStateException expected) { refusedRevisionOperations++; }
            sameCore(maximum, limited.snapshot(), "Rejected gate changed maximum-revision state");
            try { limited.resetGraph(); }
            catch (IllegalStateException expected) { refusedRevisionOperations++; }
            sameCore(maximum, limited.snapshot(), "Rejected reset changed maximum-revision state");
        }
        expect(refusedRevisionOperations == 2, "Maximum portable revision accepted another mutation");
        int[] refusedTimeOperations = {0};
        gl(() -> {
            try { activity.renderer.setTime(1_000_001f); }
            catch (IllegalArgumentException expected) { refusedTimeOperations[0]++; }
            try { activity.renderer.setTime(-1_000_001f); }
            catch (IllegalArgumentException expected) { refusedTimeOperations[0]++; }
        });
        expect(refusedTimeOperations[0] == 2, "Renderer accepted time outside the portable range");
        expect(activity.core == owner && activity.renderer == observer, "Invalid import replaced the active session");
        sameLive(before, liveProject(), "Invalid import mutated the active project");
        QuantumRenderer.Capture after = frame(false);
        expect(image.changed(after) == 0, "Invalid import changed framebuffer output");
        metrics.put("portableRejectedActiveImports", rejected);
        metrics.put("portableRejectedRevisionOperations", refusedRevisionOperations);
        metrics.put("portableRejectedTimeOperations", refusedTimeOperations[0]);
        metrics.put("portableInvalidImportChangedPixels", image.changed(after));
    }

    private void portableRecreation() throws Exception {
        install(fixture("ghz-city.cosmic"));
        gl(() -> {
            activity.renderer.resolution(320);
            activity.renderer.coupling(false);
            activity.renderer.gate(NativeCore.Y, 2, 0);
            activity.renderer.orbit(3, -1);
            activity.renderer.setTime(1.25f);
        });
        LiveProject before = liveProject();
        QuantumRenderer.Capture image = frame(false);
        MainActivity original = activity;
        NativeCore oldOwner = original.core;
        ActivityMonitor monitor = addMonitor(MainActivity.class.getName(), null, false);
        try {
            runOnMainSync(original::recreate);
            Activity next = waitForMonitorWithTimeout(monitor, 25_000);
            expect(next instanceof MainActivity && next != original, "Activity.recreate did not create a new instance");
            activity = (MainActivity) next;
            waitUntil(() -> activity.resumed, 15_000, "Recreated activity did not resume");
            waitForIdleSync();
        } finally { removeMonitor(monitor); }
        expect(activity.core != oldOwner, "Activity recreation did not reconstruct its owning native session");
        expectProjectHeading(portableName);
        LiveProject after = liveProject();
        sameLive(before, after, "Activity recreation lost the imported project");
        QuantumRenderer.Capture recreated = frame(false);
        expect(image.changed(recreated) == 0, "Activity recreation changed the saved GLES scene");
        save("portable-recreate-before.png", image);
        save("portable-recreate-after.png", recreated);
        metrics.put("portableRecreateChangedPixels", image.changed(recreated));
        metrics.put("portableRecreateNewOwner", true);
        gl(() -> activity.renderer.coupling(true));
        LiveProject reenabled = liveProject();
        sameCore(after.document.core, reenabled.document.core, "Reenabling visual coupling changed the core");
        expect(reenabled.document.graphics.quantumStrength == -.125f,
                "Recreation forgot the disabled custom coupling strength");
        metrics.put("portableRecreateRestoredCoupling", reenabled.document.graphics.quantumStrength);
    }

    private static void sameCore(NativeCore.Snapshot expected, NativeCore.Snapshot actual, String reason) {
        expect(expected.qubits == actual.qubits && expected.version == actual.version
                && Arrays.equals(expected.gateHistory, actual.gateHistory), reason);
    }

    private static void sameGraphics(QuantumRenderer.Snapshot expected, QuantumRenderer.Snapshot actual, String reason) {
        boolean equal = expected.objectCount == actual.objectCount && expected.seed == actual.seed
                && expected.selectedId == actual.selectedId && expected.resolutionLimit == actual.resolutionLimit
                && expected.time == actual.time && expected.quantumStrength == actual.quantumStrength
                && expected.coupling == actual.coupling && Arrays.equals(expected.observables, actual.observables)
                && Arrays.equals(expected.editIds, actual.editIds) && Arrays.equals(expected.editValues, actual.editValues);
        if (expected.overrideEye == null) {
            equal &= actual.overrideEye == null && actual.overrideTarget == null && expected.overview == actual.overview
                    && expected.yaw == actual.yaw && expected.pitch == actual.pitch && expected.zoom == actual.zoom;
        } else {
            equal &= Arrays.equals(expected.overrideEye, actual.overrideEye) && Arrays.equals(expected.overrideTarget, actual.overrideTarget)
                    && expected.cameraNear == actual.cameraNear && expected.cameraFar == actual.cameraFar
                    && expected.cameraFov == actual.cameraFov;
        }
        expect(equal, reason);
    }

    private static void sameLive(LiveProject expected, LiveProject actual, String reason) {
        sameCore(expected.document.core, actual.document.core, reason + " (core)");
        sameGraphics(expected.document.graphics, actual.document.graphics, reason + " (graphics)");
        expect(expected.document.name.equals(actual.document.name) && Arrays.equals(expected.generators, actual.generators)
                && Arrays.equals(expected.expectations, actual.expectations), reason + " (native observations)");
    }

    private void exportEvidence(String file, String source, boolean sourceEquivalent, LiveProject state) throws Exception {
        try (FileOutputStream output = new FileOutputStream(new File(evidence, file))) {
            output.write(CosmicProject.encode(state.document));
        }
        JSONArray history = new JSONArray(), values = new JSONArray();
        for (long value : state.document.core.gateHistory) history.put(value);
        for (float value : state.expectations) values.put((double) value);
        String proof = file + ".state.json";
        JSONObject nativeState = new JSONObject().put("schemaVersion", 1).put("projectFile", file)
                .put("qubits", state.document.core.qubits).put("revision", state.document.core.version)
                .put("nativeGateHistory", history).put("nativeStateBytes", state.stateBytes)
                .put("generators", new JSONArray(Arrays.asList(state.generators)))
                .put("observables", new JSONArray(Arrays.asList(state.document.graphics.observables)))
                .put("expectations", values);
        write(proof, nativeState.toString(2) + "\n");
        projectExports.put(new JSONObject().put("file", file).put("stateFile", proof)
                .put("sourceFixture", source).put("sourceEquivalent", sourceEquivalent));
    }

    private static void expectValidIds(QuantumRenderer.Capture ids, int count) {
        int visible = 0;
        for (int y = 0; y < ids.height; y++) for (int x = 0; x < ids.width; x++) {
            int id = ids.idAt(x, y);
            expect(id >= -1 && id < count, "Imported scene returned an invalid object ID");
            if (id >= 0) visible++;
        }
        expect(visible > 100, "Imported scene ID framebuffer is empty");
    }

    private void expectProjectHeading(String name) {
        AtomicReference<Boolean> found = new AtomicReference<>(false);
        runOnMainSync(() -> found.set(containsText(activity.getWindow().getDecorView(), name)));
        expect(Boolean.TRUE.equals(found.get()), "Project name missing from the actual activity heading");
    }

    private static boolean containsText(View view, String text) {
        return findText(view, text) != null;
    }

    private static View findText(View view, String text) {
        if (view instanceof TextView && text.contentEquals(((TextView) view).getText())) return view;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) {
                View found = findText(group.getChildAt(i), text); if (found != null) return found;
            }
        }
        return null;
    }

    private void scene(int count) throws Exception {
        gl(() -> {
            activity.renderer.reset();
            activity.renderer.objectCount(count);
            activity.renderer.resolution(320);
            activity.renderer.coupling(false);
            activity.renderer.setTime(0);
            activity.renderer.testCamera(count == 1 ? new float[]{5, 4, 7} : new float[]{13, 15, 19},
                    new float[]{0, 1, 0});
        });
    }

    private void gl(Runnable action) throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        activity.surface.queueEvent(() -> {
            try { action.run(); } catch (Throwable caught) { error.set(caught); }
            finally { done.countDown(); }
        });
        activity.surface.requestRender();
        await(done, error, "GL command");
    }

    private QuantumRenderer.Capture frame(boolean ids) throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        AtomicReference<QuantumRenderer.Capture> result = new AtomicReference<>();
        activity.surface.queueEvent(() -> {
            try {
                activity.renderer.requestCapture(ids, image -> { result.set(image); done.countDown(); });
            } catch (Throwable caught) {
                error.set(caught);
                done.countDown();
            }
        });
        activity.surface.requestRender();
        await(done, error, "GLES framebuffer capture");
        expect(result.get() != null, "Renderer returned no framebuffer");
        return result.get();
    }

    private void await(CountDownLatch done, AtomicReference<Throwable> error, String operation) throws Exception {
        long deadline = SystemClock.elapsedRealtime() + FRAME_TIMEOUT_MS;
        while (!done.await(50, TimeUnit.MILLISECONDS)) {
            expect(activity.renderer.failure == null, "Renderer failed: " + activity.renderer.failure);
            expect(SystemClock.elapsedRealtime() < deadline, operation + " timed out");
        }
        if (error.get() != null) throw new AssertionError(operation, error.get());
        expect(activity.renderer.failure == null, "Renderer failed: " + activity.renderer.failure);
    }

    private static void waitUntil(BooleanSupplier condition, long timeout, String message) {
        long deadline = SystemClock.elapsedRealtime() + timeout;
        while (!condition.getAsBoolean() && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(50);
        expect(condition.getAsBoolean(), message);
    }

    private Pick choose(QuantumRenderer.Capture ids) {
        Map<Integer, Integer> counts = new HashMap<>();
        for (int y = 0; y < ids.height; y++) for (int x = 0; x < ids.width; x++) {
            int id = ids.idAt(x, y);
            if (id >= 0) counts.put(id, counts.getOrDefault(id, 0) + 1);
        }
        int winner = -1, pixels = 0;
        for (Map.Entry<Integer, Integer> item : counts.entrySet()) if (item.getValue() > pixels) {
            winner = item.getKey();
            pixels = item.getValue();
        }
        expect(winner >= 0 && pixels > 20, "No sufficiently visible object can be selected");
        for (int y = 1; y < ids.height - 1; y++) for (int x = 1; x < ids.width - 1; x++) {
            if (ids.idAt(x, y) == winner && ids.idAt(x - 1, y) == winner
                    && ids.idAt(x + 1, y) == winner && ids.idAt(x, y - 1) == winner
                    && ids.idAt(x, y + 1) == winner) return new Pick(winner, x, y, pixels);
        }
        throw new AssertionError("No interior pixel found for selection");
    }

    private void select(Pick pick, QuantumRenderer.Capture ids) throws Exception {
        int[] dimensions = new int[2];
        runOnMainSync(() -> { dimensions[0] = activity.surface.getWidth(); dimensions[1] = activity.surface.getHeight(); });
        int x = (int) ((pick.x + .5f) * dimensions[0] / ids.width);
        int y = (int) ((pick.y + .5f) * dimensions[1] / ids.height);
        gl(() -> activity.renderer.pick(x, y));
        frame(false); // Picking resolves in onDrawFrame, after its queued request.
        expect(activity.renderer.selectedId() == pick.id, "ID picking selected a different object");
    }

    private static int area(QuantumRenderer.Capture ids, int target) {
        int count = 0;
        for (int y = 0; y < ids.height; y++) for (int x = 0; x < ids.width; x++)
            if (ids.idAt(x, y) == target) count++;
        return count;
    }

    private static int changedOutside(QuantumRenderer.Capture a, QuantumRenderer.Capture b,
                                      QuantumRenderer.Capture ids, int selected) {
        int changed = 0;
        for (int y = 0; y < a.height; y++) for (int x = 0; x < a.width; x++) {
            int p = ((a.height - 1 - y) * a.width + x) * 4;
            if (ids.idAt(x, y) != selected && (a.rgba[p] != b.rgba[p]
                    || a.rgba[p + 1] != b.rgba[p + 1] || a.rgba[p + 2] != b.rgba[p + 2])) changed++;
        }
        return changed;
    }

    private void save(String name, QuantumRenderer.Capture capture) throws Exception {
        int[] colors = new int[capture.width * capture.height];
        for (int y = 0; y < capture.height; y++) for (int x = 0; x < capture.width; x++) {
            int p = ((capture.height - 1 - y) * capture.width + x) * 4;
            colors[y * capture.width + x] = 0xff000000 | ((capture.rgba[p] & 255) << 16)
                    | ((capture.rgba[p + 1] & 255) << 8) | (capture.rgba[p + 2] & 255);
        }
        Bitmap bitmap = Bitmap.createBitmap(colors, capture.width, capture.height, Bitmap.Config.ARGB_8888);
        try { saveBitmap(name, bitmap); } finally { bitmap.recycle(); }
    }

    private void saveScreen(String name) throws Exception {
        Bitmap bitmap = getUiAutomation().takeScreenshot();
        if (bitmap == null) throw new AssertionError("Cannot capture activity screenshot");
        try { saveBitmap(name, bitmap); } finally { bitmap.recycle(); }
    }

    private void saveBitmap(String name, Bitmap bitmap) throws Exception {
        try (FileOutputStream out = new FileOutputStream(new File(evidence, name))) {
            expect(bitmap.compress(Bitmap.CompressFormat.PNG, 100, out), "Cannot encode " + name);
        }
        images.put(name);
    }

    private void writeReports() throws Exception {
        JSONArray tests = new JSONArray();
        StringBuilder xml = new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        xml.append("<testsuite name=\"MundoXGraphics\" tests=\"").append(results.size())
                .append("\" failures=\"").append(failures()).append("\" errors=\"0\">\n");
        for (Result result : results) {
            JSONObject test = new JSONObject().put("name", result.name)
                    .put("passed", result.error == null).put("durationSeconds", result.seconds);
            xml.append("  <testcase classname=\"com.mundox.graphics.GraphicsInstrumentation\" name=\"")
                    .append(escape(result.name)).append("\" time=\"")
                    .append(String.format(Locale.US, "%.3f", result.seconds)).append("\">");
            if (result.error != null) {
                test.put("error", trace(result.error));
                xml.append("<failure message=\"").append(escape(result.error.toString())).append("\">")
                        .append(escape(trace(result.error))).append("</failure>");
            }
            xml.append("</testcase>\n");
            tests.put(test);
        }
        xml.append("</testsuite>\n");
        JSONObject report = new JSONObject().put("schemaVersion", 1).put("testsRun", results.size())
                .put("testsFailed", failures()).put("tests", tests).put("metrics", metrics).put("images", images)
                .put("projectExports", projectExports);
        write("report.json", report.toString(2) + "\n");
        write("junit.xml", xml.toString());
    }

    private void write(String name, String text) throws Exception {
        try (FileOutputStream out = new FileOutputStream(new File(evidence, name))) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private int failures() {
        int count = 0;
        for (Result result : results) if (result.error != null) count++;
        return count;
    }
    private static void expect(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private static String trace(Throwable error) {
        StringWriter out = new StringWriter();
        error.printStackTrace(new PrintWriter(out));
        return out.toString();
    }
    private static String escape(String value) {
        return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
                .replace("\"", "&quot;").replace("'", "&apos;");
    }
}
