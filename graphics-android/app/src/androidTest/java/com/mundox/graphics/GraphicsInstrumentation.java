package com.mundox.graphics;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
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
    private static final int EXPECTED_TESTS = 10;
    private static final long FRAME_TIMEOUT_MS = 45_000;
    private MainActivity activity;
    private File evidence;
    private final List<Result> results = new ArrayList<>();
    private final JSONObject metrics = new JSONObject();
    private final JSONArray images = new JSONArray();

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

            runCase("native_bell_invariants", this::nativeBell);
            runCase("renderer_borrows_native_state", this::borrowedState);
            runCase("gles_one_million_objects", () -> largeScene(1_000_000, "million"));
            runCase("gles_maximum_object_limit", () -> largeScene(1_048_576, "maximum"));
            runCase("render_observes_without_mutating", this::observationIsReadOnly);
            runCase("gate_changes_coupled_image", this::coupledGate);
            runCase("gate_preserves_uncoupled_image", this::uncoupledGate);
            runCase("selection_and_individual_edits", this::selectionAndEdits);
            runCase("edit_restoration", this::editRestoration);
            runCase("pause_resume_preserves_state", this::pauseResume);

            metrics.put("glRenderer", activity.renderer.device);
            metrics.put("qubits", activity.core.qubits);
            metrics.put("nativeStateBytes", activity.core.stateBytes());
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
                .put("testsFailed", failures()).put("tests", tests).put("metrics", metrics).put("images", images);
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
