package com.mundox.lab;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Host-only bridge: export actual application methods, never copied model/planner logic. */
public final class HostGraphicsExport {
    private static Object call(Class<?> type, String name) throws Exception {
        Method method = type.getDeclaredMethod(name);
        method.setAccessible(true);
        return method.invoke(null);
    }

    private static void text(File directory, String name, String value) throws Exception {
        Files.write(new File(directory, name).toPath(), value.getBytes(StandardCharsets.UTF_8));
    }

    private static void mesh(File directory, String name, float[] values) throws Exception {
        ByteBuffer data = ByteBuffer.allocate(values.length * 4).order(ByteOrder.LITTLE_ENDIAN);
        for (float value : values) data.putFloat(value);
        Files.write(new File(directory, name).toPath(), data.array());
    }

    private static void exportRenderer(File directory, Class<?> type, String prefix,
            boolean indexed) throws Exception {
        text(directory, prefix + ".vert", (String) call(type, "vertexSource"));
        Field fragment = type.getDeclaredField("FRAGMENT_SOURCE");
        fragment.setAccessible(true);
        text(directory, prefix + ".frag", (String) fragment.get(null));
        mesh(directory, prefix + ".mesh", (float[]) call(type, "cubeMesh"));
        if (indexed) {
            short[] indices = (short[]) call(type, "cubeIndices");
            ByteBuffer data = ByteBuffer.allocate(indices.length * 2).order(ByteOrder.LITTLE_ENDIAN);
            for (short value : indices) data.putShort(value);
            Files.write(new File(directory, prefix + ".indices").toPath(), data.array());
        }
    }

    private static String plan(ObjectDrawPlan plan) {
        StringBuilder out = new StringBuilder("{");
        out.append("\"submitted\":").append(plan.submittedInstances());
        out.append(",\"culled\":").append(plan.culledInstances());
        out.append(",\"bounds_tested\":").append(plan.boundsTested());
        out.append(",\"storage_bytes\":").append(plan.storageBytes());
        out.append(",\"batches\":[");
        for (int i = 0; i < plan.batchCount(); i++) {
            if (i != 0) out.append(',');
            out.append('[').append(plan.firstId(i)).append(',')
                    .append(plan.instanceCount(i)).append(']');
        }
        return out.append("]}").toString();
    }

    public static void main(String[] args) throws Exception {
        File directory = new File(args[1]);
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IllegalStateException("output directory");
        if ("sources".equals(args[0])) {
            exportRenderer(directory, ObjectSceneRenderer.class, "current", true);
            exportRenderer(directory, ReferenceObjectSceneRenderer.class, "legacy", false);
            return;
        }
        int count = Integer.parseInt(args[2]), seed = Integer.parseInt(args[3]);
        float seconds = Float.parseFloat(args[4]);
        ObjectSceneModel model = new ObjectSceneModel(count, seed);
        if (!args[5].equals("-")) for (String edit : args[5].split(",")) {
            String[] item = edit.split(":");
            model.edit(Integer.parseInt(item[0]), Integer.parseInt(item[1]));
        }
        float[] vp = new float[16];
        for (int i = 0; i < 16; i++) vp[i] = Float.parseFloat(args[6 + i]);
        ObjectDrawPlan plan = new ObjectDrawPlan();
        plan.update(count, model.side(), vp, seconds, false);
        String unculled = plan(plan);
        plan.update(count, model.side(), vp, seconds, true);
        text(directory, "plan.json", "{\"unculled\":" + unculled + ",\"culled\":" + plan(plan) + "}");
        float[] transform = new float[4];
        ByteBuffer block = ByteBuffer.allocate(65536).order(ByteOrder.LITTLE_ENDIAN);
        try (BufferedOutputStream output = new BufferedOutputStream(
                new FileOutputStream(new File(directory, "transforms.f32")))) {
            for (int id = 0; id < count; id++) {
                model.transform(id, transform, 0);
                for (float value : transform) block.putFloat(value);
                if (!block.hasRemaining()) { output.write(block.array()); block.clear(); }
            }
            output.write(block.array(), 0, block.position());
        }
    }
}
