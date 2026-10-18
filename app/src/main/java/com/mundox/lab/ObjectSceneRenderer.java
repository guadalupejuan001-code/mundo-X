package com.mundox.lab;

import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;

import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONArray;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.ShortBuffer;
import java.util.ArrayList;
import java.util.Arrays;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * The two representations draw the same instanced cube mesh with the same shader.
 * All public commands except the constructor must run through GLSurfaceView.queueEvent.
 * Timing is CPU submission and the wall interval between frames, never GPU timer-query time.
 */
public final class ObjectSceneRenderer implements GLSurfaceView.Renderer {
    public interface Listener {
        void onStats(Stats stats);
        void onPick(int id);
        void onVerification(long requestId, String json);
        void onBenchmark(long requestId, String json);
        void onFailure(String message);
    }

    public static final class Stats {
        public final String renderer, vendor, route, visibility;
        public final int count, edits, selectedId, depthBits, viewWidth, viewHeight;
        public final int submittedInstances, culledInstances, drawCalls, boundsTested;
        public final int meshUniqueVertices, meshTriangles;
        public final long plannerStorageBytes, submittedUniqueVertexCandidates, submittedTriangles;
        public final float projectionNear, projectionFar, cameraDistance;
        public final long frames, explicitBytes, sparseBytes, meshBytes, sceneRevision;
        public final long allocatedExplicitBytes, allocatedSparseBytes, uploadedBytes;
        public final double frameP50Ms, frameP95Ms, cpuSubmitMs;
        public final boolean benchmarking, drawSuspended, animationEnabled, spatialCulling;

        private Stats(ObjectSceneRenderer r) {
            renderer = r.rendererName;
            vendor = r.vendorName;
            route = r.drawingExplicit() ? "explicit" : "procedural";
            count = r.scene == null ? 0 : r.scene.count;
            edits = r.scene == null ? 0 : r.scene.editCount();
            selectedId = r.selectedId;
            frames = r.frames;
            sceneRevision = r.sceneRevision;
            explicitBytes = (long) count * 16L;
            sparseBytes = r.sparsePayloadBytes();
            meshBytes = MESH_BYTES;
            submittedInstances = r.drawPlan.submittedInstances();
            culledInstances = r.drawPlan.culledInstances();
            drawCalls = r.drawPlan.batchCount();
            boundsTested = r.drawPlan.boundsTested();
            meshUniqueVertices = MESH_VERTICES;
            meshTriangles = MESH_INDICES / 3;
            plannerStorageBytes = r.drawPlan.storageBytes();
            submittedUniqueVertexCandidates = (long) submittedInstances * MESH_VERTICES;
            submittedTriangles = (long) submittedInstances * (MESH_INDICES / 3);
            spatialCulling = r.spatialCulling;
            allocatedExplicitBytes = r.explicitAllocatedBytes;
            allocatedSparseBytes = r.sparseAllocatedBytes;
            uploadedBytes = r.uploadedBytes;
            frameP50Ms = percentile(r.frameTimes, r.windowCount, .50);
            frameP95Ms = percentile(r.frameTimes, r.windowCount, .95);
            cpuSubmitMs = mean(r.submitTimes, r.windowCount);
            benchmarking = r.benchmarkState != null || r.pendingBenchmark;
            depthBits = r.depthBits;
            viewWidth = r.width; viewHeight = r.height;
            projectionNear = r.projectionNear; projectionFar = r.projectionFar;
            cameraDistance = r.distance;
            visibility = r.visibility;
            drawSuspended = r.drawSuspended;
            animationEnabled = r.animation;
        }
    }

    private static final int WINDOW = 240;
    private static final int MESH_VERTICES = 24;
    private static final int MESH_INDICES = 36;
    private static final int MESH_VERTEX_BYTES = MESH_VERTICES * 6 * 4;
    private static final int MESH_INDEX_BYTES = MESH_INDICES * 2;
    private static final int MESH_BYTES = MESH_VERTEX_BYTES + MESH_INDEX_BYTES;
    private static final int VERIFY_SIZE = 128;
    private static final long WARMUP_NS = 700_000_000L;
    private static final long SAMPLE_NS = 2_500_000_000L;
    private final Listener listener;
    private final Runnable requestFrame;
    private boolean statsDirty = true;
    private ObjectSceneModel.Snapshot scene;
    private long sceneRevision;
    private int width = 1, height = 1, program, vao, meshVbo, meshEbo, explicitVbo, editsTexture;
    private int uVp, uSide, uBaseId, uSeed, uTime, uRoute, uEditCount, uEdits, uSelected, uEye, uFog;
    private String rendererName = "not initialized", vendorName = "not initialized", glVersion = "";
    private int maxTextureSize, maxVertexAttributes, maxVertexTextureUnits, depthBits;
    private boolean drawSuspended;
    private int visibilityQuery;
    private boolean visibilityRequested = true, visibilityPending, visibilityQueryActive;
    private String visibility = "pending";
    private float projectionNear = .1f, projectionFar = 100f;
    private boolean ready, failed, dirty = true, explicitRoute, explicitDirty = true;
    private boolean animation = true, pendingVerify, pendingBenchmark;
    private boolean spatialCulling = true;
    private final ObjectDrawPlan drawPlan = new ObjectDrawPlan();
    private int cameraWidth = 1, cameraHeight = 1;
    private int submittedViewportWidth = 1, submittedViewportHeight = 1;
    private boolean submittedCulling;
    private float submittedTime;
    private float yaw = .65f, pitch = .80f, distance = 40f, defaultDistance = 40f;
    private int selectedId = -1;
    private double animationSeconds;
    private long lastAnimationNs, lastFrameNs, lastStatsNs, frames, uploadedBytes;
    private long verificationRequestId, benchmarkRequestId;
    private long explicitAllocatedBytes, sparseAllocatedBytes;
    private final float[] projection = new float[16], view = new float[16], vp = new float[16];
    private final float[] eye = new float[3];
    private final double[] frameTimes = new double[WINDOW], submitTimes = new double[WINDOW];
    private int windowCount, windowPosition;
    private BenchmarkState benchmarkState;

    public ObjectSceneRenderer(Listener listener, Runnable requestFrame) {
        if (listener == null || requestFrame == null) throw new IllegalArgumentException("Listener and frame callback required");
        this.listener = listener;
        this.requestFrame = requestFrame;
    }

    public void setScene(ObjectSceneModel.Snapshot snapshot, long revision) {
        if (snapshot == null) throw new IllegalArgumentException("Scene required");
        if (busy()) { listener.onFailure("Cancela la comparación antes de cambiar la escena."); return; }
        boolean resize = scene == null || scene.count != snapshot.count;
        scene = snapshot;
        sceneRevision = revision;
        resumeDrawing();
        invalidateVisibility();
        dirty = explicitDirty = true;
        if (resize) {
            defaultDistance = Math.max(12f, snapshot.side * 3.1f);
            distance = defaultDistance;
        }
        if (selectedId >= snapshot.count) selectedId = -1;
        resetWindow();
    }

    public void setExplicit(boolean value) {
        if (busy()) return;
        explicitRoute = value;
        resumeDrawing();
        invalidateVisibility();
        resetWindow();
    }

    /** Conservative clipping changes submitted ranges, never persistent object IDs. */
    public void setSpatialCulling(boolean enabled) {
        if (busy()) return;
        spatialCulling = enabled;
        resumeDrawing();
        resetWindow();
        invalidateVisibility();
    }

    /** Deltas are fractions of the viewport, not pixels. */
    public void orbit(float dx, float dy) {
        if (busy()) return;
        yaw += dx * 3f;
        pitch = clamp(pitch + dy * 3f, .18f, 1.45f);
        invalidateVisibility();
    }

    /** A value greater than one moves the camera farther away. */
    public void zoom(float factor) {
        if (busy() || !Float.isFinite(factor) || factor <= 0) return;
        distance = clamp(distance * factor, defaultDistance * .22f, defaultDistance * 4f);
        invalidateVisibility();
    }

    /** Returns to the framing chosen for this scene, and retries a suspended draw. */
    public void recenter() {
        if (busy()) return;
        yaw = .65f;
        pitch = .80f;
        distance = defaultDistance;
        resumeDrawing();
        invalidateVisibility();
        lastFrameNs = lastAnimationNs = 0;
        resetWindow();
    }

    public void select(int id) {
        if (busy()) return;
        selectedId = scene != null && id >= 0 && id < scene.count ? id : -1;
        drawChanged();
    }

    public void setAnimation(boolean enabled) {
        if (busy()) return;
        animation = enabled;
        lastAnimationNs = lastFrameNs = 0;
        resetWindow();
        invalidateVisibility();
    }

