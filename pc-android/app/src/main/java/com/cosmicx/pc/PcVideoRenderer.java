package com.cosmicx.pc;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;

import com.mundox.graphics.RenderResolution;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Locale;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * A small GLES 3 guest-video presenter, not a virtual 3D GPU. It consumes real VRAM snapshots.
 * Text uses guest CP437/attribute pairs; graphics uses the guest's top-to-bottom RGBA rows.
 * The output FBO and the guest framebuffer have independently reported resolutions.
 */
public final class PcVideoRenderer implements GLSurfaceView.Renderer {
    public static final int TEXT = 0;
    public static final int RGBA = 1;
    public static final int TEXT_COLUMNS = 80;
    public static final int TEXT_ROWS = 25;
    public static final int TEXT_BYTES = TEXT_COLUMNS * TEXT_ROWS * 2;
    public static final int MAX_STRIP_BYTES = 2 * 1024 * 1024;
    public static final long MAX_TEXTURE_BYTES = 512L * 1024 * 1024;

    private static final int GLYPH_WIDTH = 16;
    private static final int GLYPH_HEIGHT = 24;
    private static final int ATLAS_WIDTH = GLYPH_WIDTH * 16;
    private static final int ATLAS_HEIGHT = GLYPH_HEIGHT * 16;
    private static final int TEXT_RASTER_WIDTH = 640;
    private static final int TEXT_RASTER_HEIGHT = 400;

    /** An immutable native snapshot lease. Every successful acquisition is released in finally. */
    public interface FrameSource {
        /** Return null when no guest video exists. Calls run on the GL thread. */
        FrameInfo acquireFrame();
        /** Write CP437 byte/attribute byte pairs into target; return exactly TEXT_BYTES. */
        int copyText(long revision, ByteBuffer target);
        /** Write tightly packed RGBA8 rows, top to bottom; return width * rows * 4. */
        int copyRows(long revision, int firstRow, int rows, ByteBuffer target);
        /** Release the immutable lease even if no upload was needed or a GL allocation failed. */
        void releaseFrame(long revision);
    }

    public static final class FrameInfo {
        public final int kind;
        public final int width;
        public final int height;
        public final long revision;

        public FrameInfo(int kind, int width, int height, long revision) {
            if (kind != TEXT && kind != RGBA) throw new IllegalArgumentException("Formato de vídeo desconocido");
            if (width < 1 || height < 1 || width > 7680 || height > 7680
                    || (long) width * height > 7680L * 4320L)
                throw new IllegalArgumentException("Dimensiones de vídeo huésped fuera de rango");
            if (kind == TEXT && (width != TEXT_COLUMNS || height != TEXT_ROWS))
                throw new IllegalArgumentException("El modo de texto requiere 80×25 celdas");
            this.kind = kind;
            this.width = width;
            this.height = height;
            this.revision = revision;
        }

        int rasterWidth() { return kind == TEXT ? TEXT_RASTER_WIDTH : width; }
        int rasterHeight() { return kind == TEXT ? TEXT_RASTER_HEIGHT : height; }
    }

    /** Immutable, cross-thread diagnostics. Byte counts describe allocated GL textures only. */
    public static final class Status {
        public final boolean contextReady;
        public final boolean paused;
        public final boolean frameAvailable;
        public final int requestedMode;
        public final int activeMode;
        public final int sourceKind;
        public final int sourceWidth;
        public final int sourceHeight;
        public final int rasterWidth;
        public final int rasterHeight;
        public final int outputWidth;
        public final int outputHeight;
        public final int screenWidth;
        public final int screenHeight;
        public final int textureLimit;
        public final long revision;
        public final long frames;
        public final long uploadedBytes;
        public final long textureBytes;
        public final String renderer;
        public final String error;

