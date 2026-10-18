package com.mundox.lab;

/**
 * Conservative, seed-independent draw ranges for the finite procedural grid.
 * This does not replace objects with proxies or change their geometry. A range
 * is omitted only when its complete maximum-height box is outside one clip
 * plane. Surviving IDs retain their original order and global identity.
 *
 * The two reusable int arrays contain at most count/256 + side range entries,
 * bounded by 5120 for the supported 1024-by-1024 grid. Together with the six
 * double-precision planes their primitive payload is 41152 bytes, allocated
 * once. Array headers and the planner object are excluded from storageBytes().
 * There is no transform, visibility bit, or retained object per scene ID.
 * Instances of this class belong to one render thread.
 */
public final class ObjectDrawPlan {
    public static final int MAX_BATCH_INSTANCES = 16384;
    public static final int MAX_SEGMENT_INSTANCES = 256;
    private static final int MAX_SIDE = 1024;
    private static final int MAX_RANGES = ObjectSceneModel.MAX_COUNT / MAX_SEGMENT_INSTANCES + MAX_SIDE;
    private static final double HALF_WIDTH = .38;
    private static final double MAX_HEIGHT = 5.18;
    private static final double BOUNDS_PADDING = .02;
    // Margin also covers finite-precision matrix products and CPU/GPU trig at
    // clip boundaries. At large arguments we conservatively stop culling.
    private static final double CLIP_RELATIVE_MARGIN = 1e-4;
    private static final float MAX_CULL_ANGLE = 4096f;

    private final int[] starts = new int[MAX_RANGES];
    private final int[] lengths = new int[MAX_RANGES];
    private final double[] planes = new double[24];
    private int batches, submitted, culled, tested;

    /**
     * Recomputes for this exact camera and time; no stale camera/time cache.
     * vp is a column-major OpenGL clip-from-world matrix. seconds must match
     * the float time uniform used by the object shader. Invalid matrices or
     * times fail open to the complete population, still in bounded draws.
     */
    public void update(int count, int side, float[] vp, float seconds, boolean cull) {
        if (count < 1 || count > ObjectSceneModel.MAX_COUNT || side < 1 || side > MAX_SIDE
                || count > side * side || count <= (side - 1) * (side - 1)) {
            throw new IllegalArgumentException("Invalid finite grid count/side");
        }
        batches = submitted = culled = tested = 0;
        if (!cull || !preparePlanes(vp, seconds)) {
            append(0, count);
            return;
        }
        float center = (side - 1) * .5f;
        for (int rowStart = 0; rowStart < count; rowStart += side) {
            int rowCount = Math.min(side, count - rowStart);
            float z = ((rowStart / side) - center) * ObjectSceneModel.GRID_SPACING;
            for (int col = 0; col < rowCount; col += MAX_SEGMENT_INSTANCES) {
                int length = Math.min(MAX_SEGMENT_INSTANCES, rowCount - col);
                float firstX = (col - center) * ObjectSceneModel.GRID_SPACING;
                float lastX = (col + length - 1 - center) * ObjectSceneModel.GRID_SPACING;
                tested++;
                if (!outside(firstX - HALF_WIDTH - BOUNDS_PADDING,
                        lastX + HALF_WIDTH + BOUNDS_PADDING,
                        z - HALF_WIDTH - BOUNDS_PADDING, z + HALF_WIDTH + BOUNDS_PADDING)) {
                    append(rowStart + col, length);
                }
            }
        }
        culled = count - submitted;
    }

    public int batchCount() { return batches; }
    public int firstId(int batch) { checkBatch(batch); return starts[batch]; }
    public int instanceCount(int batch) { checkBatch(batch); return lengths[batch]; }
    public int submittedInstances() { return submitted; }
    public int culledInstances() { return culled; }
    public int boundsTested() { return tested; }
    public long storageBytes() { return (starts.length + lengths.length) * 4L + planes.length * 8L; }

    private void checkBatch(int batch) {
        if (batch < 0 || batch >= batches) throw new IndexOutOfBoundsException("Draw batch " + batch);
    }