    public void verify() { verify(0); }

    public void verify(long requestId) {
        if (failed) { listener.onFailure("La GPU no está disponible; vuelve a abrir el laboratorio."); return; }
        if (drawSuspended) { listener.onFailure("El dibujo está detenido; pulsa Recentrar o carga otra escena para reintentarlo."); return; }
        if (busy()) { listener.onFailure("Ya hay una comparación en curso."); return; }
        if (scene == null) { listener.onFailure("Primero carga una escena."); return; }
        verificationRequestId = requestId;
        pendingVerify = true;
        drawChanged();
    }

    public void benchmark() { benchmark(0); }

    public void benchmark(long requestId) {
        if (failed) { listener.onFailure("La GPU no está disponible; vuelve a abrir el laboratorio."); return; }
        if (drawSuspended) { listener.onFailure("El dibujo está detenido; pulsa Recentrar o carga otra escena para reintentarlo."); return; }
        if (busy()) { listener.onFailure("Ya hay una comparación en curso."); return; }
        if (scene == null) { listener.onFailure("Primero carga una escena."); return; }
        benchmarkRequestId = requestId;
        pendingBenchmark = true;
        drawChanged();
    }

    public void cancelBenchmark() {
        boolean active = pendingBenchmark || benchmarkState != null;
        long cancelledRequest = benchmarkRequestId;
        pendingVerify = false;
        pendingBenchmark = false;
        benchmarkState = null;
        lastFrameNs = lastAnimationNs = 0;
        resetWindow();
        invalidateVisibility();
        if (active) {
            JSONObject result = contextJson();
            put(result, "status", "cancelled");
            put(result, "measurement", "frame wall intervals and CPU submission; not GPU timer queries");
            listener.onBenchmark(cancelledRequest, result.toString());
        }
    }

    private boolean busy() {
        return pendingVerify || pendingBenchmark || benchmarkState != null;
    }

