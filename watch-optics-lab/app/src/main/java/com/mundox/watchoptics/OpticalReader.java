package com.mundox.watchoptics;

/**
 * Portable application-owned contract. The Samsung implementation is
 * compiled only after the developer supplies the licensed official AAR.
 * Raw PPG values are ADC-derived digital counts, NOT individual photons.
 */
public interface OpticalReader {
    interface Callback {
        void onPpg(long sensorTimestampMs,
                   Integer green, Integer red, Integer infrared,
                   Integer greenStatus, Integer redStatus, Integer infraredStatus);
        void onStatus(String description);
    }
    void start();
    void stop();
}
