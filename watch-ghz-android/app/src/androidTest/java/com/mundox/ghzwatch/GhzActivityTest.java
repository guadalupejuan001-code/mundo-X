package com.mundox.ghzwatch;

import android.app.Instrumentation;
import android.content.Intent;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.json.JSONObject;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public final class GhzActivityTest {
    private Instrumentation getInstrumentation() { return InstrumentationRegistry.getInstrumentation(); }
    @Test public void actualNativeUiLifecycle() throws Exception {
        NativeGhz.command(3, 3, 0);
        Intent intent = new Intent(getInstrumentation().getTargetContext(), MainActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        MainActivity activity = (MainActivity) getInstrumentation().startActivitySync(intent);
        try {
            awaitIdle(activity);
            assertEquals(0, activity.reportForTest().getInt("qubits"));
            click(activity, MainActivity.PREPARE_ID);
            JSONObject ghz = activity.reportForTest();
            assertEquals(3, ghz.getInt("qubits"));
            assertEquals(1.0, ghz.getDouble("x_all"), 0.0);
            click(activity, MainActivity.PHASE_ID);
            assertEquals(1.0, activity.reportForTest().getDouble("yx_all"), 0.0);
            click(activity, MainActivity.MEASURE_ID);
            JSONObject measured = activity.reportForTest();
            assertTrue(measured.getBoolean("all_equal_verified"));
            assertEquals(0.0, measured.getDouble("yx_all"), 0.0);
            click(activity, MainActivity.RESET_ID);
            assertEquals(0, activity.reportForTest().getInt("active_qubits"));
            assertEquals(0, activity.reportForTest().getInt("tableau_bytes"));
            // Also exercise the multiword native backend through Android JNI.
            for (int i = 0; i < 4; i++) click(activity, MainActivity.SIZE_ID);
            click(activity, MainActivity.PREPARE_ID);
            assertEquals(1000, activity.reportForTest().getInt("qubits"));
            assertEquals(257000, activity.reportForTest().getInt("tableau_bytes"));
            assertEquals(1.0, activity.reportForTest().getDouble("x_all"), 0.0);
        } finally {
            getInstrumentation().runOnMainSync(activity::finish);
        }
    }

    private void click(MainActivity activity, int id) throws Exception {
        getInstrumentation().runOnMainSync(() -> activity.findViewById(id).performClick());
        awaitIdle(activity);
    }

    private void awaitIdle(MainActivity activity) throws Exception {
        long until = android.os.SystemClock.elapsedRealtime() + 15000;
        while (activity.isBusyForTest() && android.os.SystemClock.elapsedRealtime() < until)
            Thread.sleep(50);
        getInstrumentation().waitForIdleSync();
        assertFalse("Native operation timed out", activity.isBusyForTest());
        assertNotNull("Native result missing", activity.reportForTest());
    }
}
