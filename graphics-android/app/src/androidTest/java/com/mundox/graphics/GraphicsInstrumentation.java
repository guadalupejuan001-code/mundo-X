package com.mundox.graphics;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.security.MessageDigest;
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
    private static final int LEGACY_TESTS = 18;
    private static final long FRAME_TIMEOUT_MS = 45_000;
    private MainActivity activity;
    private File evidence;
    private final List<Result> results = new ArrayList<>();
    private final JSONObject metrics = new JSONObject();
    private final JSONArray images = new JSONArray();
    private final JSONArray projectExports = new JSONArray();
    private final JSONObject captureSamples = new JSONObject();
    private boolean testUhd;
    private boolean testUltra;
    private int expectedTests = LEGACY_TESTS;
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
        testUltra = arguments != null && "true".equals(arguments.getString("ultra8k"));
        testUhd = testUltra || (arguments != null && "true".equals(arguments.getString("uhd4k")));
        if (testUhd) expectedTests += 4;
        if (testUltra) expectedTests += 4;
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
            metrics.put("uhd4kSuite", testUhd);
            metrics.put("ultra8kSuite", testUltra);
            metrics.put("applicationId", getTargetContext().getPackageName());

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

            if (testUhd) {
                runCase("uhd_real_framebuffer_and_legacy_return", this::uhdAndReturn);
                runCase("uhd_id_selection_and_letterbox", this::uhdPicking);
                runCase("uhd_orientation_roundtrip", this::uhdOrientation);
                runCase("uhd_lifecycle_snapshot_native_preserved", this::uhdLifecycle);
            }

            if (testUltra) {
                runCase("ultra8k_real_framebuffer_and_budget_limits", this::ultraFramebuffer);
                runCase("ultra8k_tiles_match_monolithic_color_and_ids", this::ultraDifferential);
                runCase("ultra8k_streamed_png_dimensions_and_rows", this::ultraStreamed);
                runCase("ultra8k_failure_preserves_scene_and_native_state", this::ultraFailure);
            }

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
        boolean passed = failures == 0 && results.size() == expectedTests;
        finish.putString("stream", "\nMUNDOX_GRAPHICS_TESTS_" + (passed ? "PASSED" : "FAILED")
                + " tests=" + results.size() + " failures=" + failures + "\n");
        finish(passed ? Activity.RESULT_OK : Activity.RESULT_CANCELED, finish);
    }

    private void runCase(String name, Checked test) {
        Bundle status = new Bundle();
        status.putString("class", getClass().getName());
        status.putString("test", name);
        status.putInt("numtests", expectedTests);
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

    private QuantumRenderer.Snapshot visualSnapshot() throws Exception {
        AtomicReference<QuantumRenderer.Snapshot> value = new AtomicReference<>();
        gl(() -> value.set(activity.renderer.snapshot()));
        return value.get();
    }

    private void uhdAndReturn() throws Exception {
        RenderResolution.Size portrait = RenderResolution.uhd(720, 1280);
        RenderResolution.Size landscape = RenderResolution.uhd(1280, 720);
        expect(portrait.width == 2160 && portrait.height == 3840
                && landscape.width == 3840 && landscape.height == 2160,
                "UHD request was reduced to the phone display size");
        new RenderResolution.Limits(4096, 4096, 4096, 4096).require(landscape);
        RenderResolution.Limits[] insufficient = {
            new RenderResolution.Limits(2048, 4096, 4096, 4096),
            new RenderResolution.Limits(4096, 2048, 4096, 4096),
            new RenderResolution.Limits(4096, 4096, 2048, 4096),
            new RenderResolution.Limits(4096, 4096, 4096, 2048),
        };
        int rejected = 0;
        for (RenderResolution.Limits limits : insufficient) {
            try { limits.require(landscape); }
            catch (IllegalArgumentException expected) { rejected++; }
        }
        expect(rejected == insufficient.length, "A GPU texture, renderbuffer or viewport limit was ignored");
        metrics.put("uhdRejectedInsufficientLimits", rejected);
        orient(false);
        scene(1_000_000);
        gl(() -> activity.renderer.testCamera(new float[]{0, 6000, 0}, new float[]{0, 0, 0}));
        QuantumRenderer.Capture baseline = frame(false);
        save("uhd-legacy-before.png", baseline);
        QuantumRenderer.Snapshot before = visualSnapshot();
        NativeCore owner = activity.core;
        NativeCore.Snapshot nativeBefore = owner.snapshot();
        gl(() -> activity.renderer.uhd4k());
        String fingerprint = saveUhd("uhd-million-portrait.png", false, false);
        expect(activity.renderer.isUhd4k(), "The actual renderer did not enter UHD mode");
        sameCore(nativeBefore, owner.snapshot(), "UHD drawing changed native history");
        expect(owner == activity.core, "UHD allocation replaced the authoritative native owner");
        metrics.put("uhdMillionObjectCount", activity.renderer.frameStats().objectCount);
        metrics.put("uhdPortraitWidth", 2160);
        metrics.put("uhdPortraitHeight", 3840);
        metrics.put("uhdPixels", 8_294_400);
        metrics.put("uhdMillionRgbaSha256", fingerprint);
        gl(() -> activity.renderer.resolution(before.resolutionLimit));
        QuantumRenderer.Capture returned = frame(false);
        expect(!activity.renderer.isUhd4k(), "Resolution selection did not leave UHD mode");
        expect(baseline.changed(returned) == 0, "UHD roundtrip changed the previous resolution's exact pixels");
        sameGraphics(before, visualSnapshot(), "UHD roundtrip changed the logical world");
        sameCore(nativeBefore, owner.snapshot(), "Returning from UHD altered the core");
        save("uhd-legacy-after.png", returned);
        metrics.put("uhdLegacyReturnChangedPixels", baseline.changed(returned));
        metrics.put("uhdNativeOwnerPreserved", true);
    }

    private void uhdPicking() throws Exception {
        scene(1);
        NativeCore.Snapshot nativeBefore = activity.core.snapshot();
        gl(() -> activity.renderer.uhd4k());
        // Keep no 33 MB ID capture alive when requesting the next capture.
        int[] chosen = saveUhdPick();
        gl(() -> activity.renderer.pick(chosen[1], chosen[2]));
        saveUhd("uhd-selection.png", false, false);
        expect(activity.renderer.selectedId() == chosen[0], "UHD FIT mapping picked the wrong object ID");
        metrics.put("uhdSelectedId", chosen[0]);
        QuantumRenderer.FrameStats view = activity.renderer.frameStats();
        QuantumRenderer.Snapshot beforeOutside = visualSnapshot();
        int barX, barY;
        if (view.displayLeft > 1) { barX = 0; barY = view.viewHeight / 2; }
        else {
            expect(view.displayTop > 1, "Fixture must expose a letterbox margin");
            barX = view.viewWidth / 2; barY = 0;
        }
        final int outsideX = barX, outsideY = barY;
        gl(() -> activity.renderer.pick(outsideX, outsideY));
        // Drawing processes the queued pick; the capture is not retained.
        saveUhd("uhd-letterbox-rejected.png", false, false);
        expect(activity.renderer.selectedId() == chosen[0], "A letterbox tap changed the existing object selection");
        sameGraphics(beforeOutside, visualSnapshot(), "A tap outside the scene changed the logical world");
        sameCore(nativeBefore, activity.core.snapshot(), "4K selection mutated native state");
        metrics.put("uhdLetterboxRejected", true);
        metrics.put("uhdFitRect", new JSONObject().put("left", view.displayLeft).put("top", view.displayTop)
                .put("width", view.displayWidth).put("height", view.displayHeight)
                .put("surfaceWidth", view.viewWidth).put("surfaceHeight", view.viewHeight));
    }

    private int[] saveUhdPick() throws Exception {
        QuantumRenderer.Capture ids = frame(true);
        expect(ids.width == 2160 && ids.height == 3840, "UHD ID pass dimensions differ from the color pass");
        save("uhd-portrait-ids.png", ids);
        int x = -1, y = -1;
        double nearest = Double.MAX_VALUE;
        for (int py = 32; py < ids.height - 32; py += 16) for (int px = 32; px < ids.width - 32; px += 16) {
            if (ids.idAt(px, py) != 0 || ids.idAt(px - 16, py) != 0 || ids.idAt(px + 16, py) != 0
                    || ids.idAt(px, py - 16) != 0 || ids.idAt(px, py + 16) != 0) continue;
            double distance = Math.pow(px - ids.width / 2.0, 2) + Math.pow(py - ids.height / 2.0, 2);
            if (distance < nearest) { nearest = distance; x = px; y = py; }
        }
        expect(x >= 0, "UHD ID framebuffer contains no selectable object interior");
        QuantumRenderer.FrameStats view = activity.renderer.frameStats();
        double scale = Math.min((double) view.viewWidth / ids.width, (double) view.viewHeight / ids.height);
        expect(Math.abs(view.displayWidth - ids.width * scale) <= 1
                && Math.abs(view.displayHeight - ids.height * scale) <= 1, "UHD presentation distorts the image aspect ratio");
        expect(view.displayLeft >= 0 && view.displayTop >= 0
                && view.displayLeft + view.displayWidth <= view.viewWidth
                && view.displayTop + view.displayHeight <= view.viewHeight, "FIT rectangle escapes the Android surface");
        int screenX = view.displayLeft + (int) ((x + .5) * view.displayWidth / ids.width);
        int screenY = view.displayTop + (int) ((y + .5) * view.displayHeight / ids.height);
        return new int[]{0, screenX, screenY};
    }

    private void uhdOrientation() throws Exception {
        scene(64);
        gl(() -> activity.renderer.uhd4k());
        String before = saveUhd("uhd-portrait-before-rotation.png", false, false);
        QuantumRenderer.Snapshot logical = visualSnapshot();
        NativeCore owner = activity.core;
        NativeCore.Snapshot nativeBefore = owner.snapshot();
        orient(true);
        saveUhd("uhd-landscape.png", false, true);
        saveScreen("uhd-activity-landscape.png");
        sameGraphics(logical, visualSnapshot(), "Screen orientation changed the logical scene");
        expect(activity.core == owner, "Screen rotation replaced the native owner");
        orient(false);
        String after = saveUhd("uhd-portrait-after-rotation.png", false, false);
        expect(before.equals(after), "Orientation roundtrip did not restore exact UHD pixels");
        sameGraphics(logical, visualSnapshot(), "Orientation roundtrip lost the UHD setting or scene");
        sameCore(nativeBefore, owner.snapshot(), "Screen rotation changed native history");
        saveScreen("uhd-activity-portrait.png");
        metrics.put("uhdLandscapeWidth", 3840);
        metrics.put("uhdLandscapeHeight", 2160);
        metrics.put("uhdOrientationChangedPixels", 0);
    }

    private void uhdLifecycle() throws Exception {
        gl(() -> {
            activity.renderer.coupling(true);
            activity.renderer.gate(NativeCore.Z, 0, 0);
            activity.renderer.setTime(1.25f);
        });
        QuantumRenderer.Snapshot before = visualSnapshot();
        NativeCore owner = activity.core;
        NativeCore.Snapshot nativeBefore = owner.snapshot();
        String[] generators = owner.generators(), program = owner.visualProgram();
        float[] expectations = owner.query(program);
        String pixels = saveUhd("uhd-lifecycle-before.png", false, false);
        ByteArrayOutputStream snapshotBytes = new ByteArrayOutputStream();
        try (ObjectOutputStream stream = new ObjectOutputStream(snapshotBytes)) { stream.writeObject(before); }
        try (ObjectInputStream stream = new ObjectInputStream(new ByteArrayInputStream(snapshotBytes.toByteArray()))) {
            sameGraphics(before, (QuantumRenderer.Snapshot) stream.readObject(), "Serialized snapshot lost the UHD preference");
        }
        AtomicReference<Boolean> moved = new AtomicReference<>(false);
        runOnMainSync(() -> moved.set(activity.moveTaskToBack(true)));
        expect(Boolean.TRUE.equals(moved.get()), "Could not background the UHD activity");
        try { waitUntil(() -> !activity.resumed, 15_000, "UHD activity did not pause"); }
        finally {
            Intent resume = new Intent(getTargetContext(), MainActivity.class);
            resume.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                    | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            getTargetContext().startActivity(resume);
        }
        waitUntil(() -> activity.resumed, 15_000, "UHD activity did not resume");
        waitForIdleSync();
        expect(pixels.equals(saveUhd("uhd-after-resume.png", false, false)), "Pause/resume changed exact UHD pixels");
        sameGraphics(before, visualSnapshot(), "Pause/resume lost the UHD logical snapshot");
        expect(activity.core == owner, "Pause/resume replaced the UHD native owner");
        MainActivity original = activity;
        ActivityMonitor monitor = addMonitor(MainActivity.class.getName(), null, false);
        try {
            runOnMainSync(original::recreate);
            Activity next = waitForMonitorWithTimeout(monitor, 25_000);
            expect(next instanceof MainActivity && next != original, "UHD activity was not recreated");
            activity = (MainActivity) next;
            waitUntil(() -> activity.resumed, 15_000, "Recreated UHD activity did not resume");
            waitForIdleSync();
        } finally { removeMonitor(monitor); }
        expect(activity.core != owner, "Recreated UHD activity did not reconstruct its native owner");
        expect(pixels.equals(saveUhd("uhd-after-recreate.png", false, false)), "Activity recreation changed exact UHD pixels");
        sameGraphics(before, visualSnapshot(), "Recreation lost the UHD snapshot");
        sameCore(nativeBefore, activity.core.snapshot(), "UHD lifecycle changed native gate history");
        expect(Arrays.equals(generators, activity.core.generators())
                && Arrays.equals(expectations, activity.core.query(program)), "UHD lifecycle changed native observations");
        metrics.put("uhdPauseResumeChangedPixels", 0);
        metrics.put("uhdRecreateChangedPixels", 0);
        metrics.put("uhdSnapshotSerialized", true);
        metrics.put("uhdRecreatedNativeStatePreserved", true);
        gl(() -> activity.renderer.resolution(320));
    }

    private String saveUhd(String name, boolean ids, boolean landscape) throws Exception {
        QuantumRenderer.Capture image = frame(ids);
        int width = landscape ? 3840 : 2160, height = landscape ? 2160 : 3840;
        expect(image.width == width && image.height == height,
                "Expected actual UHD " + width + "x" + height + ", got " + image.width + "x" + image.height);
        expect(image.rgba.length == 33_177_600 && image.nonBackground() > 100,
                "UHD framebuffer is missing its full pixel payload or visible scene");
        expect(activity.renderer.isUhd4k(), "UHD request was rejected: " + activity.renderer.resolutionNotice());
        save(name, image);
        JSONArray frames = metrics.optJSONArray("uhdFrames");
        if (frames == null) { frames = new JSONArray(); metrics.put("uhdFrames", frames); }
        QuantumRenderer.FrameStats statistics = activity.renderer.frameStats();
        frames.put(new JSONObject().put("file", name).put("width", image.width).put("height", image.height)
                .put("drawMs", statistics.drawMs).put("objects", statistics.objectCount));
        return sha256(image.rgba);
    }

    private void ultraFramebuffer() throws Exception {
        RenderResolution.Size landscape = RenderResolution.uhd8k(1280, 720);
        RenderResolution.Size portrait = RenderResolution.uhd8k(720, 1280);
        expect(landscape.width == 7680 && landscape.height == 4320
                && portrait.width == 4320 && portrait.height == 7680,
                "8K preset is not exact in both orientations");
        RenderResolution.Limits enough = new RenderResolution.Limits(8192, 8192, 8192, 8192);
        enough.require(landscape);
        enough.require(portrait);
        RenderResolution.Limits[] insufficient = {
            new RenderResolution.Limits(4096, 8192, 8192, 8192),
            new RenderResolution.Limits(8192, 4096, 8192, 8192),
            new RenderResolution.Limits(8192, 8192, 4096, 8192),
            new RenderResolution.Limits(8192, 8192, 8192, 4096),
        };
        int rejected = 0;
        for (RenderResolution.Limits limits : insufficient) {
            try { limits.require(landscape); }
            catch (IllegalArgumentException expected) { rejected++; }
        }
        expect(rejected == 4, "An independent GPU limit was ignored for 8K");
        RenderResolution.Size boundary = new RenderResolution.Size(8192, 4096);
        RenderResolution.requireAllocation(boundary, boundary); // Exactly 512 MiB, including old attachments.
        try {
            RenderResolution.requireAllocation(new RenderResolution.Size(8192, 4097), boundary);
            throw new AssertionError("Transactional allocation ignored the old framebuffer's live memory");
        } catch (IllegalArgumentException expected) { rejected++; }
        try {
            RenderResolution.requireAllocation(new RenderResolution.Size(Integer.MAX_VALUE, Integer.MAX_VALUE), null);
            throw new AssertionError("Framebuffer byte multiplication overflow was accepted");
        } catch (ArithmeticException | IllegalArgumentException expected) { rejected++; }
        TilePlan planned = new TilePlan(landscape, new RenderResolution.Limits(2048, 2048, 2048, 2048), 1024, 64);
        expect(planned.tileCount > 100 && planned.workingBytes < 16L * 1024 * 1024,
                "8K export planning materialized a full image or ignored small GPU limits");
        metrics.put("ultra8kRejectedLimits", rejected);
        metrics.put("ultra8kPlannedWorkingBytes", planned.workingBytes);

        orient(true);
        scene(1);
        gl(() -> activity.renderer.coupling(true));
        QuantumRenderer.Capture before = frame(false);
        QuantumRenderer.Snapshot logical = visualSnapshot();
        NativeCore owner = activity.core;
        NativeCore.Snapshot coreBefore = owner.snapshot();
        AtomicReference<QuantumRenderer.FrameStats> actual = new AtomicReference<>();
        int[] pss = {0};
        gl(() -> {
            activity.renderer.setSynchronousTiming(true);
            try {
                activity.renderer.uhd8k();
                // Shipping drawing path, with GPU completion; no 132 MB Java capture.
                activity.renderer.onDrawFrame(null);
                actual.set(activity.renderer.frameStats());
                Debug.MemoryInfo memory = new Debug.MemoryInfo();
                Debug.getMemoryInfo(memory);
                pss[0] = memory.getTotalPss();
                expect(activity.renderer.isUhd8k(), "Actual 8K target was rejected: " + activity.renderer.resolutionNotice());
            } finally {
                activity.renderer.resolution(logical.resolutionLimit);
                activity.renderer.setSynchronousTiming(false);
                // resolution() schedules allocation. Complete it on this GL thread before
                // the next queued capture checks the still-live framebuffer's RGBA budget.
                activity.renderer.onDrawFrame(null);
            }
        });
        QuantumRenderer.FrameStats rendered = actual.get();
        expect(rendered != null && rendered.width == 7680 && rendered.height == 4320
                && rendered.gpuSynchronized && rendered.objectCount == 1 && rendered.drawMs > 0,
                "Native draw did not complete on a full 8K framebuffer");
        QuantumRenderer.FrameStats adaptive = activity.renderer.frameStats();
        expect(adaptive != null && adaptive.resolutionMode == QuantumRenderer.RESOLUTION_ADAPTIVE
                && adaptive.width == before.width && adaptive.height == before.height
                && adaptive.frameNumber > rendered.frameNumber,
                "Adaptive framebuffer did not complete before RGBA capture");
        QuantumRenderer.Capture returned = frame(false);
        expect(before.changed(returned) == 0, "Returning from 8K did not preserve the original pixels");
        sameGraphics(logical, visualSnapshot(), "8K allocation changed the logical scene");
        sameCore(coreBefore, owner.snapshot(), "8K allocation or rendering changed native history");
        expect(activity.core == owner, "8K allocation replaced the native owner");
        metrics.put("ultra8kNativeWidth", rendered.width);
        metrics.put("ultra8kNativeHeight", rendered.height);
        metrics.put("ultra8kNativeDrawMs", rendered.drawMs);
        metrics.put("ultra8kNativePssKiB", pss[0]);
        metrics.put("ultra8kLegacyReturnChangedPixels", before.changed(returned));
        metrics.put("ultra8kPerformanceScope", "One-object scene on recorded GLES device; synchronized wall time, not FPS or phone results");
    }

    private void ultraDifferential() throws Exception {
        scene(64);
        gl(() -> {
            activity.renderer.coupling(true);
            activity.renderer.gate(NativeCore.Z, 0, 0);
            activity.renderer.setTime(1.125f);
            activity.renderer.resolution(512);
        });
        frame(false);
        QuantumRenderer.FrameStats surface = activity.renderer.frameStats();
        int previousPresentation = activity.renderer.presentation();
        NativeCore.Snapshot coreBefore = activity.core.snapshot();
        JSONArray comparisons = new JSONArray();
        try {
            for (int[] shape : new int[][]{{321, 181}, {181, 321}}) {
                int width = shape[0], height = shape[1];
                gl(() -> {
                    activity.renderer.onSurfaceChanged(null, width, height);
                    // Portrait exercises the COSMIC ground's fwidth derivative quads across tiles.
                    activity.renderer.setPresentation(height > width
                            ? QuantumRenderer.PRESENTATION_COSMIC : QuantumRenderer.PRESENTATION_LEGACY);
                });
                QuantumRenderer.Capture ids = frame(true);
                expect(ids.width == width && ids.height == height, "Monolithic reference did not get the odd extent");
                select(choose(ids), ids);
                gl(() -> activity.renderer.editSelected(2));
                QuantumRenderer.Snapshot logical = visualSnapshot();
                for (boolean idPass : new boolean[]{false, true}) {
                    String stem = "ultra-" + width + "x" + height + (idPass ? "-ids" : "-color");
                    QuantumRenderer.Capture reference = frame(idPass);
                    save(stem + "-reference.png", reference);
                    QuantumRenderer.ExportResult result = stream(stem + "-tiled.png",
                            new RenderResolution.Size(width, height), idPass ? 1 : 0,
                            new QuantumRenderer.ExportControl(64, 32, null));
                    expect(result.tileWidth < width && result.tileRows < height && result.tiles > 4,
                            "Differential fixture did not actually cross both tile axes");
                    int[] differences = comparePng(new File(evidence, stem + "-tiled.png"), reference,
                            result.tileWidth, result.tileRows);
                    expect(differences[0] == 0 && differences[1] == 0,
                            "Tiled output differs from monolithic GLES: " + stem + " pixels=" + differences[0]
                                    + " seamPixels=" + differences[1]);
                    comparisons.put(new JSONObject().put("file", stem + "-tiled.png")
                            .put("reference", stem + "-reference.png").put("width", width).put("height", height)
                            .put("ids", idPass).put("tileWidth", result.tileWidth).put("tileRows", result.tileRows)
                            .put("presentation", activity.renderer.presentation())
                            .put("tiles", result.tiles).put("differentPixels", differences[0])
                            .put("seamDifferentPixels", differences[1]).put("comparedPixels", width * height));
                    sameGraphics(logical, visualSnapshot(), "Tile export changed selection, edits, camera or time");
                    sameCore(coreBefore, activity.core.snapshot(), "Tile export mutated the native core");
                    expect(reference.changed(frame(idPass)) == 0, "Tile export failed to restore the current framebuffer");
                }
            }
        } finally {
            gl(() -> {
                activity.renderer.setPresentation(previousPresentation);
                activity.renderer.onSurfaceChanged(null, surface.viewWidth, surface.viewHeight);
            });
        }
        metrics.put("ultra8kDifferential", comparisons);
    }

    private void ultraStreamed() throws Exception {
        scene(1);
        gl(() -> {
            activity.renderer.coupling(true);
            activity.renderer.gate(NativeCore.Z, 0, 0);
            activity.renderer.setTime(.875f);
        });
        QuantumRenderer.Capture baseline = frame(false);
        QuantumRenderer.Snapshot logical = visualSnapshot();
        NativeCore.Snapshot nativeBefore = activity.core.snapshot();
        int[] previousRows = {-1}, progressCalls = {0};
        QuantumRenderer.ExportControl control = new QuantumRenderer.ExportControl(1024, 64, (rows, total) -> {
            expect(total == 4320 && rows > previousRows[0] && rows <= total, "Export progress lost or repeated rows");
            previousRows[0] = rows;
            progressCalls[0]++;
        });
        String name = "ultra-8k-streamed.png";
        QuantumRenderer.ExportResult result = stream(name, new RenderResolution.Size(7680, 4320), 0, control);
        expect(result.width == 7680 && result.height == 4320 && result.tiles > 100
                && result.workingBytes < 16L * 1024 * 1024 && result.elapsedMs > 0,
                "Streamed 8K export did not use the bounded real-pixel path");
        expect(previousRows[0] == 4320 && progressCalls[0] > 10, "Export did not report all completed rows");
        expect(result.coreRevision == nativeBefore.version, "Export used another native state revision");
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        File file = new File(evidence, name);
        BitmapFactory.decodeFile(file.getAbsolutePath(), bounds);
        expect(bounds.outWidth == 7680 && bounds.outHeight == 4320, "Android PNG decoder did not see an exact 8K image");
        BitmapFactory.Options sample = new BitmapFactory.Options();
        sample.inSampleSize = 8;
        Bitmap preview = BitmapFactory.decodeFile(file.getAbsolutePath(), sample);
        expect(preview != null && preview.getWidth() == 960 && preview.getHeight() == 540,
                "Bounded Android decoding of the 8K PNG failed");
        try {
            int different = 0, first = preview.getPixel(0, 0);
            for (int y = 0; y < preview.getHeight(); y += 7) for (int x = 0; x < preview.getWidth(); x += 7)
                if (preview.getPixel(x, y) != first) different++;
            expect(different > 100, "8K PNG contains no substantive rendered geometry");
        } finally { preview.recycle(); }
        sameGraphics(logical, visualSnapshot(), "8K export changed the active scene");
        sameCore(nativeBefore, activity.core.snapshot(), "8K export changed native state");
        expect(baseline.changed(frame(false)) == 0, "8K export changed the previous resolution's image");
        metrics.put("ultra8kStreamed", new JSONObject().put("file", name).put("width", result.width)
                .put("height", result.height).put("tileWidth", result.tileWidth).put("tileRows", result.tileRows)
                .put("tiles", result.tiles).put("workingBytes", result.workingBytes)
                .put("elapsedMs", result.elapsedMs).put("coreRevision", result.coreRevision)
                .put("sha256", sha256(file)).put("progressCalls", progressCalls[0])
                .put("androidBoundsVerified", true).put("androidSampleDecodeVerified", true));
    }

    private void ultraFailure() throws Exception {
        // Landscape controls leave only 320x41 render pixels on the CI phone.
        // Restore the real portrait viewport before requiring a selectable object interior.
        orient(false);
        scene(64);
        QuantumRenderer.Capture ids = frame(true);
        Pick selected = choose(ids);
        select(selected, ids);
        gl(() -> {
            activity.renderer.editSelected(2);
            activity.renderer.coupling(true);
            activity.renderer.setTime(1.375f);
        });
        QuantumRenderer.Capture baseline = frame(false);
        QuantumRenderer.Snapshot logical = visualSnapshot();
        NativeCore owner = activity.core;
        NativeCore.Snapshot nativeBefore = owner.snapshot();
        String[] generators = owner.generators();
        float[] observations = owner.query(logical.observables);
        int[] invalid = {0};
        boolean[] cancelled = {false, false}, ioFailure = {false};
        gl(() -> {
            ByteArrayOutputStream untouched = new ByteArrayOutputStream();
            for (int[] shape : new int[][]{{0, 181}, {16385, 181}, {Integer.MAX_VALUE, Integer.MAX_VALUE}}) {
                try {
                    activity.renderer.exportPng(untouched, new RenderResolution.Size(shape[0], shape[1]), 0,
                            new QuantumRenderer.ExportControl());
                } catch (IllegalArgumentException | ArithmeticException expected) { invalid[0]++; }
                catch (IOException wrong) { throw new AssertionError("Invalid extent reached the writer", wrong); }
            }
            expect(invalid[0] == 3 && untouched.size() == 0, "Invalid output extent allocated/wrote before validation");
            QuantumRenderer.ExportControl alreadyCancelled = new QuantumRenderer.ExportControl();
            alreadyCancelled.cancel();
            try {
                activity.renderer.exportPng(untouched, new RenderResolution.Size(321, 181), 0, alreadyCancelled);
            } catch (IOException expected) { cancelled[0] = true; }
            expect(cancelled[0] && untouched.size() == 0, "Pre-cancelled export wrote output");
            QuantumRenderer.ExportControl[] during = new QuantumRenderer.ExportControl[1];
            during[0] = new QuantumRenderer.ExportControl(64, 32, (rows, total) -> {
                if (rows > 0) during[0].cancel();
            });
            try {
                activity.renderer.exportPng(new ByteArrayOutputStream(), new RenderResolution.Size(321, 181), 0, during[0]);
            } catch (IOException expected) { cancelled[1] = true; }
            expect(cancelled[1], "Export ignored cancellation after completed work");
            OutputStream broken = new OutputStream() {
                int count;
                @Override public void write(int value) throws IOException {
                    if (++count > 128) throw new IOException("Injected destination failure after PNG header");
                }
                @Override public void write(byte[] value, int offset, int length) throws IOException {
                    for (int i = 0; i < length; i++) write(value[offset + i]);
                }
            };
            try {
                activity.renderer.exportPng(broken, new RenderResolution.Size(321, 181), 0,
                        new QuantumRenderer.ExportControl(64, 32, null));
            } catch (IOException expected) { ioFailure[0] = true; }
            expect(ioFailure[0], "Destination failure was swallowed");
        });
        gl(() -> {
            android.opengl.GLES30.glDepthMask(false);
            try {
                activity.renderer.exportPng(new ByteArrayOutputStream(), new RenderResolution.Size(65, 33), 0,
                        new QuantumRenderer.ExportControl(32, 16, null));
                int[] mask = {1};
                android.opengl.GLES30.glGetIntegerv(android.opengl.GLES30.GL_DEPTH_WRITEMASK, mask, 0);
                expect(mask[0] == 0, "Successful export changed the caller's depth write mask");
                QuantumRenderer.ExportControl[] cancelledMask = new QuantumRenderer.ExportControl[1];
                cancelledMask[0] = new QuantumRenderer.ExportControl(32, 16, (rows, total) -> cancelledMask[0].cancel());
                try {
                    activity.renderer.exportPng(new ByteArrayOutputStream(), new RenderResolution.Size(65, 33), 0,
                            cancelledMask[0]);
                    throw new AssertionError("Depth-mask fixture did not cancel");
                } catch (IOException expected) { }
                android.opengl.GLES30.glGetIntegerv(android.opengl.GLES30.GL_DEPTH_WRITEMASK, mask, 0);
                expect(mask[0] == 0, "Cancelled export changed the caller's depth write mask");
            } catch (IOException error) {
                throw new AssertionError("Depth-mask restoration fixture failed", error);
            } finally { android.opengl.GLES30.glDepthMask(true); }
        });
        expect(baseline.changed(frame(false)) == 0, "Failed/cancelled export left stale GL state");
        boolean preservedFile = cancelShippingImageOnPause();
        expect(activity.core == owner && owner.version() == nativeBefore.version, "Cancelled UI export replaced native ownership");
        sameCore(nativeBefore, owner.snapshot(), "Failed export changed native history");
        expect(Arrays.equals(generators, owner.generators())
                && Arrays.equals(observations, owner.query(logical.observables)), "Failed export changed native observations");
        sameGraphics(logical, visualSnapshot(), "Failed export changed selection, edits or the camera");
        QuantumRenderer.Capture recovered = frame(false);
        expect(baseline.changed(recovered) == 0 && activity.renderer.failure == null,
                "Renderer did not recover exact pixels after cancellation and I/O failure");
        // A successful export after every failure proves resources remain reusable.
        stream("ultra-after-failure.png", new RenderResolution.Size(321, 181), 0,
                new QuantumRenderer.ExportControl(64, 32, null));
        metrics.put("ultra8kFailure", new JSONObject().put("invalidRejected", invalid[0])
                .put("cancelledBeforeWrite", cancelled[0]).put("cancelledDuringExport", cancelled[1])
                .put("ioFailurePropagated", ioFailure[0]).put("stateUnchanged", true)
                .put("selectionUnchanged", activity.renderer.selectedId() == logical.selectedId)
                .put("selectedId", selected.id).put("selectedPixels", selected.area)
                .put("selectionFrameWidth", ids.width).put("selectionFrameHeight", ids.height)
                .put("recoveredChangedPixels", baseline.changed(recovered)).put("partialFileRemoved", preservedFile)
                .put("previousCompleteFilePreserved", preservedFile).put("shippingPauseCancellation", true));
    }

    /** Exercise MainActivity's actual AtomicFile/cancel-onPause path, not a test-only cleanup. */
    private boolean cancelShippingImageOnPause() throws Exception {
        File complete = new File(activity.getCacheDir(), "cosmic-image-pending.png");
        File partial = new File(complete.getPath() + ".new");
        ByteArrayOutputStream previousPng = new ByteArrayOutputStream();
        FramePng.write(new QuantumRenderer.Capture(1, 1, new byte[]{17, 22, 19, (byte)255}), previousPng);
        byte[] prior = previousPng.toByteArray();
        try (FileOutputStream output = new FileOutputStream(complete)) { output.write(prior); }
        Method export = MainActivity.class.getDeclaredMethod("exportImage", int.class);
        export.setAccessible(true);
        Field active = MainActivity.class.getDeclaredField("activeImageExport");
        active.setAccessible(true);
        Field ownerField = MainActivity.class.getDeclaredField("IMAGE_EXPORT_OWNER");
        ownerField.setAccessible(true);
        AtomicReference<?> owner = (AtomicReference<?>) ownerField.get(null);
        AtomicReference<Throwable> invoked = new AtomicReference<>();
        runOnMainSync(() -> {
            try { export.invoke(activity, QuantumRenderer.RESOLUTION_UHD_8K); }
            catch (Throwable error) { invoked.set(error); }
        });
        expect(invoked.get() == null, "Shipping PNG 8K button path could not be invoked");
        try {
            waitUntil(() -> partial.length() > 33, 30_000, "Shipping exporter never wrote an actual partial PNG");
            expect(active.get(activity) != null && owner.get() != null, "Shipping export finished before cancellation fixture");
            AtomicReference<Boolean> moved = new AtomicReference<>(false);
            runOnMainSync(() -> moved.set(activity.moveTaskToBack(true)));
            expect(Boolean.TRUE.equals(moved.get()), "Could not pause the real UI export");
            waitUntil(() -> !activity.resumed, 15_000, "Shipping export activity never paused");
            waitUntil(() -> owner.get() == null, 30_000, "Cancelled image retained the global export owner");
            expect(active.get(activity) == null, "Cancelled image retained its active controller");
            expect(!partial.exists(), "AtomicFile left a partial PNG after shipping cancellation");
            expect(Arrays.equals(prior, fileBytes(complete)), "Cancellation replaced the previous complete staged image");
        } finally {
            Object stillActive = active.get(activity);
            if (stillActive instanceof QuantumRenderer.ExportControl)
                ((QuantumRenderer.ExportControl) stillActive).cancel();
            Intent resume = new Intent(getTargetContext(), MainActivity.class);
            resume.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                    | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            getTargetContext().startActivity(resume);
        }
        waitUntil(() -> activity.resumed, 15_000, "Activity did not resume after cancelling PNG preparation");
        waitForIdleSync();
        return true;
    }

    private QuantumRenderer.ExportResult stream(String name, RenderResolution.Size size, int debug,
                                                QuantumRenderer.ExportControl control) throws Exception {
        AtomicReference<QuantumRenderer.ExportResult> result = new AtomicReference<>();
        gl(() -> {
            try (FileOutputStream output = new FileOutputStream(new File(evidence, name))) {
                result.set(activity.renderer.exportPng(output, size, debug, control));
            } catch (IOException error) { throw new AssertionError("Cannot export " + name, error); }
        });
        expect(result.get() != null, "Streaming exporter returned no metadata");
        images.put(name);
        return result.get();
    }

    private static int[] comparePng(File file, QuantumRenderer.Capture reference, int tileWidth, int stripRows) {
        Bitmap decoded = BitmapFactory.decodeFile(file.getAbsolutePath());
        expect(decoded != null, "Android could not decode tiled PNG");
        int changed = 0, seams = 0;
        try {
            expect(decoded.getWidth() == reference.width && decoded.getHeight() == reference.height,
                    "Tiled PNG dimensions differ from monolithic capture");
            for (int y = 0; y < reference.height; y++) for (int x = 0; x < reference.width; x++) {
                int p = ((reference.height - 1 - y) * reference.width + x) * 4;
                int rgba = ((reference.rgba[p + 3] & 255) << 24) | ((reference.rgba[p] & 255) << 16)
                        | ((reference.rgba[p + 1] & 255) << 8) | (reference.rgba[p + 2] & 255);
                if (decoded.getPixel(x, y) != rgba) {
                    changed++;
                    int glY = reference.height - 1 - y;
                    if (x % tileWidth <= 1 || x % tileWidth == tileWidth - 1
                            || glY % stripRows <= 1 || glY % stripRows == stripRows - 1) seams++;
                }
            }
        } finally { decoded.recycle(); }
        return new int[]{changed, seams};
    }

    private static byte[] fileBytes(File file) throws Exception {
        try (FileInputStream input = new FileInputStream(file); ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
            byte[] part = new byte[8192]; int count;
            while ((count = input.read(part)) != -1) bytes.write(part, 0, count);
            return bytes.toByteArray();
        }
    }

    private static String sha256(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] bytes = new byte[65536]; int count;
            while ((count = input.read(bytes)) != -1) digest.update(bytes, 0, count);
        }
        StringBuilder output = new StringBuilder(64);
        for (byte value : digest.digest()) output.append(String.format(Locale.ROOT, "%02x", value & 255));
        return output.toString();
    }

    private void orient(boolean landscape) {
        runOnMainSync(() -> activity.setRequestedOrientation(landscape
                ? ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE : ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));
        waitUntil(() -> {
            AtomicReference<Boolean> ready = new AtomicReference<>(false);
            runOnMainSync(() -> ready.set(activity.surface.getWidth() > 0 && activity.surface.getHeight() > 0
                    && !activity.surface.isLayoutRequested()
                    && (landscape ? activity.surface.getWidth() > activity.surface.getHeight()
                                  : activity.surface.getHeight() > activity.surface.getWidth())));
            return Boolean.TRUE.equals(ready.get());
        }, 20_000, "Android surface did not establish the requested orientation");
        waitForIdleSync();
    }

    private static String sha256(byte[] bytes) throws Exception {
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(bytes);
        StringBuilder output = new StringBuilder(64);
        for (byte value : digest) output.append(String.format(Locale.ROOT, "%02x", value & 255));
        return output.toString();
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
        JSONArray resolutionModes = new JSONArray();
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
                    // Device preferences must survive the same expansion/candidate path
                    // even though portable .cosmic deliberately omits fixed UHD modes.
                    for (int mode : new int[]{QuantumRenderer.RESOLUTION_ADAPTIVE,
                            QuantumRenderer.RESOLUTION_UHD_4K, QuantumRenderer.RESOLUTION_UHD_8K}) {
                        if (mode == QuantumRenderer.RESOLUTION_UHD_4K) visual.uhd4k();
                        else if (mode == QuantumRenderer.RESOLUTION_UHD_8K) visual.uhd8k();
                        else visual.resolution(320);
                        CosmicProject.Document candidate = CosmicProject.expandQubits(legacy.name,
                                original.snapshot(), visual.snapshot(), 1000);
                        expect(candidate.graphics.resolutionMode == mode && candidate.graphics.resolutionLimit == 320,
                                "Expansion lost the requested device resolution");
                        try (NativeCore larger = new NativeCore(candidate.core)) {
                            QuantumRenderer restored = new QuantumRenderer(getTargetContext(), larger, text -> { }, candidate.graphics);
                            sameGraphics(candidate.graphics, restored.snapshot(), "Expanded candidate lost device preferences on restore");
                        }
                        expect(CosmicProject.decode(CosmicProject.encode(candidate)).graphics.resolutionMode
                                == QuantumRenderer.RESOLUTION_ADAPTIVE, "Portable export leaked a device-only UHD preference");
                        resolutionModes.put(mode);
                    }
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
                && raw.get().overview == beforeVisual.overview && raw.get().resolutionMode == beforeVisual.resolutionMode,
                "Production expansion lost hidden session preferences");
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
        metrics.put("legacyExpansionResolutionModes", resolutionModes);
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
                && expected.resolutionMode == actual.resolutionMode
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
        long deadline = SystemClock.elapsedRealtime() + (testUltra ? 180_000 : testUhd ? 120_000 : FRAME_TIMEOUT_MS);
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
        QuantumRenderer.FrameStats view = activity.renderer.frameStats();
        int x = view.displayLeft + (int) ((pick.x + .5f) * view.displayWidth / ids.width);
        int y = view.displayTop + (int) ((pick.y + .5f) * view.displayHeight / ids.height);
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
        try (FileOutputStream out = new FileOutputStream(new File(evidence, name))) {
            FramePng.write(capture, out);
        }
        JSONArray points = new JSONArray();
        for (int row = 0; row < 5; row++) for (int column = 0; column < 5; column++) {
            int x = (capture.width - 1) * column / 4, y = (capture.height - 1) * row / 4;
            int offset = ((capture.height - 1 - y) * capture.width + x) * 4;
            JSONArray rgba = new JSONArray();
            for (int channel = 0; channel < 4; channel++) rgba.put(capture.rgba[offset + channel] & 255);
            points.put(new JSONObject().put("x", x).put("y", y).put("rgba", rgba));
        }
        captureSamples.put(name, new JSONObject().put("width", capture.width).put("height", capture.height)
                .put("rgbaSha256", sha256(capture.rgba)).put("points", points));
        // Decode the shipping exporter output, checking channel order and the
        // GL bottom-to-Android-top conversion at independently sampled pixels.
        Bitmap decoded = BitmapFactory.decodeFile(new File(evidence, name).getAbsolutePath());
        expect(decoded != null, "Exported PNG cannot be decoded: " + name);
        try {
            expect(decoded.getWidth() == capture.width && decoded.getHeight() == capture.height,
                    "PNG export changed framebuffer dimensions");
            for (int index = 0; index < points.length(); index++) {
                JSONObject point = points.getJSONObject(index);
                JSONArray rgba = point.getJSONArray("rgba");
                int pixel = decoded.getPixel(point.getInt("x"), point.getInt("y"));
                expect(((pixel >>> 24) & 255) == rgba.getInt(3)
                        && ((pixel >>> 16) & 255) == rgba.getInt(0)
                        && ((pixel >>> 8) & 255) == rgba.getInt(1) && (pixel & 255) == rgba.getInt(2),
                        "PNG export changed RGBA channels or image orientation: " + name);
            }
        } finally { decoded.recycle(); }
        captureSamples.getJSONObject(name).put("decodedPngSamplesVerified", true);
        images.put(name);
    }

    private void saveScreen(String name) throws Exception {
        // SurfaceFlinger can transiently refuse the display capture just after
        // rotation even though the app's FBO and logical state are complete.
        // Retry only an unavailable compositor image, never a pixel mismatch.
        waitForIdleSync();
        Bitmap bitmap = null;
        int attempts = 0;
        while (bitmap == null && attempts < 5) {
            attempts++;
            bitmap = getUiAutomation().takeScreenshot();
            if (bitmap == null && attempts < 5) SystemClock.sleep(200);
        }
        JSONObject history = metrics.optJSONObject("activityScreenshotAttempts");
        if (history == null) { history = new JSONObject(); metrics.put("activityScreenshotAttempts", history); }
        history.put(name, attempts);
        if (bitmap == null) throw new AssertionError("Cannot capture activity screenshot after five compositor requests");
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
                .put("projectExports", projectExports).put("captureSamples", captureSamples);
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
