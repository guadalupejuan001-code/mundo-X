package com.mundox.graphics;

import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Random;
import javax.imageio.ImageIO;

/** Pure-Java checks use independent pixel coverage and the JDK PNG decoder. */
public final class UltraHostChecks {
    private static int checks, rejected, comparedPixels, coveredPixels;
    private interface Checked { void run() throws Exception; }
    private static final class CallerStream extends ByteArrayOutputStream {
        boolean closed;
        @Override public void close() { closed = true; }
    }

    private static void require(boolean condition, String reason) {
        checks++;
        if (!condition) throw new AssertionError(reason);
    }

    private static void rejects(Checked operation, String reason) throws Exception {
        try { operation.run(); }
        catch (IllegalArgumentException | ArithmeticException | IOException expected) { rejected++; return; }
        throw new AssertionError(reason);
    }

    private static void presetsAndMemory() throws Exception {
        RenderResolution.Size landscape = RenderResolution.uhd8k(800, 450);
        RenderResolution.Size portrait = RenderResolution.uhd8k(450, 800);
        require(landscape.width == 7680 && landscape.height == 4320, "8K landscape scaled to the view");
        require(portrait.width == 4320 && portrait.height == 7680, "8K portrait scaled to the view");
        require(RenderResolution.uhd8k(1, 1).width == 7680, "Square view did not use landscape convention");
        require(landscape.pixels() == 33_177_600L && landscape.attachmentBytes() == 265_420_800L,
                "8K attachment accounting overflowed or omitted an attachment");
        require(landscape.isUhd8k() && portrait.isUhd8k() && !landscape.isUhd4k(), "8K/4K identity conflated");
        RenderResolution.Size edge = new RenderResolution.Size(8192, 4096);
        RenderResolution.requireAllocation(edge, edge);
        rejects(() -> RenderResolution.requireAllocation(new RenderResolution.Size(8192, 4097), edge),
                "Replacement peak budget ignored the surviving old framebuffer");
        rejects(() -> RenderResolution.requireAllocation(new RenderResolution.Size(Integer.MAX_VALUE, Integer.MAX_VALUE), null),
                "Attachment byte multiplication overflow was not rejected");
        rejects(() -> RenderResolution.fixed(-1, 720, 1280), "Invalid fixed mode accepted");
        rejects(() -> RenderResolution.uhd8k(0, 1), "Invalid view extent accepted");
        for (RenderResolution.Limits limits : new RenderResolution.Limits[]{
                new RenderResolution.Limits(4096, 8192, 8192, 8192),
                new RenderResolution.Limits(8192, 4096, 8192, 8192),
                new RenderResolution.Limits(8192, 8192, 4096, 8192),
                new RenderResolution.Limits(8192, 8192, 8192, 4096)})
            rejects(() -> limits.require(landscape), "A GPU extent limit was ignored");

        // The same output is possible as actual pixels on a context unable to hold a full 8K attachment.
        TilePlan lowGpu = new TilePlan(landscape, new RenderResolution.Limits(256, 128, 96, 48), 1024, 64);
        require(lowGpu.tileWidth <= 96 && lowGpu.stripRows <= 48 && lowGpu.tileCount > 100,
                "Tile plan exceeded an independent GPU limit");
        require(lowGpu.workingBytes < 16L * 1024 * 1024, "8K plan needs a full-frame working array");
        TilePlan standard = new TilePlan(landscape, new RenderResolution.Limits(8192, 8192, 8192, 8192), 1024, 64);
        require(standard.workingBytes < 16L * 1024 * 1024 && standard.tileCount > 100,
                "Native-resolution 8K output was not divided into bounded regions");
        long pixels = 0;
        for (int index = 0; index < standard.tileCount; index++) {
            TilePlan.Tile tile = standard.tile(index);
            pixels += (long)tile.width * tile.height;
        }
        require(pixels == 33_177_600L, "Full 8K plan omitted or invented pixels");
    }

    private static void coverage() throws Exception {
        RenderResolution.Limits generous = new RenderResolution.Limits(8192, 8192, 8192, 8192);
        for (int[] shape : new int[][]{{1, 1}, {1, 5}, {5, 1}, {321, 181}, {181, 321}, {127, 63}, {1025, 65}}) {
            for (int[] requested : new int[][]{{2, 2}, {64, 32}, {1024, 64}}) {
                int width = shape[0], height = shape[1];
                TilePlan plan = new TilePlan(new RenderResolution.Size(width, height), generous, requested[0], requested[1]);
                byte[] coverage = new byte[width * height];
                int previousTop = -1;
                for (int index = 0; index < plan.tileCount; index++) {
                    TilePlan.Tile tile = plan.tile(index);
                    require(tile.width > 0 && tile.height > 0 && tile.x >= 0 && tile.y >= 0
                            && tile.x + tile.width <= width && tile.y + tile.height <= height,
                            "Tile rectangle escapes the image");
                    require((tile.x & 1) == 0 && (tile.y & 1) == 0,
                            "Tile broke the monolithic derivative-quad origin");
                    require(tile.topRow == height - tile.y - tile.height && tile.topRow >= previousTop,
                            "Top-first strip order disagrees with GL coordinates");
                    previousTop = tile.topRow;
                    // Independent raster coverage detects overlaps, holes and wrong partial-band handling.
                    for (int y = tile.y; y < tile.y + tile.height; y++)
                        for (int x = tile.x; x < tile.x + tile.width; x++)
                            coverage[y * width + x]++;
                }
                for (byte value : coverage) {
                    require(value == 1, "A pixel is missing or appears in more than one tile");
                    coveredPixels++;
                }
                rejects(() -> plan.tile(-1), "Negative tile index accepted");
                rejects(() -> plan.tile(plan.tileCount), "End tile index accepted");
            }
        }
        for (int[] shape : new int[][]{{0, 1}, {-1, 1}, {16385, 1}, {Integer.MAX_VALUE, Integer.MAX_VALUE}})
            rejects(() -> new TilePlan(new RenderResolution.Size(shape[0], shape[1]), generous, 1024, 64),
                    "Invalid/overflowing image extent accepted");
        for (int[] requested : new int[][]{{0, 32}, {1, 32}, {63, 32}, {2048, 32}, {64, 0}, {64, 33}, {64, 128}})
            rejects(() -> new TilePlan(new RenderResolution.Size(321, 181), generous, requested[0], requested[1]),
                    "Invalid or unaligned tile buffer extent accepted");
        rejects(() -> new TilePlan(new RenderResolution.Size(321, 181), new RenderResolution.Limits(1, 1, 1, 1), 64, 32),
                "A multi-pixel tile was accepted on a one-pixel-limited GPU");
    }

