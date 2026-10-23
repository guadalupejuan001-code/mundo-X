package com.mundox.watchoptics;

import android.app.Activity;

import com.samsung.android.service.health.tracking.ConnectionListener;
import com.samsung.android.service.health.tracking.HealthTracker;
import com.samsung.android.service.health.tracking.HealthTrackerException;
import com.samsung.android.service.health.tracking.HealthTrackingService;
import com.samsung.android.service.health.tracking.data.DataPoint;
import com.samsung.android.service.health.tracking.data.HealthTrackerType;
import com.samsung.android.service.health.tracking.data.ValueKey;

import java.util.List;

/**
 * OPT-IN Samsung SDK integration. Only compiled when the developer supplies
 * the official samsung-health-sensor-api.aar locally.
 *
 * The SDK returns ADC-derived PPG data from Health Sensor Service.
 * This does not control the hardware LED current and does not preserve photons
 * or coherence. Capability and explicit user permission must be checked.
 */
public final class SamsungPpgReader implements OpticalReader {
    private final Activity activity;
    private final Callback callback;
    private HealthTrackingService service;
    private HealthTracker tracker;
    private boolean running;

    public SamsungPpgReader(Activity activity, Callback callback) {
        this.activity = activity;
        this.callback = callback;
    }

    @Override public void start() {
        if (running) return;
        running = true;
        try {
            service = new HealthTrackingService(new ConnectionListener() {
                @Override public void onConnectionSuccess() {
                    if (!running || service == null) return;
                    try {
                        List<HealthTrackerType> supported =
                                service.getTrackingCapability().getSupportHealthTrackerTypes();
                        if (!supported.contains(HealthTrackerType.PPG_CONTINUOUS)) {
                            callback.onStatus("PPG_CONTINUOUS no disponible en este Watch/firmware");
                            stop();
                            return;
                        }
                        tracker = service.getHealthTracker(HealthTrackerType.PPG_CONTINUOUS);
                        tracker.setEventListener(events);
                        callback.onStatus("PPG conectado: muestras ADC verde/rojo/IR cuando existan");
                    } catch (RuntimeException error) {
                        callback.onStatus("No se pudo iniciar PPG: " + error.getClass().getSimpleName());
                        stop();
                    }
                }

                @Override public void onConnectionEnded() {
                    if (running) callback.onStatus("Servicio óptico desconectado");
                }

                @Override public void onConnectionFailed(HealthTrackerException exception) {
                    callback.onStatus("PPG SDK sin acceso: " + exception.getErrorCode() +
                            ". Revisar compatibilidad, permiso o aprobación Samsung.");
                    stop();
                }
            }, activity.getApplicationContext());
            service.connectService();
        } catch (RuntimeException error) {
            callback.onStatus("PPG SDK no iniciado: " + error.getClass().getSimpleName());
            stop();
        }
    }

    private Integer value(DataPoint point, ValueKey<Integer> key) {
        try { return point.getValue(key); }
        catch (RuntimeException ignored) { return null; }
    }

    private final HealthTracker.TrackerEventListener events = new HealthTracker.TrackerEventListener() {
        @Override public void onDataReceived(List<DataPoint> points) {
            if (!running) return;
            for (DataPoint p : points) {
                callback.onPpg(
                        p.getTimestamp(),
                        value(p, ValueKey.PpgSet.PPG_GREEN),
                        value(p, ValueKey.PpgSet.PPG_RED),
                        value(p, ValueKey.PpgSet.PPG_IR),
                        value(p, ValueKey.PpgSet.GREEN_STATUS),
                        value(p, ValueKey.PpgSet.RED_STATUS),
                        value(p, ValueKey.PpgSet.IR_STATUS));
            }
        }

        @Override public void onFlushCompleted() {}

        @Override public void onError(HealthTracker.TrackerError error) {
            callback.onStatus("Error de rastreo PPG: " + error);
            // The user can stop the session; avoid silent fallback to fake data.
        }
    };

    @Override public void stop() {
        running = false;
        if (tracker != null) {
            try { tracker.unsetEventListener(); } catch (RuntimeException ignored) {}
            tracker = null;
        }
        if (service != null) {
            try { service.disconnectService(); } catch (RuntimeException ignored) {}
            service = null;
        }
    }
}
