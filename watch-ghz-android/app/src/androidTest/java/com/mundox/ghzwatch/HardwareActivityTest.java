package com.mundox.ghzwatch;

import android.app.Instrumentation;
import android.content.Intent;
import android.os.SystemClock;
import android.widget.Button;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public final class HardwareActivityTest {
    @Test public void inventoryCaptureAndCancellation() throws Exception {
        Instrumentation instrument = InstrumentationRegistry.getInstrumentation();
        Intent intent = new Intent(instrument.getTargetContext(), HardwareActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        HardwareActivity activity = (HardwareActivity) instrument.startActivitySync(intent);
        try {
            assertNotNull(activity.inventoryForTest().getJSONArray("sensors"));
            assertFalse(activity.runningForTest());
            Button start = activity.findViewById(R.id.hardware_start);
            if (!start.isEnabled()) {
                assertFalse(activity.findViewById(R.id.hardware_stop).isEnabled());
                System.out.println("Hardware probe correctly unavailable on this emulator.");
                return;
            }
            instrument.runOnMainSync(start::performClick);
            awaitStopped(instrument, activity, 18000);
            JSONObject complete = read(instrument);
            assertEquals("completed", complete.getString("status"));
            assertFalse(complete.getBoolean("physical_vibration_onset_measured"));
            assertFalse(complete.getBoolean("quantum_entanglement_test"));
            JSONArray trials = complete.getJSONArray("trials");
            assertEquals(4, trials.length());
            int submitted = 0;
            for (int i=0; i<4; i++) {
                JSONObject row = trials.getJSONObject(i);
                assertFalse(row.isNull("command_ns"));
                assertTrue(row.has("acceleration"));
                assertTrue(row.has("magnetic_field"));
                if (row.getBoolean("request_submitted")) submitted++;
            }
            assertEquals(2, submitted);
            assertTrue(complete.getJSONArray("samples").length() > 0);
            instrument.runOnMainSync(start::performClick);
            SystemClock.sleep(300);
            instrument.runOnMainSync(() -> activity.findViewById(R.id.hardware_stop).performClick());
            awaitStopped(instrument, activity, 5000);
            assertEquals("cancelled_by_user", read(instrument).getString("status"));
            instrument.runOnMainSync(start::performClick);
            SystemClock.sleep(300);
            instrument.runOnMainSync(activity::finish);
            awaitStopped(instrument, activity, 5000);
            assertEquals("activity_paused", read(instrument).getString("status"));
        } finally { instrument.runOnMainSync(activity::finish); }
    }
    private JSONObject read(Instrumentation instrument) throws Exception {
        File file = new File(instrument.getTargetContext().getFilesDir(), "hardware-latest.json");
        return new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
    }
    private void awaitStopped(Instrumentation instrument, HardwareActivity activity, long timeout) throws Exception {
        long until = SystemClock.elapsedRealtime() + timeout;
        while ((activity.runningForTest() || activity.savingForTest()) && SystemClock.elapsedRealtime() < until)
            SystemClock.sleep(50);
        instrument.waitForIdleSync();
        assertFalse("Recording still active", activity.runningForTest());
        assertFalse("Recording not saved", activity.savingForTest());
    }
}