    private static void pngPixels(Path output) throws Exception {
        Random random = new Random(20261019L);
        for (int[] shape : new int[][]{{1, 1}, {321, 181}, {181, 321}, {257, 65}}) {
            int width = shape[0], height = shape[1];
            byte[] expected = new byte[width * height * 4];
            random.nextBytes(expected); // Includes translucent pixels and all unsigned channel values.
            CallerStream destination = new CallerStream();
            try (StreamPng writer = new StreamPng(destination, width, height)) {
                byte[] sourceRow = new byte[width * 4 + 16];
                Arrays.fill(sourceRow, (byte)0xA5);
                for (int y = 0; y < height; y++) {
                    System.arraycopy(expected, y * width * 4, sourceRow, 7, width * 4);
                    writer.writeRow(sourceRow, 7);
                    require(sourceRow[0] == (byte)0xA5 && sourceRow[sourceRow.length - 1] == (byte)0xA5,
                            "Writer modified caller-owned source storage");
                }
                writer.finish();
            }
            require(!destination.closed, "Successful PNG export closed the caller's stream");
            byte[] png = destination.toByteArray();
            BufferedImage image = ImageIO.read(new ByteArrayInputStream(png));
            require(image != null && image.getWidth() == width && image.getHeight() == height,
                    "Independent ImageIO decoder rejected PNG or its exact dimensions");
            for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
                int at = (y * width + x) * 4;
                int argb = ((expected[at + 3] & 255) << 24) | ((expected[at] & 255) << 16)
                        | ((expected[at + 1] & 255) << 8) | (expected[at + 2] & 255);
                require(image.getRGB(x, y) == argb, "Independent PNG decoding changed RGBA or row orientation");
                comparedPixels++;
            }
            if (output != null) Files.write(output.resolve("host-png-" + width + "x" + height + ".png"), png);
        }
    }

    private static void writerFailure() throws Exception {
        for (int[] extent : new int[][]{{0, 1}, {1, 0}, {16385, 1}, {Integer.MAX_VALUE, Integer.MAX_VALUE}}) {
            CallerStream destination = new CallerStream();
            rejects(() -> new StreamPng(destination, extent[0], extent[1]), "Writer accepted invalid extent");
            require(destination.size() == 0 && !destination.closed, "Invalid dimensions touched destination before validation");
        }
        CallerStream partial = new CallerStream();
        try (StreamPng writer = new StreamPng(partial, 2, 2)) {
            rejects(writer::finish, "Incomplete PNG was finalized");
            rejects(() -> writer.writeRow(new byte[8], -1), "Negative source offset accepted");
            rejects(() -> writer.writeRow(new byte[8], Integer.MAX_VALUE), "Source offset overflow accepted");
            rejects(() -> writer.writeRow(new byte[7], 0), "Short row accepted");
            writer.writeRow(new byte[8], 0);
            writer.writeRow(new byte[8], 0);
            rejects(() -> writer.writeRow(new byte[8], 0), "Extra output row accepted");
            writer.finish();
            rejects(() -> writer.writeRow(new byte[8], 0), "Closed writer accepted a row");
            rejects(writer::finish, "Closed writer finalized twice");
        }
        require(!partial.closed, "Invalid row/finish/close took ownership of destination");
        require(ImageIO.read(new ByteArrayInputStream(partial.toByteArray())) != null,
                "Invalid row attempts corrupted the recoverable writer");

        for (int failAt : new int[]{0, 12, 48}) {
            boolean[] closed = {false};
            OutputStream broken = new OutputStream() {
                int count;
                @Override public void write(int value) throws IOException {
                    if (count++ >= failAt) throw new IOException("Injected I/O failure");
                }
                @Override public void close() { closed[0] = true; }
            };
            rejects(() -> {
                try (StreamPng writer = new StreamPng(broken, 32, 4)) {
                    byte[] row = new byte[128];
                    for (int y = 0; y < 4; y++) writer.writeRow(row, 0);
                    writer.finish();
                }
            }, "Output error was swallowed");
            require(!closed[0], "Failed writer closed its caller-owned destination");
        }
    }

    public static void main(String[] arguments) throws Exception {
        Path output = arguments.length == 0 ? null : Path.of(arguments[0]);
        if (output != null) Files.createDirectories(output);
        presetsAndMemory();
        coverage();
        pngPixels(output);
        writerFailure();
        System.out.println("{\"passed\":true,\"checks\":" + checks + ",\"rejectedInvalidOrFailedOperations\":" + rejected
                + ",\"independentPngPixels\":" + comparedPixels + ",\"independentTileCoveragePixels\":" + coveredPixels
                + ",\"realGles\":false,\"scope\":\"Pure Java planner and independent ImageIO PNG decoding\"}");
    }
}
