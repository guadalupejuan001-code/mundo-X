package com.mundox.graphics;

/**
 * Pure Java gesture classifier. No sensor value is a physical quantum state.
 * A deliberate wrist twist can REQUEST one simulated quantum operation.
 */
public final class WearLinkRules {
    private float previousAx, previousAy, previousAz;
    private boolean hasPrevious;
    private long lastTrigger = -100_000L;

    public void reset() {
        hasPrevious = false;
        lastTrigger = -100_000L;
    }

    public boolean observe(long nowMs, float ax, float ay, float az,
                           float gx, float gy, float gz, boolean armed) {
        if (!Float.isFinite(ax) || !Float.isFinite(ay) || !Float.isFinite(az)
                || !Float.isFinite(gx) || !Float.isFinite(gy) || !Float.isFinite(gz)) {
            return false;
        }
        float dx = ax - previousAx, dy = ay - previousAy, dz = az - previousAz;
        float accelerationChange = hasPrevious ? (float) Math.sqrt(dx * dx + dy * dy + dz * dz) : 0f;
        float angularRate = (float) Math.sqrt(gx * gx + gy * gy + gz * gz);
        hasPrevious = true;
        previousAx = ax;
        previousAy = ay;
        previousAz = az;
        boolean gesture = armed && angularRate >= 2.75f && accelerationChange >= 1.3f;
        if (gesture && nowMs - lastTrigger >= 1800L) {
            lastTrigger = nowMs;
            return true;
        }
        return false;
    }
}
