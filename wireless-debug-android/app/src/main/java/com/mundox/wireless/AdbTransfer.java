package com.mundox.wireless;

import io.github.muntashirakon.adb.AdbStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.UUID;

final class AdbTransfer {
    interface Progress { void onTransfer(long sent, long total); }
    private static final int BLOCK = 32768;
    private static final int REGULAR_FILE_0644 = 33188;

    private AdbTransfer() {}

    static String install(AdbIdentity adb, File apk, Progress progress) throws Exception {
        if (!adb.isConnected()) throw new IOException("Conecta primero al reloj");
        if (apk == null || !apk.isFile() || apk.length() < 64) {
            throw new IOException("APK ausente o vacía");
        }
        String nonce = UUID.randomUUID().toString().substring(0, 12);
        String remote = "/data/local/tmp/mx-watch-" + nonce + ".apk";
        try {
            push(adb, apk, remote, progress);
            String reply = shell(adb, "pm install -r -t " + remote).trim();
            if (!reply.contains("Success")) {
                throw new IOException("Instalación rechazada: " + (reply.isEmpty() ? "sin respuesta" : reply));
            }
            return reply;
        } finally {
            try { shell(adb, "rm -f " + remote); }
            catch (Exception ignored) { /* Removing a temporary APK is best effort. */ }
        }
    }

    static String shell(AdbIdentity adb, String command) throws Exception {
        if (!adb.isConnected()) throw new IOException("No existe conexión ADB");
        if (!command.matches("[\\x20-\\x7e]{1,100}")) {
            throw new IllegalArgumentException("Comando ADB no válido");
        }
        try (AdbStream stream = adb.openStream("shell:" + command)) {
            InputStream input = stream.openInputStream();
            java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            while (output.size() < 32768) {
                int read;
                try { read = input.read(buffer); }
                catch (IOException eof) {
                    // Some versions signal remote CLSE with IOException.
                    break;
                }
                if (read < 0) break;
                if (read > 0) output.write(buffer, 0, read);
            }
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    private static void push(AdbIdentity adb, File apk, String remote, Progress progress) throws Exception {
        try (AdbStream stream = adb.openStream("sync:")) {
            InputStream response = stream.openInputStream();
            byte[] location = (remote + "," + REGULAR_FILE_0644).getBytes(StandardCharsets.UTF_8);
            writeFrame(stream, "SEND", location, location.length);
            byte[] buffer = new byte[BLOCK];
            long sent = 0;
            try (FileInputStream source = new FileInputStream(apk)) {
                int n;
                while ((n = source.read(buffer)) != -1) {
                    if (n == 0) continue;
                    writeFrame(stream, "DATA", buffer, n);
                    sent += n;
                    progress.onTransfer(sent, apk.length());
                }
            }
            writeHeader(stream, "DONE", (int) (System.currentTimeMillis() / 1000L));
            byte[] reply = new byte[8];
            readExact(response, reply);
            String status = new String(reply, 0, 4, StandardCharsets.US_ASCII);
            if ("FAIL".equals(status)) {
                int size = le32(reply, 4);
                if (size < 0 || size > 4096) throw new IOException("Respuesta ADB inválida");
                byte[] message = new byte[size];
                readExact(response, message);
                throw new IOException("Transferencia rechazada: " + new String(message, StandardCharsets.UTF_8));
            }
            if (!"OKAY".equals(status)) throw new IOException("Transferencia sin confirmación ADB: " + status);
            try { writeHeader(stream, "QUIT", 0); }
            catch (Exception ignored) { }
        }
    }

    private static void writeFrame(AdbStream stream, String opcode, byte[] data, int length) throws Exception {
        byte[] frame = new byte[8 + length];
        writeLeHeader(frame, opcode, length);
        System.arraycopy(data, 0, frame, 8, length);
        stream.write(frame, 0, frame.length);
    }

    private static void writeHeader(AdbStream stream, String opcode, int value) throws Exception {
        byte[] header = new byte[8];
        writeLeHeader(header, opcode, value);
        stream.write(header, 0, header.length);
    }

    private static void writeLeHeader(byte[] bytes, String opcode, int value) {
        byte[] tag = opcode.getBytes(StandardCharsets.US_ASCII);
        if (tag.length != 4) throw new IllegalArgumentException("Bad sync opcode");
        System.arraycopy(tag, 0, bytes, 0, 4);
        bytes[4] = (byte) value;
        bytes[5] = (byte) (value >>> 8);
        bytes[6] = (byte) (value >>> 16);
        bytes[7] = (byte) (value >>> 24);
    }

    private static void readExact(InputStream input, byte[] target) throws IOException {
        int off = 0;
        while (off < target.length) {
            int n = input.read(target, off, target.length - off);
            if (n < 0) throw new IOException("El reloj cerró la conexión antes de responder");
            off += n;
        }
    }
    private static int le32(byte[] b, int offset) {
        return (b[offset] & 0xff)
                | ((b[offset+1] & 0xff) << 8)
                | ((b[offset+2] & 0xff) << 16)
                | ((b[offset+3] & 0xff) << 24);
    }
}
