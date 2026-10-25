package com.cosmicx.win10;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.view.View;
import android.view.ViewGroup;
import com.cosmicx.pc.NativeMachine;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.util.Arrays;

/** Tests a synthetic dual BIOS/EFI ISO with the actual JNI CPU on Android. */
public final class WindowsInstrumentation extends Instrumentation {
    private static final int SECTOR=2048;
    private static void require(boolean good,String reason){
        if(!good)throw new AssertionError(reason);
    }
    private static void descriptor(byte[] b,int lba,int type){
        int i=lba*SECTOR;
        b[i]=(byte)type;
        byte[] id="CD001".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        System.arraycopy(id,0,b,i+1,id.length);
        b[i+6]=1;
    }
    private static void put16(byte[] b,int i,int n){
        b[i]=(byte)n;b[i+1]=(byte)(n>>>8);
    }
    private static void put32(byte[] b,int i,int n){
        for(int j=0;j<4;j++)b[i+j]=(byte)(n>>>(j*8));
    }
    private static byte[] fixture(){
        byte[] b=new byte[64*SECTOR];
        descriptor(b,16,1);
        byte[] volume="WIN10_TEST_DUAL_BOOT".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        System.arraycopy(volume,0,b,16*SECTOR+40,volume.length);
        descriptor(b,17,0);
        byte[] id="EL TORITO SPECIFICATION".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        System.arraycopy(id,0,b,17*SECTOR+7,id.length);
        put32(b,17*SECTOR+0x47,20);
        descriptor(b,18,255);
        int cat=20*SECTOR;
        b[cat]=1;b[cat+1]=0;
        b[cat+30]=0x55;b[cat+31]=(byte)0xaa;
        b[cat+32]=(byte)0x88;put16(b,cat+38,4);put32(b,cat+40,24);
        b[cat+64]=(byte)0x91;b[cat+65]=(byte)0xef;put16(b,cat+66,1);
        b[cat+96]=(byte)0x88;put16(b,cat+102,1);put32(b,cat+104,30);
        int sum=0;
        for(int i=0;i<32;i+=2)
            sum=(sum+(b[cat+i]&255)+((b[cat+i+1]&255)<<8))&0xffff;
        put16(b,cat+28,(0x10000-sum)&0xffff);
        // Guest code MOV AX,0e57; INT10h; HLT -> real 'W' in VGA VRAM.
        byte[] boot={(byte)0xb8,0x57,0x0e,(byte)0xcd,0x10,(byte)0xf4};
        System.arraycopy(boot,0,b,24*SECTOR,boot.length);
        return b;
    }
    private static byte[] i8042Fixture(){
        byte[] b=fixture();
        final int cat=20*SECTOR;
        put16(b,cat+32+2,0x2000);
        put16(b,cat+32+6,8);
        final int at=24*SECTOR;
        Arrays.fill(b,at,at+4096,(byte)0);
        // Guest starts 2000:0000, reproduces BOTH real ISO fault sites: 0ACE and 0ADC.
        b[at]=(byte)0xb0;b[at+1]=(byte)0xd1;        // MOV AL,D1
        b[at+2]=(byte)0xe9;b[at+3]=(byte)0xc9;
        b[at+4]=(byte)0x0a;                        // JMP 0ACE
        final byte[] code={
            (byte)0xe6,0x64,                         // 0ACE OUT 64,D1
            (byte)0xb0,(byte)0xdf,                    // 0AD0 MOV AL,DF
            (byte)0xe6,0x60,                         // 0AD2 OUT 60,DF, A20 on
            (byte)0xe4,0x64,                         // 0AD4 IN AL,64 (8042 status)
            (byte)0xa8,0x02,                         // 0AD6 TEST AL,02
            0x75,0x08,                               // 0AD8 JNZ 0AE2 -> UD2
            (byte)0xb0,(byte)0xff,                    // 0ADA MOV AL,FF
            (byte)0xe6,0x64,                         // 0ADC OUT 64,FF, no-line pulse
            (byte)0xf4,                              // 0ADE HLT
            (byte)0x90,(byte)0x90,(byte)0x90,         // 0ADF..0AE1
            0x0f,0x0b                                // 0AE2 UD2 if busy flag
        };
        System.arraycopy(code,0,b,at+0x0ace,code.length);
        return b;
    }

