package com.cosmicx.pc;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.view.accessibility.AccessibilityNodeInfo;

import java.util.concurrent.atomic.AtomicBoolean;

/** Displays only the guest's text cells or framebuffer through the project's GLES presenter. */
public final class PcVideoView extends GLSurfaceView {
    public interface StatusListener {
        /** Called on the Android UI thread. */
        void onStatus(PcVideoRenderer.Status status);
    }

    private final PcVideoRenderer videoRenderer;
    private final AtomicBoolean statusPosted = new AtomicBoolean();
    private volatile StatusListener statusListener;
    private String guestText = "";

    public PcVideoView(Context context) {
        super(context);
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        setPreserveEGLContextOnPause(true);
        videoRenderer = new PcVideoRenderer();
        videoRenderer.setStatusCallback(() -> {
            if (statusPosted.compareAndSet(false, true)) {
                post(() -> {
                    statusPosted.set(false);
                    StatusListener listener = statusListener;
                    if (listener != null) listener.onStatus(videoRenderer.getStatus());
                });
            }
        });
        setRenderer(videoRenderer);
        // No animation or invented frames: the machine requests a redraw after guest changes.
        setRenderMode(RENDERMODE_WHEN_DIRTY);
        setContentDescription("pc-video");
    }

    public void setFrameSource(PcVideoRenderer.FrameSource source) {
        queueEvent(() -> videoRenderer.setFrameSource(source));
        requestRender();
    }

    public void setOutputMode(int mode) {
        PcVideoRenderer.requireMode(mode);
        queueEvent(() -> videoRenderer.setOutputMode(mode));
        requestRender();
    }

    public void setStatusListener(StatusListener listener) {
        statusListener = listener;
        if (listener != null) post(() -> {
            StatusListener current = statusListener;
            if (current != null) current.onStatus(videoRenderer.getStatus());
        });
    }

    public PcVideoRenderer.Status getStatus() { return videoRenderer.getStatus(); }

    /** Test/diagnostic access. Mutating renderer methods and pixel reads require queueEvent. */
    public PcVideoRenderer getVideoRenderer() { return videoRenderer; }

    public void refresh() { requestRender(); }

    /** UI-thread accessibility text copied directly from the native guest text screen. */
    public void setGuestText(String text) {
        guestText = text == null ? "" : text;
    }

    @Override
    public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info) {
        super.onInitializeAccessibilityNodeInfo(info);
        info.setClassName(PcVideoView.class.getName());
        info.setText(guestText.trim().isEmpty() ? "Sin texto en la pantalla del huésped" : guestText);
    }

    @Override
    public void onResume() {
        super.onResume();
        queueEvent(() -> videoRenderer.setPaused(false));
        requestRender();
    }

    @Override
    public void onPause() {
        queueEvent(() -> videoRenderer.setPaused(true));
        super.onPause();
    }
}
