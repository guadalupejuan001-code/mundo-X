package com.mundox.lab;

/** Pure-Java regression checks, including an independent clip-corner oracle. */
public final class ObjectDrawPlanChecks {
    private ObjectDrawPlanChecks() { }

    public static final class Result {
        public final int passed;
        public final String summary;
        private Result(int passed) {
            this.passed = passed;
            summary = "ObjectDrawPlan: " + passed + " checks passed";
        }
        @Override public String toString() { return summary; }
    }

    public static Result runAll() {
        Checks c = new Checks();
        checkCompleteAndEmpty(c);
        checkIndependentOracle(c);
        checkCameraExtremes(c);
        checkGrazingAndHeights(c);
        checkInvalidAndReuse(c);
        return new Result(c.passed);
    }

    public static void main(String[] args) { System.out.println(runAll().summary); }

    private static void checkCompleteAndEmpty(Checks c) {
        ObjectDrawPlan plan = new ObjectDrawPlan();
        int[] counts = {1, 2, 37, 1024, 16383, 16384, 16385, 65535, 65536, 65537, 1048576};
        float[] full = ortho(-4096, 4096, -4096, 4096, -4096, 4096);
        float[] empty = ortho(10000, 10001, -10, 10, -4096, 4096);
        for (int count : counts) {
            int side = side(count);
            plan.update(count, side, null, Float.NaN, false);
            checkRanges(c, plan, count);
            c.that(plan.submittedInstances() == count && plan.culledInstances() == 0,
                    "disabled culling covers all " + count);
            c.that(plan.batchCount() == (count + 16383) / 16384 && plan.boundsTested() == 0,
                    "bounded full draw " + count);
            plan.update(count, side, full, 72.5f, true);
            checkRanges(c, plan, count);
            c.that(plan.submittedInstances() == count && plan.culledInstances() == 0,
                    "full frustum does not discard " + count);
            int segments = 0;
            for (int first = 0; first < count; first += side) {
                segments += (Math.min(side, count - first) + 255) / 256;
            }
            c.that(plan.boundsTested() == segments, "every row segment tested " + count);
            plan.update(count, side, empty, 72.5f, true);
            checkRanges(c, plan, count);
            c.that(plan.batchCount() == 0 && plan.culledInstances() == count,
                    "disjoint frustum draws nothing " + count);
        }
        c.that(plan.storageBytes() == 41152, "bounded primitive storage independent of count");
    }

    private static void checkIndependentOracle(Checks c) {
        ObjectDrawPlan plan = new ObjectDrawPlan();
        int[] counts = {37, 16385, 65535, 65536, 65537, 1048576};
        float[] vp = ortho(-25, 65, -1, 6, -43, 31);
        for (int count : counts) {
            float[] times = count == 1048576 ? new float[]{17.25f} : new float[]{0, 17.25f, 89.5f};
            for (float seconds : times) {
                plan.update(count, side(count), vp, seconds, true);
                checkRanges(c, plan, count);
                checkNoVisibleOmissions(c, plan, count, vp, seconds, "orthographic " + count + "/" + seconds);
                if (count >= 65535 && seconds == 0) c.that(plan.submittedInstances() < count / 2,
                        "narrow view removes substantial offscreen work " + count);
            }
            int lastId = count - 1, side = side(count);
            float center = (side - 1) * .5f;
            float x = ((lastId % side) - center) * 2.2f;
            float z = ((lastId / side) - center) * 2.2f;
            float[] tail = ortho(x - .1f, x + .1f, 0, 5.18f, z - .1f, z + .1f);
            plan.update(count, side, tail, 0, true);
            checkRanges(c, plan, count);
            c.that(contains(plan, lastId), "partial final row retains its last object " + count);
            checkNoVisibleOmissions(c, plan, count, tail, 0, "last row " + count);
        }
    }

    private static void checkCameraExtremes(Checks c) {
        ObjectDrawPlan plan = new ObjectDrawPlan();
        int count = 65537, side = side(count);
        float[][] poses = {{0, .12f, .22f}, {1.57f, 1.4f, .22f}, {-2.8f, .12f, 4f},
                {.7f, 1.4f, 4f}, {3.1f, .65f, .5f}, {-1.4f, .8f, 1f}};
        for (int index = 0; index < poses.length; index++) {
            float[] pose = poses[index];
            float[] vp = perspectiveCamera(side, pose[0], pose[1], pose[2]);
            float seconds = index * 11.75f;
            plan.update(count, side, vp, seconds, true);
            c.that(plan.boundsTested() > 0, "perspective camera is valid " + index);
            checkRanges(c, plan, count);
            checkNoVisibleOmissions(c, plan, count, vp, seconds, "perspective pose " + index);
        }
    }

