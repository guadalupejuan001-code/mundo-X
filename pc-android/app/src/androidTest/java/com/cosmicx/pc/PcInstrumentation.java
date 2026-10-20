package com.cosmicx.pc;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Debug;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.TextView;
import android.widget.EditText;
import com.mundox.graphics.NativeCore;
import com.mundox.graphics.QuantumRenderer;
import org.json.JSONObject;
import org.json.JSONArray;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.List;
import java.util.Locale;
import java.util.Arrays;

import com.mundox.graphics.RenderResolution;

/** Executes the shipped native engine; no substituted output or mock CPU. */
public final class PcInstrumentation extends Instrumentation {
    private int passed=0;
    private int rejectedVideoRequests=0;
    private volatile Activity resumedActivity;
    private final JSONObject videoReport=new JSONObject();
    private static final int[] MARKERS={0xffff4040,0xff32e875,0xff477bff,0xfff5f7fa,0xff37d5ff};
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    @Override public void callActivityOnResume(Activity activity){super.callActivityOnResume(activity);if(activity instanceof MainActivity)resumedActivity=activity;}
    private void require(boolean ok,String reason){if(!ok)throw new AssertionError(reason);}
    private void pass(String name){passed++;Bundle b=new Bundle();b.putString("stream","PASS "+name+"\n");sendStatus(0,b);}
    private JSONObject state(String s)throws Exception{return new JSONObject(s);}
    private JSONObject drain(long h)throws Exception{
        JSONObject s=state(NativeMachine.inspect(h));
        for(int i=0;i<500&&(s.getString("status").equals("running")||s.getString("status").equals("ready"));i++)s=state(NativeMachine.run(h,1000));
        return s;
    }
    private byte[] raw(byte[] code){byte[] b=new byte[512];System.arraycopy(code,0,b,0,code.length);b[510]=0x55;b[511]=(byte)0xaa;return b;}
    private void put32(byte[] b,int p,int v){for(int i=0;i<4;i++)b[p+i]=(byte)(v>>>(8*i));}
    private byte[] iso(){
        byte[] b=new byte[32*2048];int p=16*2048;b[p]=1;byte[] id="CD001".getBytes(StandardCharsets.US_ASCII);System.arraycopy(id,0,b,p+1,5);b[p+6]=1;
        p=17*2048;System.arraycopy(id,0,b,p+1,5);b[p+6]=1;byte[] el="EL TORITO SPECIFICATION".getBytes(StandardCharsets.US_ASCII);System.arraycopy(el,0,b,p+7,el.length);put32(b,p+71,20);
        p=18*2048;b[p]=(byte)255;System.arraycopy(id,0,b,p+1,5);b[p+6]=1;
        p=20*2048;b[p]=1;b[p+30]=0x55;b[p+31]=(byte)0xaa;int sum=0;for(int i=0;i<32;i+=2)sum+=(b[p+i]&255)|((b[p+i+1]&255)<<8);int checksum=(-sum)&65535;b[p+28]=(byte)checksum;b[p+29]=(byte)(checksum>>8);
        b[p+32]=(byte)0x88;b[p+38]=1;put32(b,p+40,24);
        byte[] code={(byte)0xb8,'I',0x0e,(byte)0xcd,0x10,(byte)0xf4};System.arraycopy(code,0,b,24*2048,code.length);return b;
    }
    private JSONObject mountBytes(long h,byte[] bytes,String name)throws Exception{
        File f=new File(getTargetContext().getFilesDir(),name);try(FileOutputStream out=new FileOutputStream(f)){out.write(bytes);}
        JSONObject s;try(ParcelFileDescriptor fd=ParcelFileDescriptor.open(f,ParcelFileDescriptor.MODE_READ_ONLY)){s=state(NativeMachine.mount(h,fd.getFd(),bytes.length,"auto"));}
        // FD has closed: native engine must retain its own descriptor.
        return s;
    }
    private View find(View v,String text){if(v instanceof TextView&&text.contentEquals(((TextView)v).getText()))return v;if(v instanceof ViewGroup){ViewGroup g=(ViewGroup)v;for(int i=0;i<g.getChildCount();i++){View r=find(g.getChildAt(i),text);if(r!=null)return r;}}return null;}
    private boolean contains(View v,String part){if(v instanceof TextView&&((TextView)v).getText().toString().contains(part))return true;if("pc-screen".contentEquals(v.getContentDescription()==null?"":v.getContentDescription())){AccessibilityNodeInfo node=v.createAccessibilityNodeInfo();boolean match=node.getText()!=null&&node.getText().toString().contains(part);node.recycle();if(match)return true;}if(v instanceof ViewGroup){ViewGroup g=(ViewGroup)v;for(int i=0;i<g.getChildCount();i++)if(contains(g.getChildAt(i),part))return true;}return false;}
    private Activity current(Activity fallback){return resumedActivity==null?fallback:resumedActivity;}
    private void click(Activity a,String text)throws Exception{for(int i=0;i<80;i++){boolean[] clicked={false};runOnMainSync(()->{Activity now=current(a);if(now.isDestroyed()||now.isFinishing())return;View v=find(now.getWindow().getDecorView(),text);if(v!=null&&v.isEnabled()){require(v.performClick(),"Button refused click: "+text);clicked[0]=true;}});if(clicked[0])return;Thread.sleep(100);}throw new AssertionError("Button missing or disabled: "+text);}
    private void waitText(Activity a,String text)throws Exception{waitText(a,text,8000);}
    private void waitText(Activity a,String text,long timeout)throws Exception{long end=SystemClock.uptimeMillis()+timeout;while(SystemClock.uptimeMillis()<end){boolean[] found={false};runOnMainSync(()->{Activity now=current(a);if(!now.isDestroyed())found[0]=contains(now.getWindow().getDecorView(),text);});if(found[0])return;Thread.sleep(100);}throw new AssertionError("UI did not show: "+text);}
    private View tagged(View v,String name){if(name.contentEquals(v.getContentDescription()==null?"":v.getContentDescription()))return v;if(v instanceof ViewGroup){ViewGroup group=(ViewGroup)v;for(int i=0;i<group.getChildCount();i++){View match=tagged(group.getChildAt(i),name);if(match!=null)return match;}}return null;}
    private QuantumRenderer renderer(Activity a)throws Exception{
        Field field=MainActivity.class.getDeclaredField("quantumRenderer");field.setAccessible(true);
        return (QuantumRenderer)field.get(a);
    }
    private NativeCore graphicsOwner(Activity a)throws Exception{
        Field field=MainActivity.class.getDeclaredField("graphicsOwner");field.setAccessible(true);
        return (NativeCore)((AtomicReference<?>)field.get(a)).get();
    }
    private void waitOrientation(Activity a,int orientation)throws Exception{
        for(int i=0;i<80;i++){
            boolean[] matches={false};runOnMainSync(()->matches[0]=a.getResources().getConfiguration().orientation==orientation);
            if(matches[0])return;Thread.sleep(100);
        }
        throw new AssertionError("Requested rotation was not applied");
    }
    private QuantumRenderer.Capture frame(Activity a,boolean coupled)throws Exception{
        GLSurfaceView[] surface={null};
        QuantumRenderer observed=null;
        for(int i=0;i<100;i++){
            runOnMainSync(()->{View v=tagged(a.getWindow().getDecorView(),"pc-quantum-viewport");if(v instanceof GLSurfaceView){surface[0]=(GLSurfaceView)v;v.requestRectangleOnScreen(new Rect(0,0,v.getWidth(),v.getHeight()),true);}});
            observed=renderer(a);
            if(surface[0]!=null&&observed!=null&&observed.hasFrame)break;
            if(observed!=null&&observed.failure!=null)throw new AssertionError(observed.failure);
            Thread.sleep(100);
        }
        require(surface[0]!=null&&observed!=null,"same-session GLES viewport must exist");
        QuantumRenderer current=observed;
        CountDownLatch ready=new CountDownLatch(1);
        AtomicReference<QuantumRenderer.Capture> image=new AtomicReference<>();
        AtomicReference<Throwable> failed=new AtomicReference<>();
        surface[0].queueEvent(()->{try{current.coupling(coupled);current.setTime(0);current.requestCapture(false,capture->{image.set(capture);ready.countDown();});}catch(Throwable error){failed.set(error);ready.countDown();}});
        surface[0].requestRender();
        require(ready.await(25,TimeUnit.SECONDS),"GLES capture timed out: "+current.failure);
        if(failed.get()!=null)throw new AssertionError("GLES capture failed",failed.get());
        require(image.get()!=null&&current.failure==null,"real framebuffer captured");
        require(current.frameStats()!=null&&current.frameStats().infinite,"canonical renderer must use InfiniteCore");
        return image.get();
    }
    private void waitMemoryMeasurement(Activity a)throws Exception{for(int i=0;i<80;i++){boolean[] measured={false};runOnMainSync(()->{View v=tagged(current(a).getWindow().getDecorView(),"pc-memory");if(v instanceof TextView)measured[0]=((TextView)v).getText().toString().matches("(?s).*PSS[^\\n]*[0-9][^\\n]*.*");});if(measured[0])return;Thread.sleep(100);}throw new AssertionError("UI must display a numeric PSS sample, not only a label");}

