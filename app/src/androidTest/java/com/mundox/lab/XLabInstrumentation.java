package com.mundox.lab;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.Instrumentation;
import android.content.ComponentName;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.graphics.Bitmap;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Device-level smoke tests. Runs with the platform runner, without test libraries. */
public final class XLabInstrumentation extends Instrumentation {
    private static final long DEADLINE_MS = 20_000L;
    private Activity activity;
    private boolean largeSceneOnly;
    private int passed;
    private int failed;
    private final List<String> failures = new ArrayList<>();
    private final List<String> screenshots = new ArrayList<>();
    private final List<String> diagnosticNotes = new ArrayList<>();

    @Override
    public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        largeSceneOnly = arguments != null
                && Boolean.parseBoolean(arguments.getString("large_scene_only", "false"));
        start();
    }

    @Override
    public void onStart() {
        new Thread(this::runChecks, "XLab-device-checks").start();
    }

    private void runChecks() {
        try {
            Intent intent = new Intent(Intent.ACTION_MAIN);
            intent.setComponent(new ComponentName(getTargetContext(),
                    largeSceneOnly ? "com.mundox.lab.ObjectLabActivity" : "com.mundox.lab.XLabActivity"));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            activity = startActivitySync(intent);
            waitForIdleSync();
            if (largeSceneOnly) {
                checkMillionObjects();
                return; // The shared finally block still publishes normal instrumentation results.
            }

            check("mesa_native_default", () -> {
                requireTag("run_circuit");
                click("run_circuit");
                awaitReady("circuit_status", "circuit_result");
                requireNativeJson("circuit_result");
            });

            check("bell_and_step", () -> {
                click("tab_mesa");
                preset("preset_bell");
                select("shots_count", 0);
                click("run_circuit");
                awaitReady("circuit_status", "circuit_result");
                JSONObject bell = requireNativeJson("circuit_result");
                JSONArray marginals = bell.getJSONArray("prob_one");
                require(marginals.length() == 2, "Bell must have two marginal probabilities");
                requireProbability(marginals.getDouble(0), 0.5, "Bell q0");
                requireProbability(marginals.getDouble(1), 0.5, "Bell q1");
                JSONArray counts = bell.getJSONArray("counts");
                require(counts.length() == 4, "Bell histogram must have four bins");
                require(counts.getInt(1) == 0 && counts.getInt(2) == 0,
                        "Bell histogram contains an uncorrelated outcome");
                require(total(counts) == 128, "Bell histogram lost shots");
                screenshot("xlab-mesa.png");
                preset("preset_bell");
                click("step_next");
                awaitReady("circuit_status", "circuit_result");
                JSONObject prefix = requireNativeJson("circuit_result");
                require(prefix.getInt("step") == 1, "Next step did not execute one gate");
                requireProbability(prefix.getJSONArray("prob_one").getDouble(0), 0.5,
                        "Bell first gate q0");
                requireProbability(prefix.getJSONArray("prob_one").getDouble(1), 0.0,
                        "Bell first gate q1");
            });

            check("interference", () -> {
                click("tab_mesa");
                preset("preset_interference");
                select("shots_count", 0);
                click("run_circuit");
                awaitReady("circuit_status", "circuit_result");
                JSONObject interference = requireNativeJson("circuit_result");
                requireProbability(interference.getJSONArray("prob_one").getDouble(0),
                        1.0, "HZH interference");
                JSONArray counts = interference.getJSONArray("counts");
                require(counts.length() == 2 && counts.getInt(0) == 0
                        && counts.getInt(1) == 128, "Interference should yield only one");
            });

            check("invalid_circuit_is_error_and_recovers", () -> {
                click("tab_mesa");
                select("qubit_count", 1);
                setText("script_editor", "h 999");
                click("run_circuit");
                awaitState("circuit_status", "error");
                requireText("circuit_result");
                preset("preset_bell");
                click("run_circuit");
                awaitReady("circuit_status", "circuit_result");
                requireNativeJson("circuit_result");
            });

            check("interactive_tests_tab", () -> {
                click("tab_pruebas");
                select("protocol_selector", 0);
                setText("protocol_value", "3");
                click("protocol_run");
                awaitReady("protocol_status", "protocol_result");
                JSONObject protocol = requireNativeJson("protocol_result");
                require(protocol.getBoolean("passed"), "Superdense decoding failed");
                require("11".equals(protocol.getString("expected"))
                        && "11".equals(protocol.getString("observed")),
                        "Superdense decoding did not recover the selected message");
                screenshot("xlab-pruebas.png");
            });

            check("memory_cancel_is_real", () -> {
                click("tab_carga");
                select("memory_budget", 0); // 64 MiB; never allocate 2 GiB in a smoke test.
                select("memory_family", 0);
                setText("memory_qubits", "16320");
                long deadline = SystemClock.uptimeMillis() + 45_000L;
                click("memory_run");
                try {
                    awaitNativeMemoryRunning(deadline);
                    click("memory_cancel");
                    awaitStateUntil("memory_status", "cancelled", deadline);
                    JSONObject cancelled = requireNativeJson("memory_result");
                    require("cancelled".equals(cancelled.getString("status")),
                            "The workload did not finish as cancelled");
                    require(!cancelled.getBoolean("running"),
                            "Native work is still running after UI cancellation");
                    require(cancelled.getInt("qubits") == 16320,
                            "Cancellation result belongs to a different workload");
                } finally {
                    // An assertion failure must not leave a background workload.
                    // Cancelling an idle job cannot attach to the next run.
                    NativeCore.labMemoryCancel();
                }
            });

            check("memory_preflight_and_small_native_run", () -> {
                click("tab_carga");
                requireText("memory_estimate");
                setText("memory_qubits", "100000");
                if (main(() -> find("memory_run").isEnabled())) {
                    click("memory_run");
                    awaitState("memory_status", "error");
                }
                // Exercise the real native path, with only four qubits. The device
                // test must never request the user's multi-gigabyte workload.
                setText("memory_qubits", "4");
                requireText("memory_estimate");
                click("memory_run");
                awaitReady("memory_status", "memory_result");
                JSONObject memory = requireNativeJson("memory_result");
                require("pass".equals(memory.getString("status")), "Memory run did not pass");
                require(memory.getInt("qubits") == 4, "Memory UI ignored the small test size");
                require(memory.getInt("verified_rows") == 4, "Memory run skipped validation");
                require(memory.getLong("state_bytes") == 68L,
                        "Four-qubit packed state has an unexpected payload size");
                require(memory.getInt("completed_gates") == memory.getInt("total_gates"),
                        "Memory run did not finish its circuit");
                requireTag("memory_cancel");
                screenshot("xlab-carga.png");
            });

            check("run_is_in_logbook", () -> {
                click("tab_bitacora");
                require(main(() -> find("bitacora_content").isShown()),
                        "Logbook content is not visible after selecting its tab");
                String entries = requireText("bitacora_content");
                require(entries.contains("Circuito") && entries.contains("CompactCore"),
                        "Logbook did not retain the executed native circuit");
                click("tab_mesa");
                requireTag("run_circuit");
            });

            check("object_model_independent_checks", () -> {
                ObjectSceneModelChecks.Result result = ObjectSceneModelChecks.runAll();
                require(result.passed >= 400, result.summary);
            });

            check("objects_open_edits_and_persistence", () -> {
                ActivityMonitor monitor = addMonitor("com.mundox.lab.ObjectLabActivity", null, false);
                try {
                    click("open_objects");
                    Activity opened = waitForMonitorWithTimeout(monitor, DEADLINE_MS);
                    require(opened != null, "Objects entry did not open its activity");
                    activity = opened;
                } finally { removeMonitor(monitor); }
                waitForIdleSync(); awaitState("objects_status", "ready");
                select("objects_count", 0); setText("objects_seed", "-2147483648"); click("objects_new");
                click("objects_clear"); setText("objects_id", "528"); click("objects_select"); click("objects_raise");
                require(text("objects_selected").contains("1 cambios"), "Object edit was not recorded");
                main(() -> { activity.finish(); return null; }); waitForIdleSync();
                Intent relaunch = new Intent(Intent.ACTION_MAIN);
                relaunch.setComponent(new ComponentName(getTargetContext(), "com.mundox.lab.ObjectLabActivity"));
                relaunch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                activity = startActivitySync(relaunch); waitForIdleSync(); awaitState("objects_status", "ready");
                require(text("objects_selected").contains("1 cambios"), "Edits did not survive activity replacement");
                require("-2147483648".equals(text("objects_seed")), "Signed seed did not persist");
                click("objects_scatter"); click("objects_scatter");
                require(text("objects_selected").contains("513 cambios"), "Repeated +256 did not add new sparse changes");
            });

            check("objects_gpu_image_equivalence_and_edit_effect", () -> {
                click("objects_clear"); click("objects_verify"); awaitReady("objects_status", "objects_result");
                JSONObject baseline = requireNativeJson("objects_result");
                require(baseline.getBoolean("passed") && baseline.getInt("covered_pixels") > 32,
                        "Image A/B must render real object coverage");
                require(baseline.getBoolean("sampled_transforms_match_cpu"), "GPU transforms do not match CPU");
                click("objects_scatter"); click("objects_verify"); awaitReady("objects_status", "objects_result");
                JSONObject edited = requireNativeJson("objects_result");
                require(edited.getBoolean("passed"), "Edited procedural/explicit scene differs");
                require(!baseline.getString("hash_fnv64").equals(edited.getString("hash_fnv64")),
                        "Sparse edits did not change the rendered image");
                screenshot("xlab-objetos.png");
                // Signed boundaries, zero, sparse edits and a hidden object all
                // pass through the actual vertex program and framebuffer.
                for (String seed : new String[]{"2147483647", "0", "-1"}) {
                    setText("objects_seed", seed); click("objects_new"); click("objects_scatter");
                    setText("objects_id", "528"); click("objects_select"); click("objects_hide");
                    click("objects_verify"); awaitReady("objects_status", "objects_result");
                    JSONObject actual = requireNativeJson("objects_result");
                    require(actual.getBoolean("passed") && actual.getBoolean("sampled_transforms_match_cpu"),
                            "GPU seed, edit or hidden-object mismatch for seed " + seed);
                    saveEvidence("xlab-objetos-imagen-" + seed + ".json", actual.toString(2));
                }
            });

            check("objects_benchmark_cancel_and_recovery", () -> {
                click("objects_benchmark"); awaitDescriptionContains("objects_stats", "benchmarking=true");
                click("objects_stop"); awaitState("objects_status", "cancelled");
                click("objects_verify"); awaitReady("objects_status", "objects_result");
                require(requireNativeJson("objects_result").getBoolean("passed"), "GPU check failed after benchmark cancellation");
            });

            check("objects_benchmark_both_routes", () -> {
                click("objects_benchmark"); awaitReady("objects_status", "objects_result");
                JSONObject measurement = requireNativeJson("objects_result");
                require("completed".equals(measurement.getString("status")), "Benchmark did not complete");
                for (String route : new String[]{"procedural", "explicit"}) {
                    JSONObject sample = measurement.getJSONObject(route);
                    require(sample.getInt("samples") >= 5, "Missing real frame samples for " + route);
                    require(sample.getDouble("p50_ms") > 0 && sample.getDouble("p95_ms") >= sample.getDouble("p50_ms"),
                            "Invalid frame distribution for " + route);
                }
                saveEvidence("xlab-objetos-benchmark.json", measurement.toString(2));
            });

            check("objects_extreme_cancel_restores_scene", () -> {
                String priorSeed = text("objects_seed"), priorEdits = text("objects_selected");
                click("objects_extreme"); awaitDescriptionContains("objects_stats", "benchmarking=true");
                click("objects_stop"); awaitState("objects_status", "cancelled");
                require(priorSeed.equals(text("objects_seed")), "Scale campaign changed the user's seed");
                require(priorEdits.equals(text("objects_selected")), "Scale campaign did not restore the user's edits");
                JSONObject scale = requireNativeJson("objects_result");
                require("cancelled".equals(scale.getString("status")), "Scale cancellation was not exported");
            });

            check("objects_extreme_automatic_limits_and_restore", () -> {
                String prior = text("objects_selected");
                click("objects_extreme");
                awaitStateUntil("objects_status", "ready", SystemClock.uptimeMillis() + 75_000L);
                JSONObject scale = requireNativeJson("objects_result");
                require("progressive_scale".equals(scale.getString("type")), "Missing scale report");
                require(scale.getJSONArray("stages").length() > 0, "No actual scale stage was measured");
                require(scale.getLong("elapsed_ms") < 70_000L, "Scale duration was not bounded");
                require(prior.equals(text("objects_selected")), "Automatic scale did not restore edits");
                saveEvidence("xlab-objetos-escala.json", scale.toString(2));
                screenshot("xlab-objetos-escala.png");
            });

            check("objects_real_gl_error_recovers_without_losing_selection", () -> {
                loadObjects(0, "2026", SystemClock.uptimeMillis() + DEADLINE_MS);
                setText("objects_id", "528"); click("objects_select"); click("objects_raise");
                String before = text("objects_selected");
                // Deliberately generate a real GL_INVALID_ENUM on the rendering
                // context. Do not read glGetError here: the live renderer must
                // detect, report and suspend the failing draw itself.
                runGl(() -> GLES30.glEnable(-1), DEADLINE_MS);
                awaitState("objects_status", "error");
                require(text("objects_result").contains("0x500"), "Real GL_INVALID_ENUM was not reported");
                require("error".equals(description("objects_view_state")), "GPU failure left no visible warning");
                require(latestObjects().drawSuspended, "Draw failure did not suspend the renderer");
                click("objects_recenter");
                awaitState("objects_status", "ready");
                awaitObjectStats(1024, 0, SystemClock.uptimeMillis() + DEADLINE_MS);
                require("528".equals(text("objects_id")) && before.equals(text("objects_selected")),
                        "Recentering lost the selection or persistent edit");
                runGl(() -> GLES30.glEnable(-1), DEADLINE_MS);
                awaitState("objects_status", "error");
                require(text("objects_result").contains("0x500"), "Second GL error was not reported");
                loadObjects(0, "2027", SystemClock.uptimeMillis() + DEADLINE_MS);
                require(!latestObjects().drawSuspended, "New scene did not recover a suspended draw");
                require("528".equals(text("objects_id")), "Loading the smaller profile lost a valid selection");
            });

            check("objects_65536_visible_and_max_zoom_uses_real_framebuffer", () -> {
                try {
                    loadObjects(3, "2026", SystemClock.uptimeMillis() + 90_000L);
                    click("objects_verify");
                    awaitReadyUntil("objects_status", "objects_result", SystemClock.uptimeMillis() + 90_000L);
                    JSONObject normal = requireNativeJson("objects_result");
                    requireObjectCoverage(normal, 65536, 2026);
                    saveEvidence("xlab-65536.json", normal.toString(2));
                    ObjectSceneRenderer.Stats centered = awaitObjectStats(65536, 0,
                            SystemClock.uptimeMillis() + 90_000L);
                    JSONObject normalCapture = auxiliaryScreenshot("xlab-65536.png");
                    normal.put("screenshot_captured", normalCapture.getBoolean("captured"));
                    normal.put("screenshot_diagnostic", normalCapture);
                    saveEvidence("xlab-65536.json", normal.toString(2));
                    ObjectSceneRenderer objectRenderer = objectRenderer();
                    runGl(() -> objectRenderer.zoom(4f), 90_000L);
                    awaitObjectStats(65536, centered.cameraDistance * 3.99f,
                            SystemClock.uptimeMillis() + 90_000L);
                    click("objects_verify");
                    awaitReadyUntil("objects_status", "objects_result", SystemClock.uptimeMillis() + 90_000L);
                    JSONObject far = requireNativeJson("objects_result");
                    requireObjectCoverage(far, 65536, 2026);
                    double distance = far.getDouble("camera_distance");
                    require(distance >= centered.cameraDistance * 3.99,
                            "The regression did not actually exercise maximum zoom distance");
                    require(far.getDouble("projection_near") >= distance * .0099,
                            "Large-distance projection reverted to an imprecise fixed near plane");
                    require(far.getDouble("projection_far") > distance, "Far plane clips the centered scene");
                    saveEvidence("xlab-65536-maxzoom.json", far.toString(2));
                    awaitObjectStats(65536, centered.cameraDistance * 3.99f,
                            SystemClock.uptimeMillis() + 90_000L);
                    JSONObject farCapture = auxiliaryScreenshot("xlab-65536-maxzoom.png");
                    far.put("screenshot_captured", farCapture.getBoolean("captured"));
                    far.put("screenshot_diagnostic", farCapture);
                    saveEvidence("xlab-65536-maxzoom.json", far.toString(2));
                } finally {
                    // Never leave this larger workload running after an assertion.
                    loadObjects(0, "2026", SystemClock.uptimeMillis() + 90_000L);
                }
            });

            check("objects_clipboard_scene_migration_and_invalid_text_is_atomic", () -> {
                loadObjects(1, "-12345", SystemClock.uptimeMillis() + DEADLINE_MS);
                setText("objects_id", "777"); click("objects_select"); click("objects_raise");
                setText("objects_id", "778"); click("objects_select"); click("objects_hide");
                click("objects_copy");
                String backup = clipboardText();
                JSONObject original = new JSONObject(backup);
                require(original.getInt("count") == 4096 && original.getInt("seed") == -12345
                                && original.getInt("edits") == 2,
                        "Migration report lacks the original seed, profile or edits");
                String originalScene = original.getString("scene");
                loadObjects(0, "9", SystemClock.uptimeMillis() + DEADLINE_MS);
                require(!"-12345".equals(text("objects_seed")), "Migration setup did not reset the scene");
                setClipboardText(backup); click("objects_restore");
                awaitState("objects_status", "ready");
                awaitObjectStats(4096, 0, SystemClock.uptimeMillis() + DEADLINE_MS);
                require("-12345".equals(text("objects_seed")), "Restoring a report lost the signed seed");
                require("2".equals(description("objects_selected")), "Restoring a report lost its sparse edits");
                click("objects_copy");
                JSONObject restored = new JSONObject(clipboardText());
                require(originalScene.equals(restored.getString("scene")),
                        "Migration changed the exact exported scene and edits");
                String selected = text("objects_selected");
                setClipboardText("esto no contiene una escena Mundo X"); click("objects_restore");
                awaitState("objects_status", "error");
                require("-12345".equals(text("objects_seed")) && selected.equals(text("objects_selected")),
                        "Invalid clipboard input changed the current scene");
                click("objects_copy");
                JSONObject unchanged = new JSONObject(clipboardText());
                require(originalScene.equals(unchanged.getString("scene")),
                        "Rejected migration partially changed the model");
                saveEvidence("xlab-objetos-migration.json", unchanged.toString(2));
            });

            check("objects_quiet_requests_draws_and_animation_resumes", () -> {
                loadObjects(0, "2026", SystemClock.uptimeMillis() + DEADLINE_MS);
                if (text("objects_animate").contains("activo")) click("objects_animate");
                awaitObjectAnimation(false, SystemClock.uptimeMillis() + DEADLINE_MS);
                require(main(() -> ((GLSurfaceView) find("objects_surface")).getRenderMode())
                                == GLSurfaceView.RENDERMODE_WHEN_DIRTY,
                        "Quiet field did not select demand rendering");
                long quietBefore = rendererFrames();
                SystemClock.sleep(600L);
                long quietAfter = rendererFrames();
                require(quietAfter == quietBefore, "Quiet field kept submitting identical frames");
                setText("objects_id", "528"); click("objects_select"); click("objects_raise");
                awaitObjectStats(1024, 0, SystemClock.uptimeMillis() + DEADLINE_MS);
                long edited = rendererFrames();
                require(edited > quietAfter, "Editing a quiet field did not request an actual draw");
                click("objects_verify"); awaitReady("objects_status", "objects_result");
                JSONObject image = requireNativeJson("objects_result");
                requireObjectCoverage(image, 1024, 2026);
                click("objects_benchmark"); awaitReady("objects_status", "objects_result");
                JSONObject measured = requireNativeJson("objects_result");
                require(measured.getBoolean("passed") && "completed".equals(measured.getString("status")),
                        "Benchmark did not keep requesting measured frames while the field was quiet");
                awaitObjectAnimation(false, SystemClock.uptimeMillis() + DEADLINE_MS);
                click("objects_animate");
                awaitObjectAnimation(true, SystemClock.uptimeMillis() + DEADLINE_MS);
                long activeStart = rendererFrames();
                long activeAfter = awaitRendererFrames(activeStart + 2, SystemClock.uptimeMillis() + DEADLINE_MS);
                click("objects_animate");
                awaitObjectAnimation(false, SystemClock.uptimeMillis() + DEADLINE_MS);
                long quietFinal = rendererFrames();
                SystemClock.sleep(600L);
                long quietFinalAfter = rendererFrames();
                require(quietFinalAfter == quietFinal, "Stopping animation did not settle back to demand rendering");
                JSONObject demand = new JSONObject();
                demand.put("count", 1024); demand.put("seed", 2026);
                demand.put("quiet_frames_before", quietBefore); demand.put("quiet_frames_after_600ms", quietAfter);
                demand.put("frames_after_edit", edited); demand.put("image_ab", image);
                demand.put("benchmark_when_quiet", measured);
                demand.put("active_frames_start", activeStart); demand.put("active_frames_after", activeAfter);
                demand.put("quiet_final_frames", quietFinal); demand.put("quiet_final_frames_after_600ms", quietFinalAfter);
                saveEvidence("xlab-objetos-demand.json", demand.toString(2));
            });
        } catch (Throwable error) {
            recordFailure("launch_or_runner", error);
        } finally {
            Bundle results = new Bundle();
            results.putInt("tests_passed", passed);
            results.putInt("tests_failed", failed);
            results.putString("screenshots", join(screenshots));
            results.putString("diagnostic_notes", join(diagnosticNotes));
            results.putString("failures", join(failures));
            results.putString("stream", "\nXLab: " + passed + " passed, " + failed + " failed.\n"
                    + join(failures) + "\n");
            if (failed != 0) {
                results.putString("shortMsg", "XLab device checks failed");
            }
            finish(failed == 0 ? Activity.RESULT_OK : Activity.RESULT_CANCELED, results);
        }
    }

    private void checkMillionObjects() {
        check("objects_1048576_visible_and_actual_routes_match", () -> {
            try {
                if (text("objects_animate").contains("activo")) click("objects_animate");
                loadObjects(0, "2026", SystemClock.uptimeMillis() + DEADLINE_MS);
                ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
                ((ActivityManager) getTargetContext().getSystemService(Activity.ACTIVITY_SERVICE)).getMemoryInfo(memory);
                JSONObject preflight = new JSONObject();
                preflight.put("count", 1048576); preflight.put("seed", 2026);
                preflight.put("total_memory_bytes", memory.totalMem);
                preflight.put("available_memory_bytes", memory.availMem);
                preflight.put("low_memory", memory.lowMemory);
                preflight.put("production_estimate_bytes", 64L * 1048576 + 8L * 1024 * 1024);
                saveEvidence("xlab-1048576-preflight.json", preflight.toString(2));

                select("objects_count", 5); setText("objects_seed", "2026"); click("objects_new");
                long visibleDeadline = SystemClock.uptimeMillis() + 180_000L;
                // Use status text for a useful failure if the normal memory or
                // thermal guard rejects the scene. This test never bypasses it.
                awaitReadyUntil("objects_status", "objects_status", visibleDeadline);
                ObjectSceneRenderer.Stats live = awaitObjectStats(1048576, 0, visibleDeadline);
                require(live.count == 1048576 && "visible".equals(live.visibility) && !live.drawSuspended,
                        "The million-object test never rendered a visible live scene");
                click("objects_verify");
                awaitReadyUntil("objects_status", "objects_result", SystemClock.uptimeMillis() + 240_000L);
                JSONObject image = requireNativeJson("objects_result");
                requireObjectCoverage(image, 1048576, 2026);
                require(image.getDouble("projection_near") >= image.getDouble("camera_distance") * .0099,
                        "Million-object projection lost its distance-scaled near plane");
                image.put("device_preflight", preflight);
                image.put("live_visibility_confirmed_before_verification", true);
                image.put("context_frames_before_verification", live.frames);
                String note = "Global UiAutomation screenshot intentionally skipped for 1,048,576 objects: "
                        + "avoid compositor load; actual GL visibility, framebuffer readback and transforms are mandatory.";
                diagnosticNotes.add(note);
                image.put("screenshot_captured", false); image.put("screenshot_skipped", true);
                image.put("screenshot_skip_reason", note);
                saveEvidence("xlab-1048576.json", image.toString(2));
            } finally {
                // On both success and assertion failure, release the large
                // reference allocation by loading and visibly drawing 1,024.
                loadObjects(0, "2026", SystemClock.uptimeMillis() + 180_000L);
                ObjectSceneRenderer.Stats small = latestObjects();
                require(small.count == 1024 && "visible".equals(small.visibility) && !small.drawSuspended,
                        "The million-object regression did not return to the small visible scene");
            }
        });
    }

    private void check(String name, CheckedAction action) {
        try {
            action.run();
            passed++;
            Bundle status = new Bundle();
            status.putString("stream", "\nPASS " + name + "\n");
            status.putString("test", name);
            sendStatus(0, status);
        } catch (Throwable error) {
            recordFailure(name, error);
        }
    }

    private void recordFailure(String name, Throwable error) {
        failed++;
        StringWriter writer = new StringWriter();
        error.printStackTrace(new PrintWriter(writer));
        String detail = name + ": " + writer;
        if (detail.length() > 6_000) detail = detail.substring(0, 6_000);
        failures.add(detail);
        Bundle status = new Bundle();
        status.putString("stream", "\nFAIL " + detail + "\n");
        status.putString("test", name);
        sendStatus(-2, status);
    }

    private void requireTag(String tag) {
        main(() -> {
            find(tag);
            return null;
        });
    }

    private void click(String tag) {
        main(() -> {
            View view = find(tag);
            if (!view.isShown()) throw new AssertionError("Hidden control: " + tag);
            if (!view.isEnabled()) throw new AssertionError("Disabled control: " + tag);
            if (!view.performClick()) throw new AssertionError("No click action: " + tag);
            return null;
        });
        waitForIdleSync();
    }

    private void preset(String tag) {
        click(tag);
        awaitReady("circuit_status", "circuit_result");
    }

    private void setText(String tag, String value) {
        main(() -> {
            View view = find(tag);
            if (!(view instanceof EditText)) throw new AssertionError("Not editable: " + tag);
            ((EditText) view).setText(value);
            return null;
        });
        waitForIdleSync();
    }

    private void select(String tag, int position) {
        main(() -> {
            View view = find(tag);
            if (!(view instanceof Spinner)) throw new AssertionError("Not a selector: " + tag);
            ((Spinner) view).setSelection(position);
            return null;
        });
        waitForIdleSync();
    }

    private String requireText(String tag) {
        String text = text(tag);
        if (text.trim().isEmpty()) throw new AssertionError("Empty text: " + tag);
        return text;
    }

    private String text(String tag) {
        return main(() -> {
            View view = find(tag);
            if (!(view instanceof TextView)) throw new AssertionError("Not text: " + tag);
            return ((TextView) view).getText().toString();
        });
    }

    private void awaitReady(String statusTag, String resultTag) {
        awaitReadyUntil(statusTag, resultTag, SystemClock.uptimeMillis() + DEADLINE_MS);
    }

    private void awaitReadyUntil(String statusTag, String resultTag, long deadline) {
        String state = "";
        while (SystemClock.uptimeMillis() < deadline) {
            state = main(() -> {
                CharSequence description = find(statusTag).getContentDescription();
                return description == null ? "" : description.toString();
            });
            if ("ready".equals(state)) return;
            if ("error".equals(state) || "cancelled".equals(state)) {
                if ("objects_result".equals(resultTag)) {
                    try { saveEvidence("xlab-objetos-failed-" + SystemClock.uptimeMillis() + ".json", text(resultTag)); }
                    catch (Exception ignored) { }
                }
                throw new AssertionError(statusTag + "=" + state + ": " + text(resultTag));
            }
            SystemClock.sleep(75L);
        }
        throw new AssertionError("Timed out waiting for " + statusTag + "; state=" + state);
    }

    private void awaitState(String statusTag, String expected) {
        awaitStateUntil(statusTag, expected, SystemClock.uptimeMillis() + DEADLINE_MS);
    }

    private void awaitDescriptionContains(String tag, String expected) {
        long deadline = SystemClock.uptimeMillis() + DEADLINE_MS;
        while (SystemClock.uptimeMillis() < deadline) {
            String description = main(() -> String.valueOf(find(tag).getContentDescription()));
            if (description.contains(expected)) return;
            SystemClock.sleep(75L);
        }
        throw new AssertionError("Timed out waiting for " + tag + " to contain " + expected);
    }

    private String description(String tag) {
        return main(() -> String.valueOf(find(tag).getContentDescription()));
    }

    private static Object privateField(Object target, String name) {
        try {
            Field field = target.getClass().getDeclaredField(name);
            field.setAccessible(true);
            return field.get(target);
        } catch (ReflectiveOperationException error) {
            throw new AssertionError("Cannot inspect " + name + " for the device regression", error);
        }
    }

    private ObjectSceneRenderer objectRenderer() {
        return main(() -> (ObjectSceneRenderer) privateField(activity, "renderer"));
    }

    private ObjectSceneRenderer.Stats latestObjects() {
        ObjectSceneRenderer.Stats value = main(() -> (ObjectSceneRenderer.Stats) privateField(activity, "latest"));
        if (value == null) throw new AssertionError("Renderer has not delivered statistics");
        return value;
    }

    private ObjectSceneRenderer.Stats awaitObjectStats(int expectedCount, float minimumDistance, long deadline) {
        ObjectSceneRenderer.Stats latest = null;
        while (SystemClock.uptimeMillis() < deadline) {
            latest = main(() -> (ObjectSceneRenderer.Stats) privateField(activity, "latest"));
            if (latest != null && latest.count == expectedCount && !latest.drawSuspended
                    && "visible".equals(latest.visibility) && latest.cameraDistance >= minimumDistance) return latest;
            if ("error".equals(description("objects_status")))
                throw new AssertionError("GPU scene failed before becoming visible: " + text("objects_result"));
            SystemClock.sleep(75L);
        }
        throw new AssertionError("No visible GPU result for " + expectedCount + " objects; last="
                + (latest == null ? "none" : latest.count + "/" + latest.visibility + "/" + latest.cameraDistance));
    }

    private void awaitObjectAnimation(boolean enabled, long deadline) {
        while (SystemClock.uptimeMillis() < deadline) {
            ObjectSceneRenderer.Stats value = main(() -> (ObjectSceneRenderer.Stats) privateField(activity, "latest"));
            if (value != null && value.animationEnabled == enabled && !value.drawSuspended
                    && "visible".equals(value.visibility) && !value.benchmarking) return;
            SystemClock.sleep(75L);
        }
        throw new AssertionError("Rendering policy did not settle with animation=" + enabled);
    }

    private long rendererFrames() throws Exception {
        ObjectSceneRenderer renderer = objectRenderer();
        AtomicReference<Long> count = new AtomicReference<>();
        // Inspect the live GL counter after prior rendering commands complete.
        // queueEvent intentionally does not request a draw in WHEN_DIRTY mode.
        runGl(() -> count.set((Long) privateField(renderer, "frames")), DEADLINE_MS);
        return count.get();
    }

    private long awaitRendererFrames(long minimum, long deadline) throws Exception {
        while (SystemClock.uptimeMillis() < deadline) {
            long count = rendererFrames();
            if (count >= minimum) return count;
            SystemClock.sleep(75L);
        }
        throw new AssertionError("Animation did not submit at least " + minimum + " live frames");
    }

    private void loadObjects(int profile, String seed, long deadline) {
        select("objects_count", profile); setText("objects_seed", seed); click("objects_new");
        awaitStateUntil("objects_status", "ready", deadline);
        int[] counts = {1024, 4096, 16384, 65536, 262144, 1048576};
        awaitObjectStats(counts[profile], 0, deadline);
    }

    private void runGl(CheckedAction action, long timeoutMs) throws Exception {
        CountDownLatch complete = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        main(() -> {
            ((GLSurfaceView) find("objects_surface")).queueEvent(() -> {
                try { action.run(); }
                catch (Throwable failure) { error.set(failure); }
                finally { complete.countDown(); }
            });
            return null;
        });
        long deadline = SystemClock.uptimeMillis() + timeoutMs;
        while (!complete.await(250L, TimeUnit.MILLISECONDS)) {
            if (SystemClock.uptimeMillis() >= deadline) throw new AssertionError("GL thread command timed out");
        }
        if (error.get() != null) throw new AssertionError("GL thread regression command failed", error.get());
    }

    private static void requireObjectCoverage(JSONObject result, int count, int seed) throws Exception {
        require(result.getBoolean("passed"), "Actual render routes failed the image comparison");
        require(result.getInt("count") == count && result.getInt("seed") == seed,
                "Framebuffer result belongs to a different scene");
        require(result.getInt("covered_pixels") > 16 && result.getInt("reference_covered_pixels") > 16,
                "Image equivalence passed without visible object coverage in both routes");
        require(result.getBoolean("sampled_transforms_match_cpu"), "GPU transforms diverged at the larger profile");
        require(result.getInt("depth_bits") > 0 && result.getInt("viewport_width") > 0
                        && result.getInt("viewport_height") > 0,
                "Image regression omitted the actual depth or viewport diagnostics");
    }

    private String clipboardText() {
        return main(() -> {
            ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Activity.CLIPBOARD_SERVICE);
            require(clipboard.hasPrimaryClip() && clipboard.getPrimaryClip() != null
                    && clipboard.getPrimaryClip().getItemCount() > 0, "Missing copied scene report");
            return clipboard.getPrimaryClip().getItemAt(0).coerceToText(activity).toString();
        });
    }

    private void setClipboardText(String text) {
        main(() -> {
            ((ClipboardManager) activity.getSystemService(Activity.CLIPBOARD_SERVICE))
                    .setPrimaryClip(ClipData.newPlainText("XLab regression scene", text));
            return null;
        });
        waitForIdleSync();
    }

    private void saveEvidence(String name, String content) throws Exception {
        File directory = getTargetContext().getExternalFilesDir(null);
        if (directory == null) throw new AssertionError("Evidence directory unavailable");
        try (FileOutputStream stream = new FileOutputStream(new File(directory, name))) {
            stream.write(content.getBytes("UTF-8"));
        }
    }

    private void awaitStateUntil(String statusTag, String expected, long deadline) {
        String state = "";
        while (SystemClock.uptimeMillis() < deadline) {
            state = main(() -> {
                CharSequence description = find(statusTag).getContentDescription();
                return description == null ? "" : description.toString();
            });
            if (expected.equals(state)) return;
            SystemClock.sleep(75L);
        }
        throw new AssertionError("Expected " + statusTag + "=" + expected + "; got " + state);
    }

    private void awaitNativeMemoryRunning(long deadline) throws Exception {
        while (SystemClock.uptimeMillis() < deadline) {
            JSONObject progress = new JSONObject(NativeCore.labMemoryProgress());
            if (progress.optInt("qubits") == 16320 && progress.optInt("budget_mib") == 64) {
                String status = progress.optString("status");
                if (progress.optBoolean("running") && "evolving".equals(status)
                        && progress.optInt("completed_gates") > 0) return;
                if ("pass".equals(status)) {
                    throw new AssertionError("The bounded workload finished before cancellation; "
                            + "real cancellation was not demonstrated");
                }
                if ("error".equals(status)) {
                    throw new AssertionError("Native memory workload failed before cancellation: "
                            + progress);
                }
            }
            SystemClock.sleep(75L);
        }
        throw new AssertionError("Native memory workload never entered an active state");
    }

    private JSONObject requireNativeJson(String tag) throws Exception {
        String raw = requireText(tag).trim();
        JSONObject result = new JSONObject(raw);
        if (result.has("ok") && !result.optBoolean("ok", false)) {
            throw new AssertionError("Native result rejected: " + raw);
        }
        if ("error".equalsIgnoreCase(result.optString("status", ""))) {
            throw new AssertionError("Native result error: " + raw);
        }
        if (result.has("error") && !result.isNull("error")) {
            Object error = result.get("error");
            if (!Boolean.FALSE.equals(error) && !"".equals(String.valueOf(error))) {
                throw new AssertionError("Native error field: " + raw);
            }
        }
        return result;
    }

    private static int total(JSONArray counts) throws Exception {
        int total = 0;
        for (int i = 0; i < counts.length(); i++) {
            int count = counts.getInt(i);
            require(count >= 0, "A histogram count is negative");
            total += count;
        }
        return total;
    }

    private static void requireProbability(double actual, double expected, String name) {
        require(!Double.isNaN(actual) && Math.abs(actual - expected) <= 1e-12,
                name + ": expected " + expected + ", observed " + actual);
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private View find(String tag) {
        View view = activity.getWindow().getDecorView().findViewWithTag(tag);
        if (view == null) throw new AssertionError("Missing view tag: " + tag);
        return view;
    }

    private JSONObject auxiliaryScreenshot(String name) throws Exception {
        JSONObject diagnostic = new JSONObject();
        JSONArray notes = new JSONArray();
        boolean captured = false;
        int attempts = 0;
        // UiAutomation may return null while a large GL frame is in flight.
        // Three attempts are the complete retry budget; a window screenshot
        // never substitutes for the mandatory GL readback assertions.
        for (int attempt = 1; attempt <= 3; attempt++) {
            attempts = attempt;
            try {
                screenshot(name);
                captured = true;
                break;
            } catch (Exception | AssertionError error) {
                String note = "Auxiliary screenshot " + name + " attempt " + attempt
                        + "/3: " + error.getClass().getSimpleName() + ": " + error.getMessage();
                diagnosticNotes.add(note); notes.put(note);
            }
            if (attempt < 3) SystemClock.sleep(200L);
        }
        diagnostic.put("file", name); diagnostic.put("captured", captured);
        diagnostic.put("attempts", attempts); diagnostic.put("max_attempts", 3);
        diagnostic.put("required_for_gpu_correctness", false);
        diagnostic.put("diagnostic_notes", notes);
        if (!captured) {
            Bundle status = new Bundle();
            status.putString("stream", "\nNOTE Auxiliary screenshot unavailable after 3 attempts: " + name
                    + "; GPU correctness remains determined by query and framebuffer readback.\n");
            sendStatus(0, status);
        }
        return diagnostic;
    }

    private void screenshot(String name) throws Exception {
        main(() -> {
            scrollTop(activity.getWindow().getDecorView());
            return null;
        });
        waitForIdleSync();
        SystemClock.sleep(100L);
        Bitmap bitmap = getUiAutomation().takeScreenshot();
        if (bitmap == null) throw new AssertionError("Screenshot unavailable: " + name);
        File directory = getTargetContext().getExternalFilesDir(null);
        if (directory == null) throw new AssertionError("Screenshot directory unavailable");
        if (!directory.isDirectory() && !directory.mkdirs()) {
            throw new AssertionError("Cannot create screenshot directory");
        }
        File destination = new File(directory, name);
        try (FileOutputStream output = new FileOutputStream(destination)) {
            if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, output)) {
                throw new AssertionError("Screenshot encoding failed: " + name);
            }
        } finally {
            bitmap.recycle();
        }
        if (destination.length() == 0L) throw new AssertionError("Empty screenshot: " + name);
        screenshots.add(destination.getAbsolutePath());
    }

    private void scrollTop(View view) {
        if (view instanceof ScrollView) ((ScrollView) view).scrollTo(0, 0);
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) scrollTop(group.getChildAt(i));
        }
    }

    private <T> T main(MainValue<T> action) {
        AtomicReference<T> result = new AtomicReference<>();
        AtomicReference<Throwable> error = new AtomicReference<>();
        runOnMainSync(() -> {
            try {
                result.set(action.get());
            } catch (Throwable failure) {
                error.set(failure);
            }
        });
        if (error.get() != null) {
            if (error.get() instanceof AssertionError) throw (AssertionError) error.get();
            throw new AssertionError("Main-thread UI action failed", error.get());
        }
        return result.get();
    }

    private static String join(List<String> values) {
        StringBuilder out = new StringBuilder();
        for (String value : values) {
            if (out.length() > 0) out.append('\n');
            out.append(value);
        }
        return out.toString();
    }

    private interface MainValue<T> { T get(); }
    private interface CheckedAction { void run() throws Exception; }
}