    private static void put64(byte[] b,int at,long value){
        for(int j=0;j<8;j++) b[at+j]=(byte)(value >>> (j*8));
    }
    private static long desc(long base,long limit,int access){
        return (limit&0xffffL)|((base&0xffffffL)<<16)|
            (((long)access)<<40)|(((limit>>>16)&15L)<<48)|
            (((base>>>24)&255L)<<56);
    }

    /** Boot real16 ISO code, enter protected16, run actual group-6 x86
     * instruction at CS:IP 0050:0854 via the original PcMachine JNI. */
    private static byte[] protected16Fixture(){
        byte[] b=fixture();
        final int catalog=20*SECTOR;
        put16(b,catalog+34,0x2000);
        put16(b,catalog+38,16);
        final int at=24*SECTOR;
        Arrays.fill(b,at,at+8192,(byte)0);
        final byte[] stub={
            (byte)0xfa,0x0e,0x1f,
            0x0f,0x01,0x16,0x00,0x02, // LGDT DS:[0200]
            0x0f,0x20,(byte)0xc0,     // MOV EAX,CR0
            0x0d,0x01,0x00,           // OR AX,1
            0x0f,0x22,(byte)0xc0,     // MOV CR0,EAX, protected mode
            (byte)0xb8,0x60,0x00,     // MOV AX, 0x60 TSS selector
            (byte)0xea,0x54,0x08,0x50,0x00 // FAR JMP 0050:0854
        };
        System.arraycopy(stub,0,b,at,stub.length);
        put16(b,at+0x200,0x006f);
        put32(b,at+0x202,0x20280);
        put64(b,at+0x280+0x50,desc(0x20000,0x1fff,0x9a)); // 16-bit code
        put64(b,at+0x280+0x58,desc(0x20000,0x1fff,0x92)); // data
        put64(b,at+0x280+0x60,desc(0x25000,0x67,0x89)); // TSS available
        put64(b,at+0x280+0x68,desc(0x21800,0x1f,0x82)); // LDTR descriptor
        put64(b,at+0x1808,desc(0x20000,0x1fff,0x92)); // LDT entry
        final byte[] group6={
            0x0f,0x00,(byte)0xd8, // LTR AX at 0050:0854
            0x0f,0x00,(byte)0xc8, // STR AX
            (byte)0xb8,0x58,0x00,
            0x0f,0x00,(byte)0xe0, // VERR
            0x0f,0x00,(byte)0xe8, // VERW
            (byte)0xb8,0x68,0x00,
            0x0f,0x00,(byte)0xd0, // LLDT
            0x0f,0x00,(byte)0xc0, // SLDT
            (byte)0xb8,0x0c,0x00,
            0x0f,0x00,(byte)0xe0, // VERR LDT
            (byte)0xf4
        };
        System.arraycopy(group6,0,b,at+0x0854,group6.length);
        return b;
    }