        private Status(PcVideoRenderer r) {
            contextReady = r.contextReady;
            paused = r.paused;
            frameAvailable = r.frontInfo != null;
            requestedMode = r.requestedMode;
            activeMode = r.activeMode;
            sourceKind = frameAvailable ? r.frontInfo.kind : -1;
            sourceWidth = frameAvailable ? r.frontInfo.width : 0;
            sourceHeight = frameAvailable ? r.frontInfo.height : 0;
            rasterWidth = frameAvailable ? r.frontInfo.rasterWidth() : 0;
            rasterHeight = frameAvailable ? r.frontInfo.rasterHeight() : 0;
            outputWidth = r.outputSize == null ? 0 : r.outputSize.width;
            outputHeight = r.outputSize == null ? 0 : r.outputSize.height;
            screenWidth = r.viewWidth;
            screenHeight = r.viewHeight;
            textureLimit = r.limits == null ? 0 : r.limits.texture;
            revision = frameAvailable ? r.frontInfo.revision : -1;
            frames = r.frames;
            uploadedBytes = r.uploadedBytes;
            textureBytes = r.textureBytes();
            renderer = r.rendererName == null ? "" : r.rendererName;
            error = joinErrors(r.outputError, r.sourceError, r.contextError);
        }

        public String summary() {
            String source = !frameAvailable ? "Sin imagen huésped"
                    : sourceKind == TEXT ? "Huésped: texto 80×25 · raster 640×400"
                    : "Huésped: " + sourceWidth + "×" + sourceHeight + " píxeles";
            String output = outputWidth == 0 ? "Salida GL sin framebuffer"
                    : "Salida " + modeName(activeMode) + ": " + outputWidth + "×" + outputHeight;
            return source + "\n" + output + " · FIT"
                    + (paused ? " · pausado" : "")
                    + (error.isEmpty() ? "" : "\n" + error);
        }
    }

    private static final String VERTEX = "#version 300 es\n"
            + "precision highp float;out vec2 uv;\n"
            + "void main(){vec2 p=vec2(float(gl_VertexID&1),float((gl_VertexID>>1)&1));"
            + "uv=p;gl_Position=vec4(p.x*2.0-1.0,1.0-p.y*2.0,0.0,1.0);}";
    private static final String PIXELS = "#version 300 es\n"
            + "precision highp float;in vec2 uv;uniform sampler2D image;uniform int flipY;"
            + "out vec4 color;void main(){vec2 p=vec2(uv.x,flipY==1?1.0-uv.y:uv.y);"
            + "color=vec4(texture(image,p).rgb,1.0);}";
    private static final String CELLS = "#version 300 es\n"
            + "precision highp float;precision highp usampler2D;in vec2 uv;"
            + "uniform usampler2D cells;uniform sampler2D atlas;out vec4 color;\n"
            + "vec3 vga(uint n){vec3 c=vec3(float((n>>2u)&1u),float((n>>1u)&1u),float(n&1u))*0.66666667;"
            + "if(n==6u)c.g=0.33333333;if((n&8u)!=0u)c+=vec3(0.33333333);return c;}\n"
            + "void main(){vec2 at=min(uv,vec2(0.999999))*vec2(80.0,25.0);"
            + "uvec2 cell=texelFetch(cells,ivec2(at),0).rg;uint code=cell.r;uint attr=cell.g;"
            + "vec2 g=vec2(float(code&15u),float(code>>4u));"
            + "float ink=texture(atlas,(g+fract(at))/16.0).r;"
            + "color=vec4(mix(vga((attr>>4u)&7u),vga(attr&15u),ink),1.0);}";

    private FrameSource frameSource;
    private Runnable statusCallback;
    private volatile Status status = new Status(this);
    private Thread glThread;
    private boolean contextReady;
    private boolean paused;
    private int requestedMode = RenderResolution.ADAPTIVE;
    private int activeMode = RenderResolution.ADAPTIVE;
    private boolean outputDirty = true;
    private int viewWidth;
    private int viewHeight;
    private RenderResolution.Limits limits;
    private RenderResolution.Size outputSize;
    private int outputFbo;
    private int outputTexture;
    private int pixelProgram;
    private int textProgram;
    private int glyphAtlas;
    private Texture front;
    private Texture back;
    private FrameInfo frontInfo;
    private final ByteBuffer strip = ByteBuffer.allocateDirect(MAX_STRIP_BYTES).order(ByteOrder.nativeOrder());
    private final ByteBuffer textCells = ByteBuffer.allocateDirect(TEXT_BYTES);
    private final ByteBuffer pixelReadback = ByteBuffer.allocateDirect(4);
    private long frames;
    private long uploadedBytes;
    private String rendererName = "";
    private String outputError = "";
    private String sourceError = "";
    private String contextError = "";