    private void append(int first, int length) {
        submitted += length;
        while (length > 0) {
            if (batches > 0 && starts[batches - 1] + lengths[batches - 1] == first
                    && lengths[batches - 1] < MAX_BATCH_INSTANCES) {
                int joined = Math.min(length, MAX_BATCH_INSTANCES - lengths[batches - 1]);
                lengths[batches - 1] += joined;
                first += joined;
                length -= joined;
            } else {
                int added = Math.min(length, MAX_BATCH_INSTANCES);
                starts[batches] = first;
                lengths[batches] = added;
                batches++;
                first += added;
                length -= added;
            }
        }
    }

    private boolean preparePlanes(float[] vp, float seconds) {
        if (vp == null || vp.length < 16 || !finite(seconds)) return false;
        for (int i = 0; i < 16; i++) if (!finite(vp[i])) return false;
        if (determinant(vp) == 0.0) return false;
        float angle = seconds * .12f;
        if (!finite(angle) || Math.abs(angle) > MAX_CULL_ANGLE) return false;
        float cosine = (float) Math.cos(angle), sine = (float) Math.sin(angle);
        for (int axis = 0; axis < 3; axis++) {
            for (int direction = 0; direction < 2; direction++) {
                double sign = direction == 0 ? 1.0 : -1.0;
                double a = vp[3] + sign * vp[axis];
                double b = vp[7] + sign * vp[4 + axis];
                double c = vp[11] + sign * vp[8 + axis];
                double d = vp[15] + sign * vp[12 + axis];
                int offset = (axis * 2 + direction) * 4;
                // The shader rotates x'=cos*x+sin*z, z'=-sin*x+cos*z.
                // Substitution moves the plane into the unrotated grid.
                planes[offset] = a * cosine - c * sine;
                planes[offset + 1] = b;
                planes[offset + 2] = a * sine + c * cosine;
                planes[offset + 3] = d;
            }
        }
        return true;
    }

    private boolean outside(double minX, double maxX, double minZ, double maxZ) {
        double minY = -BOUNDS_PADDING, maxY = MAX_HEIGHT + BOUNDS_PADDING;
        for (int offset = 0; offset < planes.length; offset += 4) {
            double a = planes[offset], b = planes[offset + 1];
            double c = planes[offset + 2], d = planes[offset + 3];
            double x = a >= 0 ? maxX : minX;
            double y = b >= 0 ? maxY : minY;
            double z = c >= 0 ? maxZ : minZ;
            double maximum = a * x + b * y + c * z + d;
            double error = CLIP_RELATIVE_MARGIN * (Math.abs(a) * Math.max(Math.abs(minX), Math.abs(maxX))
                    + Math.abs(b) * maxY + Math.abs(c) * Math.max(Math.abs(minZ), Math.abs(maxZ))
                    + Math.abs(d) + 1.0);
            if (maximum < -error) return true;
        }
        return false;
    }

    private static boolean finite(float value) {
        return !Float.isNaN(value) && !Float.isInfinite(value);
    }

    private static double determinant(float[] m) {
        // A singular clip transform is an invalid camera, not proof that the
        // scene is invisible. Double intermediates also avoid float overflow.
        double a0 = (double) m[0] * m[5] - (double) m[1] * m[4];
        double a1 = (double) m[0] * m[6] - (double) m[2] * m[4];
        double a2 = (double) m[0] * m[7] - (double) m[3] * m[4];
        double a3 = (double) m[1] * m[6] - (double) m[2] * m[5];
        double a4 = (double) m[1] * m[7] - (double) m[3] * m[5];
        double a5 = (double) m[2] * m[7] - (double) m[3] * m[6];
        double b0 = (double) m[8] * m[13] - (double) m[9] * m[12];
        double b1 = (double) m[8] * m[14] - (double) m[10] * m[12];
        double b2 = (double) m[8] * m[15] - (double) m[11] * m[12];
        double b3 = (double) m[9] * m[14] - (double) m[10] * m[13];
        double b4 = (double) m[9] * m[15] - (double) m[11] * m[13];
        double b5 = (double) m[10] * m[15] - (double) m[11] * m[14];
        return a0 * b5 - a1 * b4 + a2 * b3 + a3 * b2 - a4 * b1 + a5 * b0;
    }
}
