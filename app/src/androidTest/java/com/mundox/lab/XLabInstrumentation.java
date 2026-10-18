package com.mundox.lab;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.ComponentName;
import android.content.Intent;
import android.graphics.Bitmap;
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
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;

/** Device-level smoke tests. Runs with the platform runner, without test libraries. */
public final class XLabInstrumentation extends Instrumentation {
    private static final long DEADLINE_MS = 20_000L;
    private Activity activity;
    private int passed;
    private int failed;
    private final List<String> failures = new ArrayList<>();
    private final List<String> screenshots = new ArrayList<>();

    @Override
    public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
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
                    "com.mundox.lab.XLabActivity"));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            activity = startActivitySync(intent);
            waitForIdleSync();

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
                click("objects_scatter"); click("objects_verify"); awaitReady("objects_status", "objects_result");
                JSONObject edited = requireNativeJson("objects_result");
                require(edited.getBoolean("passed"), "Edited procedural/explicit scene differs");
                require(!baseline.getString("hash_fnv64").equals(edited.getString("hash_fnv64")),
                        "Sparse edits did not change the rendered image");
                screenshot("xlab-objetos.png");
                // A second signed boundary exercises GLSL uint seed transfer.
                setText("objects_seed", "2147483647"); click("objects_new"); click("objects_scatter");
                click("objects_verify"); awaitReady("objects_status", "objects_result");
                require(requireNativeJson("objects_result").getBoolean("passed"), "Positive signed-boundary GPU seed mismatch");
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
        } catch (Throwable error) {
            recordFailure("launch_or_runner", error);
        } finally {
            Bundle results = new Bundle();
            results.putInt("tests_passed", passed);
            results.putInt("tests_failed", failed);
            results.putString("screenshots", join(screenshots));
            results.putString("failures", join(failures));
            results.putString("stream", "\nXLab: " + passed + " passed, " + failed + " failed.\n"
                    + join(failures) + "\n");
            if (failed != 0) {
                results.putString("shortMsg", "XLab device checks failed");
            }
            finish(failed == 0 ? Activity.RESULT_OK : Activity.RESULT_CANCELED, results);
        }
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
        long deadline = SystemClock.uptimeMillis() + DEADLINE_MS;
        String state = "";
        while (SystemClock.uptimeMillis() < deadline) {
            state = main(() -> {
                CharSequence description = find(statusTag).getContentDescription();
                return description == null ? "" : description.toString();
            });
            if ("ready".equals(state)) return;
            if ("error".equals(state) || "cancelled".equals(state)) {
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