    private JSONObject etherProgramRoundtrip(Activity a,NativeCore owner,QuantumRenderer.Capture baseline)throws Exception{
        String[] observables={"Z:7","Z:6 X:7 Z:8","X:0 X:1000000000","Y:0 Y:1000000000","Z:0 Z:1000000000"};
        NativeCore.Observation before=owner.observe(observables);
        long active=owner.infiniteStats().activeQubits;
        require(owner.etherStats().rules==0&&Arrays.equals(before.values,new float[]{1,0,1,-1,1}),
                "The guest Bell state must exist before explicit global preparation");
        runOnMainSync(()->((EditText)tagged(a.getWindow().getDecorView(),"pc-quantum-program")).setText(
                "h_all\ncz_neighbors\nexpect Z:6 X:7 Z:8\nstats"));
        click(a,"Aplicar programa");waitText(a,"Éter ∞ · 2 reglas");
        NativeCore.Observation framed=owner.observe(observables);
        require(graphicsOwner(a)==owner&&owner.etherStats().rules==2
                &&framed.revision==before.revision+2&&framed.epoch==before.epoch+1,
                "Explicit global commands must mutate the existing PC/graphics session atomically");
        require(Arrays.equals(framed.values,new float[]{0,1,0,0,1})&&owner.infiniteStats().activeQubits==active,
                "Global rules must transform observations while preserving the guest preparation");
        String framedSnapshot=owner.snapshot().infiniteSnapshot;
        int globalChanged=baseline.changed(frame(a,true));
        require(globalChanged>10,"Explicit Ether rules must reach the same-session GLES framebuffer");
        require(renderer(a).frameStats().coreRevision==framed.revision&&renderer(a).frameStats().coreEpoch==framed.epoch
                &&owner.epoch()==framed.epoch&&framedSnapshot.equals(owner.snapshot().infiniteSnapshot),
                "Rendering the framed state must observe its revision without resetting or preparing it");
        runOnMainSync(()->((EditText)tagged(a.getWindow().getDecorView(),"pc-quantum-program")).setText(
                "cz_neighbors\nh_all\nexpect X:0 X:1000000000"));
        click(a,"Aplicar programa");waitText(a,"Éter ∞ · 0 reglas");waitText(a,"XX=1 · YY=-1 · ZZ=1");
        NativeCore.Observation restored=owner.observe(observables);
        require(graphicsOwner(a)==owner&&owner.etherStats().rules==0&&Arrays.equals(restored.values,before.values)
                &&restored.revision==before.revision+4&&restored.epoch==before.epoch+2,
                "Inverse global layers must preserve the same guest state and advance its history");
        int restoredChanged=baseline.changed(frame(a,true));
        require(restoredChanged==0,"Inverse Ether layers must restore the exact original coupled pixels");
        return new JSONObject().put("rules",new JSONArray().put(0).put(2).put(0))
                .put("before",new JSONArray(before.values)).put("framed",new JSONArray(framed.values))
                .put("restored",new JSONArray(restored.values)).put("same_owner",true).put("render_read_only",true)
                .put("before_revision",before.revision).put("framed_revision",framed.revision).put("restored_revision",restored.revision)
                .put("before_epoch",before.epoch).put("framed_epoch",framed.epoch).put("restored_epoch",restored.epoch)
                .put("global_changed_pixels",globalChanged).put("roundtrip_changed_pixels",restoredChanged);
    }