    private static void checkGrazingAndHeights(Checks c) {
        ObjectDrawPlan plan = new ObjectDrawPlan();
        // The visibility sliver lies above all unedited geometry. Raised cubes
        // must remain candidates, including their maximum positive animation.
        float[] topSliver = ortho(-.1f, .1f, 5.17f, 5.18f, -.1f, .1f);
        plan.update(1, 1, topSliver, 0, true);
        c.that(contains(plan, 0), "maximum edited height is inside culling bounds");
        float[] tangent = ortho(.38f, .39f, 0, 5.18f, -.1f, .1f);
        plan.update(1, 1, tangent, 0, true);
        c.that(contains(plan, 0), "cube touching clip plane is retained");
        float[] base = ortho(-.1f, .1f, -.01f, 0, -.1f, .1f);
        plan.update(1, 1, base, 0, true);
        c.that(contains(plan, 0), "cube bottom is at zero");
        int[] seeds = {0, 2026, Integer.MIN_VALUE, Integer.MAX_VALUE};
        float[] transform = new float[4];
        for (int seed : seeds) {
            ObjectSceneModel model = new ObjectSceneModel(65537, seed);
            int[] ids = {0, 255, 256, 16383, 16384, 65535, 65536};
            for (int id : ids) {
                model.edit(id, 6);
                model.transform(id, transform, 0);
                c.that(transform[2] * 1.08f <= 5.18f && transform[2] >= 0,
                        "seed and maximum edit stay inside universal height " + seed + "/" + id);
            }
        }
        c.that((.7 + 255.0 / 128 + 6 * .35) * 1.08 < 5.18,
                "analytic worst-case height including animation");
    }

    private static void checkInvalidAndReuse(Checks c) {
        final ObjectDrawPlan plan = new ObjectDrawPlan();
        float[] full = ortho(-4096, 4096, -10, 10, -4096, 4096);
        float[] nan = full.clone(); nan[8] = Float.NaN;
        float[] infinity = full.clone(); infinity[13] = Float.POSITIVE_INFINITY;
        float[][] invalid = {null, new float[15], new float[16], nan, infinity};
        for (int i = 0; i < invalid.length; i++) {
            plan.update(1048576, 1024, invalid[i], 0, true);
            c.that(plan.submittedInstances() == 1048576 && plan.boundsTested() == 0,
                    "invalid matrix fails open " + i);
            checkRanges(c, plan, 1048576);
        }
        float[] times = {Float.NaN, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY, Float.MAX_VALUE};
        for (float time : times) {
            plan.update(65537, 257, full, time, true);
            c.that(plan.submittedInstances() == 65537 && plan.boundsTested() == 0,
                    "invalid or unsupported time fails open " + time);
        }
        float[] narrow = ortho(-10, 10, -1, 6, -10, 10);
        plan.update(1048576, 1024, narrow, 0, true);
        int first = plan.firstId(0), initialCount = plan.submittedInstances();
        plan.update(1048576, 1024, ortho(300, 320, -1, 6, 300, 320), 0, true);
        c.that(plan.firstId(0) != first, "camera change replaces old ranges");
        plan.update(1048576, 1024, full, 0, false);
        c.that(plan.submittedInstances() > initialCount && plan.culledInstances() == 0,
                "toggle immediately restores complete population");
        plan.update(1, 1, full, 0, true);
        c.that(plan.batchCount() == 1 && plan.firstId(0) == 0 && plan.instanceCount(0) == 1,
                "million-to-one reset does not retain stale ranges");
        c.reject(new Runnable() { public void run() { plan.update(0, 1, null, 0, true); } },
                "reject zero count");
        c.reject(new Runnable() { public void run() { plan.update(1048577, 1025, null, 0, true); } },
                "reject count outside model");
        c.reject(new Runnable() { public void run() { plan.update(65537, 256, null, 0, true); } },
                "reject undersized side");
        c.reject(new Runnable() { public void run() { plan.update(1, 1024, null, 0, true); } },
                "reject noncanonical side");
    }

    private static void checkRanges(Checks c, ObjectDrawPlan plan, int count) {
        int end = 0, total = 0;
        for (int batch = 0; batch < plan.batchCount(); batch++) {
            int first = plan.firstId(batch), size = plan.instanceCount(batch);
            c.that(first >= end && first >= 0 && size > 0 && size <= 16384 && first <= count - size,
                    "ordered unique bounded range " + batch);
            end = first + size;
            total += size;
        }
        c.that(total == plan.submittedInstances() && total + plan.culledInstances() == count,
                "range accounting covers submitted plus culled");
    }

    private static void checkNoVisibleOmissions(Checks c, ObjectDrawPlan plan, int count,
            float[] vp, float seconds, String label) {
        int side = side(count), batch = 0, checked = 0;
        float center = (side - 1) * .5f;
        float angle = seconds * .12f;
        float cosine = (float) Math.cos(angle), sine = (float) Math.sin(angle);
        for (int id = 0; id < count; id++) {
            while (batch < plan.batchCount() && id >= plan.firstId(batch) + plan.instanceCount(batch)) batch++;
            boolean submitted = batch < plan.batchCount() && id >= plan.firstId(batch);
            if (submitted) continue;
            float x = ((id % side) - center) * 2.2f;
            float z = ((id / side) - center) * 2.2f;
            if (cornersMayIntersect(vp, cosine, sine, x, z)) {
                throw new AssertionError(label + ": potentially visible ID omitted " + id);
            }
            checked++;
        }
        c.that(checked == plan.culledInstances(), label + ": every omitted ID independently checked");
    }

