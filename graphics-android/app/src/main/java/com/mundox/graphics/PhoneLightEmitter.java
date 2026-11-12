package com.mundox.graphics;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.os.Handler;
import android.os.Looper;

/**
 * Real physical actuator. The phone LED emits actual photons into the room.
 * It is NOT quantum entanglement or manipulation of spacetime geometry.
 */
final class PhoneLightEmitter {
    static final int REQUEST_CAMERA = 845;
    private static final long MAX_PULSE_MS = 1200L;
    private final Activity host;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final CameraManager camera;
    private final Runnable off = this::off;
    private String cameraId;
    private boolean enabled, lit;

    PhoneLightEmitter(Activity host) {
        this.host = host;
        this.camera = (CameraManager) host.getSystemService(Context.CAMERA_SERVICE);
    }

    boolean enabled() { return enabled; }

    String toggle() {
        if (enabled) {
            enabled = false;
            off();
            return "Salida de fotones desactivada";
        }
        if (host.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            host.requestPermissions(new String[]{ Manifest.permission.CAMERA }, REQUEST_CAMERA);
            return "Autoriza la cámara para usar solo la linterna";
        }
        return enable();
    }

    String onPermissionResult(boolean granted) {
        return granted ? enable() : "Sin permiso. No se emitió ningún pulso";
    }

    private String enable() {
        if (camera == null) return "Este teléfono no ofrece CameraManager";
        try {
            for (String id : camera.getCameraIdList()) {
                Boolean available = camera.getCameraCharacteristics(id)
                        .get(CameraCharacteristics.FLASH_INFO_AVAILABLE);
                if (Boolean.TRUE.equals(available)) {
                    cameraId = id;
                    enabled = true;
                    return "Salida física habilitada (LED de 1,2 s máximo)";
                }
            }
            return "No encontramos flash físico compatible";
        } catch (CameraAccessException | SecurityException | IllegalArgumentException e) {
            return "No se puede abrir el LED: " + e.getMessage();
        }
    }

    boolean pulse() {
        if (!enabled || camera == null || cameraId == null) return false;
        // Never extend an existing pulse; it must finish before another is accepted.
        if (lit) return false;
        try {
            camera.setTorchMode(cameraId, true);
            lit = true;
            handler.postDelayed(off, MAX_PULSE_MS);
            return true;
        } catch (CameraAccessException | SecurityException | IllegalArgumentException e) {
            return false;
        }
    }

    void off() {
        handler.removeCallbacks(off);
        if (lit && camera != null && cameraId != null) {
            try { camera.setTorchMode(cameraId, false); }
            catch (CameraAccessException | SecurityException | IllegalArgumentException ignored) {}
        }
        lit = false;
    }

    void close() {
        enabled = false;
        off();
    }
}
