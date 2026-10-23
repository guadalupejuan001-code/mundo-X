package com.mundox.solar;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.widget.TextView;
import com.mundox.graphics.RenderResolution;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
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

/** Tests the shipping Activity, shared Ether session, GL thread and real framebuffers. */
public final class SolarInstrumentation extends Instrumentation {
    private static final String TAG = "CosmicSolarTests";
    private static final int LEGACY_TESTS = 15;
    private static final int TEST_QUALITY = 640;
    private static final long FRAME_TIMEOUT_MS = 30_000;
    private static final long UHD_FRAME_TIMEOUT_MS = 150_000;
    // CI 39523264220 recorded 159880.88 ms between GL swaps for the software
    // 8K atmospheric capture. Allow 1.5x that interval for these two captures;
    // this is an operation deadline, not an interactive 8K performance claim.
    private static final long ULTRA_FRAME_TIMEOUT_MS = 240_000;
    private static final double TEST_JD = 2461331.0;
    private SolarActivity activity;
    private File evidence;
    private final List<Result> results = new ArrayList<>();
    private final JSONObject metrics = new JSONObject();
    private final JSONArray images = new JSONArray();
    private final JSONObject captureSamples = new JSONObject();
    private boolean uhdSuite,ultraSuite;
    private boolean performanceSuite;
    private volatile boolean onDemandFrames;
    private boolean glOperationTimedOut;
    private int expectedTests = LEGACY_TESTS;

