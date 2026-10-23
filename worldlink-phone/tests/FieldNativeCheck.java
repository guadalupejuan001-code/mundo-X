package com.mundox.worldlink.phone;
import java.util.Arrays;
public final class FieldNativeCheck {
    public static void main(String[] args){
        NativeFieldRule store=new NativeFieldRule();
        boolean missing=false;try{store.load();}catch(IllegalStateException expected){missing=true;}
        if(!missing)throw new AssertionError("Unfitted rule must not exist");
        double[] p={21,-8,42,4,-3,2};store.save(p);
        if(!Arrays.equals(p,store.load()))throw new AssertionError("JNI readback changed parameters");
        p[0]=Double.NaN;boolean invalid=false;try{store.save(p);}catch(IllegalStateException expected){invalid=true;}
        if(!invalid||store.load()[0]!=21)throw new AssertionError("Invalid update corrupted the rule");
        System.out.println("PASS real JNI -> canonical Ether -> readback, missing rule, invalid update rollback");
    }
}
