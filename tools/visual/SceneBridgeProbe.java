import com.mundox.lab.ObjectSceneModel;

import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;

/** Host-only oracle. Uses the Android model itself, without Android classes. */
public final class SceneBridgeProbe {
    private static void blob(DataOutputStream out, String text) throws Exception {
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        out.writeInt(bytes.length);
        out.write(bytes);
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) throw new IllegalArgumentException("scene and operations required");
        String text = new String(Files.readAllBytes(Paths.get(args[0])), StandardCharsets.UTF_8);
        ObjectSceneModel scene = ObjectSceneModel.importText(text);
        String initial = scene.exportText();
        StringBuilder operations = new StringBuilder();
        for (String line : Files.readAllLines(Paths.get(args[1]), StandardCharsets.UTF_8)) {
            if (line.length() == 0) continue;
            String[] fields = line.split(" ");
            long previous = scene.version();
            try {
                if (fields.length == 3 && fields[0].equals("edit")) {
                    scene.edit(Integer.parseInt(fields[1]), Integer.parseInt(fields[2]));
                } else if (fields.length == 1 && fields[0].equals("clear")) {
                    scene.clearEdits();
                } else {
                    throw new AssertionError("Invalid probe operation: " + line);
                }
                operations.append(scene.version() == previous ? "unchanged\n" : "changed\n");
            } catch (IllegalArgumentException error) {
                operations.append("rejected\n");
            } catch (IllegalStateException error) {
                operations.append("rejected\n");
            }
        }

        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(System.out, 65536));
        out.write("MXBRIDGE1\n".getBytes(StandardCharsets.US_ASCII));
        blob(out, initial);
        blob(out, operations.toString());
        blob(out, scene.exportText());
        out.writeInt(scene.count());
        out.writeInt(scene.side());
        out.writeLong(scene.logicalRuleBytes());
        float[] transform = new float[4];
        for (int id = 0; id < scene.count(); id++) {
            scene.transform(id, transform, 0);
            for (float value : transform) out.writeInt(Float.floatToRawIntBits(value));
        }
        out.flush();
    }
}
