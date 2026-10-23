package com.mundox.worldlink.phone;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.os.Handler;
import android.os.Looper;

/** Real photon output through the smartphone's built-in LED, under explicit consent. */
final class PhotonEmitter {
    static final int PERMISSION_REQUEST = 802;
    private static final long MAX_PULSE_MS = 950L;
    private final Activity context;
    private final CameraManager manager;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean on;
    private String flashId;
    private final Runnable off = this::off;

    PhotonEmitter(Activity activity) {
        context = activity;
        manager = (CameraManager) activity.getSystemService(Context.CAMERA_SERVICE);
    }
    boolean hasPermission() {
        return context.checkSelfPermission(Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED;
    }
    void requestPermission() {
        if (!hasPermission()) {
            context.requestPermissions(new String[]{Manifest.permission.CAMERA},PERMISSION_REQUEST);
        }
    }
    String availability() {
        if (!hasPermission()) return "Permiso de cámara pendiente";
        if (manager == null) return "No hay servicio de cámara";
        try {
            if (flashId == null) {
                for (String id : manager.getCameraIdList()) {
                    Boolean flash = manager.getCameraCharacteristics(id)
                            .get(CameraCharacteristics.FLASH_INFO_AVAILABLE);
                    if (Boolean.TRUE.equals(flash)) { flashId=id; break; }
                }
            }
            return flashId == null ? "No se encontró LED" : "LED disponible";
        } catch (CameraAccessException | SecurityException exception) {
            return "LED inaccesible: " + exception.getClass().getSimpleName();
        }
    }
    String pulse() {
        if (!hasPermission()) return "Permiso de cámara requerido";
        if (on) return "Pulso anterior todavía activo";
        String state=availability();
        if (flashId==null) return state;
        try {
            manager.setTorchMode(flashId,true);
            on=true;
            handler.postDelayed(off,MAX_PULSE_MS);
            return "Luz real emitida durante ≤ 0,95 s";
        } catch (CameraAccessException | SecurityException | IllegalArgumentException e) {
            return "No se pudo encender el LED: "+e.getClass().getSimpleName();
        }
    }
    boolean isOn(){return on;}
    void off() {
        handler.removeCallbacks(off);
        if (on && manager!=null && flashId!=null) {
            try { manager.setTorchMode(flashId,false); }
            catch (CameraAccessException | SecurityException | IllegalArgumentException ignored) {}
        }
        on=false;
    }
}