    @Override public void onSurfaceCreated(GL10 unused, EGLConfig config) {
        ready = false;
        failed = false;
        statsDirty = true;
        drawSuspended = false;
        // A recreated EGL context has already disposed the old query names.
        visibilityQuery = 0;
        visibilityPending = visibilityQueryActive = false;
        visibilityRequested = true;
        visibility = "pending";
        program = vao = meshVbo = meshEbo = explicitVbo = editsTexture = 0;
        explicitAllocatedBytes = sparseAllocatedBytes = uploadedBytes = frames = 0;
        dirty = explicitDirty = true;
        lastFrameNs = lastAnimationNs = lastStatsNs = 0;
        resetWindow();
        if (benchmarkState != null || pendingBenchmark) cancelBenchmark();
        try {
            rendererName = safe(GLES30.glGetString(GLES30.GL_RENDERER));
            vendorName = safe(GLES30.glGetString(GLES30.GL_VENDOR));
            glVersion = safe(GLES30.glGetString(GLES30.GL_VERSION));
            int[] capability = new int[1];
            GLES30.glGetIntegerv(GLES30.GL_DEPTH_BITS, capability, 0); depthBits = capability[0];
            GLES30.glGetIntegerv(GLES30.GL_MAX_TEXTURE_SIZE, capability, 0); maxTextureSize = capability[0];
            GLES30.glGetIntegerv(GLES30.GL_MAX_VERTEX_ATTRIBS, capability, 0); maxVertexAttributes = capability[0];
            GLES30.glGetIntegerv(GLES30.GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, capability, 0); maxVertexTextureUnits = capability[0];
            program = createProgram(vertexSource(), FRAGMENT_SOURCE);
            uVp = GLES30.glGetUniformLocation(program, "uVp");
            uSide = GLES30.glGetUniformLocation(program, "uSide");
            uBaseId = GLES30.glGetUniformLocation(program, "uBaseId");
            uSeed = GLES30.glGetUniformLocation(program, "uSeedBytes");
            uTime = GLES30.glGetUniformLocation(program, "uTime");
            uRoute = GLES30.glGetUniformLocation(program, "uProcedural");
            uEditCount = GLES30.glGetUniformLocation(program, "uEditCount");
            uEdits = GLES30.glGetUniformLocation(program, "uEdits");
            uSelected = GLES30.glGetUniformLocation(program, "uSelected");
            uEye = GLES30.glGetUniformLocation(program, "uEye");
            uFog = GLES30.glGetUniformLocation(program, "uFogDistance");
            int[] id = new int[1];
            GLES30.glGenVertexArrays(1, id, 0); vao = id[0];
            GLES30.glBindVertexArray(vao);
            GLES30.glGenBuffers(1, id, 0); meshVbo = id[0];
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, meshVbo);
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, MESH_VERTEX_BYTES, floats(cubeMesh()), GLES30.GL_STATIC_DRAW);
            GLES30.glGenBuffers(1, id, 0); meshEbo = id[0];
            GLES30.glBindBuffer(GLES30.GL_ELEMENT_ARRAY_BUFFER, meshEbo);
            GLES30.glBufferData(GLES30.GL_ELEMENT_ARRAY_BUFFER, MESH_INDEX_BYTES, shorts(cubeIndices()), GLES30.GL_STATIC_DRAW);
            uploadedBytes += MESH_BYTES;
            GLES30.glEnableVertexAttribArray(0);
            GLES30.glVertexAttribPointer(0, 3, GLES30.GL_FLOAT, false, 24, 0);
            GLES30.glEnableVertexAttribArray(1);
            GLES30.glVertexAttribPointer(1, 3, GLES30.GL_FLOAT, false, 24, 12);
            GLES30.glBindVertexArray(0);
            GLES30.glGenTextures(1, id, 0); editsTexture = id[0];
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, editsTexture);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE);
            checkGl("crear recursos");
            ready = true;
        } catch (RuntimeException error) { fail("No se pudo iniciar OpenGL ES 3: " + error.getMessage()); }
    }

    @Override public void onSurfaceChanged(GL10 unused, int newWidth, int newHeight) {
        width = Math.max(1, newWidth);
        height = Math.max(1, newHeight);
        GLES30.glViewport(0, 0, width, height);
        invalidateVisibility();
        if (benchmarkState != null) cancelBenchmark();
    }

    @Override public void onDrawFrame(GL10 unused) {
        long now = System.nanoTime();
        if (lastAnimationNs != 0 && animation && benchmarkState == null && !pendingBenchmark)
            animationSeconds += Math.min(.1, (now - lastAnimationNs) / 1_000_000_000.0);
        lastAnimationNs = now;
        if (!ready || failed || drawSuspended || scene == null) {
            GLES30.glClearColor(.067f, .086f, .075f, 1f);
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT | GLES30.GL_DEPTH_BUFFER_BIT);
            return;
        }
        try {
            String previousVisibility = visibility;
            boolean wasBenchmarking = benchmarkState != null || pendingBenchmark;
            if (dirty) uploadSparse();
            if (pendingVerify) {
                invalidateVisibility();
                pendingVerify = false;
                performVerification(true);
                now = System.nanoTime();
                lastFrameNs = 0;
            }
            if (pendingBenchmark) {
                invalidateVisibility();
                pendingBenchmark = false;
                JSONObject imageCheck = performVerification(false);
                if (!imageCheck.optBoolean("passed")) {
                    JSONObject rejected = contextJson();
                    put(rejected, "status", "equivalence_failed"); put(rejected, "passed", false);
                    put(rejected, "image_check", imageCheck);
                    listener.onBenchmark(benchmarkRequestId, rejected.toString());
                } else {
                    ensureExplicit(); GLES30.glFinish();
                    benchmarkState = new BenchmarkState(System.nanoTime(), 0.0, imageCheck);
                }
                now = System.nanoTime();
                resetWindow();
                lastFrameNs = 0;
            }
            updateCamera(width, height);
            double drawTime = benchmarkState == null ? animationSeconds : benchmarkState.time;
            // A visibility query observes this normal draw. Never add it to a
            // measured comparison, and never block waiting for its result.
            if (benchmarkState == null) {
                pollVisibility();
                if (visibilityRequested && !visibilityPending) beginVisibility();
            }
            long submitStart = System.nanoTime();
            render(drawingExplicit(), (float) drawTime);
            long afterSubmit = System.nanoTime();
            if (visibilityQueryActive) endVisibility();
            double submitMs = (afterSubmit - submitStart) / 1_000_000.0;
            boolean timedFrames = animation || benchmarkState != null;
            double frameMs = !timedFrames || lastFrameNs == 0 ? 0 : (now - lastFrameNs) / 1_000_000.0;
            lastFrameNs = timedFrames ? now : 0;
            frames++;
            if (timedFrames) {
                if (frameMs > 0 && frameMs < 10_000) recordWindow(frameMs, submitMs);
            } else {
                // Human idle time between requested draws is not a frame-time
                // measurement. Retain this draw's CPU submission cost only.
                resetWindow();
                recordWindow(0, submitMs);
            }
            if (benchmarkState != null) advanceBenchmark(now, afterSubmit, frameMs, submitMs);
            boolean isBenchmarking = benchmarkState != null || pendingBenchmark;
            if (statsDirty || !previousVisibility.equals(visibility) || wasBenchmarking != isBenchmarking
                    || afterSubmit - lastStatsNs >= 500_000_000L) {
                lastStatsNs = afterSubmit;
                checkGl("dibujar escena");
                statsDirty = false;
                listener.onStats(new Stats(this));
            }
            // WHEN_DIRTY needs another actual frame for an asynchronous query
            // result or for each measured benchmark frame. Stop once settled.
            if (!animation && (visibilityPending || visibilityRequested || busy())) requestFrame.run();
        } catch (RuntimeException error) {
            suspendDrawing(error);
        }
    }

    private boolean drawingExplicit() {
        return benchmarkState == null ? explicitRoute : benchmarkState.phase >= 2;
    }

    private void uploadSparse() {
        int count = Math.max(1, scene.editCount());
        // ES 3 guarantees a texture width of at least 2048. A 2-D texture also
        // accommodates the full 4096-edit budget on that minimum implementation.
        int textureWidth = Math.min(1024, count);
        int textureHeight = (count + textureWidth - 1) / textureWidth;
        IntBuffer data = ByteBuffer.allocateDirect(textureWidth * textureHeight * 8)
                .order(ByteOrder.nativeOrder()).asIntBuffer();
        for (int i = 0; i < textureWidth * textureHeight; i++) {
            data.put(i < scene.editCount() ? scene.editedId(i) : -1);
            data.put(i < scene.editCount() ? scene.editedState(i) : 0);
        }
        data.position(0);
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, editsTexture);
        GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 4);
        GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RG32I, textureWidth,
                textureHeight, 0, GLES30.GL_RG_INTEGER, GLES30.GL_INT, data);
        sparseAllocatedBytes = (long) textureWidth * textureHeight * 8L;
        uploadedBytes += sparseAllocatedBytes;
        // A new snapshot invalidates the reference VBO. Delete the previous
        // payload now; it is rebuilt only if reference rendering is requested.
        if (explicitVbo != 0) {
            // The VAO would otherwise keep the deleted buffer alive through
            // attribute 2. Redirect the disabled attribute to the shared mesh
            // before deleting the old per-instance storage.
            GLES30.glBindVertexArray(vao);
            GLES30.glDisableVertexAttribArray(2);
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, meshVbo);
            GLES30.glVertexAttribPointer(2, 4, GLES30.GL_FLOAT, false, 16, 0);
            GLES30.glBindVertexArray(0);
            GLES30.glDeleteBuffers(1, new int[]{explicitVbo}, 0);
            explicitVbo = 0;
            explicitAllocatedBytes = 0;
        }
        explicitDirty = true;
        dirty = false;
        checkGl("subir ediciones dispersas");
    }

    private void ensureExplicit() {
        if (!explicitDirty && explicitVbo != 0) return;
        float[] transforms = scene.explicitTransforms();
        int[] id = new int[1];
        if (explicitVbo == 0) {
            GLES30.glGenBuffers(1, id, 0);
            explicitVbo = id[0];
        }
        GLES30.glBindVertexArray(vao);
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, explicitVbo);
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, transforms.length * 4,
                floats(transforms), GLES30.GL_STATIC_DRAW);
        GLES30.glVertexAttribPointer(2, 4, GLES30.GL_FLOAT, false, 16, 0);
        GLES30.glVertexAttribDivisor(2, 1);
        GLES30.glBindVertexArray(0);
        explicitAllocatedBytes = (long) transforms.length * 4L;
        uploadedBytes += explicitAllocatedBytes;
        explicitDirty = false;
        checkGl("subir referencia explícita");
    }

    private void updateCamera(int renderWidth, int renderHeight) {
        cameraWidth = renderWidth; cameraHeight = renderHeight;
        float cp = (float) Math.cos(pitch);
        eye[0] = (float) Math.sin(yaw) * cp * distance;
        eye[1] = (float) Math.sin(pitch) * distance + .5f;
        eye[2] = (float) Math.cos(yaw) * cp * distance;
        Matrix.setLookAtM(view, 0, eye[0], eye[1], eye[2], 0, .5f, 0, 0, 1, 0);
        updateProjectionPlanes();
        // A fixed 0.1 near plane loses almost all useful depth precision as
        // the camera retreats over a large grid, notably with 16-bit depth.
        Matrix.perspectiveM(projection, 0, 50f, (float) renderWidth / renderHeight,
                projectionNear, projectionFar);
        Matrix.multiplyMM(vp, 0, projection, 0, view, 0);
    }

    private void updateProjectionPlanes() {
        projectionNear = Math.max(.1f, distance * .01f);
        projectionFar = Math.max(100f, distance + (scene == null ? 0f : scene.side * 4f) + 20f);
    }

    private void invalidateVisibility() {
        // Commands are on the GL thread. A pending, ended query can be deleted
        // without waiting for the driver to return its result.
        discardVisibilityQuery();
        visibilityRequested = true;
        visibility = drawSuspended || failed ? "error" : "pending";
        drawChanged();
    }

    private void drawChanged() {
        statsDirty = true;
        lastFrameNs = 0;
        if (!animation && benchmarkState == null) resetWindow();
        requestFrame.run();
    }

    private void discardVisibilityQuery() {
        if (visibilityQueryActive) {
            GLES30.glEndQuery(GLES30.GL_ANY_SAMPLES_PASSED);
            visibilityQueryActive = false;
        }
        if (visibilityQuery != 0) {
            GLES30.glDeleteQueries(1, new int[]{visibilityQuery}, 0);
            visibilityQuery = 0;
        }
        visibilityPending = false;
    }

    private void beginVisibility() {
        if (visibilityQuery == 0) {
            int[] query = new int[1];
            GLES30.glGenQueries(1, query, 0);
            visibilityQuery = query[0];
        }
        GLES30.glBeginQuery(GLES30.GL_ANY_SAMPLES_PASSED, visibilityQuery);
        visibilityQueryActive = true;
        checkGl("iniciar comprobación de visibilidad");
    }

    private void endVisibility() {
        GLES30.glEndQuery(GLES30.GL_ANY_SAMPLES_PASSED);
        visibilityQueryActive = false;
        checkGl("enviar comprobación de visibilidad");
        visibilityPending = true;
        visibilityRequested = false;
        visibility = "pending";
    }

    private void pollVisibility() {
        if (!visibilityPending) return;
        int[] result = new int[1];
        GLES30.glGetQueryObjectuiv(visibilityQuery, GLES30.GL_QUERY_RESULT_AVAILABLE, result, 0);
        checkGl("consultar disponibilidad de visibilidad");
        if (result[0] == 0) return;
        GLES30.glGetQueryObjectuiv(visibilityQuery, GLES30.GL_QUERY_RESULT, result, 0);
        checkGl("leer visibilidad");
        visibility = result[0] != 0 ? "visible" : "empty";
        visibilityPending = false;
    }

    private void render(boolean explicit, float seconds) {
        render(explicit, seconds, spatialCulling);
    }

    private void render(boolean explicit, float seconds, boolean cull) {
        GLES30.glEnable(GLES30.GL_DEPTH_TEST);
        GLES30.glDepthFunc(GLES30.GL_LESS);
        GLES30.glEnable(GLES30.GL_CULL_FACE);
        GLES30.glCullFace(GLES30.GL_BACK);
        GLES30.glDisable(GLES30.GL_BLEND);
        GLES30.glDisable(GLES30.GL_SCISSOR_TEST);
        GLES30.glColorMask(true, true, true, true);
        GLES30.glDepthMask(true);
        GLES30.glClearColor(.067f, .086f, .075f, 1f);
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT | GLES30.GL_DEPTH_BUFFER_BIT);
        bindDrawState(explicit, seconds);
        // Rebuild with this exact camera and analytic time. The same plan is
        // used by the procedural and explicit routes, preserving ID order.
        drawPlan.update(scene.count, scene.side, vp, seconds, cull);
        submittedViewportWidth = cameraWidth; submittedViewportHeight = cameraHeight;
        submittedTime = seconds; submittedCulling = cull;
        for (int batch = 0; batch < drawPlan.batchCount(); batch++) {
            bindBatch(explicit, drawPlan.firstId(batch));
            GLES30.glDrawElementsInstanced(GLES30.GL_TRIANGLES, MESH_INDICES,
                    GLES30.GL_UNSIGNED_SHORT, 0, drawPlan.instanceCount(batch));
        }
        GLES30.glBindVertexArray(0);
    }

    private void bindDrawState(boolean explicit, float seconds) {
        if (explicit) ensureExplicit();
        GLES30.glUseProgram(program);
        GLES30.glUniformMatrix4fv(uVp, 1, false, vp, 0);
        GLES30.glUniform1i(uSide, scene.side);
        GLES30.glUniform4ui(uSeed, scene.seed & 255, (scene.seed >>> 8) & 255,
                (scene.seed >>> 16) & 255, (scene.seed >>> 24) & 255);
        GLES30.glUniform1f(uTime, seconds);
        GLES30.glUniform1i(uRoute, explicit ? 0 : 1);
        GLES30.glUniform1i(uEditCount, scene.editCount());
        GLES30.glUniform1i(uSelected, selectedId);
        GLES30.glUniform3fv(uEye, 1, eye, 0);
        GLES30.glUniform1f(uFog, distance + scene.side * 2.2f);
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0);
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, editsTexture);
        GLES30.glUniform1i(uEdits, 0);
        GLES30.glBindVertexArray(vao);
        if (explicit) GLES30.glEnableVertexAttribArray(2);
        else GLES30.glDisableVertexAttribArray(2);
        // Every use, including TF diagnostics, starts with the complete buffer.
        bindBatch(explicit, 0);
    }

    private void bindBatch(boolean explicit, int firstId) {
        GLES30.glUniform1i(uBaseId, firstId);
        if (explicit) {
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, explicitVbo);
            GLES30.glVertexAttribPointer(2, 4, GLES30.GL_FLOAT, false, 16, firstId * 16);
            GLES30.glVertexAttribDivisor(2, 1);
        }
    }

    /** A CPU ray/cube intersection uses exactly the shader's analytic animation. */
    public void pick(float normalizedX, float normalizedY) {
        if (busy() || scene == null || !ready) return;
        updateCamera(width, height);
        float[] inverse = new float[16];
        if (!Matrix.invertM(inverse, 0, vp, 0)) { listener.onPick(-1); return; }
        float[] near = new float[4], far = new float[4];
        Matrix.multiplyMV(near, 0, inverse, 0,
                new float[]{normalizedX * 2f - 1f, 1f - normalizedY * 2f, -1, 1}, 0);
        Matrix.multiplyMV(far, 0, inverse, 0,
                new float[]{normalizedX * 2f - 1f, 1f - normalizedY * 2f, 1, 1}, 0);
        for (int i = 0; i < 3; i++) { near[i] /= near[3]; far[i] /= far[3]; }
        float angle = (float) animationSeconds * .12f;
        float c = (float) Math.cos(angle), s = (float) Math.sin(angle);
        float ox = c * near[0] - s * near[2], oz = s * near[0] + c * near[2];
        float dx = c * (far[0] - near[0]) - s * (far[2] - near[2]);
        float dz = s * (far[0] - near[0]) + c * (far[2] - near[2]);
        float[] origin = {ox, near[1], oz};
        float[] direction = {dx, far[1] - near[1], dz};
        float[] transform = new float[4];
        float nearest = Float.POSITIVE_INFINITY;
        int hit = -1;
        for (int id = 0; id < scene.count; id++) {
            scene.transform(id, transform, 0);
            if (transform[2] <= 0) continue;
            float h = transform[2] * (1f + .08f * (float) Math.sin(
                    animationSeconds * .8 + transform[0] * .07f + transform[1] * .06f));
            float t = boxIntersection(origin, direction, transform[0] - .38f, 0,
                    transform[1] - .38f, transform[0] + .38f, h, transform[1] + .38f);
            if (t >= 0 && t < nearest) { nearest = t; hit = id; }
        }
        selectedId = hit;
        drawChanged();
        listener.onPick(hit);
    }

    private JSONObject performVerification(boolean notify) {
        int[] oldFramebuffer = new int[1], oldViewport = new int[4];
        GLES30.glGetIntegerv(GLES30.GL_FRAMEBUFFER_BINDING, oldFramebuffer, 0);
        GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, oldViewport, 0);
        int fbo = 0, color = 0, depth = 0;
        JSONObject result = contextJson();
        try {
            ensureExplicit();
            int[] id = new int[1];
            GLES30.glGenFramebuffers(1, id, 0); fbo = id[0];
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fbo);
            GLES30.glGenTextures(1, id, 0); color = id[0];
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, color);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST);
            GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8, VERIFY_SIZE,
                    VERIFY_SIZE, 0, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, null);
            GLES30.glFramebufferTexture2D(GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0,
                    GLES30.GL_TEXTURE_2D, color, 0);
            GLES30.glGenRenderbuffers(1, id, 0); depth = id[0];
            GLES30.glBindRenderbuffer(GLES30.GL_RENDERBUFFER, depth);
            GLES30.glRenderbufferStorage(GLES30.GL_RENDERBUFFER, GLES30.GL_DEPTH_COMPONENT16,
                    VERIFY_SIZE, VERIFY_SIZE);
            GLES30.glFramebufferRenderbuffer(GLES30.GL_FRAMEBUFFER, GLES30.GL_DEPTH_ATTACHMENT,
                    GLES30.GL_RENDERBUFFER, depth);
            if (GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER) != GLES30.GL_FRAMEBUFFER_COMPLETE)
                throw new IllegalStateException("Framebuffer de verificación incompleto");
            GLES30.glViewport(0, 0, VERIFY_SIZE, VERIFY_SIZE);
            updateCamera(VERIFY_SIZE, VERIFY_SIZE);
            JSONObject transforms = captureTransformDiagnostics();
            put(result, "transform_diagnostics", transforms);
            JSONObject proceduralTransforms = transforms.optJSONObject("cpu_vs_procedural");
            JSONObject explicitTransforms = transforms.optJSONObject("cpu_vs_explicit");
            boolean transformsMatch = proceduralTransforms != null && explicitTransforms != null
                    && proceduralTransforms.optInt("different_count", -1) == 0
                    && explicitTransforms.optInt("different_count", -1) == 0
                    && proceduralTransforms.optInt("id_different_count", -1) == 0
                    && explicitTransforms.optInt("id_different_count", -1) == 0;
            ByteBuffer procedural = ByteBuffer.allocateDirect(VERIFY_SIZE * VERIFY_SIZE * 4);
            ByteBuffer reference = ByteBuffer.allocateDirect(VERIFY_SIZE * VERIFY_SIZE * 4);
            ByteBuffer fullReference = ByteBuffer.allocateDirect(VERIFY_SIZE * VERIFY_SIZE * 4);
            // Fixed analytic time, including when the live scene is animating.
            render(false, 0f);
            GLES30.glReadPixels(0, 0, VERIFY_SIZE, VERIFY_SIZE,
                    GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, procedural);
            render(true, 0f);
            GLES30.glReadPixels(0, 0, VERIFY_SIZE, VERIFY_SIZE,
                    GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, reference);
            JSONObject optimizedSubmission = submissionJson();
            // Full-population explicit rendering uses the same camera and time,
            // but bypasses spatial clipping. This catches a shared planner bug
            // that a procedural/explicit comparison alone could conceal.
            render(true, 0f, false);
            GLES30.glReadPixels(0, 0, VERIFY_SIZE, VERIFY_SIZE,
                    GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, fullReference);
            checkGl("leer los tres framebuffers");
            JSONObject cullingCheck = compareImages(reference, fullReference, 0);
            JSONObject fullReferenceCheck = compareImages(procedural, fullReference, 2);
            put(result, "verification_submission", optimizedSubmission);
            put(result, "full_reference_submission", submissionJson());
            put(result, "culling_equivalence", cullingCheck);
            put(result, "optimized_vs_full_reference", fullReferenceCheck);
            put(result, "full_reference_hash_fnv64", pixelHash(fullReference));
            put(result, "full_reference_covered_pixels", fullReferenceCheck.optInt("reference_covered_pixels"));
            int different = 0, overTolerance = 0, maxDelta = 0, visible = 0, referenceVisible = 0;
            int backgroundR = 17, backgroundG = 22, backgroundB = 19;
            for (int p = 0; p < VERIFY_SIZE * VERIFY_SIZE; p++) {
                int pixelDelta = 0;
                for (int channel = 0; channel < 4; channel++) {
                    int offset = p * 4 + channel;
                    pixelDelta = Math.max(pixelDelta,
                            Math.abs((procedural.get(offset) & 255) - (reference.get(offset) & 255)));
                }
                if (pixelDelta != 0) different++;
                if (pixelDelta > 2) overTolerance++;
                maxDelta = Math.max(maxDelta, pixelDelta);
                if (Math.abs((procedural.get(p * 4) & 255) - backgroundR) > 2
                        || Math.abs((procedural.get(p * 4 + 1) & 255) - backgroundG) > 2
                        || Math.abs((procedural.get(p * 4 + 2) & 255) - backgroundB) > 2) visible++;
                if (Math.abs((reference.get(p * 4) & 255) - backgroundR) > 2
                        || Math.abs((reference.get(p * 4 + 1) & 255) - backgroundG) > 2
                        || Math.abs((reference.get(p * 4 + 2) & 255) - backgroundB) > 2) referenceVisible++;
            }
            put(result, "status", "complete");
            put(result, "passed", overTolerance == 0 && visible >= 16 && referenceVisible >= 16
                    && transformsMatch && cullingCheck.optBoolean("passed")
                    && fullReferenceCheck.optBoolean("passed"));
            put(result, "sampled_transforms_match_cpu", transformsMatch);
            put(result, "proof", "RGBA8 readback of procedural and explicit routes, plus full-population explicit rendering with clipping disabled");
            put(result, "width", VERIFY_SIZE); put(result, "height", VERIFY_SIZE);
            put(result, "fixedTimeSeconds", 0);
            put(result, "toleranceLevels", 2);
            put(result, "pixelsDifferent", different);
            put(result, "pixelsOverTolerance", overTolerance);
            put(result, "maxChannelDelta", maxDelta);
            put(result, "nonBackgroundPixels", visible);
            put(result, "nonBackgroundReferencePixels", referenceVisible);
            put(result, "reference_covered_pixels", referenceVisible);
            put(result, "covered_pixels", visible);
            put(result, "minimum_covered_pixels", 16);
            put(result, "differing_pixels", different);
            put(result, "max_channel_delta", maxDelta);
            put(result, "hash_fnv64", pixelHash(procedural));
            put(result, "reference_hash_fnv64", pixelHash(reference));
            put(result, "temporaryFramebufferAttachmentPayloadBytes", (long) VERIFY_SIZE * VERIFY_SIZE * 6L);
            put(result, "temporaryCpuReadbackBytes", (long) VERIFY_SIZE * VERIFY_SIZE * 12L);
            put(result, "scope", "This camera, seed and edit snapshot; not a proof for every possible scene");
            putMemory(result);
            if (notify) listener.onVerification(verificationRequestId, result.toString());
        } catch (RuntimeException error) {
            put(result, "status", "error"); put(result, "passed", false);
            put(result, "error", safe(error.getMessage()));
            if (notify) listener.onVerification(verificationRequestId, result.toString());
        } finally {
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, oldFramebuffer[0]);
            GLES30.glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
            if (depth != 0) GLES30.glDeleteRenderbuffers(1, new int[]{depth}, 0);
            if (color != 0) GLES30.glDeleteTextures(1, new int[]{color}, 0);
            if (fbo != 0) GLES30.glDeleteFramebuffers(1, new int[]{fbo}, 0);
            GLES30.glBindRenderbuffer(GLES30.GL_RENDERBUFFER, 0);
            GLES30.glBindVertexArray(0);
            updateCamera(width, height);
        }
        return result;
    }

    /**
     * Captures the very vertex program used for rendering, before projection.
     * One vertex per instance gives an ordered (x,z,height,tone,id) record.
     * This diagnostic runs only during an explicit verification, never in a
     * benchmark or live frame. It does not replace the framebuffer comparison.
     */
    private JSONObject captureTransformDiagnostics() {
        JSONObject result = new JSONObject();
        ArrayList<int[]> ranges = new ArrayList<>();
        int firstCount = Math.min(scene.count, 1024);
        ranges.add(new int[]{0, firstCount});
        int samples = firstCount;
        int lastCovered = firstCount - 1;
        for (int boundary = ObjectDrawPlan.MAX_BATCH_INSTANCES; boundary < scene.count;
                boundary += ObjectDrawPlan.MAX_BATCH_INSTANCES) {
            ranges.add(new int[]{boundary - 1, 2});
            samples += 2;
            lastCovered = boundary;
        }
        if (lastCovered < scene.count - 1) {
            ranges.add(new int[]{scene.count - 1, 1});
            samples++;
        }
        int[] sampleIds = new int[samples];
        int sampleCursor = 0;
        JSONArray sampleRanges = new JSONArray();
        for (int[] range : ranges) {
            JSONObject description = new JSONObject();
            put(description, "first_id", range[0]); put(description, "count", range[1]);
            sampleRanges.put(description);
            for (int i = 0; i < range[1]; i++) sampleIds[sampleCursor++] = range[0] + i;
        }
        int bytes = samples * 20;
        put(result, "samples", samples);
        put(result, "sample_id_ranges", sampleRanges);
        put(result, "scope", "first 1024 IDs, both sides of each 16384-instance boundary, and final ID");
        put(result, "temporary_feedback_buffer_bytes", bytes);
        put(result, "method", "same linked vertex shader, one GL_POINTS vertex per instance, transform feedback");
        int[] oldFeedback = new int[1], oldBuffer = new int[1], generated = new int[1];
        GLES30.glGetIntegerv(GLES30.GL_TRANSFORM_FEEDBACK_BINDING, oldFeedback, 0);
        GLES30.glGetIntegerv(GLES30.GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, oldBuffer, 0);
        boolean oldDiscard = GLES30.glIsEnabled(GLES30.GL_RASTERIZER_DISCARD);
        int feedback = 0, buffer = 0;
        try {
            GLES30.glGenTransformFeedbacks(1, generated, 0); feedback = generated[0];
            GLES30.glBindTransformFeedback(GLES30.GL_TRANSFORM_FEEDBACK, feedback);
            GLES30.glGenBuffers(1, generated, 0); buffer = generated[0];
            GLES30.glBindBuffer(GLES30.GL_TRANSFORM_FEEDBACK_BUFFER, buffer);
            GLES30.glBufferData(GLES30.GL_TRANSFORM_FEEDBACK_BUFFER, bytes, null, GLES30.GL_STREAM_READ);
            GLES30.glBindBufferBase(GLES30.GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer);
            GLES30.glEnable(GLES30.GL_RASTERIZER_DISCARD);
            for (int route = 0; route < 2; route++) {
                boolean explicit = route == 1;
                bindDrawState(explicit, 0f);
                JSONObject comparison = new JSONObject();
                put(comparison, "attrib2", attribute2State());
                int[] uniform = new int[1];
                GLES30.glGetUniformiv(program, uSide, uniform, 0);
                put(comparison, "uniform_side", uniform[0]);
                GLES30.glGetUniformiv(program, uRoute, uniform, 0);
                put(comparison, "uniform_procedural", uniform[0]);
                int[] seedBytes = new int[4];
                GLES30.glGetUniformuiv(program, uSeed, seedBytes, 0);
                JSONArray actualSeed = new JSONArray();
                for (int seedByte : seedBytes) actualSeed.put(seedByte);
                put(comparison, "uniform_seed_bytes", actualSeed);
                GLES30.glBeginTransformFeedback(GLES30.GL_POINTS);
                for (int[] range : ranges) {
                    bindBatch(explicit, range[0]);
                    GLES30.glDrawArraysInstanced(GLES30.GL_POINTS, 0, 1, range[1]);
                }
                GLES30.glEndTransformFeedback();
                bindBatch(explicit, 0);
                checkGl("capturar transformaciones " + route);
                java.nio.Buffer mappedBuffer = GLES30.glMapBufferRange(
                        GLES30.GL_TRANSFORM_FEEDBACK_BUFFER, 0, bytes, GLES30.GL_MAP_READ_BIT);
                if (!(mappedBuffer instanceof ByteBuffer))
                    throw new IllegalStateException("No se pudo leer el buffer de transformaciones");
                ByteBuffer mapped = ((ByteBuffer) mappedBuffer).order(ByteOrder.nativeOrder());
                boolean unmapped;
                try { compareTransforms(mapped, sampleIds, comparison); }
                finally { unmapped = GLES30.glUnmapBuffer(GLES30.GL_TRANSFORM_FEEDBACK_BUFFER); }
                if (!unmapped) throw new IllegalStateException("Buffer de transformaciones invalidado");
                put(result, explicit ? "cpu_vs_explicit" : "cpu_vs_procedural", comparison);
            }
            checkGl("diagnóstico de transformaciones");
            put(result, "status", "complete");
        } finally {
            if (!oldDiscard) GLES30.glDisable(GLES30.GL_RASTERIZER_DISCARD);
            GLES30.glBindVertexArray(0);
            GLES30.glBindTransformFeedback(GLES30.GL_TRANSFORM_FEEDBACK, oldFeedback[0]);
            GLES30.glBindBuffer(GLES30.GL_TRANSFORM_FEEDBACK_BUFFER, oldBuffer[0]);
            if (feedback != 0) GLES30.glDeleteTransformFeedbacks(1, new int[]{feedback}, 0);
            if (buffer != 0) GLES30.glDeleteBuffers(1, new int[]{buffer}, 0);
        }
        return result;
    }

    private JSONObject attribute2State() {
        JSONObject result = new JSONObject();
        int[] value = new int[1];
        GLES30.glGetVertexAttribiv(2, GLES30.GL_VERTEX_ATTRIB_ARRAY_ENABLED, value, 0);
        put(result, "enabled", value[0]);
        GLES30.glGetVertexAttribiv(2, GLES30.GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, value, 0);
        put(result, "buffer", value[0]);
        GLES30.glGetVertexAttribiv(2, GLES30.GL_VERTEX_ATTRIB_ARRAY_STRIDE, value, 0);
        put(result, "stride", value[0]);
        // Some legacy Android translators do not populate the inherited
        // glGetVertexAttribiv array overload for the ES3 divisor enum.
        // Report our configured value; instance IDs and values are checked by TF.
        put(result, "configured_divisor", 1);
        GLES30.glGetVertexAttribiv(2, GLES30.GL_VERTEX_ATTRIB_ARRAY_SIZE, value, 0);
        put(result, "components", value[0]);
        return result;
    }

    private void compareTransforms(ByteBuffer actual, int[] sampleIds, JSONObject result) {
        int different = 0, exactDifferent = 0, idDifferent = 0;
        double maxAbs = 0;
        JSONArray examples = new JSONArray();
        float[] expected = new float[4];
        for (int sample = 0; sample < sampleIds.length; sample++) {
            int id = sampleIds[sample];
            scene.transform(id, expected, 0);
            int actualId = actual.getInt(sample * 20 + 16);
            if (actualId != id) idDifferent++;
            boolean mismatch = actualId != id, exactMismatch = actualId != id;
            JSONArray cpuValues = new JSONArray(), gpuValues = new JSONArray();
            for (int component = 0; component < 4; component++) {
                float value = actual.getFloat(sample * 20 + component * 4);
                double delta = Float.isFinite(value) ? Math.abs((double) value - expected[component]) : Double.MAX_VALUE;
                maxAbs = Math.max(maxAbs, delta);
                mismatch |= delta > .00001;
                exactMismatch |= Float.floatToIntBits(value) != Float.floatToIntBits(expected[component]);
                cpuValues.put((Object) Double.valueOf(expected[component]));
                gpuValues.put(Float.isFinite(value) ? (Object) Double.valueOf(value) : (Object) Float.toString(value));
            }
            if (mismatch) different++;
            if (exactMismatch) exactDifferent++;
            if (mismatch && examples.length() < 8) {
                JSONObject example = new JSONObject();
                put(example, "id", id); put(example, "gpu_id", actualId);
                put(example, "cpu", cpuValues); put(example, "gpu", gpuValues);
                examples.put(example);
            }
        }
        put(result, "different_count", different);
        put(result, "exact_different_count", exactDifferent);
        put(result, "id_different_count", idDifferent);
        put(result, "max_abs", maxAbs);
        put(result, "absolute_tolerance", .00001);
        put(result, "examples", examples);
    }

    private static final class BenchmarkState {
        final double time;
        final JSONObject imageCheck;
        final ArrayList<Double> proceduralFrames = new ArrayList<>();
        final ArrayList<Double> explicitFrames = new ArrayList<>();
        final ArrayList<Double> proceduralSubmit = new ArrayList<>();
        final ArrayList<Double> explicitSubmit = new ArrayList<>();
        int phase;
        long phaseStart;
        boolean firstFrame = true;
        BenchmarkState(long now, double time, JSONObject imageCheck) {
            phaseStart = now; this.time = time; this.imageCheck = imageCheck;
        }
    }

    private void advanceBenchmark(long frameStart, long afterSubmit, double frameMs, double submitMs) {
        BenchmarkState b = benchmarkState;
        if (b == null) return;
        if (frameMs > 1000 || submitMs > 1000) {
            JSONObject result = contextJson();
            put(result, "status", "stopped"); put(result, "passed", false);
            put(result, "reason", "frame_exceeded_1000_ms");
            put(result, "frame_ms", frameMs); put(result, "cpu_submit_ms", submitMs);
            putMemory(result);
            benchmarkState = null;
            lastAnimationNs = lastFrameNs = 0;
            listener.onBenchmark(benchmarkRequestId, result.toString());
            return;
        }
        if ((b.phase & 1) == 1 && !b.firstFrame && frameMs > 0) {
            (b.phase == 1 ? b.proceduralFrames : b.explicitFrames).add(frameMs);
            (b.phase == 1 ? b.proceduralSubmit : b.explicitSubmit).add(submitMs);
        }
        b.firstFrame = false;
        long duration = (b.phase & 1) == 0 ? WARMUP_NS : SAMPLE_NS;
        if (afterSubmit - b.phaseStart < duration) return;
        GLES30.glFinish();
        checkGl("comparación de rutas");
        b.phase++;
        b.firstFrame = true;
        b.phaseStart = System.nanoTime();
        lastFrameNs = 0;
        resetWindow();
        if (b.phase < 4) return;
        JSONObject result = contextJson();
        boolean enoughSamples = b.proceduralFrames.size() >= 3 && b.explicitFrames.size() >= 3;
        put(result, "status", enoughSamples ? "completed" : "insufficient_samples");
        put(result, "passed", enoughSamples);
        put(result, "image_check", b.imageCheck);
        put(result, "measurement", "wall intervals between onDrawFrame calls; includes swap pacing and scheduling");
        put(result, "cpuMeasurement", "CPU time planning and submitting the frame batches; asynchronous and not elapsed GPU execution");
        put(result, "warmupMsPerRoute", WARMUP_NS / 1_000_000);
        put(result, "sampleMsPerRoute", SAMPLE_NS / 1_000_000);
        put(result, "frozenTimeSeconds", b.time);
        put(result, "cameraAndSceneFrozen", true);
        put(result, "sameMeshShaderAndInstanceCount", true);
        put(result, "routeOrder", "procedural then explicit; thermal/order effects remain possible");
        put(result, "limit", "Soft stop after a returned frame exceeds 1000 ms; cannot interrupt a blocked driver call");
        put(result, "procedural", timingJson(b.proceduralFrames, b.proceduralSubmit));
        put(result, "explicit", timingJson(b.explicitFrames, b.explicitSubmit));
        putMemory(result);
        benchmarkState = null;
        lastAnimationNs = lastFrameNs = 0;
        listener.onBenchmark(benchmarkRequestId, result.toString());
    }

    private JSONObject contextJson() {
        JSONObject json = new JSONObject();
        put(json, "schema", "mundox-object-scene-1");
        put(json, "renderer", rendererName); put(json, "vendor", vendorName);
        put(json, "glVersion", glVersion);
        put(json, "max_texture_size", maxTextureSize);
        put(json, "max_vertex_attributes", maxVertexAttributes);
        put(json, "max_vertex_texture_units", maxVertexTextureUnits);
        updateProjectionPlanes();
        put(json, "depth_bits", depthBits);
        put(json, "projection_near", projectionNear); put(json, "projection_far", projectionFar);
        put(json, "camera_distance", distance);
        put(json, "visibility", visibility); put(json, "draw_suspended", drawSuspended);
        put(json, "animation_enabled", animation);
        put(json, "render_policy", animation ? "continuous" : "when_dirty");
        put(json, "graphics_submission", submissionJson());
        if (scene != null) {
            put(json, "count", scene.count); put(json, "seed", scene.seed);
            put(json, "edits", scene.editCount()); put(json, "version", scene.version);
            put(json, "scene_version", scene.version);
            put(json, "scene_fingerprint", sceneFingerprint());
        }
        put(json, "animation", "analytic global rotation and height ripple; not physics");
        put(json, "viewport_width", width); put(json, "viewport_height", height);
        return json;
    }

    private void putMemory(JSONObject json) {
        JSONObject memory = new JSONObject();
        put(memory, "explicitTransformPayloadBytes", scene == null ? 0 : (long) scene.count * 16L);
        put(memory, "logicalSparsePairsBytes", sparsePayloadBytes());
        put(memory, "logicalRuleAndEditsBytes", scene == null ? 0 : scene.logicalRuleBytes());
        put(memory, "allocatedExplicitVboPayloadBytes", explicitAllocatedBytes);
        put(memory, "allocatedSparseTexturePayloadBytes", sparseAllocatedBytes);
        put(memory, "sharedMeshPayloadBytes", MESH_BYTES);
        put(memory, "sharedMeshVertexPayloadBytes", MESH_VERTEX_BYTES);
        put(memory, "sharedMeshIndexPayloadBytes", MESH_INDEX_BYTES);
        put(memory, "cpuDrawPlannerPrimitiveArrayBytes", drawPlan.storageBytes());
        put(memory, "uploadedPayloadBytesThisContext", uploadedBytes);
        put(memory, "bothRepresentationsCanRemainAllocated", true);
        put(memory, "driverOverheadBytes", JSONObject.NULL);
        put(memory, "totalRamMeasured", false);
        put(json, "memory", memory);
    }

    /** Submission counts are candidates, not measured shader invocations or visible objects. */
    private JSONObject submissionJson() {
        JSONObject json = new JSONObject();
        put(json, "mode", "indexed mesh, bounded ID ranges, conservative frustum clipping");
        put(json, "spatial_culling_enabled", spatialCulling);
        put(json, "last_draw_used_culling", submittedCulling);
        put(json, "submitted_instances", drawPlan.submittedInstances());
        put(json, "culled_instances", drawPlan.culledInstances());
        put(json, "draw_calls", drawPlan.batchCount());
        put(json, "bounds_tested", drawPlan.boundsTested());
        put(json, "maximum_instances_per_draw", ObjectDrawPlan.MAX_BATCH_INSTANCES);
        put(json, "unique_mesh_vertices", MESH_VERTICES);
        put(json, "mesh_indices", MESH_INDICES);
        put(json, "mesh_triangles", MESH_INDICES / 3);
        put(json, "submitted_unique_vertex_candidates", (long) drawPlan.submittedInstances() * MESH_VERTICES);
        put(json, "submitted_triangle_candidates", (long) drawPlan.submittedInstances() * (MESH_INDICES / 3));
        put(json, "gpu_vertex_invocations_measured", false);
        put(json, "visible_object_count_measured", false);
        put(json, "last_draw_viewport_width", submittedViewportWidth);
        put(json, "last_draw_viewport_height", submittedViewportHeight);
        put(json, "last_draw_time_seconds", submittedTime);
        put(json, "planner_primitive_array_bytes", drawPlan.storageBytes());
        return json;
    }

    private long sparsePayloadBytes() { return scene == null ? 0 : 8L * Math.max(1, scene.editCount()); }

    private void fail(String message) {
        failed = true;
        visibility = "error";
        listener.onFailure(message);
    }

    private void suspendDrawing(RuntimeException error) {
        boolean wasExplicit = drawingExplicit();
        benchmarkState = null;
        pendingBenchmark = pendingVerify = false;
        drawSuspended = true;
        discardVisibilityQuery();
        visibilityRequested = false;
        visibility = "error";
        lastFrameNs = lastAnimationNs = 0;
        resetWindow();
        listener.onStats(new Stats(this));
        listener.onFailure("Dibujo detenido con " + (scene == null ? 0 : scene.count)
                + " objetos (" + (wasExplicit ? "explicit" : "procedural") + ", "
                + rendererName + "): " + safe(error.getMessage())
                + ". Pulsa Recentrar o carga otra escena para reintentarlo.");
    }

    private void resumeDrawing() {
        if (!ready || failed) return;
        if (drawSuspended) {
            dirty = explicitDirty = true;
            // The failed operation was reported. Clear any remaining error
            // flags before retrying; a new driver error will suspend again.
            for (int i = 0; i < 16; i++) if (GLES30.glGetError() == GLES30.GL_NO_ERROR) break;
        }
        drawSuspended = false;
        lastFrameNs = lastAnimationNs = 0;
    }

    private void resetWindow() {
        windowCount = windowPosition = 0;
        Arrays.fill(frameTimes, 0); Arrays.fill(submitTimes, 0);
    }

    private void recordWindow(double frame, double submission) {
        frameTimes[windowPosition] = frame;
        submitTimes[windowPosition] = submission;
        windowPosition = (windowPosition + 1) % WINDOW;
        windowCount = Math.min(WINDOW, windowCount + 1);
    }

    private static JSONObject timingJson(ArrayList<Double> frames, ArrayList<Double> submit) {
        JSONObject json = new JSONObject();
        double[] wall = new double[frames.size()], cpu = new double[submit.size()];
        for (int i = 0; i < wall.length; i++) wall[i] = frames.get(i);
        for (int i = 0; i < cpu.length; i++) cpu[i] = submit.get(i);
        put(json, "samples", wall.length);
        put(json, "frameP50Ms", percentile(wall, wall.length, .5));
        put(json, "frameP95Ms", percentile(wall, wall.length, .95));
        put(json, "cpuSubmitMeanMs", mean(cpu, cpu.length));
        put(json, "cpuSubmitP95Ms", percentile(cpu, cpu.length, .95));
        put(json, "p50_ms", percentile(wall, wall.length, .5));
        put(json, "p95_ms", percentile(wall, wall.length, .95));
        put(json, "cpu_submit_ms", mean(cpu, cpu.length));
        return json;
    }

    private String sceneFingerprint() {
        long hash = 0xcbf29ce484222325L;
        String text = scene.exportText();
        for (int i = 0; i < text.length(); i++) {
            hash ^= text.charAt(i); hash *= 0x100000001b3L;
        }
        return Long.toHexString(hash);
    }

    private static JSONObject compareImages(ByteBuffer actual, ByteBuffer reference, int tolerance) {
        int different = 0, overTolerance = 0, maxDelta = 0, covered = 0, referenceCovered = 0;
        for (int pixel = 0; pixel < actual.capacity() / 4; pixel++) {
            int offset = pixel * 4, delta = 0;
            for (int channel = 0; channel < 4; channel++)
                delta = Math.max(delta, Math.abs((actual.get(offset + channel) & 255)
                        - (reference.get(offset + channel) & 255)));
            if (delta > 0) different++;
            if (delta > tolerance) overTolerance++;
            maxDelta = Math.max(maxDelta, delta);
            if (isObjectPixel(actual, offset)) covered++;
            if (isObjectPixel(reference, offset)) referenceCovered++;
        }
        JSONObject result = new JSONObject();
        put(result, "passed", overTolerance == 0 && covered >= 16 && referenceCovered >= 16);
        put(result, "differing_pixels", different);
        put(result, "pixels_over_tolerance", overTolerance);
        put(result, "max_channel_delta", maxDelta);
        put(result, "tolerance_levels", tolerance);
        put(result, "covered_pixels", covered);
        put(result, "reference_covered_pixels", referenceCovered);
        put(result, "minimum_covered_pixels", 16);
        put(result, "reference_scope", "all scene IDs, indexed batches, spatial clipping disabled");
        return result;
    }

    private static boolean isObjectPixel(ByteBuffer pixels, int offset) {
        return Math.abs((pixels.get(offset) & 255) - 17) > 2
                || Math.abs((pixels.get(offset + 1) & 255) - 22) > 2
                || Math.abs((pixels.get(offset + 2) & 255) - 19) > 2;
    }

    private static String pixelHash(ByteBuffer pixels) {
        long hash = 0xcbf29ce484222325L;
        for (int i = 0; i < pixels.capacity(); i++) {
            hash ^= pixels.get(i) & 255; hash *= 0x100000001b3L;
        }
        return Long.toHexString(hash);
    }

    private static double percentile(double[] values, int count, double quantile) {
        if (count == 0) return 0;
        double[] sorted = Arrays.copyOf(values, count);
        Arrays.sort(sorted);
        return sorted[Math.min(count - 1, Math.max(0, (int) Math.ceil(count * quantile) - 1))];
    }

    private static double mean(double[] values, int count) {
        if (count == 0) return 0;
        double sum = 0; for (int i = 0; i < count; i++) sum += values[i];
        return sum / count;
    }

    private static float boxIntersection(float[] origin, float[] direction,
            float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
        float tMin = 0f, tMax = Float.POSITIVE_INFINITY;
        for (int axis = 0; axis < 3; axis++) {
            float low = axis == 0 ? minX : axis == 1 ? minY : minZ;
            float high = axis == 0 ? maxX : axis == 1 ? maxY : maxZ;
            if (Math.abs(direction[axis]) < 1e-8f) {
                if (origin[axis] < low || origin[axis] > high) return -1;
            } else {
                float a = (low - origin[axis]) / direction[axis];
                float b = (high - origin[axis]) / direction[axis];
                if (a > b) { float swap = a; a = b; b = swap; }
                tMin = Math.max(tMin, a); tMax = Math.min(tMax, b);
                if (tMax < tMin) return -1;
            }
        }
        return tMin;
    }

    private static void put(JSONObject json, String key, Object value) {
        try { json.put(key, value); }
        catch (JSONException error) { throw new IllegalStateException(error); }
    }

    private static String safe(String text) { return text == null ? "unknown" : text; }
    private static float clamp(float x, float low, float high) { return Math.max(low, Math.min(high, x)); }
    private static FloatBuffer floats(float[] values) {
        FloatBuffer data = ByteBuffer.allocateDirect(values.length * 4)
                .order(ByteOrder.nativeOrder()).asFloatBuffer();
        data.put(values).position(0);
        return data;
    }

    private static ShortBuffer shorts(short[] values) {
        ShortBuffer data = ByteBuffer.allocateDirect(values.length * 2)
                .order(ByteOrder.nativeOrder()).asShortBuffer();
        data.put(values).position(0);
        return data;
    }

    private static void checkGl(String operation) {
        int error = GLES30.glGetError();
        if (error != GLES30.GL_NO_ERROR) {
            while (GLES30.glGetError() != GLES30.GL_NO_ERROR) { /* clear remaining errors */ }
            throw new IllegalStateException(operation + " (0x" + Integer.toHexString(error) + ")");
        }
    }

    private static int shader(int type, String source) {
        int shader = GLES30.glCreateShader(type);
        GLES30.glShaderSource(shader, source);
        GLES30.glCompileShader(shader);
        int[] ok = new int[1]; GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, ok, 0);
        if (ok[0] == 0) {
            String log = GLES30.glGetShaderInfoLog(shader);
            GLES30.glDeleteShader(shader);
            throw new IllegalStateException("Shader: " + log);
        }
        return shader;
    }

    private static int createProgram(String vertex, String fragment) {
        int vs = shader(GLES30.GL_VERTEX_SHADER, vertex);
        int fs;
        try { fs = shader(GLES30.GL_FRAGMENT_SHADER, fragment); }
        catch (RuntimeException error) { GLES30.glDeleteShader(vs); throw error; }
        int program = GLES30.glCreateProgram();
        GLES30.glAttachShader(program, vs); GLES30.glAttachShader(program, fs);
        GLES30.glTransformFeedbackVaryings(program, new String[]{"tfTransform", "tfId"}, GLES30.GL_INTERLEAVED_ATTRIBS);
        GLES30.glLinkProgram(program);
        GLES30.glDeleteShader(vs); GLES30.glDeleteShader(fs);
        int[] ok = new int[1]; GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, ok, 0);
        if (ok[0] == 0) {
            String log = GLES30.glGetProgramInfoLog(program);
            GLES30.glDeleteProgram(program);
            throw new IllegalStateException("Enlace de shader: " + log);
        }
        return program;
    }

    private static String vertexSource() {
        return "#version 300 es\n"
                + "precision highp float; precision highp int;\n"
                + "layout(location=0) in vec3 aPosition; layout(location=1) in vec3 aNormal;\n"
                + "layout(location=2) in vec4 aTransform;\n"
                + "uniform mat4 uVp; uniform int uSide,uProcedural,uEditCount,uSelected; uniform highp int uBaseId;\n"
                + "uniform highp uvec4 uSeedBytes; uniform float uTime; uniform highp isampler2D uEdits;\n"
                + "out vec3 vNormal; out vec3 vWorld; out float vTone; flat out int vSelected;\n"
                + "out vec4 tfTransform; flat out int tfId;\n"
                + ObjectSceneModel.hashGLSL() + "\n"
                + "int editState(int id){ int lo=0,hi=uEditCount-1; int width=textureSize(uEdits,0).x;\n"
                + " for(int step=0;step<13;step++){ if(lo>hi)break; int mid=(lo+hi)/2;\n"
                + " ivec2 item=texelFetch(uEdits,ivec2(mid%width,mid/width),0).rg;\n"
                + " if(item.x==id)return item.y; if(item.x<id)lo=mid+1;else hi=mid-1;}return 0;}\n"
                + "void main(){ highp int id=uBaseId+gl_InstanceID; vec4 transform=aTransform;\n"
                + " if(uProcedural==1){highp uvec4 h=mxHashBytes(id,uSeedBytes); float center=float(uSide-1)*.5;\n"
                + " transform=vec4((float(id%uSide)-center)*2.2,(float(id/uSide)-center)*2.2,\n"
                + " .7+float(h.x)/128.0,float(h.y)/255.0);\n"
                + " int state=uEditCount>0?editState(id):0; if(state==7)transform.z=0.0;\n"
                + " else if(state>0)transform.z+=float(state)*.35;}\n"
                + " tfTransform=transform;tfId=id;\n"
                + " if(transform.z<=0.0){gl_Position=vec4(2.0,2.0,2.0,1.0);"
                + "vNormal=vec3(0,1,0);vWorld=vec3(0);vTone=0.0;vSelected=0;return;}\n"
                + " float h=transform.z*(1.0+.08*sin(uTime*.8+transform.x*.07+transform.y*.06));\n"
                + " vec3 p=aPosition*vec3(.76,h,.76)+vec3(transform.x,h*.5,transform.y);\n"
                + " float c=cos(uTime*.12),s=sin(uTime*.12);\n"
                + " mat3 rotation=mat3(c,0.0,-s,0.0,1.0,0.0,s,0.0,c);\n"
                + " vWorld=rotation*p;vNormal=rotation*aNormal;vTone=transform.w;\n"
                + " vSelected=id==uSelected?1:0;gl_Position=uVp*vec4(vWorld,1.0);}\n";
    }

    private static final String FRAGMENT_SOURCE = "#version 300 es\n"
            + "precision highp float;precision highp int;\n"
            + "in vec3 vNormal;in vec3 vWorld;in float vTone;flat in int vSelected;\n"
            + "uniform vec3 uEye;uniform float uFogDistance;out vec4 outColor;\n"
            + "void main(){vec3 bone=vec3(.925,.902,.839),lime=vec3(.839,.929,.404),"
            + "coral=vec3(.875,.506,.424),ink=vec3(.067,.086,.075);\n"
            + "vec3 color=vTone<.65?mix(bone,lime,vTone*.55):mix(bone,coral,(vTone-.65)/.35);"
            + "if(vSelected==1)color=lime;\n"
            + "float light=.32+.68*max(dot(normalize(vNormal),normalize(vec3(-.6,1.0,.3))),0.0);\n"
            + "float fog=smoothstep(uFogDistance*.45,uFogDistance*1.5,distance(uEye,vWorld))*.7;\n"
            + "outColor=vec4(mix(color*light,ink,fog),1.0);}\n";

    private static float[] cubeMesh() {
        float[][] faces = {
                {-.5f,-.5f,.5f, .5f,-.5f,.5f, .5f,.5f,.5f, -.5f,.5f,.5f, 0,0,1},
                {.5f,-.5f,-.5f, -.5f,-.5f,-.5f, -.5f,.5f,-.5f, .5f,.5f,-.5f, 0,0,-1},
                {.5f,-.5f,.5f, .5f,-.5f,-.5f, .5f,.5f,-.5f, .5f,.5f,.5f, 1,0,0},
                {-.5f,-.5f,-.5f, -.5f,-.5f,.5f, -.5f,.5f,.5f, -.5f,.5f,-.5f, -1,0,0},
                {-.5f,.5f,.5f, .5f,.5f,.5f, .5f,.5f,-.5f, -.5f,.5f,-.5f, 0,1,0},
                {-.5f,-.5f,-.5f, .5f,-.5f,-.5f, .5f,-.5f,.5f, -.5f,-.5f,.5f, 0,-1,0}
        };
        float[] result = new float[MESH_VERTICES * 6];
        int cursor = 0;
        for (float[] face : faces) for (int index = 0; index < 4; index++) {
            result[cursor++] = face[index * 3];
            result[cursor++] = face[index * 3 + 1];
            result[cursor++] = face[index * 3 + 2];
            result[cursor++] = face[12]; result[cursor++] = face[13]; result[cursor++] = face[14];
        }
        return result;
    }

    /** Same triangle order and face normals as the original expanded mesh. */
    private static short[] cubeIndices() {
        int[] triangle = {0, 1, 2, 0, 2, 3};
        short[] result = new short[MESH_INDICES];
        int cursor = 0;
        for (int face = 0; face < 6; face++)
            for (int vertex : triangle) result[cursor++] = (short) (face * 4 + vertex);
        return result;
    }
}