    private interface Checked { void run() throws Exception; }
    private static final class Result {
        final String name;
        final double seconds;
        final Throwable error;
        Result(String name, double seconds, Throwable error) {
            this.name = name; this.seconds = seconds; this.error = error;
        }
    }

    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        performanceSuite=arguments!=null&&"true".equals(arguments.getString("performance"));
        ultraSuite = arguments != null && "true".equals(arguments.getString("ultra8k"));
        uhdSuite = ultraSuite || (arguments != null && "true".equals(arguments.getString("uhd4k")));
        if (ultraSuite) expectedTests += 2;
        else if (uhdSuite) expectedTests += 7;
        start();
    }

    @Override public void onStart() {
        if(performanceSuite){SolarPerformanceChecks.run(this);return;}
        try {
            evidence = new File(getTargetContext().getFilesDir(), "solar-evidence");
            expect(evidence.isDirectory() || evidence.mkdirs(), "Cannot create solar evidence directory");
            expect(getTargetContext().getSharedPreferences("solar", 0).edit().clear().commit(),
                    "Cannot prepare isolated first-launch preferences");
            metrics.put("androidSdk", Build.VERSION.SDK_INT);
            metrics.put("androidRelease", Build.VERSION.RELEASE);
            metrics.put("abis", new JSONArray(Arrays.asList(Build.SUPPORTED_ABIS)));
            metrics.put("performanceScope", "Software GLES emulator; not Samsung A16 performance measurements");
            metrics.put("physicalScope", "JPL analytical orbits + JPL-GM local gravity; illustrative moon phases and magnetic geometries");
            metrics.put("uhd4kSuite", uhdSuite && !ultraSuite);
            metrics.put("ultra8kSuite", ultraSuite);
            metrics.put("applicationId", getTargetContext().getPackageName());
            Intent launch = new Intent(getTargetContext(), SolarActivity.class);
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            activity = (SolarActivity) startActivitySync(launch);
            ready(60_000);

            runCase("native_gles_launch_and_textures", this::launchAndTextures);
            runCase("cinematic_opt_in_shader_and_legacy_restore", this::cinematicShader);
            runCase("smart_4k_live_preview_is_native_and_bounded", this::smart4kPreview);
            runCase("astronomy_and_magnetic_classification", this::astronomyAndMagnetism);
            runCase("native_ether_fusion_and_readonly_observation", this::nativeEtherFusion);
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
            if (uhdSuite) {
                // UHD tests draw on demand so software GLES does not rerender the
                // same 4K/8K framebuffer while assertions run on the CPU.
                onDemandFrames = true;
                main(() -> activity.surface().setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY));
                runCase(ultraSuite?"ultra8k_framebuffer_return_preserves_scene":"uhd_framebuffer_return_preserves_scene", this::uhdReturn);
                if (ultraSuite) {
                    runCase("ultra8k_earth_entry_atmosphere", this::ultraEntry);
                } else {
                    runCase("uhd_fit_picking_preserves_scene", this::uhdPicking);
                    runCase("entry_all_planets_and_moons", this::entryCatalogue);
                    runCase("entry_atmosphere_and_grazing_frames", this::entryLayers);
                    runCase("entry_uhd_lifecycle_and_orientation", this::entryLifecycle);
                    runCase("map_ui_planet_and_moon_selection", this::mapSelection);
                    runCase("smart_4k_png_capture_native_then_preview",
                            this::smart4kCapture);
                }
                // Keep demand mode through reporting. Restoring continuous mode
                // here queued another 8K entry draw after the failed capture,
                // delaying the reporting command by a second 150-second wait.
            }

            if (glOperationTimedOut) metrics.put("glReportingSkippedAfterTimeout", true);
            else gl(() -> {
                try {
                    metrics.put("glRenderer", GLES30.glGetString(GLES30.GL_RENDERER));
                    metrics.put("glVersion", GLES30.glGetString(GLES30.GL_VERSION));
                    metrics.put("nativeDomain", "q0,q1,...");
                    metrics.put("nativePreparationSites", QuantumSolarBridge.PREPARATION_SITES);
                    metrics.put("nativeActiveSites", activity.bridge().activeSites());
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
        boolean passed = failures == 0 && results.size() == expectedTests;
        output.putInt("tests_run", results.size()); output.putInt("tests_failed", failures);
        output.putString("stream", "\nCOSMIC_SOLAR_TESTS_" + (passed ? "PASSED" : "FAILED")
                + " tests=" + results.size() + " failures=" + failures + "\n");
        finish(passed ? Activity.RESULT_OK : Activity.RESULT_CANCELED, output);
    }

    private void cinematicShader() throws Exception {
        // The visual upgrade must compile and render on the shipping GLES
        // pipeline, while its OFF path preserves the original appearance.
        prepare(3,false,false);
        gl(()->activity.renderer().setCinematic(false));
        SolarRenderer.Capture legacy=frame();
        gl(()->activity.renderer().setCinematic(true));
        SolarRenderer.Capture cinematic=frame();
        expect(activity.renderer().cinematicEnabled(),"Cine X setting was not applied");
        expect(legacy.width==cinematic.width && legacy.height==cinematic.height,
                "Cine X must not change framebuffer dimensions");
        int changedPixels=changed(legacy,cinematic);
        expect(changedPixels>0,"Cine X produced no visible pixels");
        gl(()->activity.renderer().setCinematic(false));
        SolarRenderer.Capture restored=frame();
        expect(!activity.renderer().cinematicEnabled(),"Cine X did not turn off");
        expect(changed(legacy,restored)==0,
                "Legacy rendering was not restored with Cine X disabled");
        metrics.put("cinematicChangedPixels",changedPixels);
        metrics.put("cinematicLegacyRestored",true);
    }

    private void launchAndTextures() throws Exception {
        expect(activity.bridge() != null && activity.surface() != null && activity.renderer() != null,
                "The real solar Activity did not attach the core and GLSurfaceView");
        waitUntil(() -> activity.hasWindowFocus(), 15_000,
                "Solar Activity is obscured by another window at launch");
        metrics.put("initialWindowFocused", true);
        expect(activity.bridge().stateBytes() > 0 && activity.bridge().etherRules() == 2,
                "The default finite preparation and Ether frame are absent");
        prepare(3, false, false);
        awaitStableViewport();
        prepare(3, false, false);
        SolarRenderer.Capture earth = frame();
        double firstDate = activity.renderer().julianDate();
        SolarRenderer.Capture repeated = frame();
        boolean sameDimensions = earth.width == repeated.width && earth.height == repeated.height;
        int delta = sameDimensions ? changed(earth, repeated) : -1;
        JSONObject pair = new JSONObject().put("format", "RGBA8, bottom row first")
                .put("firstWidth", earth.width).put("firstHeight", earth.height)
                .put("secondWidth", repeated.width).put("secondHeight", repeated.height)
                .put("changedPixels", delta).put("firstJulianDate", firstDate)
                .put("secondJulianDate", activity.renderer().julianDate())
                .put("paused", activity.renderer().isPaused()).put("qualityLimit", TEST_QUALITY);
        write("initial-paused-pair.json", pair.toString(2));
        metrics.put("deterministicPausedChangedPixels", delta);
        if (delta != 0) {
            save("initial-paused-first.png", earth); save("initial-paused-second.png", repeated);
            try (FileOutputStream out = new FileOutputStream(new File(evidence, "initial-paused-first.rgba"))) {
                out.write(earth.rgba);
            }
            try (FileOutputStream out = new FileOutputStream(new File(evidence, "initial-paused-second.rgba"))) {
                out.write(repeated.rgba);
            }
        }
        substantial(earth, "Earth launch");
        expect(sameDimensions, "Initial framebuffer dimensions changed: " + pair);
        expect(delta == 0, "Paused surface changed " + delta + " pixels: " + pair);
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
        double earthGravity=SolarData.gravityMs2(3,0),earthEscape=SolarData.escapeVelocityKmS(3,0);
        SolarFlightPhysics.State falling=new SolarFlightPhysics.State(100,0);
        for(int step=0;step<100;step++)SolarFlightPhysics.step(3,SolarEnvironment.environment(3),falling,0,.01);
        expect(earthGravity>9.81&&earthGravity<9.83&&earthEscape>11.18&&earthEscape<11.20,
                "Packaged JPL-GM Earth gravity/escape model is wrong");
        expect(falling.altitudeKm<100&&falling.radialVelocityKmS<0,
                "Packaged local gravity integrator did not produce free fall");
        metrics.put("bodyCount", SolarData.BODIES.length).put("planetCount", 8).put("moonCount", 14);
        metrics.put("earthJ2000DistanceAU", earthAU).put("earthMoonDistanceKm", moonKm);
        metrics.put("earthGravityMs2",earthGravity).put("earthEscapeKmS",earthEscape)
                .put("oneSecondFallAltitudeKm",falling.altitudeKm)
                .put("oneSecondFallVelocityKmS",falling.radialVelocityKmS);
        metrics.put("fieldTypes", new JSONObject().put("earth", 1).put("venus", 2).put("mars", 3).put("moon", 0).put("ganymede", 1));
    }

    private void nativeEtherFusion() throws Exception {
        // Independent fixtures for local preparation followed by global H and
        // nearest-neighbour CZ. These are physical observables after the frame,
        // not the old isolated eight-qubit Bell expectations.
        float[][] localExpected = {
            {0,0,0,0,0,0,0,0},
            {0,0,1,0,0,1,0,1},
            {0,0,0,0,0,1,0,1},
            {0,-1,0,0,0,0,0,0}
        };
        float[][] etherExpected = {
            {1,1,0,0,0},
            {0,0,0,1,1},
            {0,0,0,0,0},
            {0,0,0,0,0}
        };
        JSONArray preparations = new JSONArray();
        for (int preset=0; preset<QuantumSolarBridge.PRESETS.length; preset++) {
            final int selected = preset;
            gl(() -> activity.bridge().setPreset(selected));
            QuantumSolarBridge bridge = activity.bridge();
            float[] actual = bridge.verifyReadOnly(), ether = bridge.verifyEtherReadOnly();
            expect(Arrays.equals(actual,localExpected[preset]),
                    "Framed preparation has wrong local observables for preset " + preset);
            expect(Arrays.equals(ether,etherExpected[preset]),
                    "Ether signals did not follow the local preparation for preset " + preset);
            expect(Arrays.equals(actual,bridge.coefficients()) && Arrays.equals(ether,bridge.etherCoefficients()),
                    "Renderer coefficients do not observe the same native state");
            expect(bridge.etherRules()==2 && bridge.activeSites()==(preset==0?0:8),
                    "Global rules materialized the background or lost the finite preparation");
            expect(bridge.observationRevision()==bridge.nativeRevision()
                    && bridge.observationEpoch()==bridge.nativeEpoch(),
                    "The two visual coefficient groups do not share one atomic observation");
            preparations.put(new JSONObject().put("preset",preset)
                    .put("local",floats(actual)).put("ether",floats(ether))
                    .put("activeSites",bridge.activeSites()).put("rules",bridge.etherRules())
                    .put("revision",bridge.observationRevision()).put("epoch",bridge.observationEpoch()));
        }
        gl(() -> activity.bridge().setPreset(2));
        float[] actual = activity.bridge().verifyReadOnly();
        long revision = activity.bridge().nativeRevision(), epoch = activity.bridge().nativeEpoch();
        float[] cached = activity.bridge().coefficients().clone();
        float[] ether = activity.bridge().verifyEtherReadOnly();
        frame(); frame();
        expect(activity.bridge().nativeRevision()==revision && activity.bridge().nativeEpoch()==epoch,
                "Drawing frames mutated the native revision or epoch");
        expect(Arrays.equals(cached, activity.bridge().verifyReadOnly()), "Native read-only observations drifted");
        expect(Arrays.equals(ether,activity.bridge().verifyEtherReadOnly()),
                "EtherCore read-only observations drifted");
        metrics.put("etherFusionPresets",preparations).put("etherFusionSingleObservation",true);
        metrics.put("framedBellExpectations", floats(actual));
        metrics.put("etherExpectations", floats(ether)).put("etherRules",activity.bridge().etherRules());
        metrics.put("observationPreservedNativeRevision", true).put("observationPreservedNativeEpoch",true)
                .put("etherObservationReadOnly",true);
    }

    private void quantumCoupling() throws Exception {
        prepare(3, true, false);
        gl(() -> activity.bridge().setPreset(0));
        double[] physicalBefore = SolarData.position(3, activity.renderer().julianDate());
        SolarRenderer.Capture baseline = frame();
        // After the Ether frame, the Fase preset has <Y0>=-1. This is a
        // nonzero input to the existing magnetic-tracer phase at paused time.
        gl(() -> activity.bridge().setPreset(3));
        SolarRenderer.Capture phased = frame();
        int coupled = changed(baseline, phased);
        expect(coupled > 20, "The native preparation does not change its visible field traces");
        double[] physicalAfter = SolarData.position(3, activity.renderer().julianDate());
        expect(Arrays.equals(physicalBefore, physicalAfter), "Visual core preparation changed astronomical coordinates");
        save("earth-fields-base.png", baseline);
        save("earth-fields-phase.png", phased);
        main(() -> activity.fieldsForTest(false));
        gl(() -> { activity.renderer().setWind(false); activity.bridge().setPreset(0); });
        SolarRenderer.Capture uncoupledA = frame();
        gl(() -> activity.bridge().setPreset(3));
        SolarRenderer.Capture uncoupledB = frame();
        int uncoupled = changed(uncoupledA, uncoupledB);
        expect(uncoupled == 0, "Core preparation changes surfaces when its visual layers are disabled");
        metrics.put("coupledChangedPixels", coupled).put("uncoupledChangedPixels", uncoupled);
        metrics.put("coupledComparisonPresets",new JSONArray().put(0).put(3));
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
        // Persist the adaptive fixture in the Activity as well as the renderer:
        // standalone APKs otherwise restore their original UHD mode on recreate.
        main(() -> { activity.setUhdQuality(false); activity.setUltraQuality(false);
            activity.scaleForTest(true); });
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
        metrics.put("recreationPreservedAdaptiveMode", true);
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
                && activity.renderer().realScaleEnabled() && activity.renderer().qualityLimit() == 960
                && !activity.renderer().isUhd4k() && !activity.renderer().isUhd8k(),
                "Activity recreation lost its live time, scale, layer or adaptive quality settings"));
        expectHeading("CAMPO INTRÍNSECO");
        frame(); frame();
        expect(activity.renderer().julianDate() == date, "Recreated paused scene advanced in time");
    }

    private void uhdReturn() throws Exception {
        prepare(3, false, false);
        SolarRenderer.Capture before = frame();
        save("uhd-adaptive-before.png", before);
        double date = activity.renderer().julianDate();
        long revision = activity.bridge().nativeRevision();
        QuantumSolarBridge owner = activity.bridge();
        float[] coefficients = owner.verifyReadOnly();
        setUhdMode(true);
        saveUhd(ultraSuite?"ultra8k-earth-orbit.png":"uhd-earth-orbit.png", false);
        expect(ultraSuite?activity.renderer().isUhd8k():activity.renderer().isUhd4k(), "The renderer did not accept actual UHD mode");
        setUhdMode(false);
        SolarRenderer.Capture after = frame();
        save("uhd-adaptive-after.png", after);
        expect(changed(before, after) == 0, "UHD roundtrip did not preserve the previous exact framebuffer");
        expect(activity.renderer().qualityLimit() == TEST_QUALITY && !activity.renderer().isUhd4k() && !activity.renderer().isUhd8k(),
                "Returning from UHD lost the previous quality setting");
        expect(owner == activity.bridge() && date == activity.renderer().julianDate()
                && revision == owner.nativeRevision() && Arrays.equals(coefficients, owner.verifyReadOnly()),
                "UHD allocation changed the native preparation or astronomical time");
        metrics.put("uhdPixels", ultraSuite?33_177_600:8_294_400).put("uhdReturnChangedPixels", 0)
                .put("uhdNativeOwnerPreserved", true);
    }

    private void uhdPicking() throws Exception {
        prepare(3, false, false);
        main(() -> activity.setUhdQuality(true));
        saveUhd("uhd-picking-before.png", false);
        RenderResolution.Fit fit = activity.renderer().displayRect();
        expect(fit != null && fit.width > 0 && fit.height > 0, "No actual FIT rectangle was published");
        expect(fit.framebufferWidth == 2160 && fit.framebufferHeight == 3840,
                "Solar picking does not use the actual UHD framebuffer dimensions");
        int centerX = fit.left + fit.width / 2, centerY = fit.top + fit.height / 2;
        gl(() -> activity.renderer().pick(centerX, centerY));
        String before = saveUhd("uhd-picking-earth.png", false);
        expect(activity.renderer().selectedBody() == 3 && activity.selectedDestination() == 3,
                "FIT picking failed to select the visible Earth");
        final int barX, barY;
        if (fit.top > 1) { barX = fit.left + fit.width / 2; barY = 0; }
        else {
            expect(fit.left > 1, "The UHD emulator fixture must expose a letterbox band");
            barX = 0; barY = fit.top + fit.height / 2;
        }
        long revision = activity.bridge().nativeRevision();
        gl(() -> activity.renderer().pick(barX, barY));
        String after = saveUhd("uhd-picking-band.png", false);
        expect(before.equals(after) && activity.renderer().selectedBody() == 3,
                "Tapping a FIT band changed the selection or exact image");
        expect(activity.bridge().nativeRevision() == revision, "Picking applied a native operation");
        metrics.put("uhdSelectedBody", 3).put("uhdBandChangedPixels", 0);
        metrics.put("uhdFit", new JSONObject().put("left", fit.left).put("top", fit.top)
                .put("width", fit.width).put("height", fit.height)
                .put("surfaceWidth", activity.renderer().surfaceWidth())
                .put("surfaceHeight", activity.renderer().surfaceHeight()));
        main(() -> activity.setUhdQuality(false));
    }

    private void entryCatalogue() throws Exception {
        JSONArray entries = new JSONArray();
        long revision = activity.bridge().nativeRevision();
        float[] coefficients = activity.bridge().verifyReadOnly();
        for (int id = 1; id < SolarData.BODIES.length; id++) {
            final int target = id;
            final double date = entryFixtureDate(id);
            SolarEnvironment environment = SolarEnvironment.environment(id);
            prepare(id, false, false);
            gl(() -> { activity.renderer().setQuality(480); activity.renderer().setJulianDate(date); });
            main(() -> activity.enterSelectedBody());
            SolarRenderer.Capture entered = frame();
            expect(activity.renderer().isEntryMode() && activity.renderer().selectedBody() == id
                    && activity.selectedDestination() == id, "A destination did not enter its local environment: " + id);
            double altitude = activity.renderer().altitudeKm();
            expect(Double.isFinite(altitude) && altitude >= environment.minAltitudeKm
                    && altitude <= environment.maxAltitudeKm, "Entry altitude escaped the declared environment bounds");
            substantial(entered, "Local environment " + SolarData.BODIES[id].name);
            save(String.format(Locale.ROOT, "entry-body-%02d.png", id), entered);
            expect(environment.gasGiant == (id >= 5 && id <= 8), "Gas giants were given the wrong descent category");
            expect(environment.solidSurface != environment.gasGiant, "Gas giant descent invented a solid landing surface");
            main(() -> activity.setEntryAltitude(environment.minAltitudeKm - 1000));
            gl(() -> expect(activity.renderer().altitudeKm() == environment.minAltitudeKm,
                    "Descent passed the modeled lower altitude bound: " + target));
            main(() -> activity.setEntryAltitude(environment.maxAltitudeKm + 1000));
            gl(() -> expect(activity.renderer().altitudeKm() == environment.maxAltitudeKm,
                    "Ascent passed the modeled upper altitude bound: " + target));
            expect(activity.renderer().julianDate() == date && Arrays.equals(SolarData.position(id, date),
                    SolarData.position(id, activity.renderer().julianDate())), "Entry control changed the orbital date");
            entries.put(new JSONObject().put("body", id).put("gasGiant", environment.gasGiant)
                    .put("solidSurface", environment.solidSurface).put("hasAtmosphere", environment.hasAtmosphere)
                    .put("julianDate", date).put("altitudeKm", altitude).put("brightPixels", bright(entered))
                    .put("rgbaSha256", sha256(entered.rgba)));
            main(() -> activity.exitEntry());
            gl(() -> expect(!activity.renderer().isEntryMode(), "Exit did not return to orbital navigation"));
        }
        expect(activity.bridge().nativeRevision() == revision
                && Arrays.equals(coefficients, activity.bridge().verifyReadOnly()),
                "Local environment navigation mutated the native preparation");
        metrics.put("entryBodies", entries).put("entryNativePreparationPreserved", true);
    }

    /**
     * Observe every local terrain in daylight for the unchanged brightness test.
     * At TEST_JD Europa is eclipsed by Jupiter in the shipping model. These moon-only offsets
     * place the subsolar surface outside every foreground body's full penumbra
     * in the shipping model; the smallest clearance is 0.8372 Titan radii.
     * Eclipse rendering stays enabled. Planets retain the original test date.
     */
    private static double entryFixtureDate(int body) {
        switch (body) {
            case 11: case 15: case 16: return TEST_JD - .25; // Europa, Enceladus, Triton
            case 14: return TEST_JD + 6; // Titan
            case 17: return TEST_JD + .125; // Phobos
            case 19: return TEST_JD - 1; // Rhea
            default: return TEST_JD;
        }
    }

    private void entryLayers() throws Exception {
        JSONObject differences = new JSONObject();
        int[] targets = {3, 2, 5, 14, 9};
        String[] names = {"earth", "venus", "jupiter", "titan", "moon"};
        for (int index = 0; index < targets.length; index++) {
            int target = targets[index];
            SolarEnvironment environment = SolarEnvironment.environment(target);
            prepare(target, false, false);
            main(() -> activity.enterSelectedBody());
            gl(() -> activity.renderer().setAtmosphereEnabled(false));
            SolarRenderer.Capture vacuum = frame();
            gl(() -> activity.renderer().setAtmosphereEnabled(true));
            SolarRenderer.Capture atmosphere = frame();
            int layerDelta = changed(vacuum, atmosphere);
            if (target == 9) expect(layerDelta == 0, "The Moon acquired an invented dense sky");
            else expect(layerDelta > 20, "The atmospheric layer has no visible effect for " + names[index]);
            save("entry-" + names[index] + "-atmosphere.png", atmosphere);
            if (target == 3 || target == 9) save("entry-" + names[index] + "-vacuum.png", vacuum);
            main(() -> activity.setEntryAltitude(environment.minAltitudeKm));
            SolarRenderer.Capture grazing = frame();
            int descentDelta = changed(atmosphere, grazing);
            expect(descentDelta > 50, "Lower altitude did not change the local view for " + names[index]);
            substantial(grazing, "Grazing view " + names[index]);
            save("entry-" + names[index] + "-grazing.png", grazing);
            differences.put(names[index], new JSONObject().put("atmosphereChangedPixels", layerDelta)
                    .put("descentChangedPixels", descentDelta).put("minAltitudeKm", environment.minAltitudeKm));
            main(() -> activity.exitEntry());
        }
        metrics.put("entryLayerComparisons", differences);
    }

    private void entryLifecycle() throws Exception {
        prepare(3, true, true);
        main(() -> activity.enterSelectedBody());
        gl(() -> {
            activity.bridge().setPreset(3);
            activity.renderer().setOrbits(true);
            activity.renderer().setAtmosphereEnabled(true);
            activity.renderer().rotate(31, -9);
        });
        main(() -> activity.setEntryAltitude(12));
        main(() -> activity.setUhdQuality(true));
        String before = saveUhd("uhd-entry-before.png", false);
        double[] camera = activity.renderer().entryState();
        expect(camera != null && camera.length == 6, "The local camera snapshot is incomplete");
        double date = activity.renderer().julianDate();
        float[] coefficients = activity.bridge().verifyReadOnly();
        QuantumSolarBridge oldOwner = activity.bridge();
        recreate(() -> activity.recreate());
        expect(activity.bridge() != oldOwner, "Recreation did not reconstruct the native owner");
        assertEntryRestored(camera, date, coefficients);
        String recreated = saveUhd("uhd-entry-recreated.png", false);
        expect(before.equals(recreated), "Recreation changed exact UHD entry pixels");
        recreate(() -> activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));
        assertEntryRestored(camera, date, coefficients);
        saveUhd("uhd-entry-landscape.png", true);
        saveScreen("entry-activity-landscape.png");
        recreate(() -> activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));
        assertEntryRestored(camera, date, coefficients);
        String portrait = saveUhd("uhd-entry-portrait-return.png", false);
        expect(before.equals(portrait), "Rotation roundtrip changed exact UHD entry pixels");
        saveScreen("entry-activity-portrait.png");
        metrics.put("entryRecreateChangedPixels", 0).put("entryRotationChangedPixels", 0)
                .put("entryRecreatedPreset", activity.bridge().preset()).put("entryCameraPreserved", true);
        main(() -> { activity.setUhdQuality(false); activity.exitEntry(); });
    }

    private void assertEntryRestored(double[] camera, double date, float[] coefficients) throws Exception {
        gl(() -> expect(activity.renderer().isEntryMode() && activity.renderer().isUhd4k()
                && activity.renderer().selectedBody() == 3 && activity.renderer().isPaused()
                && activity.renderer().fieldsEnabled() && activity.renderer().windEnabled()
                && activity.renderer().orbitsEnabled() && activity.renderer().atmosphereEnabled(),
                "Lifecycle lost a local-camera, layer or UHD preference"));
        expect(Arrays.equals(camera, activity.renderer().entryState()), "Lifecycle changed the body-relative camera state");
        expect(activity.renderer().julianDate() == date && activity.bridge().preset() == 3
                && Arrays.equals(coefficients, activity.bridge().verifyReadOnly()),
                "Lifecycle lost the date or native preparation while inside the atmosphere");
    }

    private void mapSelection() throws Exception {
        prepare(3, false, false);
        long revision = activity.bridge().nativeRevision();
        double date = activity.renderer().julianDate();
        tapMapBody(8);
        expectHeading("Neptuno");
        tapMapBody(5);
        expectHeading("Júpiter");
        tapMapBody(11);
        expectHeading("Europa");
        expect(activity.bridge().nativeRevision() == revision && activity.renderer().julianDate() == date,
                "Map navigation changed the native preparation or astronomical time");
        save("map-selected-europa.png", frame());
        metrics.put("mapUiSelectedBodies", new JSONArray().put(8).put(5).put(11))
                .put("mapNavigationPreservedCoreAndDate", true);
    }

    private void tapMapBody(int body) throws Exception {
        main(() -> activity.openMap());
        awaitMapTarget(body);
        saveScreen("map-ui-body-" + body + ".png");
        // Showing the dialog and expanding its window are separate layouts.
        // Screenshot capture may span that transition. Resolve the focused,
        // settled position again immediately before the one real input event.
        float[] point = awaitMapTarget(body);
        JSONObject touch = new JSONObject().put("body", body).put("x", point[0]).put("y", point[1])
                .put("mapWidth", point[2]).put("mapHeight", point[3])
                .put("mapLeft", point[4]).put("mapTop", point[5])
                .put("beforeUiBody", activity.selectedDestination());
        JSONArray touches = metrics.optJSONArray("mapUiTouches");
        if (touches == null) { touches = new JSONArray(); metrics.put("mapUiTouches", touches); }
        touches.put(touch);
        long when = SystemClock.uptimeMillis();
        MotionEvent down = MotionEvent.obtain(when, when, MotionEvent.ACTION_DOWN, point[0], point[1], 0);
        MotionEvent up = MotionEvent.obtain(when, when + 50, MotionEvent.ACTION_UP, point[0], point[1], 0);
        down.setSource(InputDevice.SOURCE_TOUCHSCREEN); up.setSource(InputDevice.SOURCE_TOUCHSCREEN);
        try { sendPointerSync(down); sendPointerSync(up); }
        finally { down.recycle(); up.recycle(); }
        waitUntil(() -> activity.selectedDestination() == body && activity.renderer().selectedBody() == body,
                10_000, "The actual map touch at " + point[0] + "," + point[1]
                        + " did not select the requested body " + body);
        touch.put("afterUiBody", activity.selectedDestination())
                .put("afterRendererBody", activity.renderer().selectedBody());
        frame();
    }

    private float[] awaitMapTarget(int body) throws Exception {
        long deadline = SystemClock.elapsedRealtime() + 10_000, stableSince = 0;
        float[] previous = null;
        int samples = 0;
        while (SystemClock.elapsedRealtime() < deadline) {
            AtomicReference<float[]> current = new AtomicReference<>();
            main(() -> {
                SolarMapView map = activity.mapView();
                if (map == null || !map.isShown() || !map.hasWindowFocus()
                        || !map.isLaidOut() || map.isLayoutRequested()) return;
                float[] point = map.bodyScreenPosition(body);
                if (point == null) return;
                int[] location = new int[2]; map.getLocationOnScreen(location);
                float x = point[0] + location[0], y = point[1] + location[1];
                android.graphics.Rect visible = new android.graphics.Rect();
                map.getWindowVisibleDisplayFrame(visible);
                if (point[0] < 0 || point[0] >= map.getWidth() || point[1] < 0
                        || point[1] >= map.getHeight() || !visible.contains((int)x, (int)y)) return;
                current.set(new float[]{x, y, map.getWidth(), map.getHeight(), location[0], location[1]});
            });
            float[] value = current.get();
            long now = SystemClock.elapsedRealtime();
            if (value != null && Arrays.equals(previous, value)) samples++;
            else { samples = value == null ? 0 : 1; stableSince = now; }
            previous = value;
            if (samples >= 3 && now - stableSince >= 400) return value;
            SystemClock.sleep(80);
        }
        throw new AssertionError("Map has no focused, stable visible target for body " + body);
    }

    private String saveUhd(String name, boolean landscape) throws Exception {
        SolarRenderer.Capture image = frame(ultraSuite ? ULTRA_FRAME_TIMEOUT_MS : UHD_FRAME_TIMEOUT_MS);
        int longEdge=ultraSuite?7680:3840,shortEdge=ultraSuite?4320:2160;
        int width = landscape ? longEdge : shortEdge, height = landscape ? shortEdge : longEdge;
        long expectedBytes=(long)width*height*4;
        expect(image.width == width && image.height == height && image.rgba.length == expectedBytes,
                "Expected actual UHD " + width + "x" + height + "; got " + image.width + "x" + image.height);
        expect(ultraSuite?activity.renderer().isUhd8k():activity.renderer().isUhd4k(),
                "The UHD request was rejected: " + activity.renderer().resolutionNotice());
        substantial(image, ultraSuite?"8K observation":"UHD observation");
        save(name, image);
        return sha256(image.rgba);
    }

    private void setUhdMode(boolean enabled) {
        main(() -> { if(ultraSuite) activity.setUltraQuality(enabled); else activity.setUhdQuality(enabled); });
    }

    private void ultraEntry() throws Exception {
        prepare(3,false,false);
        main(() -> activity.enterSelectedBody());
        main(() -> activity.setEntryAltitude(25));
        setUhdMode(true);
        SolarRenderer.Capture image=frame(ULTRA_FRAME_TIMEOUT_MS);
        expect(image.width==4320&&image.height==7680&&activity.renderer().isUhd8k(),
                "Earth entry did not use the real portrait 8K target");
        substantial(image,"8K Earth atmospheric entry");
        save("ultra8k-earth-entry.png",image);
        expect(activity.renderer().atmosphereEnabled()&&activity.renderer().isEntryMode(),
                "8K entry lost the local atmosphere");
        metrics.put("ultra8kEntry",new JSONObject().put("width",image.width).put("height",image.height)
                .put("altitudeKm",activity.renderer().altitudeKm()).put("brightPixels",bright(image)));
        setUhdMode(false);
        main(() -> activity.exitEntry());
    }

    private void prepare(int destination, boolean fields, boolean wind) throws Exception {
        if (activity.renderer().isEntryMode()) main(() -> activity.exitEntry());
        main(() -> { activity.pauseForTest(true); activity.fieldsForTest(fields);
            activity.scaleForTest(false); activity.navigateForTest(destination); });
        gl(() -> { activity.renderer().setQuality(TEST_QUALITY); activity.renderer().setWind(wind);
            activity.renderer().setOrbits(false); activity.renderer().setSpeed(.2);
            activity.renderer().setJulianDate(TEST_JD); activity.renderer().setFreeFlight(false);
            activity.renderer().setAnimationTimeForTest(0);
            if (destination < 0) activity.renderer().overview(); else activity.renderer().focus(destination); });
    }

    /** Complete initial Surface/layout and quality transitions before the pixel experiment. */
    private void awaitStableViewport() throws Exception {
        long deadline = SystemClock.elapsedRealtime() + 15_000;
        long stableSince = SystemClock.elapsedRealtime();
        int[] previous = null;
        int stableFrames = 0, samples = 0;
        while (SystemClock.elapsedRealtime() < deadline) {
            int[] geometry = new int[6];
            main(() -> {
                geometry[0] = activity.surface().getWidth(); geometry[1] = activity.surface().getHeight();
                android.graphics.Rect bounds = activity.surface().getHolder().getSurfaceFrame();
                geometry[2] = bounds.width(); geometry[3] = bounds.height();
                geometry[4] = activity.surface().isLayoutRequested() ? 1 : 0;
                geometry[5] = activity.hasWindowFocus() ? 1 : 0;
            });
            SolarRenderer.Capture observed = frame();
            float ratio = Math.min(1f, (float) TEST_QUALITY / Math.max(1, Math.max(geometry[2], geometry[3])));
            boolean fitted = geometry[0] > 0 && geometry[1] > 0 && geometry[0] == geometry[2]
                    && geometry[1] == geometry[3] && geometry[4] == 0 && geometry[5] == 1
                    && observed.width == Math.round(geometry[2] * ratio)
                    && observed.height == Math.round(geometry[3] * ratio);
            int[] signature = {geometry[0], geometry[1], geometry[2], geometry[3], observed.width, observed.height};
            if (fitted && Arrays.equals(previous, signature)) stableFrames++;
            else { stableFrames = fitted ? 1 : 0; stableSince = SystemClock.elapsedRealtime(); }
            previous = signature; samples++;
            if (stableFrames >= 3 && SystemClock.elapsedRealtime() - stableSince >= 500) {
                metrics.put("startupViewportSamples", samples);
                metrics.put("startupViewport", new JSONArray().put(observed.width).put(observed.height));
                return;
            }
            SystemClock.sleep(50);
        }
        throw new AssertionError("Initial Surface/framebuffer geometry did not stabilize: " + Arrays.toString(previous));
    }

    private void ready(long timeout) {
        waitUntil(() -> activity.renderer() != null && (activity.renderer().hasFrame || activity.renderer().failure != null),
                timeout, "Solar renderer did not produce its first frame");
        expect(activity.renderer().failure == null, "Renderer initialization failed: " + activity.renderer().failure);
        if (!onDemandFrames) waitForIdleSync();
    }

    private void recreate(Runnable change) throws Exception {
        SolarActivity original = activity;
        ActivityMonitor monitor = addMonitor(SolarActivity.class.getName(), null, false);
        try {
            main(change);
            // The first software-UHD run measured 79 s from relaunch to drawing.
            // This wait is a device-operation budget, not a performance assertion.
            Activity next = waitForMonitorWithTimeout(monitor, onDemandFrames ? 150_000 : 30_000);
            expect(next instanceof SolarActivity && next != original, "Android did not recreate the solar Activity");
            activity = (SolarActivity) next;
            if (onDemandFrames)
                main(() -> activity.surface().setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY));
            ready(onDemandFrames ? 150_000 : 45_000);
        } finally { removeMonitor(monitor); }
    }

    private void main(Runnable action) {
        runOnMainSync(action);
        if (!onDemandFrames) waitForIdleSync();
    }

    private void gl(Runnable action) throws Exception {
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        activity.surface().queueEvent(() -> {
            try { action.run(); } catch (Throwable caught) { error.set(caught); }
            finally { done.countDown(); }
        });
        if (!onDemandFrames) activity.surface().requestRender();
        await(done, error, "GL command");
    }

    private SolarRenderer.Capture frame() throws Exception {
        return frame(uhdSuite ? UHD_FRAME_TIMEOUT_MS : FRAME_TIMEOUT_MS);
    }

    private SolarRenderer.Capture frame(long timeoutMs) throws Exception {
        long started = SystemClock.elapsedRealtime();
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        AtomicReference<SolarRenderer.Capture> result = new AtomicReference<>();
        AtomicReference<String> phase = new AtomicReference<>("queued");
        activity.surface().queueEvent(() -> {
            try {
                phase.set("capture requested; awaiting draw, readback and blit");
                activity.renderer().requestCapture(image -> {
                    try {
                        // This callback runs on the GL thread after readback and blit.
                        // Check the same frame without queuing another UHD draw.
                        if (onDemandFrames)
                            expect(GLES30.glGetError() == GLES30.GL_NO_ERROR, "GLES reported an error after drawing");
                        result.set(image);
                        phase.set("capture callback completed");
                    } catch (Throwable caught) { error.set(caught); }
                    finally { done.countDown(); }
                });
            } catch (Throwable caught) { error.set(caught); done.countDown(); }
        });
        activity.surface().requestRender();
        try { await(done, error, "Actual GLES framebuffer capture", timeoutMs); }
        finally {
            if (timeoutMs == ULTRA_FRAME_TIMEOUT_MS) {
                JSONArray waits = metrics.optJSONArray("ultraCaptureWaits");
                if (waits == null) { waits = new JSONArray(); metrics.put("ultraCaptureWaits", waits); }
                waits.put(new JSONObject().put("deadlineMs", timeoutMs)
                        .put("elapsedMs", SystemClock.elapsedRealtime() - started).put("phase", phase.get())
                        .put("width", activity.renderer().renderedWidth()).put("height", activity.renderer().renderedHeight())
                        .put("callbackCompleted", result.get() != null && error.get() == null));
            }
        }
        SolarRenderer.Capture capture = result.get();
        expect(capture != null && capture.width > 0 && capture.height > 0
                && capture.rgba.length == capture.width * capture.height * 4, "Invalid GLES framebuffer");
        if (!onDemandFrames)
            gl(() -> expect(GLES30.glGetError() == GLES30.GL_NO_ERROR, "GLES reported an error after drawing"));
        return capture;
    }

    private void await(CountDownLatch done, AtomicReference<Throwable> error, String operation) throws Exception {
        await(done, error, operation, uhdSuite ? UHD_FRAME_TIMEOUT_MS : FRAME_TIMEOUT_MS);
    }

    private void await(CountDownLatch done, AtomicReference<Throwable> error, String operation, long timeoutMs) throws Exception {
        long deadline = SystemClock.elapsedRealtime() + timeoutMs;
        while (!done.await(50, TimeUnit.MILLISECONDS)) {
            expect(activity.renderer().failure == null, "Renderer failed: " + activity.renderer().failure);
            if (SystemClock.elapsedRealtime() >= deadline) {
                glOperationTimedOut = true;
                throw new AssertionError(operation + " timed out after " + timeoutMs + " ms");
            }
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
        try (FileOutputStream output = new FileOutputStream(new File(evidence, name))) {
            SolarPng.write(capture, output);
        }
        JSONArray points = new JSONArray();
        for (int row = 0; row < 5; row++) for (int column = 0; column < 5; column++) {
            int x = (capture.width - 1) * column / 4, y = (capture.height - 1) * row / 4;
            int offset = ((capture.height - 1 - y) * capture.width + x) * 4;
            JSONArray rgba = new JSONArray();
            for (int channel = 0; channel < 4; channel++) rgba.put(capture.rgba[offset + channel] & 255);
            points.put(new JSONObject().put("x", x).put("y", y).put("rgba", rgba));
        }
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inPremultiplied = false;
        Bitmap decoded = BitmapFactory.decodeFile(new File(evidence, name).getAbsolutePath(), options);
        expect(decoded != null, "Shipping PNG export cannot be decoded: " + name);
        try {
            expect(decoded.getWidth() == capture.width && decoded.getHeight() == capture.height,
                    "PNG export changed the framebuffer dimensions");
            for (int index = 0; index < points.length(); index++) {
                JSONObject point = points.getJSONObject(index);
                JSONArray rgba = point.getJSONArray("rgba");
                int pixel = decoded.getPixel(point.getInt("x"), point.getInt("y"));
                expect(((pixel >>> 24) & 255) == rgba.getInt(3)
                        && ((pixel >>> 16) & 255) == rgba.getInt(0)
                        && ((pixel >>> 8) & 255) == rgba.getInt(1) && (pixel & 255) == rgba.getInt(2),
                        "PNG export changed RGBA or image orientation: " + name);
            }
        } finally { decoded.recycle(); }
        captureSamples.put(name, new JSONObject().put("width", capture.width).put("height", capture.height)
                .put("rgbaSha256", sha256(capture.rgba)).put("points", points)
                .put("decodedPngSamplesVerified", true));
        images.put(name);
    }
    private void saveScreen(String name) throws Exception {
        if (name.startsWith("activity-"))
            waitUntil(() -> activity.hasWindowFocus(), 15_000,
                    "Cannot capture an Activity obscured by another window");
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
        status.putInt("numtests", expectedTests); status.putInt("current", results.size()+1);
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
                .put("testsFailed", failures()).put("tests", tests).put("metrics", metrics).put("images", images)
                .put("captureSamples", captureSamples);
        write("report.json", report.toString(2) + "\n"); write("junit.xml", xml.toString());
    }
    private void write(String name, String text) throws Exception {
        try (FileOutputStream out = new FileOutputStream(new File(evidence, name))) { out.write(text.getBytes(StandardCharsets.UTF_8)); }
    }
    private static String sha256(byte[] bytes) throws Exception {
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(bytes);
        StringBuilder output = new StringBuilder(64);
        for (byte value : digest) output.append(String.format(Locale.ROOT, "%02x", value & 255));
        return output.toString();
    }
    private int failures() { int n=0; for(Result r : results) if(r.error != null) n++; return n; }
    private static void expect(boolean value, String message) { if(!value) throw new AssertionError(message); }
    private static String trace(Throwable error) { StringWriter out=new StringWriter(); error.printStackTrace(new PrintWriter(out)); return out.toString(); }
    private static String escape(String text) { return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\"", "&quot;").replace("'", "&apos;"); }
}
