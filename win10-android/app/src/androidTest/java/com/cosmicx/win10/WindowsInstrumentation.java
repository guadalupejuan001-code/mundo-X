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
    private static View find(View view,String description){
        if(description.contentEquals(view.getContentDescription()))return view;
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
                    "long_mode=true ether_bytes=256 windows_ready=false");
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
