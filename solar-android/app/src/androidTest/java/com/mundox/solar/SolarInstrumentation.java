package com.mundox.solar;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.opengl.GLES30;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;
import org.json.JSONArray;
import org.json.JSONObject;

/** Tests the shipping Activity, native CompactCore, GL thread and real framebuffers. */
public final class SolarInstrumentation extends Instrumentation {
    private static final String TAG = "CosmicSolarTests";
    private static final int EXPECTED_TESTS = 13;
    private static final int TEST_QUALITY = 640;
    private static final long FRAME_TIMEOUT_MS = 30_000;
    private static final double TEST_JD = 2461331.0;
    private SolarActivity activity;
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
            this.name = name; this.seconds = seconds; this.error = error;
        }
    }

    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }

    @Override public void onStart() {
        try {
            evidence = new File(getTargetContext().getFilesDir(), "solar-evidence");
            expect(evidence.isDirectory() || evidence.mkdirs(), "Cannot create solar evidence directory");
            expect(getTargetContext().getSharedPreferences("solar", 0).edit().clear().commit(),
                    "Cannot prepare isolated first-launch preferences");
            metrics.put("androidSdk", Build.VERSION.SDK_INT);
            metrics.put("androidRelease", Build.VERSION.RELEASE);
            metrics.put("abis", new JSONArray(Arrays.asList(Build.SUPPORTED_ABIS)));
            metrics.put("performanceScope", "Software GLES emulator; not Samsung A16 performance measurements");
            metrics.put("physicalScope", "JPL analytical orbits; illustrative moon phases and magnetic geometries");
            Intent launch = new Intent(getTargetContext(), SolarActivity.class);
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            activity = (SolarActivity) startActivitySync(launch);
            ready(60_000);

            runCase("native_gles_launch_and_textures", this::launchAndTextures);
            runCase("astronomy_and_magnetic_classification", this::astronomyAndMagnetism);
            runCase("native_bell_and_readonly_observation", this::nativeBell);
            runCase("quantum_coupled_and_uncoupled_frames", this::quantumCoupling);
            runCase("intrinsic_and_induced_field_frames", this::magneticLayers);
            runCase("solar_wind_layer", this::solarWind);
            runCase("saturn_rings_and_moon_navigation", this::destinations);
            runCase("forward_reverse_and_paused_time", this::timeControls);
            runCase("real_scale_earth_focus", this::realScale);
            runCase("camera_rotation_zoom_pan_and_flight", this::cameraControls);
            runCase("resolution_budget", this::resolutionBudget);
            runCase("background_resume_preserves_state", this::backgroundResume);
            runCase("recreation_and_orientation_preserve_state", this::recreationAndOrientation);

            gl(() -> {
                try {
                    metrics.put("glRenderer", GLES30.glGetString(GLES30.GL_RENDERER));
                    metrics.put("glVersion", GLES30.glGetString(GLES30.GL_VERSION));
                    metrics.put("nativeQubits", QuantumSolarBridge.QUBITS);
                    metrics.put("nativeStateBytes", activity.bridge().stateBytes());
                    metrics.put("framesRendered", activity.renderer().frameCount);
                } catch (Exception error) { throw new RuntimeException(error); }
            });
        } catch (Throwable error) {
            results.add(new Result("instrumentation_setup_or_reporting", 0, error));
            Log.e(TAG, "Solar setup/reporting failed", error);
        }
        int failures = failures();
        Bundle output = new Bundle();
        try { writeReports(); }
        catch (Throwable error) {
            failures++; output.putString("report_error", trace(error));
            Log.e(TAG, "Cannot save solar reports", error);
        }
        boolean passed = failures == 0 && results.size() == EXPECTED_TESTS;
        output.putInt("tests_run", results.size()); output.putInt("tests_failed", failures);
        output.putString("stream", "\nCOSMIC_SOLAR_TESTS_" + (passed ? "PASSED" : "FAILED")
                + " tests=" + results.size() + " failures=" + failures + "\n");
        finish(passed ? Activity.RESULT_OK : Activity.RESULT_CANCELED, output);
    }

    private void launchAndTextures() throws Exception {
        expect(activity.bridge() != null && activity.surface() != null && activity.renderer() != null,
                "The real solar Activity did not attach the core and GLSurfaceView");
        waitUntil(() -> activity.hasWindowFocus(), 15_000,
                "Solar Activity is obscured by another window at launch");
        metrics.put("initialWindowFocused", true);
        expect(activity.bridge().stateBytes() > 0, "CompactCore native state is absent");
        prepare(3, false, false);
        SolarRenderer.Capture earth = frame();
        substantial(earth, "Earth launch");
        expect(changed(earth, frame()) == 0, "Paused surface is not deterministic");
        JSONArray textures = new JSONArray();
        for (String path : new String[]{"textures/earth.png", "textures/moon.jpg", "textures/jupiter.jpg"}) {
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inJustDecodeBounds = true;
            try (InputStream input = getTargetContext().getAssets().open(path)) {
                BitmapFactory.decodeStream(input, null, options);
            }
            expect(options.outWidth >= 512 && options.outHeight >= 256, "NASA map is missing or invalid: " + path);
            textures.put(new JSONObject().put("asset", path).put("width", options.outWidth).put("height", options.outHeight));
        }
        gl(() -> {
            expect(activity.renderer().loadedMapCount() == 3, "All three NASA maps were not uploaded to GLES");
            expect(activity.renderer().textureLayerForBody(3) >= 0 && activity.renderer().textureLayerForBody(5) >= 0
                    && activity.renderer().textureLayerForBody(9) >= 0, "A textured body has no actual GPU texture layer");
        });
        save("earth-surface.png", earth);
        metrics.put("packagedNasaTextures", textures);
        metrics.put("uploadedNasaTextures", 3);
        metrics.put("initialBrightPixels", bright(earth));
        metrics.put("deterministicPausedChangedPixels", 0);
    }

    private void astronomyAndMagnetism() throws Exception {
        expect(SolarData.BODIES.length == 23, "Expected Sun, eight planets and fourteen modeled moons");
        expect("Sol".equals(SolarData.BODIES[0].name), "Body zero is not the Sun");
        for (int i = 0; i < SolarData.BODIES.length; i++) {
            SolarData.Body body = SolarData.BODIES[i];
            expect(body.id == i && body.radiusKm > 0, "Body identity/radius is invalid");
            double[] p = SolarData.position(i, TEST_JD);
            for (double component : p) expect(Double.isFinite(component), "Non-finite astronomical position");
            if (i > 0 && i <= 8) expect(body.parent == 0, "A planet does not orbit the Sun");
            if (i > 8) expect(body.parent > 0 && body.parent < 9, "A moon has no planetary parent");
        }
        double[] sun = SolarData.position(0, TEST_JD);
        expect(sun[0] == 0 && sun[1] == 0 && sun[2] == 0, "Sun is not heliocentric origin");
        double earthAU = length(SolarData.position(3, SolarData.J2000));
        expect(earthAU > .98 && earthAU < 1.02, "Earth is not approximately one AU from the Sun");
        double moonKm = distance(SolarData.position(3, TEST_JD), SolarData.position(9, TEST_JD)) * SolarData.AU_KM;
        expect(moonKm > 350_000 && moonKm < 410_000, "Earth-Moon separation is outside the modeled orbit");
        expect(SolarData.BODIES[3].fieldType == SolarData.FIELD_INTRINSIC, "Earth has the wrong field model");
        expect(SolarData.BODIES[2].fieldType == SolarData.FIELD_INDUCED, "Venus must use an induced environment");
        expect(SolarData.BODIES[4].fieldType == SolarData.FIELD_INDUCED_CRUSTAL, "Mars must distinguish crustal and induced fields");
        expect(SolarData.BODIES[9].fieldType == SolarData.FIELD_NONE, "Moon must not have an invented global dipole");
        expect(SolarData.BODIES[12].fieldType == SolarData.FIELD_INTRINSIC, "Ganymede intrinsic field is absent");
        metrics.put("bodyCount", SolarData.BODIES.length).put("planetCount", 8).put("moonCount", 14);
        metrics.put("earthJ2000DistanceAU", earthAU).put("earthMoonDistanceKm", moonKm);
        metrics.put("fieldTypes", new JSONObject().put("earth", 1).put("venus", 2).put("mars", 3).put("moon", 0).put("ganymede", 1));
    }

    private void nativeBell() throws Exception {
        gl(() -> activity.bridge().setPreset(2));
        float[] expected = {0, 0, 0, 1, -1, 1, 1, 1};
        float[] actual = activity.bridge().verifyReadOnly();
        expect(actual.length == expected.length, "The native Pauli program is incomplete");
        for (int i = 0; i < expected.length; i++)
            expect(Math.abs(expected[i] - actual[i]) < 1e-6, "Bell expectation failed at Pauli " + i);
        long revision = activity.bridge().nativeRevision();
        float[] cached = activity.bridge().coefficients().clone();
        frame(); frame();
        expect(activity.bridge().nativeRevision() == revision, "Drawing frames mutated the native gate revision");
        expect(Arrays.equals(cached, activity.bridge().verifyReadOnly()), "Native read-only observations drifted");
        metrics.put("bellExpectations", floats(actual));
        metrics.put("observationPreservedNativeRevision", true);
    }

    private void quantumCoupling() throws Exception {
        prepare(3, true, false);
        gl(() -> activity.bridge().setPreset(0));
        double[] physicalBefore = SolarData.position(3, activity.renderer().julianDate());
        SolarRenderer.Capture baseline = frame();
        gl(() -> activity.bridge().setPreset(2));
        SolarRenderer.Capture entangled = frame();
        int coupled = changed(baseline, entangled);
        expect(coupled > 20, "The native preparation does not change its visible field traces");
        double[] physicalAfter = SolarData.position(3, activity.renderer().julianDate());
        expect(Arrays.equals(physicalBefore, physicalAfter), "Visual core preparation changed astronomical coordinates");
        save("earth-fields-base.png", baseline);
        save("earth-fields-entangled.png", entangled);
        main(() -> activity.fieldsForTest(false));
        gl(() -> { activity.renderer().setWind(false); activity.bridge().setPreset(0); });
        SolarRenderer.Capture uncoupledA = frame();
        gl(() -> activity.bridge().setPreset(2));
        SolarRenderer.Capture uncoupledB = frame();
        int uncoupled = changed(uncoupledA, uncoupledB);
        expect(uncoupled == 0, "Core preparation changes surfaces when its visual layers are disabled");
        metrics.put("coupledChangedPixels", coupled).put("uncoupledChangedPixels", uncoupled);
        metrics.put("quantumPreparationOrbitalDeltaAU", distance(physicalBefore, physicalAfter));
    }

    private void magneticLayers() throws Exception {
        JSONObject differences = new JSONObject();
        int[] targets = {3, 2, 4};
        String[] names = {"earth", "venus", "mars"};
        for (int i = 0; i < targets.length; i++) {
            // Start in the wider field view; toggling the layer must not move the camera.
            prepare(targets[i], true, false);
            frame();
            main(() -> activity.fieldsForTest(false));
            SolarRenderer.Capture surface = frame();
            main(() -> activity.fieldsForTest(true));
            SolarRenderer.Capture fields = frame();
            int delta = changed(surface, fields);
            expect(delta > 20, "Magnetic layer produced no visible geometry for " + names[i]);
            substantial(fields, names[i] + " field layer");
            differences.put(names[i], delta);
            if (targets[i] != 3) save(names[i] + "-magnetic-environment.png", fields);
        }
        expectHeading("Marte");
        metrics.put("fieldToggleChangedPixels", differences);
    }

    private void solarWind() throws Exception {
        prepare(0, false, false);
        SolarRenderer.Capture noWind = frame();
        gl(() -> activity.renderer().setWind(true));
        SolarRenderer.Capture wind = frame();
        int delta = changed(noWind, wind);
        expect(delta > 20, "Solar wind toggle did not change actual GLES output");
        save("sun-solar-wind.png", wind);
        metrics.put("windToggleChangedPixels", delta);
    }

    private void destinations() throws Exception {
        prepare(6, false, false);
        SolarRenderer.Capture saturn = frame();
        substantial(saturn, "Saturn and rings");
        expect(activity.renderer().selectedBody() == 6 && activity.selectedDestination() == 6,
                "Saturn navigation did not update the live selection");
        expect(SolarData.BODIES[6].ringOuter > 2 && SolarData.BODIES[6].ringInner > 1,
                "Saturn annulus configuration is missing");
        expectHeading("Saturno");
        save("saturn-rings.png", saturn);
        main(() -> activity.navigateForTest(9));
        SolarRenderer.Capture moon = frame();
        substantial(moon, "Moon focus");
        expect(activity.renderer().selectedBody() == 9 && activity.selectedDestination() == 9,
                "Moon navigation did not update the live selection");
        expectHeading("Luna");
        expect(changed(saturn, moon) > 100, "Travel to the Moon did not change the rendered destination");
        save("moon-surface.png", moon);
        gl(() -> activity.renderer().setOrbits(true));
        main(() -> activity.navigateForTest(-1));
        SolarRenderer.Capture overview = frame();
        expect(activity.selectedDestination() == -1, "System overview did not clear the destination");
        substantial(overview, "Solar system overview");
        save("solar-system-overview.png", overview);
        metrics.put("saturnBrightPixels", bright(saturn)).put("moonBrightPixels", bright(moon));
        metrics.put("navigationDestinationChangedPixels", changed(saturn, moon));
        metrics.put("systemOverviewRendered", true);
    }

    private void timeControls() throws Exception {
        prepare(3, false, false);
        double start = activity.renderer().julianDate();
        gl(() -> { activity.renderer().setSpeed(2); activity.renderer().setPaused(false); });
        waitUntil(() -> activity.renderer().julianDate() > start + .01, 8_000, "Forward time did not advance");
        gl(() -> activity.renderer().setPaused(true));
        double forward = activity.renderer().julianDate();
        gl(() -> { activity.renderer().setSpeed(-2); activity.renderer().setPaused(false); });
        waitUntil(() -> activity.renderer().julianDate() < forward - .01, 8_000, "Reverse time did not move backward");
        main(() -> activity.pauseForTest(true));
        gl(() -> activity.renderer().setSpeed(.2));
        double pausedDate = activity.renderer().julianDate();
        frame(); frame();
        expect(activity.renderer().julianDate() == pausedDate, "Pause allowed the astronomical date to change");
        gl(() -> activity.renderer().setJulianDate(SolarData.MIN_JD - 100));
        expect(activity.renderer().julianDate() == SolarData.MIN_JD, "Earlier dates escaped the model validity interval");
        gl(() -> activity.renderer().setJulianDate(SolarData.MAX_JD + 100));
        expect(activity.renderer().julianDate() == SolarData.MAX_JD, "Later dates escaped the model validity interval");
        gl(() -> activity.renderer().setJulianDate(TEST_JD));
        metrics.put("forwardDaysAdvanced", forward - start);
        metrics.put("reverseDaysAdvanced", pausedDate - forward);
        metrics.put("pausedDatePreserved", true).put("dateBoundsEnforced", true);
    }

    private void realScale() throws Exception {
        prepare(3, false, false);
        main(() -> activity.scaleForTest(true));
        gl(() -> activity.renderer().focus(3));
        SolarRenderer.Capture earth = frame();
        substantial(earth, "Earth at physical proportions");
        expect(activity.renderer().selectedBody() == 3, "Real-scale transition lost Earth focus");
        expect(SolarData.BODIES[3].radiusKm / SolarData.AU_KM < .00005, "Earth physical radius is not in AU");
        save("earth-real-scale.png", earth);
        metrics.put("realScaleEarthBrightPixels", bright(earth));
        main(() -> activity.scaleForTest(false));
    }

    private void cameraControls() throws Exception {
        prepare(6, false, false);
        SolarRenderer.Capture before = frame();
        gl(() -> activity.renderer().rotate(75, 28));
        SolarRenderer.Capture rotated = frame();
        gl(() -> activity.renderer().zoom(1.35f));
        SolarRenderer.Capture zoomed = frame();
        gl(() -> activity.renderer().pan(32, -18));
        SolarRenderer.Capture panned = frame();
        gl(() -> { activity.renderer().setFreeFlight(true); activity.renderer().move(.25f, .14f); });
        SolarRenderer.Capture moved = frame();
        int rotationDelta = changed(before, rotated), zoomDelta = changed(rotated, zoomed);
        int panDelta = changed(zoomed, panned), flightDelta = changed(panned, moved);
        expect(rotationDelta > 50 && zoomDelta > 50 && panDelta > 50 && flightDelta > 50,
                "At least one camera control did not alter the real view");
        gl(() -> activity.renderer().setFreeFlight(false));
        metrics.put("cameraChangedPixels", new JSONObject().put("rotation", rotationDelta).put("zoom", zoomDelta)
                .put("pan", panDelta).put("flight", flightDelta));
    }

    private void resolutionBudget() throws Exception {
        prepare(3, false, false);
        gl(() -> activity.renderer().setQuality(480));
        SolarRenderer.Capture low = frame();
        gl(() -> activity.renderer().setQuality(960));
        SolarRenderer.Capture high = frame();
        int lowEdge = Math.max(low.width, low.height), highEdge = Math.max(high.width, high.height);
        expect(lowEdge <= 480 && highEdge <= 960 && highEdge > lowEdge,
                "Quality control did not change the bounded render target resolution");
        substantial(low, "Low-resolution render"); substantial(high, "High-resolution render");
        metrics.put("lowResolution", new JSONArray().put(low.width).put(low.height));
        metrics.put("highResolution", new JSONArray().put(high.width).put(high.height));
        gl(() -> activity.renderer().setQuality(TEST_QUALITY));
    }

    private void backgroundResume() throws Exception {
        prepare(3, true, false);
        gl(() -> activity.bridge().setPreset(3));
        SolarRenderer.Capture before = frame();
        double date = activity.renderer().julianDate();
        long revision = activity.bridge().nativeRevision();
        QuantumSolarBridge sameCore = activity.bridge();
        SolarActivity sameActivity = activity;
        AtomicReference<Boolean> moved = new AtomicReference<>(false);
        main(() -> moved.set(activity.moveTaskToBack(true)));
        expect(Boolean.TRUE.equals(moved.get()), "Android did not move the real Activity to the background");
        try {
            waitUntil(() -> !sameActivity.hasWindowFocus(), 10_000, "Activity did not enter the background");
            waitForIdleSync();
        } finally {
            Intent resume = new Intent(getTargetContext(), SolarActivity.class);
            resume.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            getTargetContext().startActivity(resume);
        }
        waitUntil(() -> sameActivity.hasWindowFocus(), 15_000, "Activity did not return to the foreground");
        ready(30_000);
        SolarRenderer.Capture after = frame();
        expect(activity == sameActivity && activity.bridge() == sameCore, "Background resume replaced the live native owner");
        expect(activity.renderer().julianDate() == date && activity.bridge().nativeRevision() == revision,
                "Background resume changed a paused date or core revision");
        int delta = changed(before, after);
        expect(delta == 0, "Background resume changed a paused scene");
        metrics.put("backgroundResumeChangedPixels", delta).put("backgroundPreservedNativeOwner", true);
    }

    private void recreationAndOrientation() throws Exception {
        prepare(3, true, true);
        main(() -> activity.scaleForTest(true));
        gl(() -> { activity.renderer().setQuality(960); activity.renderer().setOrbits(true);
            activity.bridge().setPreset(3); activity.renderer().focus(3); });
        double date = activity.renderer().julianDate();
        float[] coefficients = activity.bridge().coefficients().clone();
        SolarRenderer.Capture before = frame();
        QuantumSolarBridge oldOwner = activity.bridge();
        recreate(() -> activity.recreate());
        expect(activity.bridge() != oldOwner, "Activity.recreate did not construct a fresh native owner");
        assertRestored(date, coefficients);
        SolarRenderer.Capture after = frame();
        expect(changed(before, after) == 0, "Activity.recreate did not restore the focused paused scene exactly");
        saveScreen("activity-portrait.png");
        recreate(() -> activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));
        expect(activity.getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE,
                "Landscape orientation did not take effect");
        assertRestored(date, coefficients);
        SolarRenderer.Capture landscape = frame();
        expect(landscape.width > landscape.height, "GLES target did not become landscape");
        substantial(landscape, "Landscape view");
        saveScreen("activity-landscape.png");
        recreate(() -> activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));
        expect(activity.getResources().getConfiguration().orientation == Configuration.ORIENTATION_PORTRAIT,
                "Portrait orientation did not return");
        assertRestored(date, coefficients);
        metrics.put("recreationChangedPixels", changed(before, after));
        metrics.put("recreationPreservedPreset", activity.bridge().preset());
        metrics.put("recreationPreservedDate", date);
        metrics.put("orientationRoundtripPassed", true);
    }

    private void assertRestored(double date, float[] coefficients) throws Exception {
        expect(activity.selectedDestination() == 3 && activity.renderer().selectedBody() == 3,
                "Activity recreation lost the focused destination");
        expect(activity.renderer().julianDate() == date, "Activity recreation lost the selected date");
        expect(activity.bridge().preset() == 3 && Arrays.equals(coefficients, activity.bridge().verifyReadOnly()),
                "Activity recreation lost its native preparation");
        gl(() -> expect(activity.renderer().isPaused() && activity.renderer().fieldsEnabled()
                && activity.renderer().windEnabled() && activity.renderer().orbitsEnabled()
                && activity.renderer().realScaleEnabled() && activity.renderer().qualityLimit() == 960,
                "Activity recreation lost its live time, scale, layer or quality settings"));
        expectHeading("CAMPO INTRÍNSECO");
        frame(); frame();
        expect(activity.renderer().julianDate() == date, "Recreated paused scene advanced in time");
    }

    private void prepare(int destination, boolean fields, boolean wind) throws Exception {
        main(() -> { activity.pauseForTest(true); activity.fieldsForTest(fields);
            activity.scaleForTest(false); activity.navigateForTest(destination); });
        gl(() -> { activity.renderer().setQuality(TEST_QUALITY); activity.renderer().setWind(wind);
            activity.renderer().setOrbits(false); activity.renderer().setSpeed(.2);
            activity.renderer().setJulianDate(TEST_JD); activity.renderer().setFreeFlight(false);
            activity.renderer().setAnimationTimeForTest(0);
            if (destination < 0) activity.renderer().overview(); else activity.renderer().focus(destination); });
    }

    private void ready(long timeout) {
        waitUntil(() -> activity.renderer() != null && (activity.renderer().hasFrame || activity.renderer().failure != null),
                timeout, "Solar renderer did not produce its first frame");
        expect(activity.renderer().failure == null, "Renderer initialization failed: " + activity.renderer().failure);
        waitForIdleSync();
    }

    private void recreate(Runnable change) throws Exception {
        SolarActivity original = activity;
        ActivityMonitor monitor = addMonitor(SolarActivity.class.getName(), null, false);
        try {
            main(change);
            Activity next = waitForMonitorWithTimeout(monitor, 30_000);
            expect(next instanceof SolarActivity && next != original, "Android did not recreate the solar Activity");
            activity = (SolarActivity) next;
            ready(45_000);
        } finally { removeMonitor(monitor); }
    }

    private void main(Runnable action) { runOnMainSync(action); waitForIdleSync(); }

    private void gl(Runnable action) throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        activity.surface().queueEvent(() -> {
            try { action.run(); } catch (Throwable caught) { error.set(caught); }
            finally { done.countDown(); }
        });
        activity.surface().requestRender();
        await(done, error, "GL command");
    }

    private SolarRenderer.Capture frame() throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        AtomicReference<SolarRenderer.Capture> result = new AtomicReference<>();
        activity.surface().queueEvent(() -> {
            try { activity.renderer().requestCapture(image -> { result.set(image); done.countDown(); }); }
            catch (Throwable caught) { error.set(caught); done.countDown(); }
        });
        activity.surface().requestRender();
        await(done, error, "Actual GLES framebuffer capture");
        SolarRenderer.Capture capture = result.get();
        expect(capture != null && capture.width > 0 && capture.height > 0
                && capture.rgba.length == capture.width * capture.height * 4, "Invalid GLES framebuffer");
        gl(() -> expect(GLES30.glGetError() == GLES30.GL_NO_ERROR, "GLES reported an error after drawing"));
        return capture;
    }

    private void await(CountDownLatch done, AtomicReference<Throwable> error, String operation) throws Exception {
        long deadline = SystemClock.elapsedRealtime() + FRAME_TIMEOUT_MS;
        while (!done.await(50, TimeUnit.MILLISECONDS)) {
            expect(activity.renderer().failure == null, "Renderer failed: " + activity.renderer().failure);
            expect(SystemClock.elapsedRealtime() < deadline, operation + " timed out");
        }
        if (error.get() != null) throw new AssertionError(operation, error.get());
        expect(activity.renderer().failure == null, "Renderer failed: " + activity.renderer().failure);
    }

    private static void waitUntil(BooleanSupplier condition, long timeout, String message) {
        long deadline = SystemClock.elapsedRealtime() + timeout;
        while (!condition.getAsBoolean() && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(50);
        expect(condition.getAsBoolean(), message);
    }

    private void expectHeading(String text) {
        AtomicReference<Boolean> found = new AtomicReference<>(false);
        runOnMainSync(() -> found.set(containsText(activity.getWindow().getDecorView(), text)));
        expect(Boolean.TRUE.equals(found.get()), "Actual Activity heading is missing: " + text);
    }
    private static boolean containsText(View view, String text) {
        if (view instanceof TextView && ((TextView) view).getText().toString().contains(text)) return true;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); i++) if (containsText(group.getChildAt(i), text)) return true;
        }
        return false;
    }

    private static int changed(SolarRenderer.Capture a, SolarRenderer.Capture b) {
        expect(a.width == b.width && a.height == b.height, "Cannot compare framebuffers with different dimensions");
        int count = 0;
        for (int p = 0; p < a.rgba.length; p += 4)
            if (a.rgba[p] != b.rgba[p] || a.rgba[p + 1] != b.rgba[p + 1] || a.rgba[p + 2] != b.rgba[p + 2]) count++;
        return count;
    }
    private static int bright(SolarRenderer.Capture image) {
        int count = 0;
        for (int p = 0; p < image.rgba.length; p += 4)
            if ((image.rgba[p] & 255) > 48 || (image.rgba[p + 1] & 255) > 48 || (image.rgba[p + 2] & 255) > 48) count++;
        return count;
    }
    private static void substantial(SolarRenderer.Capture image, String context) {
        expect(bright(image) > Math.max(80, image.width * image.height / 500),
                context + " produced an empty or insubstantial framebuffer");
    }
    private static double length(double[] value) { return Math.sqrt(value[0]*value[0] + value[1]*value[1] + value[2]*value[2]); }
    private static double distance(double[] a, double[] b) { return length(new double[]{a[0]-b[0], a[1]-b[1], a[2]-b[2]}); }
    private static JSONArray floats(float[] values) throws Exception { JSONArray out = new JSONArray(); for(float v : values) out.put((double)v); return out; }

    private void save(String name, SolarRenderer.Capture capture) throws Exception {
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
        if (name.startsWith("activity-"))
            waitUntil(() -> activity.hasWindowFocus(), 15_000,
                    "Cannot capture an Activity obscured by another window");
        Bitmap bitmap = getUiAutomation().takeScreenshot();
        expect(bitmap != null, "Cannot capture actual Activity screenshot");
        try { saveBitmap(name, bitmap); } finally { bitmap.recycle(); }
    }
    private void saveBitmap(String name, Bitmap bitmap) throws Exception {
        try (FileOutputStream out = new FileOutputStream(new File(evidence, name))) {
            expect(bitmap.compress(Bitmap.CompressFormat.PNG, 100, out), "Cannot encode " + name);
        }
        images.put(name);
    }

    private void runCase(String name, Checked test) {
        Bundle status = new Bundle();
        status.putString("class", getClass().getName()); status.putString("test", name);
        status.putInt("numtests", EXPECTED_TESTS); status.putInt("current", results.size()+1);
        sendStatus(1, status);
        long started = SystemClock.elapsedRealtime();
        Throwable failure = null;
        try { test.run(); }
        catch (Throwable error) {
            failure = error; status.putString("stack", trace(error)); Log.e(TAG, name, error);
            try { saveScreen("failure-" + name + ".png"); } catch (Throwable ignored) { }
        }
        results.add(new Result(name, (SystemClock.elapsedRealtime()-started)/1000.0, failure));
        sendStatus(failure == null ? 0 : -2, status);
    }
    private void writeReports() throws Exception {
        JSONArray tests = new JSONArray();
        StringBuilder xml = new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        xml.append("<testsuite name=\"CosmicXSolar\" tests=\"").append(results.size())
                .append("\" failures=\"").append(failures()).append("\" errors=\"0\">\n");
        for (Result result : results) {
            JSONObject test = new JSONObject().put("name", result.name).put("passed", result.error == null)
                    .put("durationSeconds", result.seconds);
            xml.append("  <testcase classname=\"com.mundox.solar.SolarInstrumentation\" name=\"")
                    .append(escape(result.name)).append("\" time=\"").append(String.format(Locale.US, "%.3f", result.seconds)).append("\">");
            if (result.error != null) {
                test.put("error", trace(result.error));
                xml.append("<failure message=\"").append(escape(result.error.toString())).append("\">")
                        .append(escape(trace(result.error))).append("</failure>");
            }
            tests.put(test); xml.append("</testcase>\n");
        }
        xml.append("</testsuite>\n");
        JSONObject report = new JSONObject().put("schemaVersion", 1).put("testsRun", results.size())
                .put("testsFailed", failures()).put("tests", tests).put("metrics", metrics).put("images", images);
        write("report.json", report.toString(2) + "\n"); write("junit.xml", xml.toString());
    }
    private void write(String name, String text) throws Exception {
        try (FileOutputStream out = new FileOutputStream(new File(evidence, name))) { out.write(text.getBytes(StandardCharsets.UTF_8)); }
    }
    private int failures() { int n=0; for(Result r : results) if(r.error != null) n++; return n; }
    private static void expect(boolean value, String message) { if(!value) throw new AssertionError(message); }
    private static String trace(Throwable error) { StringWriter out=new StringWriter(); error.printStackTrace(new PrintWriter(out)); return out.toString(); }
    private static String escape(String text) { return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\"", "&quot;").replace("'", "&apos;"); }
}