    private static final class Texture {
        final int id;
        final int kind;
        final int width;
        final int height;
        final long bytes;
        Texture(int id, FrameInfo frame) {
            this.id = id;
            kind = frame.kind;
            width = frame.width;
            height = frame.height;
            bytes = (long) width * height * (kind == TEXT ? 2 : 4);
        }
        boolean matches(FrameInfo frame) {
            return frame.kind == kind && frame.width == width && frame.height == height;
        }
    }

    public static void requireMode(int mode) {
        if (!RenderResolution.validMode(mode)) throw new IllegalArgumentException("Modo de salida inválido");
    }

    public static String modeName(int mode) {
        return mode == RenderResolution.UHD_8K ? "8K"
                : mode == RenderResolution.UHD_4K ? "4K" : "adaptativa";
    }

    public Status getStatus() { return status; }

    void setStatusCallback(Runnable callback) { statusCallback = callback; }

    /** GL-thread call; the view's setter queues it. */
    public void setFrameSource(FrameSource source) {
        frameSource = source;
        if (contextReady) {
            deleteTexture(front);
            deleteTexture(back);
        }
        front = back = null;
        frontInfo = null;
        sourceError = "";
        publishStatus();
    }

    /** GL-thread call; a failed request preserves the last complete output FBO and activeMode. */
    public void setOutputMode(int mode) {
        requireMode(mode);
        requestedMode = mode;
        outputDirty = true;
    }

    void setPaused(boolean value) {
        paused = value;
        publishStatus();
    }

