package com.mundox.cosmic;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Point;
import android.graphics.Rect;
import android.net.Uri;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.Choreographer;
import android.view.PixelCopy;
import android.view.Surface;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.TextView;
import com.mundox.graphics.CosmicProject;
import com.mundox.graphics.NativeCore;
import com.mundox.graphics.QuantumRenderer;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.json.JSONArray;
import org.json.JSONObject;

/** Real Android/JNI/GLES integration checks for the new application, without an external test framework. */
public final class CosmicInstrumentation extends Instrumentation {
    private static final int EXPECTED_TESTS = 25;
    private static final long TIMEOUT = 45_000;
    private static final String[][] EXPERIMENTS = {
        {"circuitos", "com.mundox.lab.XLabActivity"},
        {"micromundo", "com.mundox.lab.MicroLabActivity"},
        {"objetos", "com.mundox.lab.ObjectLabActivity"},
        {"ecos", "com.mundox.echoes.MainActivity"},
        {"vm8", "com.mundox.lab.BasisVmActivity"},
        {"cuantica", "com.mundox.lab.QuantumInterfaceActivity"},
        {"visual", "com.mundox.lab.VisualExperimentActivity"},
        {"gpu", "com.mundox.lab.GpuVisualActivity"},
        {"juego", "com.mundox.lab.ProceduralGameDemoActivity"},
        {"sincronizada", "com.mundox.lab.SyncedGpuActivity"},
        {"red", "com.mundox.lab.InternetSyncActivity"},
        {"dispositivo", "com.mundox.lab.DeviceLauncherActivity"},
        {"rendimiento", "com.mundox.lab.PerformanceLauncherActivity"},
        {"clasico", "com.mundox.lab.MainActivity"},
    };
    private CosmicActivity activity;
    private ActivityMonitor ownerMonitor;
    private File evidence;
    private final List<JSONObject> results = new ArrayList<>();
    private final JSONObject metrics = new JSONObject();
    private final JSONArray images = new JSONArray();
    private final JSONArray experiments = new JSONArray();
    private int failures;

