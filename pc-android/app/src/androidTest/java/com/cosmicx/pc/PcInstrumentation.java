package com.cosmicx.pc;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.Debug;
import android.os.ParcelFileDescriptor;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.TextView;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

/** Executes the shipped native engine; no substituted output or mock CPU. */
public final class PcInstrumentation extends Instrumentation {
    private int passed=0;
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
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
    private void click(Activity a,String text)throws Exception{for(int i=0;i<80;i++){boolean[] clicked={false};runOnMainSync(()->{View v=find(a.getWindow().getDecorView(),text);require(v!=null,"Button missing: "+text);if(v.isEnabled()){v.performClick();clicked[0]=true;}});if(clicked[0])return;Thread.sleep(100);}throw new AssertionError("Button stayed disabled: "+text);}
    private void waitText(Activity a,String text)throws Exception{for(int i=0;i<80;i++){boolean[] found={false};runOnMainSync(()->found[0]=contains(a.getWindow().getDecorView(),text));if(found[0])return;Thread.sleep(100);}throw new AssertionError("UI did not show: "+text);}
    private View tagged(View v,String name){if(name.contentEquals(v.getContentDescription()==null?"":v.getContentDescription()))return v;if(v instanceof ViewGroup){ViewGroup group=(ViewGroup)v;for(int i=0;i<group.getChildCount();i++){View match=tagged(group.getChildAt(i),name);if(match!=null)return match;}}return null;}
    private void waitMemoryMeasurement(Activity a)throws Exception{for(int i=0;i<80;i++){boolean[] measured={false};runOnMainSync(()->{View v=tagged(a.getWindow().getDecorView(),"pc-memory");if(v instanceof TextView)measured[0]=((TextView)v).getText().toString().matches("(?s).*PSS[^\\n]*[0-9][^\\n]*.*");});if(measured[0])return;Thread.sleep(100);}throw new AssertionError("UI must display a numeric PSS sample, not only a label");}
    @Override public void onStart(){Bundle result=new Bundle();long h=0;try{
        h=NativeMachine.create(16);require(h!=0,"native handle");JSONObject s=state(NativeMachine.bootDemo(h));require(s.getString("status").equals("ready"),s.toString());
        s=drain(h);require(s.getString("status").equals("waiting_key"),s.toString());require(NativeMachine.screen(h).contains("ALU + RAM + lectura de disco: OK"),"native guest output");require(s.getInt("disk_reads")==1,"native disk read");pass("native CPU + BIOS disk");
        NativeMachine.key(h,'K',0x25);s=drain(h);require(s.getString("status").equals("halted"),s.toString());require(NativeMachine.screen(h).contains("Pulsa una tecla: K"),"keyboard echo");require(!s.getBoolean("windows_ready"),"must not claim Windows");pass("keyboard + HLT");
        state(NativeMachine.reset(h));s=drain(h);require(s.getString("status").equals("waiting_key"),s.toString());pass("reset");
        s=mountBytes(h,raw(new byte[]{(byte)0xb8,'R',0x0e,(byte)0xcd,0x10,(byte)0xf4}),"pc-test.img");require(s.getString("status").equals("ready"),s.toString());s=drain(h);require(NativeMachine.screen(h).charAt(0)=='R',"raw guest executed");pass("raw image via retained fd");
        s=mountBytes(h,iso(),"pc-test.iso");require(s.getString("boot_kind").equals("iso"),s.toString());s=drain(h);require(s.getString("status").equals("halted"),s.toString());require(NativeMachine.screen(h).charAt(0)=='I',"ISO code executed");pass("El Torito guest");
        s=mountBytes(h,raw(new byte[]{0x0f,0x0b}),"pc-invalid-op.img");s=drain(h);require(s.getString("status").equals("fault"),"invalid opcode stops");require(s.getInt("ip")==0x7c00,"fault location");pass("unsupported instruction diagnostic");
        s=mountBytes(h,new byte[512],"pc-invalid.img");require(s.getString("status").equals("fault"),"bad signature rejected");pass("invalid image rejected");
        NativeMachine.destroy(h);h=0;
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
        NativeMachine.destroy(h);h=0;
        Intent intent=new Intent(getTargetContext(),MainActivity.class);intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);Activity a=startActivitySync(intent);waitForIdleSync();
        click(a,"Arranque de prueba");click(a,"Ejecutar");waitText(a,"ALU + RAM + lectura de disco: OK");pass("Activity operates real CPU");
        click(a,"Prueba x64");click(a,"Ejecutar");waitText(a,"long64");waitText(a,"DETENIDO · HLT");waitText(a,"COSMIC PC x64: OK");waitMemoryMeasurement(a);
        Debug.getMemoryInfo(memoryInfo);long activityPssBytes=memoryInfo.getTotalPss()*1024L;
        require(activityPssBytes>0&&activityPssBytes<2147483648L,"Activity test process must remain within the 2 GiB smoke-test budget");
        pass("Activity operates x64 guest and displays measured memory");
        File evidence=getTargetContext().getFilesDir();Bitmap shot=getUiAutomation().takeScreenshot();if(shot!=null){try(FileOutputStream out=new FileOutputStream(new File(evidence,"pc-screen.png"))){shot.compress(Bitmap.CompressFormat.PNG,100,out);}shot.recycle();}
        runOnMainSync(a::finish);waitForIdleSync();
        String summary="COSMIC_PC_TESTS_PASSED tests="+passed+" failures=0\n";result.putString("stream",summary);result.putInt("passed",passed);
        JSONObject report=new JSONObject();report.put("tests",passed);report.put("failures",0);report.put("windows_booted",false);report.put("x64_guest_verified",true);report.put("native_test_process_pss_bytes",processPssBytes);report.put("activity_process_pss_bytes",activityPssBytes);report.put("logical_ram_bytes",2147483648L);report.put("guest_payload_cap_bytes",1073741824L);
        try(FileOutputStream out=new FileOutputStream(new File(evidence,"pc-tests.json"))){out.write(report.toString(2).getBytes(StandardCharsets.UTF_8));}finish(Activity.RESULT_OK,result);
    }catch(Throwable e){if(h!=0)try{NativeMachine.destroy(h);}catch(Throwable ignored){}result.putString("stream","COSMIC_PC_TESTS_FAILED passed="+passed+" "+android.util.Log.getStackTraceString(e));finish(Activity.RESULT_CANCELED,result);}}
}