    private static View find(View view,String description){
        if(view.getContentDescription()!=null && description.contentEquals(view.getContentDescription()))return view;
        if(view instanceof ViewGroup){
            ViewGroup group=(ViewGroup)view;
            for(int i=0;i<group.getChildCount();i++){
                View got=find(group.getChildAt(i),description);
                if(got!=null)return got;
            }
        }
        return null;
    }
    @Override public void onCreate(Bundle arguments){
        super.onCreate(arguments);
        start();
    }
    @Override public void onStart(){
        super.onStart();
        Bundle response=new Bundle();
        long handle=0;
        Activity launched=null;
        try{
            File path=new File(getTargetContext().getCacheDir(),"windows-iso-fixture.iso");
            byte[] bytes=fixture();
            try(FileOutputStream out=new FileOutputStream(path)){out.write(bytes);}
            try(ParcelFileDescriptor fd=ParcelFileDescriptor.open(path,ParcelFileDescriptor.MODE_READ_ONLY)){
                JSONObject info=new JSONObject(NativeMachine.probeIso(fd.getFd(),path.length()));
                require(info.optBoolean("bios_entry"),"BIOS El Torito absent");
                require(info.optBoolean("uefi_entry"),"EFI El Torito absent");
                require(info.optBoolean("can_attempt_bios_boot"),"own BIOS loader should accept fixture");
                require(info.getInt("bios_image_lba")==24&&info.getInt("uefi_image_lba")==30,
                        "catalog locations were misread");
                require(info.getLong("bytes_examined")<40000,
                        "probe copied the ISO rather than consulting bounded headers");
                require(!info.optBoolean("windows_installer_implemented"),
                        "false Windows 10 compatibility claim");
                handle=NativeMachine.createWithBudget(2048,256);
                require(handle!=0,"native x86 machine could not start");
                JSONObject mounted=new JSONObject(NativeMachine.mount(handle,fd.getFd(),
                                            path.length(),"iso"));
                require("ready".equals(mounted.optString("status"))&&
                        "iso".equals(mounted.optString("boot_kind")),"BootMedia did not mount real ISO bytes");
                JSONObject state=new JSONObject(NativeMachine.run(handle,5000));
                require("halted".equals(state.optString("status")),"guest did not run real ISO code");
                require(state.optLong("instructions")>=3,"insufficient guest instructions");
                require(NativeMachine.screen(handle).contains("W"),"guest VGA output not from CPU");
                require(!state.optBoolean("windows_ready"),"incomplete emulator marked Windows ready");
                require(state.optInt("ether_memory_bytes")==256,"Ether memory window absent");
                JSONObject long64=new JSONObject(NativeMachine.bootLongModeDemo(handle));
                for(int i=0;i<10;i++){
                    if("halted".equals(long64.optString("status")))break;
                    long64=new JSONObject(NativeMachine.run(handle,5000));
                }
                require("long64".equals(long64.optString("cpu_mode")),
                        "real->protected->long 64-bit transition missing");
            }
            File ps2Path=new File(getTargetContext().getCacheDir(),"ps2-controller-2000-0ACE.iso");
            try(FileOutputStream out=new FileOutputStream(ps2Path)){out.write(i8042Fixture());}
            try(ParcelFileDescriptor fd=ParcelFileDescriptor.open(
                ps2Path,ParcelFileDescriptor.MODE_READ_ONLY)){
                JSONObject m=new JSONObject(NativeMachine.mount(handle,fd.getFd(),
                                ps2Path.length(),"iso"));
                require("ready".equals(m.optString("status")) &&
                        m.optInt("cs")==0x2000&&m.optInt("ip")==0,
                        "El Torito fixture must boot at CS:IP 2000:0000");
                // Capture the execution of the *actual* E6 64 command.
                JSONObject before=new JSONObject(NativeMachine.run(handle,2));
                require(before.optInt("ip")==0x0ace &&
                        before.optLong("instructions")==2,
                        "synthetic guest must reach 2000:0ACE exactly");
                JSONObject after=new JSONObject(NativeMachine.run(handle,1));
                JSONObject kbc=after.getJSONObject("i8042");
                require(!"fault".equals(after.optString("status")) &&
                        after.optInt("ip")==0x0ad0,
                        "OUT 0x64 must execute successfully and advance to 0AD0");
                require(kbc.getInt("last_command")==0xd1 &&
                        kbc.getInt("pending_payload")==2 &&
                        kbc.getInt("command_writes")==1,
                        "i8042 must consume actual guest command D1");
                JSONObject beforeFf=new JSONObject(NativeMachine.run(handle,6));
                JSONObject afterD1=beforeFf.getJSONObject("i8042");
                require(!"fault".equals(beforeFf.optString("status")) &&
                        beforeFf.optInt("cs")==0x2000 &&
                        beforeFf.optInt("ip")==0x0adc &&
                        (beforeFf.getJSONArray("regs").getLong(0)&255L)==0xffL,
                        "guest must reach actual second ISO fault 2000:0ADC with AL=FF");
                require(afterD1.getInt("command_writes")==1 &&
                        afterD1.getInt("last_command")==0xd1 &&
                        afterD1.getBoolean("a20_enabled"),
                        "8042 A20 remains on before valid FF command");
                JSONObject afterFf=new JSONObject(NativeMachine.run(handle,1));
                JSONObject afterFf8042=afterFf.getJSONObject("i8042");
                require(!"fault".equals(afterFf.optString("status")) &&
                        afterFf.optInt("ip")==0x0ade &&
                        afterFf.getLong("instructions")==beforeFf.getLong("instructions")+1,
                        "FF at 2000:0ADC must execute and advance to 0ADE");
                require(afterFf8042.getInt("last_command")==0xff &&
                        afterFf8042.getInt("command_writes")==2 &&
                        afterFf8042.getInt("data_writes")==1 &&
                        afterFf8042.getBoolean("a20_enabled"),
                        "FF records real command without altering A20 or keyboard data");
                JSONObject finished=new JSONObject(NativeMachine.run(handle,100));
                JSONObject completed=finished.getJSONObject("i8042");
                require("halted".equals(finished.optString("status")) &&
                        "real16".equals(finished.optString("cpu_mode")),
                        "guest must finish after i8042 and stay in BIOS real16");
                require(completed.getInt("data_writes")==1 &&
                        completed.getInt("status_reads")>=1 &&
                        completed.getBoolean("a20_enabled"),
                        "i8042 data port/status/A20 must be active on Android");
                require(!finished.optBoolean("windows_ready"),
                        "i8042 support does not establish Windows 10 boot");
            }
            ps2Path.delete();
            // Real guest instruction in protected16, using actual GDT/LDT/TSS
            // bytes from a self-contained synthetic El Torito ISO.
            File group6Path=new File(getTargetContext().getCacheDir(),
                "windows-protected16-group6-0050-0854.iso");
            try(FileOutputStream out=new FileOutputStream(group6Path)){
                out.write(protected16Fixture());
            }
            try(ParcelFileDescriptor fd=ParcelFileDescriptor.open(
                    group6Path,ParcelFileDescriptor.MODE_READ_ONLY)){
                JSONObject loaded=new JSONObject(NativeMachine.mount(handle,
                    fd.getFd(),group6Path.length(),"iso"));
                require("ready".equals(loaded.optString("status")) &&
                    loaded.getInt("cs")==0x2000 && loaded.getInt("ip")==0,
                    "group6 fixture did not mount to genuine BIOS CS:IP");
                JSONObject beforeGroup6=loaded;
                for(int i=0;i<60;i++){
                    if(beforeGroup6.optInt("cs")==0x50 &&
                        beforeGroup6.optInt("ip")==0x0854)break;
                    require(!"fault".equals(beforeGroup6.optString("status")),
                        "mode switch fault before group6: "+
                        beforeGroup6.optString("message"));
                    beforeGroup6=new JSONObject(NativeMachine.run(handle,1));
                }
                require(beforeGroup6.getInt("cs")==0x50 &&
                    beforeGroup6.getInt("ip")==0x0854 &&
                    "protected16".equals(beforeGroup6.getString("cpu_mode")),
                    "ISO guest failed to enter protected16 at screenshot 0050:0854");
                JSONObject afterGroup6=new JSONObject(NativeMachine.run(handle,1));
                JSONObject task=afterGroup6.getJSONObject("system_registers");
                require(!"fault".equals(afterGroup6.optString("status")) &&
                    afterGroup6.getInt("ip")==0x0857 &&
                    task.optBoolean("tr_valid") &&
                    task.getInt("tr_selector")==0x60 &&
                    !task.optBoolean("task_switching_implemented"),
                    "0F 00 D8 LTR must load task register and advance three bytes");
                JSONObject full=new JSONObject(NativeMachine.run(handle,100));
                JSONObject sr=full.getJSONObject("system_registers");
                require("halted".equals(full.getString("status")) &&
                    "protected16".equals(full.getString("cpu_mode")) &&
                    sr.optBoolean("ldtr_valid") &&
                    sr.getInt("ldtr_selector")==0x68 &&
                    sr.getInt("tr_selector")==0x60,
                    "group6 complete ISA operations failed on Android JNI");
                require(!full.optBoolean("windows_ready"),
                    "protected16 group6 success is not Windows 10 support");
            }
            group6Path.delete();
            // Validate that the user-visible app is a real ISO frontend, not
            // a pre-rendered Windows desktop.
            Intent intent=new Intent();
            intent.setClassName("com.cosmicx.win10","com.cosmicx.win10.WindowsActivity");
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            launched=startActivitySync(intent);
            waitForIdleSync();
            require(find(launched.getWindow().getDecorView(),
                         "mundo-x-win10-pick-iso")!=null,
                    "user cannot select an ISO with the UI");
            response.putString("stream",
                    "WIN10_ISO_ANDROID_PASS bios=1 uefi=1 real_guest_boot=W "+
                    "long_mode=true ether_bytes=256 i8042=true "+
                    "exact_guest=2000:0ACE ff_guest=2000:0ADC "+
                    "pulse_ff_nop=true group6_protected16=true "+
                    "exact_group6_guest=0050:0854 windows_ready=false");
            final Activity activity=launched;
            runOnMainSync(activity::finish);
            NativeMachine.destroy(handle);handle=0;
            path.delete();
            finish(Activity.RESULT_OK,response);
        }catch(Throwable error){
            response.putString("stream","WIN10_ISO_ANDROID_FAIL "+error);
            if(handle!=0)NativeMachine.destroy(handle);
            final Activity activity=launched;
            if(activity!=null)runOnMainSync(activity::finish);
            finish(Activity.RESULT_CANCELED,response);
        }
    }
}
