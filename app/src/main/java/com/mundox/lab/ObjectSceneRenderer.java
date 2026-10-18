package com.mundox.lab;

import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;

import org.json.JSONException;
import org.json.JSONObject;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
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
        public final String renderer, vendor, route;
        public final int count, edits, selectedId;
        public final long frames, explicitBytes, sparseBytes, meshBytes;
        public final long allocatedExplicitBytes, allocatedSparseBytes, uploadedBytes;
        public final double frameP50Ms, frameP95Ms, cpuSubmitMs;
        public final boolean benchmarking;

        private Stats(ObjectSceneRenderer r) {
            renderer = r.rendererName;
            vendor = r.vendorName;
            route = r.drawingExplicit() ? "explicit" : "procedural";
            count = r.scene == null ? 0 : r.scene.count;
            edits = r.scene == null ? 0 : r.scene.editCount();
            selectedId = r.selectedId;
            frames = r.frames;
            explicitBytes = (long) count * 16L;
            sparseBytes = r.sparsePayloadBytes();
            meshBytes = MESH_BYTES;
            allocatedExplicitBytes = r.explicitAllocatedBytes;
            allocatedSparseBytes = r.sparseAllocatedBytes;
            uploadedBytes = r.uploadedBytes;
            frameP50Ms = percentile(r.frameTimes, r.windowCount, .50);
            frameP95Ms = percentile(r.frameTimes, r.windowCount, .95);
            cpuSubmitMs = mean(r.submitTimes, r.windowCount);
            benchmarking = r.benchmarkState != null || r.pendingBenchmark;
        }
    }

    private static final int WINDOW = 240;
    private static final int MESH_VERTICES = 36;
    private static final int MESH_BYTES = MESH_VERTICES * 6 * 4;
    private static final int VERIFY_SIZE = 128;
    private static final long WARMUP_NS = 700_000_000L;
    private static final long SAMPLE_NS = 2_500_000_000L;
    private final Listener listener;
    private ObjectSceneModel.Snapshot scene;
    private int width = 1, height = 1, program, vao, meshVbo, explicitVbo, editsTexture;
    private int uVp, uSide, uSeed, uTime, uRoute, uEditCount, uEdits, uSelected, uEye, uFog;
    private String rendererName = "not initialized", vendorName = "not initialized", glVersion = "";
    private int maxTextureSize, maxVertexAttributes, maxVertexTextureUnits;
    private boolean ready, failed, dirty = true, explicitRoute, explicitDirty = true;
    private boolean animation = true, pendingVerify, pendingBenchmark;
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

    public ObjectSceneRenderer(Listener listener) {
        if (listener == null) throw new IllegalArgumentException("Listener required");
        this.listener = listener;
    }

    public void setScene(ObjectSceneModel.Snapshot snapshot) {
        if (snapshot == null) throw new IllegalArgumentException("Scene required");
        if (busy()) { listener.onFailure("Cancela la comparación antes de cambiar la escena."); return; }
        boolean resize = scene == null || scene.count != snapshot.count;
        scene = snapshot;
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
        resetWindow();
    }

    /** Deltas are fractions of the viewport, not pixels. */
    public void orbit(float dx, float dy) {
        if (busy()) return;
        yaw += dx * 3f;
        pitch = clamp(pitch + dy * 3f, .18f, 1.45f);
    }

    /** A value greater than one moves the camera farther away. */
    public void zoom(float factor) {
        if (busy() || !Float.isFinite(factor) || factor <= 0) return;
        distance = clamp(distance * factor, defaultDistance * .22f, defaultDistance * 4f);
    }

    public void select(int id) {
        if (busy()) return;
        selectedId = scene != null && id >= 0 && id < scene.count ? id : -1;
    }

    public void setAnimation(boolean enabled) {
        if (busy()) return;
        animation = enabled;
    }

    public void verify() { verify(0); }

    public void verify(long requestId) {
        if (failed) { listener.onFailure("La GPU no está disponible; vuelve a abrir el laboratorio."); return; }
        if (busy()) { listener.onFailure("Ya hay una comparación en curso."); return; }
        if (scene == null) { listener.onFailure("Primero carga una escena."); return; }
        verificationRequestId = requestId;
        pendingVerify = true;
    }

    public void benchmark() { benchmark(0); }

    public void benchmark(long requestId) {
        if (failed) { listener.onFailure("La GPU no está disponible; vuelve a abrir el laboratorio."); return; }
        if (busy()) { listener.onFailure("Ya hay una comparación en curso."); return; }
        if (scene == null) { listener.onFailure("Primero carga una escena."); return; }
        benchmarkRequestId = requestId;
        pendingBenchmark = true;
    }

    public void cancelBenchmark() {
        boolean active = pendingBenchmark || benchmarkState != null;
        long cancelledRequest = benchmarkRequestId;
        pendingVerify = false;
        pendingBenchmark = false;
        benchmarkState = null;
        lastFrameNs = lastAnimationNs = 0;
        resetWindow();
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
        program = vao = meshVbo = explicitVbo = editsTexture = 0;
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
            GLES30.glGetIntegerv(GLES30.GL_MAX_TEXTURE_SIZE, capability, 0); maxTextureSize = capability[0];
            GLES30.glGetIntegerv(GLES30.GL_MAX_VERTEX_ATTRIBS, capability, 0); maxVertexAttributes = capability[0];
            GLES30.glGetIntegerv(GLES30.GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, capability, 0); maxVertexTextureUnits = capability[0];
            program = createProgram(vertexSource(), FRAGMENT_SOURCE);
            uVp = GLES30.glGetUniformLocation(program, "uVp");
            uSide = GLES30.glGetUniformLocation(program, "uSide");
            uSeed = GLES30.glGetUniformLocation(program, "uSeed");
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
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, MESH_BYTES, floats(cubeMesh()), GLES30.GL_STATIC_DRAW);
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
        if (benchmarkState != null) cancelBenchmark();
    }

    @Override public void onDrawFrame(GL10 unused) {
        long now = System.nanoTime();
        if (lastAnimationNs != 0 && animation && benchmarkState == null && !pendingBenchmark)
            animationSeconds += Math.min(.1, (now - lastAnimationNs) / 1_000_000_000.0);
        lastAnimationNs = now;
        if (!ready || failed || scene == null) {
            GLES30.glClearColor(.067f, .086f, .075f, 1f);
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT | GLES30.GL_DEPTH_BUFFER_BIT);
            return;
        }
        try {
            if (dirty) uploadSparse();
            if (pendingVerify) {
                pendingVerify = false;
                performVerification();
                now = System.nanoTime();
                lastFrameNs = 0;
            }
            if (pendingBenchmark) {
                pendingBenchmark = false;
                ensureExplicit();
                GLES30.glFinish();
                benchmarkState = new BenchmarkState(System.nanoTime(), animationSeconds);
                resetWindow();
                lastFrameNs = 0;
            }
            updateCamera(width, height);
            double drawTime = benchmarkState == null ? animationSeconds : benchmarkState.time;
            long submitStart = System.nanoTime();
            render(drawingExplicit(), (float) drawTime);
            long afterSubmit = System.nanoTime();
            double submitMs = (afterSubmit - submitStart) / 1_000_000.0;
            double frameMs = lastFrameNs == 0 ? 0 : (now - lastFrameNs) / 1_000_000.0;
            lastFrameNs = now;
            frames++;
            if (frameMs > 0 && frameMs < 10_000) recordWindow(frameMs, submitMs);
            if (benchmarkState != null) advanceBenchmark(now, afterSubmit, frameMs, submitMs);
            if (afterSubmit - lastStatsNs >= 500_000_000L) {
                lastStatsNs = afterSubmit;
                checkGl("dibujar escena");
                listener.onStats(new Stats(this));
            }
        } catch (RuntimeException error) {
            benchmarkState = null;
            pendingBenchmark = pendingVerify = false;
            fail("OpenGL: " + error.getMessage());
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
        float cp = (float) Math.cos(pitch);
        eye[0] = (float) Math.sin(yaw) * cp * distance;
        eye[1] = (float) Math.sin(pitch) * distance + .5f;
        eye[2] = (float) Math.cos(yaw) * cp * distance;
        Matrix.setLookAtM(view, 0, eye[0], eye[1], eye[2], 0, .5f, 0, 0, 1, 0);
        Matrix.perspectiveM(projection, 0, 50f, (float) renderWidth / renderHeight,
                .1f, Math.max(100f, distance + scene.side * 4f + 20f));
        Matrix.multiplyMM(vp, 0, projection, 0, view, 0);
    }

    private void render(boolean explicit, float seconds) {
        if (explicit) ensureExplicit();
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
        GLES30.glUseProgram(program);
        GLES30.glUniformMatrix4fv(uVp, 1, false, vp, 0);
        GLES30.glUniform1i(uSide, scene.side);
        GLES30.glUniform1ui(uSeed, scene.seed);
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
        GLES30.glDrawArraysInstanced(GLES30.GL_TRIANGLES, 0, MESH_VERTICES, scene.count);
        GLES30.glBindVertexArray(0);
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
        listener.onPick(hit);
    }

    private void performVerification() {
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
            ByteBuffer procedural = ByteBuffer.allocateDirect(VERIFY_SIZE * VERIFY_SIZE * 4);
            ByteBuffer reference = ByteBuffer.allocateDirect(VERIFY_SIZE * VERIFY_SIZE * 4);
            // Fixed analytic time, including when the live scene is animating.
            render(false, 0f);
            GLES30.glReadPixels(0, 0, VERIFY_SIZE, VERIFY_SIZE,
                    GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, procedural);
            render(true, 0f);
            GLES30.glReadPixels(0, 0, VERIFY_SIZE, VERIFY_SIZE,
                    GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, reference);
            checkGl("leer ambos framebuffers");
            int different = 0, overTolerance = 0, maxDelta = 0, visible = 0;
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
            }
            put(result, "status", "complete");
            put(result, "passed", overTolerance == 0 && visible >= 16);
            put(result, "proof", "RGBA8 offscreen framebuffer readback of both actual render routes");
            put(result, "width", VERIFY_SIZE); put(result, "height", VERIFY_SIZE);
            put(result, "fixedTimeSeconds", 0);
            put(result, "toleranceLevels", 2);
            put(result, "pixelsDifferent", different);
            put(result, "pixelsOverTolerance", overTolerance);
            put(result, "maxChannelDelta", maxDelta);
            put(result, "nonBackgroundPixels", visible);
            put(result, "covered_pixels", visible);
            put(result, "minimum_covered_pixels", 16);
            put(result, "differing_pixels", different);
            put(result, "max_channel_delta", maxDelta);
            put(result, "hash_fnv64", pixelHash(procedural));
            put(result, "reference_hash_fnv64", pixelHash(reference));
            put(result, "temporaryFramebufferAttachmentPayloadBytes", (long) VERIFY_SIZE * VERIFY_SIZE * 6L);
            put(result, "temporaryCpuReadbackBytes", (long) VERIFY_SIZE * VERIFY_SIZE * 8L);
            put(result, "scope", "This camera, seed and edit snapshot; not a proof for every possible scene");
            putMemory(result);
            listener.onVerification(verificationRequestId, result.toString());
        } catch (RuntimeException error) {
            put(result, "status", "error"); put(result, "passed", false);
            put(result, "error", safe(error.getMessage()));
            listener.onVerification(verificationRequestId, result.toString());
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
    }

    private static final class BenchmarkState {
        final double time;
        final ArrayList<Double> proceduralFrames = new ArrayList<>();
        final ArrayList<Double> explicitFrames = new ArrayList<>();
        final ArrayList<Double> proceduralSubmit = new ArrayList<>();
        final ArrayList<Double> explicitSubmit = new ArrayList<>();
        int phase;
        long phaseStart;
        boolean firstFrame = true;
        BenchmarkState(long now, double time) { phaseStart = now; this.time = time; }
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
        put(result, "measurement", "wall intervals between onDrawFrame calls; includes swap pacing and scheduling");
        put(result, "cpuMeasurement", "CPU time submitting one draw; asynchronous and not elapsed GPU execution");
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
        put(memory, "uploadedPayloadBytesThisContext", uploadedBytes);
        put(memory, "bothRepresentationsCanRemainAllocated", true);
        put(memory, "driverOverheadBytes", JSONObject.NULL);
        put(memory, "totalRamMeasured", false);
        put(json, "memory", memory);
    }

    private long sparsePayloadBytes() { return scene == null ? 0 : 8L * Math.max(1, scene.editCount()); }

    private void fail(String message) {
        failed = true;
        listener.onFailure(message);
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
                + "uniform mat4 uVp; uniform int uSide,uProcedural,uEditCount,uSelected;\n"
                + "uniform uint uSeed; uniform float uTime; uniform highp isampler2D uEdits;\n"
                + "out vec3 vNormal; out vec3 vWorld; out float vTone; flat out int vSelected;\n"
                + ObjectSceneModel.hashGLSL() + "\n"
                + "int editState(int id){ int lo=0,hi=uEditCount-1; int width=textureSize(uEdits,0).x;\n"
                + " for(int step=0;step<13;step++){ if(lo>hi)break; int mid=(lo+hi)/2;\n"
                + " ivec2 item=texelFetch(uEdits,ivec2(mid%width,mid/width),0).rg;\n"
                + " if(item.x==id)return item.y; if(item.x<id)lo=mid+1;else hi=mid-1;}return 0;}\n"
                + "void main(){ int id=gl_InstanceID; vec4 transform=aTransform;\n"
                + " if(uProcedural==1){uint h=mxHash(uint(id),uSeed); float center=float(uSide-1)*.5;\n"
                + " transform=vec4((float(id%uSide)-center)*2.2,(float(id/uSide)-center)*2.2,\n"
                + " .7+float(h&255u)/128.0,float((h>>8u)&255u)/255.0);\n"
                + " int state=uEditCount>0?editState(id):0; if(state==7)transform.z=0.0;\n"
                + " else if(state>0)transform.z+=float(state)*.35;}\n"
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
        int[] indices = {0,1,2,0,2,3};
        float[] result = new float[MESH_VERTICES * 6];
        int cursor = 0;
        for (float[] face : faces) for (int index : indices) {
            result[cursor++] = face[index * 3];
            result[cursor++] = face[index * 3 + 1];
            result[cursor++] = face[index * 3 + 2];
            result[cursor++] = face[12]; result[cursor++] = face[13]; result[cursor++] = face[14];
        }
        return result;
    }
}