    private interface Checked {void run()throws Exception;}
    private void rejected(Checked action,String reason)throws Exception{
        boolean rejected=false;try{action.run();}catch(RuntimeException expected){rejected=true;}
        require(rejected,reason);rejectedVideoRequests++;
    }
    private void writeJson(String name,JSONObject json)throws Exception{
        try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getFilesDir(),name))){out.write(json.toString(2).getBytes(StandardCharsets.UTF_8));}
    }
    private JSONArray colors(int[] values){JSONArray result=new JSONArray();for(int value:values)result.put(String.format(Locale.ROOT,"%08X",Integer.rotateLeft(value,8)));return result;}
    private int argb(ByteBuffer row,int x){int p=x*4;return (row.get(p+3)&255)<<24|(row.get(p)&255)<<16|(row.get(p+1)&255)<<8|(row.get(p+2)&255);}
    private void requireMarkers(int[] actual,String where){for(int i=0;i<MARKERS.length;i++)require(actual[i]==MARKERS[i],where+" marker "+i+": "+Integer.toHexString(actual[i])+" expected "+Integer.toHexString(MARKERS[i]));}
    private int[] nativeMarkers(JSONObject frame)throws Exception{
        int width=frame.getInt("width"),height=frame.getInt("height");long revision=frame.getLong("revision");
        ByteBuffer row=ByteBuffer.allocateDirect(width*4);int[] result=new int[5];
        require(NativeMachine.copyVideoRows(revision,0,1,row)==width*4,"first guest row");result[0]=argb(row,0);result[1]=argb(row,width-1);
        require(NativeMachine.copyVideoRows(revision,height-1,1,row)==width*4,"last guest row");result[2]=argb(row,0);result[3]=argb(row,width-1);
        require(NativeMachine.copyVideoRows(revision,height/2,1,row)==width*4,"middle guest row");result[4]=argb(row,width/2);return result;
    }
    private JSONObject drainGraphics(long h)throws Exception{
        JSONObject value=state(NativeMachine.inspect(h));long end=SystemClock.uptimeMillis()+120000;
        for(int i=0;i<100&&SystemClock.uptimeMillis()<end&&(value.getString("status").equals("ready")||value.getString("status").equals("running"));i++)value=state(NativeMachine.run(h,50000));
        require(value.getString("status").equals("halted"),"Graphics guest did not complete within 5,000,000 steps / 120s: "+value);
        require(value.getString("cpu_mode").equals("protected32"),"Graphics guest must enter protected mode itself");return value;
    }
    private void testNativeVideo(long h)throws Exception{
        JSONObject text=state(NativeMachine.acquireVideoFrame(h));long textId=text.getLong("revision");
        try{
            require(text.getInt("kind")==PcVideoRenderer.TEXT&&text.getInt("width")==80&&text.getInt("height")==25,"VGA metadata");
            ByteBuffer cells=ByteBuffer.allocateDirect(4000);require(NativeMachine.copyVideoText(textId,cells)==4000,"real VGA text cells");
            require(cells.get(0)=='C',"VGA snapshot must contain the actual x64 guest message");
        }finally{NativeMachine.releaseVideoFrame(textId);}
        state(NativeMachine.bootGraphicsDemo(h,1280,720));JSONObject small=drainGraphics(h);
        require(small.getString("video_kind").equals("vbe32")&&small.getInt("video_width")==1280&&small.getInt("video_height")==720,"720p VBE guest metadata");
        require(!small.getBoolean("windows_ready")&&!small.getBoolean("graphics_3d_ready"),"Framebuffer must not claim Windows/3D");
        JSONObject first=state(NativeMachine.acquireVideoFrame(h));long firstId=first.getLong("revision");
        try{
            require(first.getInt("kind")==PcVideoRenderer.RGBA&&first.getLong("snapshot_bytes")==1280L*720*4,"RGBA snapshot footprint");
            requireMarkers(nativeMarkers(first),"720p native");
            state(NativeMachine.inspect(h));JSONObject again=state(NativeMachine.acquireVideoFrame(h));long secondId=again.getLong("revision");
            try{require(firstId==secondId,"Read-only inspect must reuse immutable snapshot/cache");}finally{NativeMachine.releaseVideoFrame(secondId);}
            requireMarkers(nativeMarkers(first),"remaining 720p borrower");
            JSONObject report=new JSONObject();report.put("state",small);report.put("rgba",colors(nativeMarkers(first)));report.put("cached_revision",firstId);report.put("cache_reused",true);videoReport.put("native_720",report);
            pass("JNI guest 720p pixels + immutable snapshot cache");

            final ByteBuffer direct=ByteBuffer.allocateDirect(2*1024*1024);
            rejected(()->NativeMachine.copyVideoRows(firstId,-1,1,direct),"negative row accepted");
            rejected(()->NativeMachine.copyVideoRows(firstId,720,1,direct),"row after guest accepted");
            rejected(()->NativeMachine.copyVideoRows(firstId,0,0,direct),"zero rows accepted");
            rejected(()->NativeMachine.copyVideoRows(firstId,0,410,direct),"transfer above 2 MiB accepted");
            rejected(()->NativeMachine.copyVideoRows(firstId,0,1,ByteBuffer.allocate(5120)),"heap buffer accepted");
            rejected(()->NativeMachine.copyVideoRows(firstId,0,1,ByteBuffer.allocateDirect(5119)),"short direct buffer accepted");
            rejected(()->NativeMachine.copyVideoRows(firstId,0,1,direct.asReadOnlyBuffer()),"read-only direct buffer accepted");
            rejected(()->NativeMachine.copyVideoRows(firstId,0,1,null),"null buffer accepted");
            rejected(()->NativeMachine.copyVideoText(firstId,direct),"pixel snapshot accepted as text");
            ByteBuffer guarded=ByteBuffer.allocateDirect(5120+32);for(int i=0;i<guarded.capacity();i++)guarded.put(i,(byte)0x5a);guarded.position(16);guarded.limit(5136);
            ByteBuffer slice=guarded.slice();require(NativeMachine.copyVideoRows(firstId,0,1,slice)==5120,"direct slice must be usable");
            guarded.clear();for(int i=0;i<16;i++)require(guarded.get(i)==0x5a&&guarded.get(5136+i)==0x5a,"copy escaped its direct-buffer slice");
            require(argb(slice,0)==MARKERS[0]&&argb(slice,1279)==MARKERS[1],"slice pixels must preserve channel order");
        }finally{NativeMachine.releaseVideoFrame(firstId);}
        rejected(()->NativeMachine.copyVideoRows(firstId,0,1,ByteBuffer.allocateDirect(5120)),"released snapshot remains readable");
        NativeMachine.releaseVideoFrame(firstId); // Cleanup is deliberately idempotent.
        videoReport.put("jni_invalid_requests_rejected",rejectedVideoRequests);videoReport.put("jni_direct_slice_guards_preserved",true);
        pass("JNI video rejects invalid leases, ranges and buffers");

        state(NativeMachine.bootGraphicsDemo(h,7680,4320));JSONObject large=drainGraphics(h);
        require(large.getInt("video_width")==7680&&large.getInt("video_height")==4320&&large.getLong("video_bytes")==132710400L,"full 8K guest geometry");
        require(large.getLong("ram_allocated_bytes")<12L*1024*1024,"sparse 8K guest must account actual written pages");
        JSONObject eight=state(NativeMachine.acquireVideoFrame(h));long eightId=eight.getLong("revision");
        try{
            require(eight.getLong("snapshot_bytes")==132710400L,"8K snapshot byte accounting");
            requireMarkers(nativeMarkers(eight),"8K native");
            JSONObject withLease=state(NativeMachine.inspect(h));require(withLease.getLong("video_snapshot_bytes")>=132710400L&&withLease.getLong("video_snapshot_bytes")<=268435456L,"native snapshot memory budget");
            state(NativeMachine.reset(h));requireMarkers(nativeMarkers(eight),"8K lease after guest reset");
            JSONObject resetFrame=state(NativeMachine.acquireVideoFrame(h));long resetId=resetFrame.getLong("revision");
            try{
                require(resetFrame.getInt("kind")==PcVideoRenderer.TEXT&&resetId!=eightId,"reset returns real VGA mode and a new revision");
                ByteBuffer cleared=ByteBuffer.allocateDirect(4000);require(NativeMachine.copyVideoText(resetId,cleared)==4000,"reset text snapshot");
                NativeMachine.destroy(h);
                require(NativeMachine.copyVideoText(resetId,cleared)==4000,"VGA snapshot survives machine destruction");
                requireMarkers(nativeMarkers(eight),"8K lease after machine destroy");
            }finally{NativeMachine.releaseVideoFrame(resetId);}
            JSONObject report=new JSONObject();report.put("state",large);report.put("rgba",colors(nativeMarkers(eight)));report.put("snapshot_bytes",eight.getLong("snapshot_bytes"));report.put("snapshot_budget_bytes",268435456L);report.put("stable_after_reset",true);report.put("stable_after_destroy",true);videoReport.put("native_8k",report);
        }finally{NativeMachine.releaseVideoFrame(eightId);}
        rejected(()->NativeMachine.copyVideoRows(eightId,0,1,ByteBuffer.allocateDirect(30720)),"released 8K lease remains readable");
        videoReport.put("jni_invalid_requests_rejected",rejectedVideoRequests);
        long probe=NativeMachine.create(16);try{require(state(NativeMachine.inspect(probe)).getLong("video_snapshot_bytes")==0,"all native leases/cache must release their bytes after destruction");}finally{NativeMachine.destroy(probe);}
        videoReport.put("native_snapshot_bytes_after_cleanup",0);pass("JNI full 8K guest + leases survive reset/destroy and release memory");
        writeJson("pc-video-state.json",videoReport);
    }

    private void selectDialog(String label)throws Exception{
        long end=SystemClock.uptimeMillis()+10000;
        while(SystemClock.uptimeMillis()<end){
            AccessibilityNodeInfo root=getUiAutomation().getRootInActiveWindow();boolean clicked=false;
            if(root!=null){
                List<AccessibilityNodeInfo> nodes=root.findAccessibilityNodeInfosByText(label);
                for(AccessibilityNodeInfo node:nodes){
                    if(!clicked&&label.contentEquals(node.getText()==null?"":node.getText())&&node.isVisibleToUser()){
                        clicked=node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
                        if(!clicked){AccessibilityNodeInfo parent=node.getParent();if(parent!=null){clicked=parent.performAction(AccessibilityNodeInfo.ACTION_CLICK);parent.recycle();}}
                    }
                    node.recycle();
                }
                root.recycle();
            }
            if(clicked){waitForIdleSync();return;}Thread.sleep(100);
        }
        throw new AssertionError("Resolution dialog option missing: "+label);
    }
    private PcVideoView videoView(Activity a){
        PcVideoView[] result={null};runOnMainSync(()->{View found=tagged(current(a).getWindow().getDecorView(),"pc-screen");require(found instanceof PcVideoView,"Activity must use the real GLES PC video view");result[0]=(PcVideoView)found;found.requestRectangleOnScreen(new Rect(0,0,found.getWidth(),found.getHeight()),true);});return result[0];
    }
    private long activityMachine(Activity a)throws Exception{
        Field field=MainActivity.class.getDeclaredField("machine");field.setAccessible(true);long value=field.getLong(current(a));require(value!=0,"Activity has no native machine");return value;
    }
    private long finalVideoRevision(Activity a)throws Exception{
        JSONObject frame=state(NativeMachine.acquireVideoFrame(activityMachine(a)));long revision=frame.getLong("revision");NativeMachine.releaseVideoFrame(revision);return revision;
    }
    private void gl(PcVideoView view,Checked action)throws Exception{
        CountDownLatch done=new CountDownLatch(1);Throwable[] error={null};view.queueEvent(()->{try{action.run();}catch(Throwable problem){error[0]=problem;}finally{done.countDown();}});view.refresh();
        require(done.await(30,TimeUnit.SECONDS),"GL work timed out");if(error[0]!=null)throw new AssertionError("GL test failed",error[0]);
    }
    private PcVideoRenderer.Status waitVideo(PcVideoView view,int width,int height,int mode,long revision)throws Exception{
        long end=SystemClock.uptimeMillis()+30000;PcVideoRenderer.Status value=view.getStatus();view.refresh();
        while(SystemClock.uptimeMillis()<end){
            value=view.getStatus();
            if(value.contextReady&&!value.paused&&value.frameAvailable&&value.sourceKind==PcVideoRenderer.RGBA&&value.sourceWidth==width&&value.sourceHeight==height&&value.revision==revision&&value.activeMode==mode&&value.requestedMode==mode&&value.error.isEmpty()){
                if(mode==RenderResolution.ADAPTIVE&&value.outputWidth>0&&value.outputHeight>0)return value;
                if(mode!=RenderResolution.ADAPTIVE){RenderResolution.Size expected=RenderResolution.fixed(mode,value.screenWidth,value.screenHeight);if(value.outputWidth==expected.width&&value.outputHeight==expected.height)return value;}
            }
            Thread.sleep(50);
        }
        throw new AssertionError("Expected complete guest "+width+"x"+height+", output mode "+mode+", revision "+revision+"; got "+value.summary()+" revision="+value.revision);
    }
    private int[] glMarkers(PcVideoView view)throws Exception{
        int[] result=new int[5];gl(view,()->{
            PcVideoRenderer renderer=view.getVideoRenderer();PcVideoRenderer.Status s=renderer.getStatus();
            require(s.frameAvailable&&s.sourceKind==PcVideoRenderer.RGBA,"real guest pixel source required");
            RenderResolution.Fit fit=RenderResolution.fit(s.outputWidth,s.outputHeight,new RenderResolution.Size(s.sourceWidth,s.sourceHeight));
            int[] xs={s.sourceWidth/64,s.sourceWidth-1-s.sourceWidth/64,s.sourceWidth/64,s.sourceWidth-1-s.sourceWidth/64,s.sourceWidth/2+s.sourceWidth/64};
            int[] ys={s.sourceHeight/64,s.sourceHeight/64,s.sourceHeight-1-s.sourceHeight/64,s.sourceHeight-1-s.sourceHeight/64,s.sourceHeight/2};
            for(int i=0;i<5;i++){
                int x=fit.left+(int)(((2L*xs[i]+1)*fit.width)/(2L*s.sourceWidth));
                int y=fit.top+(int)(((2L*ys[i]+1)*fit.height)/(2L*s.sourceHeight));
                result[i]=renderer.readOutputPixelArgb(x,y);
            }
        });return result;
    }
    private JSONObject videoStatus(PcVideoRenderer.Status s,int[] sampled)throws Exception{
        JSONObject value=new JSONObject();value.put("guest_width",s.sourceWidth);value.put("guest_height",s.sourceHeight);value.put("output_width",s.outputWidth);value.put("output_height",s.outputHeight);value.put("active_mode",s.activeMode);value.put("requested_mode",s.requestedMode);value.put("revision",s.revision);value.put("frames",s.frames);value.put("uploaded_bytes",s.uploadedBytes);value.put("gl_texture_bytes",s.textureBytes);value.put("gl_texture_budget_bytes",PcVideoRenderer.MAX_TEXTURE_BYTES);value.put("texture_limit",s.textureLimit);value.put("renderer",s.renderer);value.put("error",s.error);value.put("rgba",colors(sampled));
        Debug.MemoryInfo memory=new Debug.MemoryInfo();Debug.getMemoryInfo(memory);long pss=memory.getTotalPss()*1024L;require(pss>0&&pss<2147483648L,"8K test process must remain below 2 GiB PSS");value.put("process_pss_bytes",pss);require(s.textureBytes>0&&s.textureBytes<=PcVideoRenderer.MAX_TEXTURE_BYTES,"GL textures exceed their declared budget");return value;
    }
    private void saveVideoScreenshot(Activity a,PcVideoView view)throws Exception{
        runOnMainSync(()->view.requestRectangleOnScreen(new Rect(0,0,view.getWidth(),view.getHeight()),true));waitForIdleSync();Thread.sleep(250);
        Bitmap shot=getUiAutomation().takeScreenshot();require(shot!=null,"Video screenshot unavailable");
        try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getFilesDir(),"pc-video-screen.png"))){require(shot.compress(Bitmap.CompressFormat.PNG,100,out),"Video screenshot encoding failed");}finally{shot.recycle();}
    }
    private void testActivityVideo(Activity a)throws Exception{
        click(a,"Prueba gráfica");selectDialog("1280 × 720");click(a,"Ejecutar");waitText(a,"DETENIDO · HLT",120000);
        PcVideoView view=videoView(a);long revision=finalVideoRevision(a);click(a,"4K");PcVideoRenderer.Status four=waitVideo(view,1280,720,RenderResolution.UHD_4K,revision);
        int[] fourPixels=glMarkers(view);requireMarkers(fourPixels,"Activity 720p -> real 4K FBO");videoReport.put("ui_4k",videoStatus(four,fourPixels));saveVideoScreenshot(a,view);pass("Activity guest 720p + actual GLES 4K framebuffer pixels");

        Field limitsField=PcVideoRenderer.class.getDeclaredField("limits");limitsField.setAccessible(true);RenderResolution.Limits[] actual={null};PcVideoRenderer renderer=view.getVideoRenderer();
        try{
            gl(view,()->{actual[0]=(RenderResolution.Limits)limitsField.get(renderer);require(actual[0].texture>=7680,"CI GPU must expose the actual 8K texture capability under test");limitsField.set(renderer,new RenderResolution.Limits(4096,actual[0].renderbuffer,actual[0].viewportWidth,actual[0].viewportHeight));renderer.setOutputMode(RenderResolution.UHD_8K);});
            long end=SystemClock.uptimeMillis()+10000;PcVideoRenderer.Status rejected=view.getStatus();
            while(SystemClock.uptimeMillis()<end){rejected=view.getStatus();if(rejected.requestedMode==RenderResolution.UHD_8K&&rejected.error.contains("GL_MAX_TEXTURE_SIZE=4096"))break;Thread.sleep(50);}
            require(rejected.requestedMode==RenderResolution.UHD_8K&&rejected.activeMode==RenderResolution.UHD_4K&&rejected.error.contains("GL_MAX_TEXTURE_SIZE=4096"),"Rejected 8K request must remain visible with active 4K");
            require(rejected.outputWidth==four.outputWidth&&rejected.outputHeight==four.outputHeight&&rejected.revision==revision,"Rejected mode must preserve complete source/output");requireMarkers(glMarkers(view),"4K pixels after rejected 8K");
            JSONObject rollback=new JSONObject();rollback.put("injected_texture_limit",4096);rollback.put("requested_mode",rejected.requestedMode);rollback.put("active_mode",rejected.activeMode);rollback.put("output_width",rejected.outputWidth);rollback.put("output_height",rejected.outputHeight);rollback.put("pixels_preserved",true);rollback.put("error",rejected.error);videoReport.put("gpu_limit_rollback",rollback);
        }finally{if(actual[0]!=null)gl(view,()->{limitsField.set(renderer,actual[0]);renderer.setOutputMode(RenderResolution.UHD_4K);});}
        waitVideo(view,1280,720,RenderResolution.UHD_4K,revision);pass("Unsupported 8K output preserves previous real FBO and guest pixels");

        click(a,"Prueba gráfica");selectDialog("7680 × 4320 (8K)");click(a,"Ejecutar");waitText(a,"DETENIDO · HLT",120000);
        view=videoView(a);revision=finalVideoRevision(a);click(a,"8K");PcVideoRenderer.Status eight=waitVideo(view,7680,4320,RenderResolution.UHD_8K,revision);
        int[] eightPixels=glMarkers(view);requireMarkers(eightPixels,"Activity full 8K guest -> actual 8K FBO");videoReport.put("ui_8k",videoStatus(eight,eightPixels));videoReport.put("ui_guest_state",state(NativeMachine.inspect(activityMachine(a))));saveVideoScreenshot(a,view);pass("Activity full 8K guest + actual GLES 8K framebuffer pixels");

        final PcVideoView originalView=view;long beforeUploads=eight.uploadedBytes;long beforeFrames=eight.frames;
        runOnMainSync(()->originalView.setPreserveEGLContextOnPause(false));sendKeyDownUpSync(KeyEvent.KEYCODE_HOME);
        long pauseEnd=SystemClock.uptimeMillis()+10000;while(SystemClock.uptimeMillis()<pauseEnd&&!originalView.getStatus().paused)Thread.sleep(50);
        require(originalView.getStatus().paused,"Activity/GL must pause when sent to background");
        Intent resume=new Intent(getTargetContext(),MainActivity.class);resume.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_REORDER_TO_FRONT|Intent.FLAG_ACTIVITY_SINGLE_TOP);runOnMainSync(()->getTargetContext().startActivity(resume));waitForIdleSync();
        long resumeEnd=SystemClock.uptimeMillis()+10000;while(SystemClock.uptimeMillis()<resumeEnd&&originalView.getStatus().paused)Thread.sleep(50);
        require(videoView(a)==originalView,"Returning to live Activity must preserve the guest instance");PcVideoRenderer.Status restored=waitVideo(originalView,7680,4320,RenderResolution.UHD_8K,revision);int[] restoredPixels=glMarkers(originalView);requireMarkers(restoredPixels,"8K guest after EGL context recreation");
        require(restored.uploadedBytes>=beforeUploads+132710400L&&restored.frames>beforeFrames,"Lost context must re-upload the real 8K guest frame");
        runOnMainSync(()->originalView.setPreserveEGLContextOnPause(true));JSONObject lifecycle=videoStatus(restored,restoredPixels);lifecycle.put("forced_context_loss",true);lifecycle.put("same_guest_revision",restored.revision==revision);videoReport.put("pause_resume",lifecycle);saveVideoScreenshot(a,originalView);pass("Activity pause/resume recreates EGL and preserves the full 8K guest");
        videoReport.put("status","passed");videoReport.put("windows_booted",false);videoReport.put("virtual_3d_gpu",false);writeJson("pc-video-state.json",videoReport);
    }
    @Override public void onStart(){Bundle result=new Bundle();long h=0;try{
        h=NativeMachine.create(16);require(h!=0,"native handle");JSONObject s=state(NativeMachine.bootDemo(h));require(s.getString("status").equals("ready"),s.toString());
        s=drain(h);require(s.getString("status").equals("waiting_key"),s.toString());require(NativeMachine.screen(h).contains("ALU + RAM + lectura de disco: OK"),"native guest output");require(s.getInt("disk_reads")==1,"native disk read");pass("native CPU + BIOS disk");
        NativeMachine.key(h,'K',0x25);s=drain(h);require(s.getString("status").equals("halted"),s.toString());require(NativeMachine.screen(h).contains("Pulsa una tecla: K"),"keyboard echo");require(!s.getBoolean("windows_ready"),"must not claim Windows");pass("keyboard + HLT");
        state(NativeMachine.reset(h));s=drain(h);require(s.getString("status").equals("waiting_key"),s.toString());pass("reset");
        s=mountBytes(h,raw(new byte[]{(byte)0xb8,'R',0x0e,(byte)0xcd,0x10,(byte)0xf4}),"pc-test.img");require(s.getString("status").equals("ready"),s.toString());s=drain(h);require(NativeMachine.screen(h).charAt(0)=='R',"raw guest executed");pass("raw image via retained fd");
        s=mountBytes(h,iso(),"pc-test.iso");require(s.getString("boot_kind").equals("iso"),s.toString());s=drain(h);require(s.getString("status").equals("halted"),s.toString());require(NativeMachine.screen(h).charAt(0)=='I',"ISO code executed");pass("El Torito guest");
        byte[] shiftIso=iso();byte[] shiftCode=raw(new byte[]{(byte)0xb8,0x34,0x12,(byte)0xba,0x78,0x56,(byte)0xb1,0x04,0x0f,(byte)0xad,(byte)0xd0,(byte)0xf4});System.arraycopy(shiftCode,0,shiftIso,24*2048,512);
        s=mountBytes(h,shiftIso,"pc-shrd.iso");require(s.getString("boot_kind").equals("iso"),"SHRD fixture must boot through El Torito");s=drain(h);require(s.getString("status").equals("halted")&&(s.getJSONArray("regs").getLong(0)&65535L)==0x8123,"ISO SHRD AX,DX,CL (0F AD) must execute, AX=8123: "+s);videoReport.put("iso_shrd",s);pass("El Torito ISO executes reported 0F AD SHRD instruction");
        s=mountBytes(h,raw(new byte[]{0x0f,0x0b}),"pc-invalid-op.img");s=drain(h);require(s.getString("status").equals("fault"),"invalid opcode stops");require(s.getInt("ip")==0x7c00,"fault location");pass("unsupported instruction diagnostic");
        s=mountBytes(h,new byte[512],"pc-invalid.img");require(s.getString("status").equals("fault"),"bad signature rejected");pass("invalid image rejected");
        NativeMachine.destroy(h);h=0;
        h=NativeMachine.create(16);
        String huge="1"+new String(new char[100]).replace('\0','0');
        s=state(NativeMachine.quantumCommand(h,"h 0\ncx 0 "+huge+"\nexpect X:0 X:"+huge));
        require(s.getString("status").equals("empty"),"host quantum program preserves empty PC");
        s=state(NativeMachine.quantumProbe(h,"0",huge));
        require(s.getJSONObject("quantum").getString("second").equals(huge),"JNI must preserve arbitrary decimal addresses");
        require(s.getJSONObject("quantum").getJSONArray("values").getDouble(0)==1,"probe sees actual Bell XX");
        String saved=NativeMachine.quantumSnapshot(h);
        s=state(NativeMachine.quantumCommand(h,"x -1"));
        require(!s.getJSONObject("quantum").getString("error").isEmpty()&&s.getString("status").equals("empty"),"quantum validation error does not fault CPU");
        require(NativeMachine.quantumSnapshot(h).equals(saved),"failed JNI command preserves complete state");
        NativeMachine.quantumReset(h,"bell");s=state(NativeMachine.quantumRestore(h,saved));
        require(s.getJSONObject("quantum").getJSONArray("values").getDouble(0)==1,"snapshot restores exact observed state");
        pass("host InfiniteCore commands + huge addresses + atomic snapshot restore");
        for(boolean longMode:new boolean[]{false,true}){
            NativeMachine.bootQuantumDemo(h,longMode);s=drain(h);
            require(s.getString("status").equals("halted"),"quantum guest reaches HLT");
            require(s.getString("cpu_mode").equals(longMode?"long64":"real16"),"quantum guest executes its mode transition");
            JSONObject q=s.getJSONObject("quantum");
            require(q.getLong("guest_requests")==1&&q.getLong("guest_errors")==0,"guest uses actual device ports");
            s=state(NativeMachine.quantumProbe(h,"0","1000000000"));q=s.getJSONObject("quantum");
            require(q.getJSONArray("values").getDouble(0)==1&&q.getJSONArray("values").getDouble(1)==-1,"guest produces signed Bell correlators");
            pass(longMode?"InfiniteCore guest long64 ports":"InfiniteCore guest real16 ports");
        }
        NativeMachine.bootQuantumPhaseDemo(h);s=drain(h);
        require(s.getJSONObject("quantum").getJSONArray("values").getDouble(0)==-1,"guest Z changes phase correlation");
        require(NativeMachine.screen(h).contains("InfiniteCore guest Z: OK"),"guest confirms its Z computation");
        pass("guest Z changes the live InfiniteCore state");
        NativeCore.ensureLoaded();
        String beforeGraphics=NativeMachine.quantumSnapshot(h);
        JSONObject beforeGraphicsState=state(NativeMachine.inspect(h)).getJSONObject("quantum");
        try(NativeCore shared=NativeCore.adoptInfiniteHandle(NativeMachine.quantumGraphicsHandle(h))){
            require(shared.query(new String[]{"X:0 X:1000000000"})[0]==-1,"graphics wrapper observes PC session");
            require(shared.etherStats().rules==0&&beforeGraphics.equals(shared.snapshot().infiniteSnapshot)
                    &&beforeGraphics.equals(NativeMachine.quantumSnapshot(h))
                    &&shared.version()==beforeGraphicsState.getLong("revision")&&shared.epoch()==beforeGraphicsState.getLong("epoch"),
                    "Adopting and observing the graphics handle must preserve the complete guest state and epoch");
            NativeMachine.quantumCommand(h,"z 0");
            require(shared.query(new String[]{"X:0 X:1000000000"})[0]==1,"host edit reaches same graphics owner");
            NativeMachine.destroy(h);h=0;
            require(shared.query(new String[]{"X:0 X:1000000000"})[0]==1,"owned graphics handle outlives destroyed PC safely");
        }
        pass("graphics bridge shares session and closes independently");
        h=NativeMachine.createWithBudget(2048,1024);
        s=state(NativeMachine.inspect(h));
        require(s.getLong("ram_bytes")==2147483648L,"2 GiB logical RAM must survive JNI/JSON");
        require(s.getLong("ram_budget_bytes")==1073741824L,"1 GiB guest payload cap");
        require(s.getLong("ram_allocated_bytes")<1048576L,"sparse startup must not commit 2 GiB");
        require(s.getLong("ram_metadata_bytes")<16777216L,"bounded RAM metadata");
        Debug.MemoryInfo memoryInfo=new Debug.MemoryInfo();Debug.getMemoryInfo(memoryInfo);
        long processPssBytes=memoryInfo.getTotalPss()*1024L;
        require(processPssBytes>0&&processPssBytes<2147483648L,"test process must remain within declared 2 GiB budget");
        pass("2 GiB sparse profile + measured Android PSS");
        s=state(NativeMachine.bootLongModeDemo(h));
        require(s.getString("cpu_mode").equals("real16"),"x64 guest must begin in real mode");
        s=drain(h);
        require(s.getString("status").equals("halted")&&s.getString("cpu_mode").equals("long64"),s.toString());
        require(s.getJSONArray("regs_hex").getString(0).equals("2345678922222223"),"64-bit arithmetic must preserve high bits");
        require(s.getJSONArray("regs_hex").getString(9).equals("2345678922222223"),"extended register R9 must preserve high bits");
        require(NativeMachine.screen(h).contains("COSMIC PC x64: OK"),"guest must report success after its own 64-bit memory comparison");
        require(!s.getBoolean("windows_ready")&&!s.getBoolean("graphics_3d_ready"),"x64 milestone must not claim Windows or gaming");
        require(s.getLong("ram_allocated_bytes")<1048576L,"x64 smoke test must keep a small working set");
        try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getFilesDir(),"pc-x64-state.json"))){out.write(s.toString(2).getBytes(StandardCharsets.UTF_8));}
        pass("guest real-to-protected-to-long mode + 64-bit result");
        s=state(NativeMachine.reset(h));require(s.getString("cpu_mode").equals("real16"),"reset must leave long mode");
        s=drain(h);require(s.getString("cpu_mode").equals("long64")&&s.getString("status").equals("halted"),"x64 guest must boot again after reset");
        require(s.getJSONArray("regs_hex").getString(0).equals("2345678922222223"),"x64 reset result");
        pass("x64 reset reruns actual transition");
        testNativeVideo(h);h=0;
        Intent intent=new Intent(getTargetContext(),MainActivity.class);intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);Activity a=startActivitySync(intent);waitForIdleSync();
        waitText(a,"Éter ∞ · 0 reglas");NativeCore activityCore=graphicsOwner(a);
        require(activityCore!=null&&activityCore.isInfinite()&&activityCore.etherStats().rules==0
                &&activityCore.version()==0&&activityCore.infiniteStats().activeQubits==0,
                "Opening the viewport must leave the new PC zero background unprepared");
        click(a,"Arranque de prueba");click(a,"Ejecutar");waitText(a,"ALU + RAM + lectura de disco: OK");pass("Activity operates real CPU");
        click(a,"Prueba x64");click(a,"Ejecutar");waitText(a,"long64");waitText(a,"DETENIDO · HLT");waitText(a,"COSMIC PC x64: OK");waitMemoryMeasurement(a);
        Debug.getMemoryInfo(memoryInfo);long activityPssBytes=memoryInfo.getTotalPss()*1024L;
        require(activityPssBytes>0&&activityPssBytes<2147483648L,"Activity test process must remain within the 2 GiB smoke-test budget");
        pass("Activity operates x64 guest and displays measured memory");
        click(a,"Prueba núcleo x86");click(a,"Ejecutar");waitText(a,"InfiniteCore x86: OK");waitText(a,"XX=1 · YY=-1 · ZZ=1");waitText(a,"Éter ∞ · 0 reglas");
        require(graphicsOwner(a)==activityCore&&activityCore.etherStats().rules==0,"Guest execution must use the existing graphics owner");
        String guestState=activityCore.snapshot().infiniteSnapshot;long guestEpoch=activityCore.epoch();
        QuantumRenderer.Capture coupledBefore=frame(a,true);
        require(coupledBefore.nonBackground()>100,"viewport contains rendered scene pixels");
        QuantumRenderer.Capture uncoupledBefore=frame(a,false);
        require(guestState.equals(activityCore.snapshot().infiniteSnapshot)&&activityCore.epoch()==guestEpoch,
                "Drawing coupled and uncoupled views must not prepare or reset the guest session");
        click(a,"Prueba Z del huésped");click(a,"Ejecutar");waitText(a,"InfiniteCore guest Z: OK");waitText(a,"XX=-1 · YY=1 · ZZ=1");
        QuantumRenderer.Capture coupledAfter=frame(a,true);
        int coupledChanged=coupledBefore.changed(coupledAfter);
        require(coupledChanged>10,"guest Z must visibly change the canonical coupled GLES framebuffer");
        pass("Activity guest Z changes same-session GLES pixels");
        QuantumRenderer.Capture uncoupledAfter=frame(a,false);
        require(uncoupledBefore.changed(uncoupledAfter)==0,"uncoupled frozen scene must ignore guest core mutation");
        pass("GLES uncoupled control retains identical pixels");
        runOnMainSync(()->((EditText)tagged(a.getWindow().getDecorView(),"pc-quantum-program")).setText("z 0\nexpect X:0 X:1000000000"));
        click(a,"Aplicar programa");waitText(a,"XX=1 · YY=-1 · ZZ=1");
        require(coupledBefore.changed(frame(a,true))==0,"UI Z inverse restores exact coupled pixels");
        JSONObject etherContract=etherProgramRoundtrip(a,activityCore,coupledBefore);
        long unchangedEpoch=renderer(a).frameStats().coreEpoch;
        String oversized="x 0\n"+new String(new char[65533]).replace('\0',' ');
        runOnMainSync(()->{EditText editor=(EditText)tagged(a.getWindow().getDecorView(),"pc-quantum-program");editor.setText(oversized);require(editor.getText().length()==oversized.length(),"pasted program must not be silently truncated");});
        click(a,"Aplicar programa");waitText(a,"Texto del núcleo excede el límite de bytes");
        require(coupledBefore.changed(frame(a,true))==0&&renderer(a).frameStats().coreEpoch==unchangedEpoch,"oversized pasted request must preserve state, epoch and pixels");
        pass("Activity applies quantum program to its live PC session");
        runOnMainSync(()->a.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));
        waitOrientation(a,Configuration.ORIENTATION_LANDSCAPE);waitForIdleSync();waitText(a,"XX=1 · YY=-1 · ZZ=1");waitText(a,"InfiniteCore guest Z: OK");
        require(!a.isDestroyed(),"configured rotation must retain the PC Activity and native session");
        runOnMainSync(()->a.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));waitOrientation(a,Configuration.ORIENTATION_PORTRAIT);waitForIdleSync();
        pass("rotation retains PC and InfiniteCore state");
        File evidence=getTargetContext().getFilesDir();Bitmap shot=getUiAutomation().takeScreenshot();if(shot!=null){try(FileOutputStream out=new FileOutputStream(new File(evidence,"pc-screen.png"))){shot.compress(Bitmap.CompressFormat.PNG,100,out);}shot.recycle();}
        testActivityVideo(a);require(passed==29,"Expected exactly 29 tests: 12 existing, 8 ISO/video and 9 InfiniteCore");
        runOnMainSync(()->current(a).finish());waitForIdleSync();
        String summary="COSMIC_PC_TESTS_PASSED tests="+passed+" failures=0\n";result.putString("stream",summary);result.putInt("passed",passed);
        JSONObject report=new JSONObject();report.put("tests",passed);report.put("failures",0);report.put("windows_booted",false);report.put("x64_guest_verified",true);report.put("native_test_process_pss_bytes",processPssBytes);report.put("activity_process_pss_bytes",activityPssBytes);report.put("logical_ram_bytes",2147483648L);report.put("guest_payload_cap_bytes",1073741824L);report.put("infinite_guest_verified",true);report.put("shared_session_graphics_verified",true);report.put("coupled_changed_pixels",coupledChanged);report.put("uncoupled_changed_pixels",uncoupledBefore.changed(uncoupledAfter));
        report.put("graphics_bridge_read_only",true).put("graphics_open_read_only",true).put("ether_fusion",etherContract);
        report.put("iso_shrd_verified",true);report.put("guest_8k_verified",true);report.put("output_8k_verified",true);report.put("video_tests",8);
        try(FileOutputStream out=new FileOutputStream(new File(evidence,"pc-tests.json"))){out.write(report.toString(2).getBytes(StandardCharsets.UTF_8));}finish(Activity.RESULT_OK,result);
    }catch(Throwable e){if(h!=0)try{NativeMachine.destroy(h);}catch(Throwable ignored){}try{videoReport.put("status","failed");videoReport.put("passed_before_failure",passed);videoReport.put("failure",android.util.Log.getStackTraceString(e));writeJson("pc-video-state.json",videoReport);}catch(Throwable ignored){}result.putString("stream","COSMIC_PC_TESTS_FAILED passed="+passed+" "+android.util.Log.getStackTraceString(e));finish(Activity.RESULT_CANCELED,result);}}
}
