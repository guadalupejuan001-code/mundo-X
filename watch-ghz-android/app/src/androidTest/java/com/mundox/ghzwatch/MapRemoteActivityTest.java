package com.mundox.ghzwatch;
import android.app.Instrumentation;
import android.content.Intent;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public final class MapRemoteActivityTest {
    @Test public void opensControllerWithoutInventingAConnection()throws Exception {
        Instrumentation i=InstrumentationRegistry.getInstrumentation();
        Intent intent=new Intent(i.getTargetContext(),MapRemoteActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        MapRemoteActivity activity=(MapRemoteActivity)i.startActivitySync(intent);
        try{i.waitForIdleSync();assertNotNull(activity.findViewById(R.id.map_connect));
            assertFalse(((android.widget.TextView)activity.findViewById(R.id.map_status)).getText().toString().contains("APLICADO"));
        }finally{i.runOnMainSync(activity::finish);}
    }
}