    @Override
    public void onSurfaceCreated(GL10 ignored, EGLConfig config) {
        glThread = Thread.currentThread();
        // A new EGL context invalidates every old name, even with context preservation requested.
        outputFbo = outputTexture = pixelProgram = textProgram = glyphAtlas = 0;
        outputSize = null;
        front = back = null;
        frontInfo = null;
        outputDirty = true;
        contextReady = false;
        contextError = outputError = sourceError = "";
        try {
            int[] maximum = new int[2];
            GLES30.glGetIntegerv(GLES30.GL_MAX_TEXTURE_SIZE, maximum, 0);
            int texture = maximum[0];
            GLES30.glGetIntegerv(GLES30.GL_MAX_RENDERBUFFER_SIZE, maximum, 0);
            int renderbuffer = maximum[0];
            GLES30.glGetIntegerv(GLES30.GL_MAX_VIEWPORT_DIMS, maximum, 0);
            limits = new RenderResolution.Limits(texture, renderbuffer, maximum[0], maximum[1]);
            rendererName = String.valueOf(GLES30.glGetString(GLES30.GL_RENDERER));
            pixelProgram = program(VERTEX, PIXELS);
            textProgram = program(VERTEX, CELLS);
            glyphAtlas = createGlyphAtlas();
            GLES30.glDisable(GLES30.GL_DEPTH_TEST);
            GLES30.glDisable(GLES30.GL_BLEND);
            GLES30.glDisable(GLES30.GL_CULL_FACE);
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 1);
            checkGl("Inicialización de vídeo");
            contextReady = true;
        } catch (RuntimeException | OutOfMemoryError error) {
            contextError = "No se pudo iniciar OpenGL ES 3: " + errorMessage(error);
            releaseContextResources();
        }
        publishStatus();
    }

    @Override
    public void onSurfaceChanged(GL10 ignored, int width, int height) {
        viewWidth = Math.max(1, width);
        viewHeight = Math.max(1, height);
        outputDirty = true;
    }

    @Override
    public void onDrawFrame(GL10 ignored) {
        if (!contextReady || viewWidth == 0 || viewHeight == 0 || paused) {
            publishStatus();
            return;
        }
        if (outputDirty) updateOutput();
        updateGuestTexture();
        try {
            GLES30.glDisable(GLES30.GL_SCISSOR_TEST);
            GLES30.glDisable(GLES30.GL_DEPTH_TEST);
            GLES30.glDisable(GLES30.GL_BLEND);
            if (outputFbo != 0) {
                GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, outputFbo);
                GLES30.glViewport(0, 0, outputSize.width, outputSize.height);
                clear();
                if (frontInfo != null) {
                    RenderResolution.Fit fit = RenderResolution.fit(outputSize.width, outputSize.height,
                            new RenderResolution.Size(frontInfo.rasterWidth(), frontInfo.rasterHeight()));
                    GLES30.glViewport(fit.left, outputSize.height - fit.top - fit.height, fit.width, fit.height);
                    drawGuest();
                    frames++;
                }
            }
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0);
            GLES30.glViewport(0, 0, viewWidth, viewHeight);
            clear();
            if (outputFbo != 0) {
                RenderResolution.Fit fit = RenderResolution.fit(viewWidth, viewHeight, outputSize);
                GLES30.glViewport(fit.left, viewHeight - fit.top - fit.height, fit.width, fit.height);
                drawTexture(outputTexture, true);
            }
            checkGl("Presentación de vídeo");
            contextError = "";
        } catch (RuntimeException error) {
            contextError = "Error de presentación GL: " + errorMessage(error);
        }
        publishStatus();
    }

    private void updateOutput() {
        outputDirty = false;
        int newFbo = 0;
        int newTexture = 0;
        try {
            RenderResolution.Size next = requestedMode == RenderResolution.ADAPTIVE
                    ? RenderResolution.adaptive(viewWidth, viewHeight, 4096, limits)
                    : RenderResolution.fixed(requestedMode, viewWidth, viewHeight);
            limits.require(next);
            if (outputSize != null && next.width == outputSize.width && next.height == outputSize.height) {
                activeMode = requestedMode;
                outputError = "";
                return;
            }
            RenderResolution.requireAllocation(next, outputSize);
            requireTextureBudget(next.pixels() * 4);
            clearErrors();
            newTexture = allocateTexture(next.width, next.height, GLES30.GL_RGBA8,
                    GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, GLES30.GL_LINEAR);
            int[] names = new int[1];
            GLES30.glGenFramebuffers(1, names, 0);
            newFbo = names[0];
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, newFbo);
            GLES30.glFramebufferTexture2D(GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0,
                    GLES30.GL_TEXTURE_2D, newTexture, 0);
            int completeness = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER);
            checkGl("Reserva de la salida " + modeName(requestedMode));
            if (completeness != GLES30.GL_FRAMEBUFFER_COMPLETE)
                throw new IllegalStateException("Framebuffer incompleto: 0x" + Integer.toHexString(completeness));
            GLES30.glViewport(0, 0, next.width, next.height);
            clear();
            checkGl("Primer borrado del framebuffer");
            // Commit only after a complete, usable replacement exists. Old output is still valid above.
            deleteFramebuffer(outputFbo);
            deleteTextureName(outputTexture);
            outputFbo = newFbo;
            outputTexture = newTexture;
            outputSize = next;
            activeMode = requestedMode;
            newFbo = newTexture = 0;
            outputError = "";
        } catch (RuntimeException | OutOfMemoryError error) {
            outputError = "Salida " + modeName(requestedMode) + " no disponible: " + errorMessage(error)
                    + (outputSize == null ? "" : ". Se conserva " + outputSize.width + "×" + outputSize.height);
        } finally {
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0);
            deleteFramebuffer(newFbo);
            deleteTextureName(newTexture);
        }
        // Restoring an 8K preference in a different/lost context may fail before any FBO exists.
        // Bootstrap an honest adaptive output while retaining the rejected requested mode/error.
        if (outputSize == null && requestedMode != RenderResolution.ADAPTIVE) {
            int rejectedMode = requestedMode;
            String rejectedError = outputError;
            requestedMode = RenderResolution.ADAPTIVE;
            updateOutput();
            requestedMode = rejectedMode;
            outputError = rejectedError + (outputSize == null
                    ? ". La salida adaptativa tampoco pudo iniciarse: " + outputError
                    : ". Se usa salida adaptativa " + outputSize.width + "×" + outputSize.height);
        }
    }

    private void updateGuestTexture() {
        if (frameSource == null) return;
        FrameSource source = frameSource;
        FrameInfo frame = null;
        try {
            frame = source.acquireFrame();
            if (frame == null) return;
            if (frontInfo != null && frontInfo.revision == frame.revision && front.matches(frame)) {
                sourceError = "";
                return;
            }
            if (frame.width > limits.texture || frame.height > limits.texture)
                throw new IllegalArgumentException("GL_MAX_TEXTURE_SIZE=" + limits.texture
                        + " no admite el vídeo huésped " + frame.width + "×" + frame.height);
            if (back != null && !back.matches(frame)) {
                deleteTexture(back);
                back = null;
            }
            if (back == null) {
                requireTextureBudget((long) frame.width * frame.height * (frame.kind == TEXT ? 2 : 4));
                int name = allocateTexture(frame.width, frame.height,
                        frame.kind == TEXT ? GLES30.GL_RG8UI : GLES30.GL_RGBA8,
                        frame.kind == TEXT ? GLES30.GL_RG_INTEGER : GLES30.GL_RGBA,
                        GLES30.GL_UNSIGNED_BYTE, GLES30.GL_NEAREST);
                back = new Texture(name, frame);
            }
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0);
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, back.id);
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 1);
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ROW_LENGTH, 0);
            GLES30.glBindBuffer(GLES30.GL_PIXEL_UNPACK_BUFFER, 0);
            if (frame.kind == TEXT) {
                textCells.clear();
                int count = source.copyText(frame.revision, textCells);
                if (count != TEXT_BYTES) throw new IllegalStateException("Snapshot de texto incompleto: " + count);
                textCells.position(0);
                textCells.limit(TEXT_BYTES);
                GLES30.glTexSubImage2D(GLES30.GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height,
                        GLES30.GL_RG_INTEGER, GLES30.GL_UNSIGNED_BYTE, textCells);
                checkGl("Subida de las celdas de texto");
                uploadedBytes += TEXT_BYTES;
            } else {
                int rowBytes = Math.multiplyExact(frame.width, 4);
                int rowsPerStrip = Math.max(1, Math.min(64, MAX_STRIP_BYTES / rowBytes));
                for (int y = 0; y < frame.height; y += rowsPerStrip) {
                    int rows = Math.min(rowsPerStrip, frame.height - y);
                    int bytes = Math.multiplyExact(rowBytes, rows);
                    strip.clear();
                    strip.limit(bytes);
                    int copied = source.copyRows(frame.revision, y, rows, strip);
                    if (copied != bytes) throw new IllegalStateException("Snapshot de píxeles incompleto: " + copied + "/" + bytes);
                    strip.position(0);
                    strip.limit(bytes);
                    GLES30.glTexSubImage2D(GLES30.GL_TEXTURE_2D, 0, 0, y, frame.width, rows,
                            GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, strip);
                    checkGl("Subida de vídeo huésped, fila " + y);
                    uploadedBytes += bytes;
                }
            }
            Texture previous = front;
            front = back;
            back = previous;
            frontInfo = frame;
            // A text reset immediately releases a large graphics buffer that cannot be reused.
            if (back != null && !back.matches(frame)) {
                deleteTexture(back);
                back = null;
            }
            sourceError = "";
        } catch (RuntimeException | OutOfMemoryError error) {
            sourceError = "Vídeo huésped: " + errorMessage(error)
                    + (frontInfo == null ? "" : ". Se conserva la última imagen completa");
        } finally {
            if (frame != null) {
                try {
                    source.releaseFrame(frame.revision);
                } catch (RuntimeException error) {
                    sourceError = "No se pudo liberar el snapshot de vídeo: " + errorMessage(error);
                }
            }
        }
    }

    private void drawGuest() {
        if (frontInfo.kind == RGBA) {
            drawTexture(front.id, false);
        } else {
            GLES30.glUseProgram(textProgram);
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0);
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, front.id);
            GLES30.glUniform1i(GLES30.glGetUniformLocation(textProgram, "cells"), 0);
            GLES30.glActiveTexture(GLES30.GL_TEXTURE1);
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glyphAtlas);
            GLES30.glUniform1i(GLES30.glGetUniformLocation(textProgram, "atlas"), 1);
            GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    private void drawTexture(int texture, boolean flipY) {
        GLES30.glUseProgram(pixelProgram);
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0);
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture);
        GLES30.glUniform1i(GLES30.glGetUniformLocation(pixelProgram, "image"), 0);
        GLES30.glUniform1i(GLES30.glGetUniformLocation(pixelProgram, "flipY"), flipY ? 1 : 0);
        GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4);
    }

    /** Reads one real output pixel for instrumentation; never allocates or reads an entire 8K image. */
    public int readOutputPixelArgb(int x, int y) {
        if (Thread.currentThread() != glThread) throw new IllegalStateException("La lectura requiere el hilo GL");
        if (outputSize == null || x < 0 || y < 0 || x >= outputSize.width || y >= outputSize.height)
            throw new IllegalArgumentException("Píxel fuera de la salida GL");
        int[] previous = new int[1];
        GLES30.glGetIntegerv(GLES30.GL_READ_FRAMEBUFFER_BINDING, previous, 0);
        try {
            // GL_READ_FRAMEBUFFER does not disturb the draw binding or the viewport.
            GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, outputFbo);
            pixelReadback.clear();
            GLES30.glReadPixels(x, outputSize.height - 1 - y, 1, 1, GLES30.GL_RGBA,
                    GLES30.GL_UNSIGNED_BYTE, pixelReadback);
            checkGl("Lectura de un píxel de salida");
            return (pixelReadback.get(3) & 255) << 24 | (pixelReadback.get(0) & 255) << 16
                    | (pixelReadback.get(1) & 255) << 8 | pixelReadback.get(2) & 255;
        } finally {
            GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, previous[0]);
        }
    }

    private int createGlyphAtlas() {
        // A 96 KiB font atlas is the only Bitmap. Guest pixel frames never use Android Bitmaps.
        Bitmap bitmap = Bitmap.createBitmap(ATLAS_WIDTH, ATLAS_HEIGHT, Bitmap.Config.ALPHA_8);
        try {
            Canvas canvas = new Canvas(bitmap);
            Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
            paint.setColor(Color.WHITE);
            paint.setTypeface(Typeface.MONOSPACE);
            paint.setTextSize(20);
            Paint.FontMetrics metrics = paint.getFontMetrics();
            float baseline = (GLYPH_HEIGHT - (metrics.descent - metrics.ascent)) / 2f - metrics.ascent;
            for (int code = 1; code < 256; code++) {
                String glyph = String.valueOf(cp437(code));
                int left = (code & 15) * GLYPH_WIDTH;
                int top = (code >> 4) * GLYPH_HEIGHT;
                canvas.save();
                canvas.clipRect(left, top, left + GLYPH_WIDTH, top + GLYPH_HEIGHT);
                float advance = paint.measureText(glyph);
                canvas.drawText(glyph, left + (GLYPH_WIDTH - advance) / 2f, top + baseline, paint);
                canvas.restore();
            }
            ByteBuffer pixels = ByteBuffer.allocateDirect(ATLAS_WIDTH * ATLAS_HEIGHT);
            bitmap.copyPixelsToBuffer(pixels);
            pixels.position(0);
            int texture = allocateTexture(ATLAS_WIDTH, ATLAS_HEIGHT, GLES30.GL_R8,
                    GLES30.GL_RED, GLES30.GL_UNSIGNED_BYTE, GLES30.GL_NEAREST);
            try {
                GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture);
                GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 1);
                GLES30.glTexSubImage2D(GLES30.GL_TEXTURE_2D, 0, 0, 0, ATLAS_WIDTH, ATLAS_HEIGHT,
                        GLES30.GL_RED, GLES30.GL_UNSIGNED_BYTE, pixels);
                checkGl("Atlas CP437");
                return texture;
            } catch (RuntimeException error) {
                deleteTextureName(texture);
                throw error;
            }
        } finally {
            bitmap.recycle();
        }
    }

    /** CP437 character mapping; glyph outlines come from Android's installed monospace font. */
    public static char cp437(int code) {
        if (code < 0 || code > 255) throw new IllegalArgumentException("Byte CP437 fuera de rango");
        if (code == 0) return ' ';
        if (code < 32) return "\u0000☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼".charAt(code);
        if (code < 127) return (char) code;
        if (code == 127) return '⌂';
        return ("ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒ"
                + "áíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐"
                + "└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀"
                + "αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ").charAt(code - 128);
    }

    private static int allocateTexture(int width, int height, int internalFormat,
                                       int format, int type, int filtering) {
        clearErrors();
        int[] names = new int[1];
        GLES30.glGenTextures(1, names, 0);
        int texture = names[0];
        try {
            if (texture == 0) throw new IllegalStateException("OpenGL no asignó un nombre de textura");
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, filtering);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, filtering);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE);
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE);
            GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, null);
            checkGl("Reserva de textura " + width + "×" + height);
            return texture;
        } catch (RuntimeException error) {
            deleteTextureName(texture);
            throw error;
        }
    }

    private static int program(String vertexSource, String fragmentSource) {
        int vertex = shader(GLES30.GL_VERTEX_SHADER, vertexSource);
        int fragment = 0;
        int linked = 0;
        try {
            fragment = shader(GLES30.GL_FRAGMENT_SHADER, fragmentSource);
            linked = GLES30.glCreateProgram();
            GLES30.glAttachShader(linked, vertex);
            GLES30.glAttachShader(linked, fragment);
            GLES30.glLinkProgram(linked);
            int[] ok = new int[1];
            GLES30.glGetProgramiv(linked, GLES30.GL_LINK_STATUS, ok, 0);
            if (ok[0] == 0) throw new IllegalStateException(GLES30.glGetProgramInfoLog(linked));
            return linked;
        } catch (RuntimeException error) {
            if (linked != 0) GLES30.glDeleteProgram(linked);
            throw error;
        } finally {
            GLES30.glDeleteShader(vertex);
            if (fragment != 0) GLES30.glDeleteShader(fragment);
        }
    }

    private static int shader(int type, String source) {
        int shader = GLES30.glCreateShader(type);
        GLES30.glShaderSource(shader, source);
        GLES30.glCompileShader(shader);
        int[] ok = new int[1];
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, ok, 0);
        if (ok[0] == 0) {
            String log = GLES30.glGetShaderInfoLog(shader);
            GLES30.glDeleteShader(shader);
            throw new IllegalStateException(log);
        }
        return shader;
    }

    private long textureBytes() {
        return (outputSize == null ? 0 : outputSize.pixels() * 4)
                + (front == null ? 0 : front.bytes) + (back == null ? 0 : back.bytes)
                + (glyphAtlas == 0 ? 0 : ATLAS_WIDTH * ATLAS_HEIGHT);
    }

    private void requireTextureBudget(long additional) {
        long peak = Math.addExact(textureBytes(), additional);
        if (peak > MAX_TEXTURE_BYTES)
            throw new IllegalArgumentException(String.format(Locale.ROOT,
                    "Las texturas simultáneas requieren %.1f MiB; presupuesto 512 MiB", peak / 1048576.0));
    }

    private void releaseContextResources() {
        deleteTexture(front);
        deleteTexture(back);
        deleteTextureName(outputTexture);
        deleteTextureName(glyphAtlas);
        deleteFramebuffer(outputFbo);
        if (pixelProgram != 0) GLES30.glDeleteProgram(pixelProgram);
        if (textProgram != 0) GLES30.glDeleteProgram(textProgram);
        front = back = null;
        frontInfo = null;
        outputSize = null;
        outputFbo = outputTexture = glyphAtlas = pixelProgram = textProgram = 0;
    }

    private static void deleteTexture(Texture texture) {
        if (texture != null) deleteTextureName(texture.id);
    }

    private static void deleteTextureName(int texture) {
        if (texture != 0) GLES30.glDeleteTextures(1, new int[]{texture}, 0);
    }

    private static void deleteFramebuffer(int framebuffer) {
        if (framebuffer != 0) GLES30.glDeleteFramebuffers(1, new int[]{framebuffer}, 0);
    }

    private static void clear() {
        GLES30.glClearColor(0f, 0f, 0f, 1f);
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT);
    }

    private static void clearErrors() {
        for (int i = 0; i < 16 && GLES30.glGetError() != GLES30.GL_NO_ERROR; i++) { }
    }

    private static void checkGl(String operation) {
        int error = GLES30.glGetError();
        if (error != GLES30.GL_NO_ERROR)
            throw new IllegalStateException(operation + ": GL 0x" + Integer.toHexString(error));
    }

    private void publishStatus() {
        status = new Status(this);
        if (statusCallback != null) statusCallback.run();
    }

    private static String errorMessage(Throwable error) {
        return error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage();
    }

    private static String joinErrors(String... errors) {
        StringBuilder value = new StringBuilder();
        for (String error : errors) {
            if (error != null && !error.isEmpty()) {
                if (value.length() != 0) value.append('\n');
                value.append(error);
            }
        }
        return value.toString();
    }
}