    /**
     * Independent oracle: transform all eight corners of one maximum-height
     * cube to homogeneous clip space. No plane extraction or range bounds.
     * A cube is definitely outside only if every corner fails the same one
     * of the six homogeneous clipping inequalities.
     */
    private static boolean cornersMayIntersect(float[] vp, float cosine, float sine, float x, float z) {
        int commonOutside = 63;
        for (int corner = 0; corner < 8; corner++) {
            double localX = x + ((corner & 1) == 0 ? -.38 : .38);
            double localY = (corner & 2) == 0 ? 0 : 5.18;
            double localZ = z + ((corner & 4) == 0 ? -.38 : .38);
            double worldX = cosine * localX + sine * localZ;
            double worldZ = -sine * localX + cosine * localZ;
            double cx = vp[0] * worldX + vp[4] * localY + vp[8] * worldZ + vp[12];
            double cy = vp[1] * worldX + vp[5] * localY + vp[9] * worldZ + vp[13];
            double cz = vp[2] * worldX + vp[6] * localY + vp[10] * worldZ + vp[14];
            double cw = vp[3] * worldX + vp[7] * localY + vp[11] * worldZ + vp[15];
            int outside = (cx < -cw ? 1 : 0) | (cx > cw ? 2 : 0)
                    | (cy < -cw ? 4 : 0) | (cy > cw ? 8 : 0)
                    | (cz < -cw ? 16 : 0) | (cz > cw ? 32 : 0);
            commonOutside &= outside;
            if (commonOutside == 0) return true;
        }
        return false;
    }

    private static boolean contains(ObjectDrawPlan plan, int id) {
        for (int batch = 0; batch < plan.batchCount(); batch++) {
            if (id >= plan.firstId(batch) && id < plan.firstId(batch) + plan.instanceCount(batch)) return true;
        }
        return false;
    }

    private static int side(int count) { return (int) Math.ceil(Math.sqrt(count)); }

    private static float[] ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
        float[] result = new float[16];
        result[0] = 2f / (right - left); result[5] = 2f / (top - bottom); result[10] = 2f / (farZ - nearZ);
        result[12] = -(right + left) / (right - left);
        result[13] = -(top + bottom) / (top - bottom);
        result[14] = -(farZ + nearZ) / (farZ - nearZ); result[15] = 1;
        return result;
    }

    private static float[] perspectiveCamera(int side, float yaw, float pitch, float zoom) {
        double distance = side * 3.1 * zoom;
        double ex = Math.sin(yaw) * Math.cos(pitch) * distance;
        double ey = Math.sin(pitch) * distance;
        double ez = Math.cos(yaw) * Math.cos(pitch) * distance;
        double zx = ex / distance, zy = ey / distance, zz = ez / distance;
        double rightLength = Math.sqrt(zx * zx + zz * zz);
        double xx = zz / rightLength, xz = -zx / rightLength;
        double yx = zy * xz, yy = zz * xx - zx * xz, yz = -zy * xx;
        float[] view = new float[16];
        view[0] = (float) xx; view[4] = 0; view[8] = (float) xz;
        view[1] = (float) yx; view[5] = (float) yy; view[9] = (float) yz;
        view[2] = (float) zx; view[6] = (float) zy; view[10] = (float) zz;
        view[12] = (float) -(xx * ex + xz * ez);
        view[13] = (float) -(yx * ex + yy * (ey + .5) + yz * ez);
        view[14] = (float) -(zx * ex + zy * (ey + .5) + zz * ez); view[15] = 1;
        double near = Math.max(.1, distance * .01), far = distance + side * 4 + 20;
        float[] projection = new float[16];
        projection[0] = (float) (1 / Math.tan(Math.PI / 8) / 1.4);
        projection[5] = (float) (1 / Math.tan(Math.PI / 8));
        projection[10] = (float) (-(far + near) / (far - near));
        projection[14] = (float) (-2 * far * near / (far - near)); projection[11] = -1;
        float[] result = new float[16];
        for (int col = 0; col < 4; col++) for (int row = 0; row < 4; row++) {
            for (int inner = 0; inner < 4; inner++) result[col * 4 + row] += projection[inner * 4 + row] * view[col * 4 + inner];
        }
        return result;
    }

    private static final class Checks {
        int passed;
        void that(boolean condition, String label) {
            if (!condition) throw new AssertionError(label);
            passed++;
        }
        void reject(Runnable action, String label) {
            try { action.run(); }
            catch (IllegalArgumentException expected) { passed++; return; }
            throw new AssertionError(label);
        }
    }
}