    private interface Checked { void run() throws Exception; }
    private interface Condition { boolean ready() throws Exception; }
    private static final class World {
        final NativeCore owner;
        final NativeCore.Snapshot core;
        final QuantumRenderer.Snapshot graphics;
        final String[] generators;
        final float[] observations;
        final byte[] normalized;
        World(CosmicActivity activity) throws Exception {
            owner = activity.core;
            core = owner.snapshot();
            graphics = activity.renderer.snapshot();
            generators = owner.generators();
            observations = owner.query(graphics.observables);
            normalized = CosmicProject.encode(CosmicProject.fromSnapshots("Validation", core, graphics));
        }
    }

    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }

    @Override public void onStart() {
        try {
            evidence = new File(getTargetContext().getFilesDir(), "cosmic-evidence");
            require(evidence.isDirectory() || evidence.mkdirs(), "Cannot create evidence directory");
            metrics.put("androidSdk", Build.VERSION.SDK_INT);
            metrics.put("abis", new JSONArray(Arrays.asList(Build.SUPPORTED_ABIS)));
            metrics.put("performanceScope", "Android 15 software GLES emulator; no physical phone benchmark");
            ownerMonitor = addMonitor(CosmicActivity.class.getName(), null, false);
            activity = (CosmicActivity) startActivitySync(new Intent(getTargetContext(), CosmicActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
            await(() -> activity.resumed && activity.renderer != null && activity.renderer.hasFrame, "new Activity frame");
            pauseAnimation();

            check("application_identity_and_exclusions", this::identity);
            check("new_motor_live_framebuffer", this::firstFrame);
            check("four_panels_retain_native_owner", this::panels);
            check("camera_and_observer_preserve_state", this::camera);
            check("gate_changes_coupled_image", this::coupled);
            check("gate_preserves_uncoupled_image", this::uncoupled);
            check("saf_export_and_reimport", this::documents);
            check("legacy_project_expansion_preserves_world", this::expandLegacy);
            check("background_resume_preserves_world", this::background);
            check("activity_recreation_restores_project", this::recreate);
            for (String[] experiment : EXPERIMENTS) {
                check("experiment_" + experiment[0] + "_launch", () -> experiment(experiment[0], experiment[1]));
            }
            check("invalid_saf_import_preserves_session", this::invalidDocument);
            metrics.put("glRenderer", activity.renderer.device);
            metrics.put("nativeStateBytes", activity.core.stateBytes());
        } catch (Throwable error) {
            failures++;
            results.add(result("runner_setup_or_reporting", false, 0, error));
            Log.e("CosmicTests", "Runner setup/reporting", error);
        } finally {
            if (ownerMonitor != null) removeMonitor(ownerMonitor);
        }
        Bundle summary = new Bundle();
        try { reports(); }
        catch (Throwable error) { failures++; summary.putString("report_error", Log.getStackTraceString(error)); }
        boolean passed = failures == 0 && results.size() == EXPECTED_TESTS;
        summary.putInt("tests_run", results.size());
        summary.putInt("tests_failed", failures);
        summary.putString("stream", "\nCOSMIC_UNIFIED_TESTS_" + (passed ? "PASSED" : "FAILED")
                + " tests=" + results.size() + " failures=" + failures + "\n");
        if (activity != null) mainSync(() -> activity.finish());
        finish(passed ? Activity.RESULT_OK : Activity.RESULT_CANCELED, summary);
    }

    private void check(String name, Checked body) {
        Bundle status = new Bundle();
        status.putString("class", getClass().getName());
        status.putString("test", name);
        status.putInt("numtests", EXPECTED_TESTS);
        status.putInt("current", results.size() + 1);
        sendStatus(1, status);
        long began = SystemClock.elapsedRealtime();
        Throwable error = null;
        try { body.run(); }
        catch (Throwable caught) {
            error = caught;
            failures++;
            status.putString("stack", Log.getStackTraceString(caught));
            Log.e("CosmicTests", name, caught);
            try { screen("failure-" + name + ".png"); } catch (Throwable ignored) { }
        }
        results.add(result(name, error == null, SystemClock.elapsedRealtime() - began, error));
        // Keep completed proof if a later Android call stalls or its process is terminated.
        // The verifier still requires the complete, exact 25-case inventory.
        try { reports(); }
        catch (Exception reporting) { throw new AssertionError("Could not checkpoint Android evidence", reporting); }
        sendStatus(error == null ? 0 : -2, status);
    }

    private static JSONObject result(String name, boolean passed, long millis, Throwable failure) {
        JSONObject item = new JSONObject();
        try {
            item.put("name", name).put("passed", passed).put("durationMs", millis);
            if (failure != null) item.put("failure", Log.getStackTraceString(failure));
        } catch (Exception impossible) { throw new AssertionError(impossible); }
        return item;
    }

    private void identity() throws Exception {
        require("com.mundox.cosmic".equals(getTargetContext().getPackageName()), "Wrong target application ID");
        PackageInfo info = getTargetContext().getPackageManager().getPackageInfo("com.mundox.cosmic", PackageManager.GET_ACTIVITIES);
        require("0.3.1".equals(info.versionName) && info.getLongVersionCode() == 2, "Wrong new application version");
        Set<String> names = new HashSet<>();
        for (ActivityInfo item : info.activities) {
            names.add(item.name);
            String lower = item.name.toLowerCase(java.util.Locale.ROOT);
            require(!lower.contains(".pc.") && !lower.contains("windows") && !lower.contains(".x86."),
                    "Excluded Windows activity is installed");
            if (!item.name.equals(CosmicActivity.class.getName())) require(!item.exported, "Internal lab was exported: " + item.name);
        }
        for (String[] experiment : EXPERIMENTS) require(names.contains(experiment[1]), "Missing experiment: " + experiment[0]);
        require(names.size() == 16, "Unexpected Activity inventory");
        require(names.contains("com.mundox.graphics.MainActivity"), "Missing original observer Activity");
        metrics.put("applicationId", info.packageName).put("version", info.versionName)
                .put("activityCount", names.size()).put("windowsActivityPresent", false);
    }

    private void firstFrame() throws Exception {
        click("nav_motor");
        require(activity.core.stateBytes() > 0, "New viewport has no authoritative native state");
        World state = world();
        require(NativeCore.DEFAULT_QUBITS == 1000 && state.core.qubits == 1000
                && state.graphics.observables.length == 3000, "New home did not use the complete 1000-qubit profile");
        QuantumRenderer.Capture color = frame(false), ids = frame(true);
        int visible = 0;
        Set<Integer> objects = new HashSet<>();
        for (int y = 0; y < ids.height; y++) for (int x = 0; x < ids.width; x++) {
            int id = ids.idAt(x, y);
            if (id >= 0) { require(id < state.graphics.objectCount, "Viewport ID exceeds scene"); visible++; objects.add(id); }
        }
        require(visible > 100 && !objects.isEmpty(), "New home viewport did not render real objects");
        sameNative(state, world(), "Initial frame mutated native state");
        saveFrame("cosmic-render-color.png", color);
        saveFrame("cosmic-render-ids.png", ids);
        // ID captures are also blitted to the visible surface. Acknowledge its color repaint.
        frame(false);
        screen("cosmic-motor.png");
        metrics.put("initialObjects", state.graphics.objectCount).put("initialVisiblePixels", visible)
                .put("initialDistinctIds", objects.size()).put("initialResolutionLimit", state.graphics.resolutionLimit)
                .put("initialQubits", state.core.qubits).put("initialObservables", state.graphics.observables.length);
    }

    private void panels() throws Exception {
        World before = world();
        QuantumRenderer observer = activity.renderer;
        for (String section : new String[]{"nucleo", "labs", "archivo", "motor"}) {
            click("nav_" + section);
            require(onMainVisible(activity, "panel_" + section), "Selected panel is not visible: " + section);
            require(activity.core == before.owner && activity.renderer == observer, "Panel navigation replaced the core or observer");
            require(Arrays.equals(before.core.gateHistory, activity.core.snapshot().gateHistory)
                    && before.core.version == activity.core.version(), "Panel navigation changed native history");
            if (!section.equals("motor")) screen("cosmic-" + section + ".png");
        }
        sameNative(before, world(), "Navigation changed native observables");
        metrics.put("panelCount", 4).put("panelsPreservedOwner", true);
    }

    private void camera() throws Exception {
        World before = world();
        gl(() -> { activity.renderer.orbit(8, -3); activity.renderer.zoom(1.05f); });
        frame(false); frame(true);
        sameNative(before, world(), "Moving the camera or observing changed native state");
        require(activity.core == before.owner, "Camera movement replaced core owner");
        metrics.put("cameraPreservedOwner", true).put("observationWasReadOnly", true);
    }

    private void unitScene(boolean coupled) throws Exception {
        click("nav_motor");
        pauseAnimation();
        gl(() -> {
            activity.renderer.reset();
            activity.renderer.objectCount(1);
            activity.renderer.resolution(320);
            activity.renderer.coupling(coupled);
            activity.renderer.setTime(0);
            activity.renderer.testCamera(new float[]{5, 4, 7}, new float[]{0, 1, 0});
        });
    }

    private void coupled() throws Exception {
        unitScene(true);
        NativeCore owner = activity.core;
        String observable = owner.generators()[0];
        long version = owner.version();
        require(owner.query(new String[]{observable})[0] == 1, "Prepared generator is not stabilized");
        QuantumRenderer.Capture before = frame(false);
        click("nav_nucleo"); click("gate_z");
        await(() -> owner.version() == version + 1, "Z control reaching the actual native core");
        click("nav_motor");
        QuantumRenderer.Capture after = frame(false);
        require(activity.core == owner && owner.query(new String[]{observable})[0] == -1, "Z control did not mutate the borrowed core");
        int changed = before.changed(after);
        require(changed > 20, "A native gate did not change the coupled new viewport");
        saveFrame("cosmic-coupled-before.png", before); saveFrame("cosmic-coupled-after.png", after);
        metrics.put("coupledChangedPixels", changed).put("gatePreservedOwner", true);
    }

    private void uncoupled() throws Exception {
        unitScene(false);
        NativeCore owner = activity.core;
        String observable = owner.generators()[0];
        long version = owner.version();
        QuantumRenderer.Capture before = frame(false);
        click("nav_nucleo"); click("gate_z");
        await(() -> owner.version() == version + 1, "uncoupled native gate");
        click("nav_motor");
        QuantumRenderer.Capture after = frame(false);
        require(owner.query(new String[]{observable})[0] == -1, "Native gate was incorrectly disabled with visual coupling");
        require(before.changed(after) == 0, "Disabled coupling still changed new viewport pixels");
        saveFrame("cosmic-uncoupled-before.png", before); saveFrame("cosmic-uncoupled-after.png", after);
        metrics.put("uncoupledChangedPixels", 0);
    }

    private void documents() throws Exception {
        click("nav_archivo");
        NativeCore oldOwner = activity.core;
        click("example_bell");
        await(() -> activity.core != oldOwner && activity.core.qubits == 2 && activity.renderer.hasFrame, "Bell example import");
        click("nav_motor"); pauseAnimation();
        gl(() -> { activity.renderer.resolution(320); activity.renderer.setTime(.75f); activity.renderer.gate(NativeCore.Z, 0, 0); });
        World before = world();
        QuantumRenderer.Capture beforeImage = frame(false);
        Uri uri = projectUri("session.cosmic");
        getTargetContext().getContentResolver().delete(uri, null, null);
        click("nav_archivo");
        documentAction("project_save", Intent.ACTION_CREATE_DOCUMENT, uri);
        await(() -> providerLong(uri, OpenableColumns.SIZE) > 0
                && !new File(getTargetContext().getCacheDir(), "cosmic-pending-export.json").exists(), "real SAF export completion");
        byte[] bytes = readProject(uri);
        CosmicProject.Document exported = CosmicProject.decode(bytes);
        require(Arrays.equals(before.normalized, CosmicProject.encode(CosmicProject.fromSnapshots("Validation", exported.core, exported.graphics))),
                "SAF export lost native or graphics state");
        writeBytes("unified-saf-export.cosmic", bytes);
        JSONArray history = new JSONArray(); for (long gate : before.core.gateHistory) history.put(gate);
        JSONObject nativeProof = new JSONObject().put("schemaVersion", 1).put("qubits", before.core.qubits).put("revision", before.core.version)
                .put("nativeGateHistory", history).put("nativeStateBytes", before.owner.stateBytes())
                .put("generators", new JSONArray(Arrays.asList(before.generators)))
                .put("observables", new JSONArray(Arrays.asList(before.graphics.observables)))
                .put("expectations", floats(before.observations));
        write("unified-saf-native.json", nativeProof.toString(2));
        gl(() -> activity.renderer.gate(NativeCore.X, 0, 0));
        NativeCore mutated = activity.core;
        documentAction("project_open", Intent.ACTION_OPEN_DOCUMENT, uri);
        await(() -> activity.core != mutated && activity.renderer.hasFrame, "real SAF import");
        click("nav_motor");
        World after = world();
        sameWorld(before, after, "SAF round trip changed the world");
        QuantumRenderer.Capture afterImage = frame(false);
        require(beforeImage.changed(afterImage) == 0, "SAF round trip changed real GLES pixels");
        saveFrame("cosmic-saf-before.png", beforeImage); saveFrame("cosmic-saf-after.png", afterImage);
        metrics.put("safExportBytes", bytes.length).put("safRoundtripChangedPixels", 0)
                .put("safExportQubits", exported.core.qubits).put("safUsedProductionCallbacks", true);
    }

    private void expandLegacy() throws Exception {
        click("nav_motor"); pauseAnimation();
        World before = world();
        require(before.core.qubits == 2, "The legacy Bell project was silently resized on import");
        CosmicProject.Document expected = CosmicProject.expandQubits("Validation", before.core, before.graphics, 1000);
        QuantumRenderer.Capture image = frame(false);
        click("nav_archivo");
        mainSync(() -> {
            require(find(activity, "project_expand_qubits").performClick(), "Missing production expansion action");
            require(find(activity, "gate_h").performClick(), "Missing gate callback during expansion race check");
            CountDownLatch checked = new CountDownLatch(1);
            AtomicReference<Long> revision = new AtomicReference<>();
            activity.surface.queueEvent(() -> { revision.set(before.owner.version()); checked.countDown(); });
            try { require(checked.await(10, TimeUnit.SECONDS), "Pending expansion gate check timed out"); }
            catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); throw new AssertionError(interrupted); }
            require(revision.get() == before.core.version, "Gate callback mutated the source during pending expansion");
        });
        await(() -> activity.core != before.owner && activity.core.qubits == 1000 && activity.renderer.hasFrame,
                "production 1000-qubit expansion");
        click("nav_motor"); World after = world();
        require(after.core.version == before.core.version && Arrays.equals(after.core.gateHistory, before.core.gateHistory)
                && Arrays.equals(after.observations, before.observations), "Expanding a legacy project reset its native state");
        require(Arrays.equals(CosmicProject.encode(expected), after.normalized)
                && before.graphics.enabledQuantumStrength == after.graphics.enabledQuantumStrength
                && before.graphics.yaw == after.graphics.yaw && before.graphics.pitch == after.graphics.pitch
                && before.graphics.zoom == after.graphics.zoom && before.graphics.overview == after.graphics.overview,
                "Expanding a legacy project changed its visual or hidden session preferences");
        require(after.generators.length == 1000, "Expanded core does not expose all 1000 generators");
        for (int q = 2; q < 1000; q++) {
            char[] zero = new char[1000]; Arrays.fill(zero, 'I'); zero[q] = 'Z';
            require(after.generators[q].equals("+" + new String(zero)), "Added qubit was not initialized to |0>: " + q);
        }
        QuantumRenderer.Capture expanded = frame(false);
        require(image.changed(expanded) == 0, "Legacy expansion changed the frozen viewport");
        saveFrame("cosmic-expansion-before.png", image); saveFrame("cosmic-expansion-after.png", expanded);
        mainSync(() -> require(activity.getWindow().getDecorView().findViewWithTag("project_expand_qubits") == null,
                "The expansion action is still available at the maximum profile"));
        metrics.put("expandedFromQubits", 2).put("expandedToQubits", 1000).put("expansionChangedPixels", 0)
                .put("expansionPreservedState", true).put("expansionAddedQubitsZero", true)
                .put("expansionPreservedHiddenState", true).put("expansionBlockedConcurrentGate", true);
    }

    private void background() throws Exception {
        click("nav_motor");
        World before = world();
        QuantumRenderer.Capture image = frame(false);
        AtomicReference<Boolean> moved = new AtomicReference<>(false);
        mainSync(() -> moved.set(activity.moveTaskToBack(true)));
        require(Boolean.TRUE.equals(moved.get()), "Could not move the app to the real background");
        await(() -> !activity.resumed, "real onPause");
        getTargetContext().startActivity(new Intent(getTargetContext(), CosmicActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP));
        refreshOwner();
        require(activity.core == before.owner, "Background/resume replaced the native owner");
        sameWorld(before, world(), "Background/resume changed the world");
        QuantumRenderer.Capture after = frame(false);
        require(image.changed(after) == 0, "Background/resume changed pixels");
        saveFrame("cosmic-background-before.png", image); saveFrame("cosmic-background-after.png", after);
        metrics.put("backgroundChangedPixels", 0).put("backgroundPreservedOwner", true);
    }

    private void recreate() throws Exception {
        click("nav_archivo");
        NativeCore previous = activity.core;
        click("example_ghz");
        await(() -> activity.core != previous && activity.core.qubits == 3 && activity.renderer.hasFrame, "GHZ lifecycle fixture");
        click("nav_nucleo"); click("coupling"); click("nav_motor");
        gl(() -> {
            activity.renderer.objectCount(64); activity.renderer.resolution(320); activity.renderer.setTime(1.25f);
            activity.renderer.orbit(13, -5); activity.renderer.zoom(1.2f);
            activity.renderer.testCamera(new float[]{13, 15, 19}, new float[]{0, 1, 0});
        });
        World before = world();
        require(before.graphics.quantumStrength == 0 && before.graphics.enabledQuantumStrength == -.125f
                && before.graphics.overrideEye != null && before.graphics.zoom != 1,
                "Lifecycle fixture did not contain disabled negative coupling and hidden orbit state");
        QuantumRenderer.Capture image = frame(false);
        CosmicActivity original = activity;
        mainSync(original::recreate);
        await(() -> ownerMonitor.getLastActivity() instanceof CosmicActivity
                && ownerMonitor.getLastActivity() != original, "new Activity instance");
        refreshOwner();
        require(activity.core != before.owner, "Recreation did not reconstruct an owning native session");
        click("nav_motor");
        sameWorld(before, world(), "Recreation lost the saved project");
        QuantumRenderer.Capture after = frame(false);
        require(image.changed(after) == 0, "Activity recreation changed real GLES pixels");
        saveFrame("cosmic-recreate-before.png", image); saveFrame("cosmic-recreate-after.png", after);
        metrics.put("recreateChangedPixels", 0).put("recreateCreatedNewOwner", true);

        // Finish/start uses the private on-disk session; no retained Activity object is available.
        CosmicActivity closing = activity;
        NativeCore closingOwner = activity.core;
        mainSync(closing::finish);
        await(closing::isDestroyed, "finished Activity destruction");
        activity = (CosmicActivity) startActivitySync(new Intent(getTargetContext(), CosmicActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
        refreshOwner(); click("nav_motor");
        require(activity.core != closingOwner, "Close/reopen reused a destroyed native owner");
        sameWorld(before, world(), "Private on-disk session lost disabled coupling or hidden orbit fields");
        QuantumRenderer.Capture reopened = frame(false);
        require(image.changed(reopened) == 0, "Close/reopen changed the saved real GLES frame");
        saveFrame("cosmic-reopen-after.png", reopened);
        click("nav_nucleo"); click("coupling"); click("nav_motor");
        World enabled = world();
        sameNative(before, enabled, "Reenabling a saved visual coupling changed the quantum core");
        require(enabled.graphics.quantumStrength == -.125f, "Private restore forgot the negative coupling magnitude");
        click("nav_nucleo"); click("coupling"); click("nav_motor");
        sameWorld(before, world(), "Disabling restored coupling changed hidden state");
        metrics.put("reopenChangedPixels", 0).put("restoredEnabledCoupling", -.125)
                .put("hiddenOrbitStatePreserved", true).put("diskSessionCreatedNewOwner", true);
    }

    private void experiment(String id, String className) throws Exception {
        refreshOwner();
        click("nav_motor");
        World before = world();
        CosmicActivity oldActivity = activity;
        click("nav_labs");
        ActivityMonitor monitor = addMonitor(className, null, false);
        Activity child = null;
        try {
            click("experiment_" + id);
            child = waitForMonitorWithTimeout(monitor, 25_000);
            require(child != null && className.equals(child.getClass().getName()), "Card did not open its included experiment: " + id);
            final Activity opened = child;
            await(() -> onMainWindowReady(opened), "experiment window " + id);
            afterUiTraversal();
            JSONObject details = new JSONObject().put("id", id).put("activity", className);
            if (id.equals("circuitos")) {
                await(() -> onMainEnabled(opened, "run_circuit"), "circuit control");
                click(opened, "run_circuit");
                await(() -> "ready".equals(description(opened, "circuit_status")), "XLab native circuit");
                JSONObject result = new JSONObject(text(opened, "circuit_result"));
                require(!result.has("ok") || result.getBoolean("ok"), "XLab native result failed");
                require(!"error".equalsIgnoreCase(result.optString("status")), "XLab returned a native error");
                details.put("nativeCircuit", result);
            } else if (id.equals("vm8")) {
                await(() -> onMainEnabled(opened, "vm_run"), "VM example loading");
                click(opened, "vm_run");
                await(() -> text(opened, "vm_result").startsWith("PASS"), "original VM execution");
                JSONObject result = new JSONObject(privateString(opened, "lastJson"));
                require(result.getBoolean("reference_ok") && result.getJSONArray("output").getInt(0) == 55, "Original VM sum differs from reference");
                details.put("nativeVm", result);
            } else if (id.equals("ecos")) {
                click(opened, "echo_run");
                await(() -> text(opened, "echo_result").equals("VERIFICADO"), "original native echo");
                JSONObject result = new JSONObject(privateString(opened, "lastReport"));
                require(result.getBoolean("reference_ok") && Math.abs(result.getDouble("signal_z") + .5) < 1e-10
                        && Math.abs(result.getDouble("control_z") - 1) < 1e-10, "Original echo differs from its dense reference");
                details.put("nativeEcho", result);
            } else if (id.equals("objetos")) {
                await(() -> description(opened, "objects_stats").matches("(?s).*frames=[1-9][0-9]*;.*"), "object GPU frame");
            } else if (id.equals("gpu") || id.equals("visual")) {
                await(() -> privateText(opened, "stats").contains("core_bytes="), "original native visual renderer");
                details.put("rendererStatus", privateText(opened, "stats"));
            } else if (id.equals("juego")) {
                await(() -> !privateText(opened, "hud").contains("initializing") && privateText(opened, "hud").contains("CITY"), "procedural game GPU frame");
                details.put("surfaceCapture", gameSurface(opened));
            } else if (id.equals("sincronizada")) {
                await(() -> privateText(opened, "stats").contains("frame="), "offline synchronized GPU frame");
                String state = privateText(opened, "stats");
                require(state.contains("tx=0B rx=0B"), "Network experiment unexpectedly exchanged data");
                details.put("rendererStatus", state);
            } else if (id.equals("micromundo")) {
                await(() -> new JSONObject(privateString(opened, "latestSnapshot")).optLong("ticks") > 2, "MicroLab native frame");
            }
            SystemClock.sleep(450);
            screen("experiment-" + id + ".png");
            details.put("screen", "experiment-" + id + ".png").put("passed", true)
                    .put("scope", Arrays.asList("red", "dispositivo", "rendimiento").contains(id)
                            ? "Offline launch and UI; no connection, app launch or settings change requested"
                            : "Included Activity launched; native/render readiness checked where available");
            experiments.put(details);
        } finally {
            removeMonitor(monitor);
            if (child != null) { final Activity opened = child; mainSync(opened::finish); }
            refreshOwner();
        }
        click("nav_motor");
        sameWorld(before, world(), "Opening " + id + " changed the main quantum project");
        if (activity == oldActivity) require(activity.core == before.owner, "Opening " + id + " replaced the retained native core");
        metrics.put("experimentReturnsPreservedProject", experiments.length());
    }

    private void invalidDocument() throws Exception {
        click("nav_motor");
        World before = world();
        QuantumRenderer.Capture image = frame(false);
        Uri uri = projectUri("invalid.cosmic");
        try (OutputStream stream = getTargetContext().getContentResolver().openOutputStream(uri, "wt")) {
            require(stream != null, "No invalid-document stream");
            stream.write("{\"format\":\"cosmic-x-engine-project\",\"version\":99}".getBytes(StandardCharsets.UTF_8));
        }
        long reads = providerLong(uri, "reads");
        click("nav_archivo");
        documentAction("project_open", Intent.ACTION_OPEN_DOCUMENT, uri);
        await(() -> providerLong(uri, "reads") > reads, "invalid document read by production importer");
        SystemClock.sleep(500);
        afterUiTraversal();
        require(activity.core == before.owner, "Invalid document replaced the active native session");
        sameWorld(before, world(), "Invalid document changed the active project");
        click("nav_motor");
        // The GL framebuffer is independent of the Android error notification.
        QuantumRenderer.Capture after = frame(false);
        require(image.changed(after) == 0, "Invalid import changed the active GLES framebuffer");
        metrics.put("invalidImportChangedPixels", 0).put("invalidImportPreservedOwner", true);
    }

    private void documentAction(String tag, String action, Uri uri) throws Exception {
        Intent answer = new Intent().setData(uri)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        IntentFilter filter = new IntentFilter(action);
        filter.addCategory(Intent.CATEGORY_OPENABLE);
        filter.addDataType("*/*");
        ActivityMonitor picker = addMonitor(filter, new ActivityResult(Activity.RESULT_OK, answer), true);
        try {
            click(tag);
            await(() -> picker.getHits() == 1, "production document request " + action);
        } finally { removeMonitor(picker); }
    }

    private JSONObject gameSurface(Activity target) throws Exception {
        Field field = target.getClass().getDeclaredField("gl"); field.setAccessible(true);
        GLSurfaceView view = (GLSurfaceView) field.get(target);
        int[] size = new int[2];
        int[] displayState = new int[3];
        await(() -> {
            mainSync(() -> {
                size[0] = view.getWidth(); size[1] = view.getHeight();
                if (target.getResources().getConfiguration().orientation != Configuration.ORIENTATION_LANDSCAPE) size[0] = 0;
                // Use the application display, not the Activity's temporary fixed-rotation configuration.
                android.view.Display display = ((WindowManager) getTargetContext().getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
                Point real = new Point(); display.getRealSize(real);
                displayState[0] = display.getRotation(); displayState[1] = real.x; displayState[2] = real.y;
            });
            return size[0] > size[1] && size[1] > 0 && displayState[1] > displayState[2]
                    && (displayState[0] == Surface.ROTATION_90 || displayState[0] == Surface.ROTATION_270);
        }, "landscape game surface and actual display rotation");
        Bitmap bitmap = Bitmap.createBitmap(size[0], size[1], Bitmap.Config.ARGB_8888);
        try {
            long deadline = SystemClock.elapsedRealtime() + TIMEOUT;
            int status;
            do {
                CountDownLatch copied = new CountDownLatch(1);
                AtomicReference<Integer> result = new AtomicReference<>(PixelCopy.ERROR_UNKNOWN);
                mainSync(() -> PixelCopy.request(view, bitmap, value -> { result.set(value); copied.countDown(); },
                        new Handler(Looper.getMainLooper())));
                require(copied.await(TIMEOUT, TimeUnit.MILLISECONDS), "Game PixelCopy timed out");
                status = result.get();
                if (status == PixelCopy.ERROR_SOURCE_NO_DATA) SystemClock.sleep(80);
            } while (status == PixelCopy.ERROR_SOURCE_NO_DATA && SystemClock.elapsedRealtime() < deadline);
            require(status == PixelCopy.SUCCESS, "Game PixelCopy failed with Android result " + status);
            saveBitmap("experiment-juego-surface.png", bitmap);
            int[] pixels = new int[size[0] * size[1]]; bitmap.getPixels(pixels, 0, size[0], 0, 0, size[0], size[1]);
            int nonblack = 0, colorful = 0;
            Set<Integer> colors = new HashSet<>();
            for (int pixel : pixels) {
                int r = Color.red(pixel), g = Color.green(pixel), b = Color.blue(pixel);
                int high = Math.max(r, Math.max(g, b)), low = Math.min(r, Math.min(g, b));
                if (high > 8) nonblack++;
                if (high > 20 && high - low > 8) colorful++;
                if (colors.size() < 4096) colors.add(pixel & 0xffffff);
            }
            JSONObject proof = new JSONObject().put("source", "android.view.PixelCopy of actual GLSurfaceView")
                    .put("result", status).put("width", size[0]).put("height", size[1])
                    .put("displayRotation", displayState[0] * 90).put("displayWidth", displayState[1]).put("displayHeight", displayState[2])
                    .put("nonblackPixels", nonblack).put("colorfulPixels", colorful)
                    .put("distinctRgbColorsCappedAt4096", colors.size()).put("image", "experiment-juego-surface.png");
            write("game-surface-readback.json", proof.toString(2));
            if (nonblack <= pixels.length / 100 || colorful <= 1024 || colors.size() < 32) gameGlDiagnostics(target, view);
            require(nonblack > pixels.length / 100 && colorful > 1024 && colors.size() >= 32,
                    "Actual game SurfaceView is blank or lacks rendered colors: " + proof);
            return proof;
        } finally { bitmap.recycle(); }
    }

    private void gameGlDiagnostics(Activity target, GLSurfaceView view) throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<JSONObject> result = new AtomicReference<>();
        view.queueEvent(() -> {
            JSONObject data = new JSONObject();
            int[] previous = new int[1]; GLES30.glGetIntegerv(GLES30.GL_FRAMEBUFFER_BINDING, previous, 0);
            try {
                data.put("pendingGlError", GLES30.glGetError());
                Field rendererField = target.getClass().getDeclaredField("renderer"); rendererField.setAccessible(true);
                Object observer = rendererField.get(target);
                Field framebufferField = observer.getClass().getDeclaredField("fbo"); framebufferField.setAccessible(true);
                int framebuffer = framebufferField.getInt(observer);
                GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, framebuffer);
                data.put("colorFramebuffer", framebuffer).put("framebufferStatus", GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER));
                java.nio.ByteBuffer pixel = java.nio.ByteBuffer.allocateDirect(4);
                GLES30.glReadPixels(0, 0, 1, 1, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, pixel);
                JSONArray rgba = new JSONArray(); for (int i = 0; i < 4; i++) rgba.put(pixel.get(i) & 255);
                data.put("colorAttachmentCornerRgba", rgba).put("diagnosticGlError", GLES30.glGetError());
            } catch (Throwable error) { try { data.put("error", Log.getStackTraceString(error)); } catch (Exception ignored) { } }
            finally { GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, previous[0]); result.set(data); done.countDown(); }
        });
        require(done.await(TIMEOUT, TimeUnit.MILLISECONDS), "Game GL diagnosis timed out");
        write("game-gles-diagnostics.json", result.get().toString(2));
    }

    private static Uri projectUri(String file) { return Uri.parse("content://" + ProjectEvidenceProvider.AUTHORITY + "/" + file); }
    private long providerLong(Uri uri, String column) {
        try (Cursor cursor = getTargetContext().getContentResolver().query(uri, new String[]{column}, null, null, null)) {
            require(cursor != null && cursor.moveToFirst(), "No provider metadata");
            return cursor.getLong(0);
        }
    }
    private byte[] readProject(Uri uri) throws Exception {
        try (InputStream stream = getTargetContext().getContentResolver().openInputStream(uri)) {
            require(stream != null, "No exported document stream");
            ByteArrayOutputStream result = new ByteArrayOutputStream();
            byte[] chunk = new byte[8192];
            for (int count; (count = stream.read(chunk)) >= 0;) {
                result.write(chunk, 0, count);
                require(result.size() <= 16 * 1024 * 1024, "Oversized exported document");
            }
            return result.toByteArray();
        }
    }

    private void pauseAnimation() throws Exception {
        // This is the real control, so UI state and the animation scheduler agree.
        click("nav_motor");
        AtomicReference<String> title = new AtomicReference<>("");
        mainSync(() -> { View view = find(activity, "toggle_animation"); if (view instanceof TextView) title.set(((TextView) view).getText().toString()); });
        String lower = title.get().toLowerCase(java.util.Locale.ROOT);
        if (lower.contains("paus") || lower.contains("detener")) click("toggle_animation");
    }
    private void refreshOwner() throws Exception {
        await(() -> {
            Activity candidate = ownerMonitor.getLastActivity();
            if (candidate instanceof CosmicActivity && ((CosmicActivity) candidate).resumed) activity = (CosmicActivity) candidate;
            return activity != null && activity.resumed && activity.renderer != null;
        }, "return to CosmicActivity");
        // A restored Labs tab deliberately pauses its hidden GLSurfaceView. Show Motor
        // before asking that observer for a frame, including after a landscape lab.
        click("nav_motor");
        await(() -> activity.renderer.hasFrame, "returned Motor frame");
    }
    private World world() throws Exception {
        AtomicReference<World> result = new AtomicReference<>();
        gl(() -> { try { result.set(new World(activity)); } catch (Exception error) { throw new IllegalStateException(error); } });
        return result.get();
    }
    private static void sameNative(World a, World b, String reason) {
        require(a.core.qubits == b.core.qubits && a.core.version == b.core.version
                && Arrays.equals(a.core.gateHistory, b.core.gateHistory)
                && Arrays.equals(a.generators, b.generators) && Arrays.equals(a.observations, b.observations), reason);
    }
    private static void sameWorld(World a, World b, String reason) {
        sameNative(a, b, reason);
        require(Arrays.equals(a.normalized, b.normalized)
                && a.graphics.enabledQuantumStrength == b.graphics.enabledQuantumStrength
                && a.graphics.overview == b.graphics.overview && a.graphics.yaw == b.graphics.yaw
                && a.graphics.pitch == b.graphics.pitch && a.graphics.zoom == b.graphics.zoom, reason);
    }
    private void gl(Runnable command) throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        activity.surface.queueEvent(() -> {
            try { command.run(); } catch (Throwable caught) { error.set(caught); } finally { done.countDown(); }
        });
        activity.surface.requestRender();
        waitGl(done, error);
    }
    private QuantumRenderer.Capture frame(boolean ids) throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        AtomicReference<QuantumRenderer.Capture> image = new AtomicReference<>();
        activity.surface.queueEvent(() -> {
            try { activity.renderer.requestCapture(ids, capture -> { image.set(capture); done.countDown(); }); }
            catch (Throwable caught) { error.set(caught); done.countDown(); }
        });
        activity.surface.requestRender();
        waitGl(done, error);
        require(image.get() != null, "No native GLES framebuffer captured");
        return image.get();
    }
    private void waitGl(CountDownLatch done, AtomicReference<Throwable> error) throws Exception {
        long end = SystemClock.elapsedRealtime() + TIMEOUT;
        while (!done.await(50, TimeUnit.MILLISECONDS)) {
            require(activity.renderer.failure == null, "GLES failure: " + activity.renderer.failure);
            require(SystemClock.elapsedRealtime() < end, "GLES operation timed out");
        }
        if (error.get() != null) throw new AssertionError("GLES operation", error.get());
        require(activity.renderer.failure == null, "GLES failure: " + activity.renderer.failure);
    }
    private void await(Condition condition, String description) throws Exception {
        long end = SystemClock.elapsedRealtime() + TIMEOUT;
        while (SystemClock.elapsedRealtime() < end) {
            if (condition.ready()) return;
            SystemClock.sleep(60);
        }
        throw new AssertionError("Timed out: " + description);
    }
    private static View find(Activity target, String tag) {
        View view = target.getWindow().getDecorView().findViewWithTag(tag);
        require(view != null, "Missing UI control: " + tag);
        return view;
    }
    /** Animated Canvas experiments keep the queue busy; acknowledge commands, not global idleness. */
    private void mainSync(Runnable command) {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        require(new Handler(Looper.getMainLooper()).post(() -> {
            try { command.run(); }
            catch (Throwable caught) { error.set(caught); }
            finally { done.countDown(); }
        }), "Android UI queue rejected an operation");
        try {
            if (!done.await(TIMEOUT, TimeUnit.MILLISECONDS)) throw new AssertionError("Android UI operation timed out; main stack: "
                    + Arrays.toString(Looper.getMainLooper().getThread().getStackTrace()));
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new AssertionError("Interrupted Android UI operation", interrupted);
        }
        if (error.get() != null) throw new AssertionError("Android UI operation", error.get());
    }
    private void afterUiTraversal() {
        CountDownLatch done = new CountDownLatch(1);
        // Post back after the next vsync dispatch, which includes View traversal and
        // SurfaceView resizing. Continuous animation may proceed without becoming idle.
        mainSync(() -> Choreographer.getInstance().postFrameCallback(frameTime ->
                new Handler(Looper.getMainLooper()).post(done::countDown)));
        try {
            require(done.await(TIMEOUT, TimeUnit.MILLISECONDS), "Android View traversal timed out");
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new AssertionError("Interrupted Android View traversal", interrupted);
        }
    }
    private void click(String tag) { click(activity, tag); }
    private void click(Activity target, String tag) {
        mainSync(() -> {
            View view = find(target, tag);
            view.requestRectangleOnScreen(new Rect(0, 0, view.getWidth(), view.getHeight()), true);
            require(view.isEnabled() && view.performClick(), "Control did not activate: " + tag);
        });
        afterUiTraversal();
    }
    private boolean onMainVisible(Activity target, String tag) {
        AtomicReference<Boolean> result = new AtomicReference<>(false);
        mainSync(() -> result.set(find(target, tag).isShown()));
        return result.get();
    }
    private boolean onMainEnabled(Activity target, String tag) {
        AtomicReference<Boolean> result = new AtomicReference<>(false);
        mainSync(() -> result.set(find(target, tag).isEnabled()));
        return result.get();
    }
    private boolean onMainWindowReady(Activity target) {
        AtomicReference<Boolean> result = new AtomicReference<>(false);
        mainSync(() -> { View view = target.getWindow().getDecorView(); result.set(view.isShown() && view.getWidth() > 0 && view.getHeight() > 0); });
        return result.get();
    }
    private String text(Activity target, String tag) {
        AtomicReference<String> result = new AtomicReference<>("");
        mainSync(() -> result.set(((TextView) find(target, tag)).getText().toString()));
        return result.get();
    }
    private String description(Activity target, String tag) {
        AtomicReference<String> result = new AtomicReference<>("");
        mainSync(() -> { CharSequence value = find(target, tag).getContentDescription(); result.set(value == null ? "" : value.toString()); });
        return result.get();
    }
    private String privateString(Activity target, String name) throws Exception {
        Field field = target.getClass().getDeclaredField(name); field.setAccessible(true);
        AtomicReference<String> result = new AtomicReference<>("");
        AtomicReference<Throwable> error = new AtomicReference<>();
        mainSync(() -> {
            try { Object value = field.get(target); result.set(value == null ? "" : value.toString()); }
            catch (Throwable caught) { error.set(caught); }
        });
        if (error.get() != null) throw new AssertionError("Read experiment evidence", error.get());
        return result.get();
    }
    private String privateText(Activity target, String name) throws Exception {
        Field field = target.getClass().getDeclaredField(name); field.setAccessible(true);
        AtomicReference<String> result = new AtomicReference<>("");
        AtomicReference<Throwable> error = new AtomicReference<>();
        mainSync(() -> {
            try { result.set(((TextView) field.get(target)).getText().toString()); }
            catch (Throwable caught) { error.set(caught); }
        });
        if (error.get() != null) throw new AssertionError("Read rendered experiment status", error.get());
        return result.get();
    }
    private void screen(String name) throws Exception {
        afterUiTraversal();
        SystemClock.sleep(250);
        Bitmap bitmap = getUiAutomation().takeScreenshot();
        require(bitmap != null, "No actual Android screenshot");
        try { saveBitmap(name, bitmap); } finally { bitmap.recycle(); }
    }
    private void saveFrame(String name, QuantumRenderer.Capture capture) throws Exception {
        int[] pixels = new int[capture.width * capture.height];
        for (int y = 0; y < capture.height; y++) for (int x = 0; x < capture.width; x++) {
            int p = ((capture.height - 1 - y) * capture.width + x) * 4;
            pixels[y * capture.width + x] = Color.argb(capture.rgba[p + 3] & 255, capture.rgba[p] & 255,
                    capture.rgba[p + 1] & 255, capture.rgba[p + 2] & 255);
        }
        Bitmap bitmap = Bitmap.createBitmap(pixels, capture.width, capture.height, Bitmap.Config.ARGB_8888);
        try { saveBitmap(name, bitmap); } finally { bitmap.recycle(); }
    }
    private void saveBitmap(String name, Bitmap bitmap) throws Exception {
        try (OutputStream stream = new FileOutputStream(new File(evidence, name))) {
            require(bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream), "Could not write PNG " + name);
        }
        images.put(name);
    }
    private static JSONArray floats(float[] values) throws Exception {
        JSONArray result = new JSONArray(); for (float value : values) result.put(value); return result;
    }
    private void write(String name, String contents) throws Exception { writeBytes(name, (contents + "\n").getBytes(StandardCharsets.UTF_8)); }
    private void writeBytes(String name, byte[] bytes) throws Exception {
        try (OutputStream stream = new FileOutputStream(new File(evidence, name))) { stream.write(bytes); }
    }
    private void reports() throws Exception {
        JSONObject report = new JSONObject().put("schemaVersion", 1).put("testsRun", results.size())
                .put("testsFailed", failures).put("tests", new JSONArray(results)).put("metrics", metrics)
                .put("images", images).put("experiments", experiments);
        write("report.json", report.toString(2));
        StringBuilder xml = new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\"?><testsuite name=\"COSMIC Android\" tests=\"")
                .append(results.size()).append("\" failures=\"").append(failures).append("\">");
        for (JSONObject item : results) {
            xml.append("<testcase name=\"").append(item.getString("name")).append("\" time=\"")
                    .append(item.getLong("durationMs") / 1000.0).append("\">");
            if (!item.getBoolean("passed")) xml.append("<failure>").append(escape(item.optString("failure"))).append("</failure>");
            xml.append("</testcase>");
        }
        xml.append("</testsuite>"); write("junit.xml", xml.toString());
    }
    private static String escape(String value) { return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"); }
    private static void require(boolean condition, String message) { if (!condition) throw new AssertionError(message); }
}
