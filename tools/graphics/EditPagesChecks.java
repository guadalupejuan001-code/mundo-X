package com.mundox.graphics;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.TreeMap;

/** Exhaustive decoding against the original map; also exports cross-language fixtures. */
public final class EditPagesChecks {
    private static long checks;
    private static void require(boolean ok){checks++;if(!ok)throw new AssertionError("Edit page check "+checks);}
    private static int decode(EditPages pages,int id){
        int base=pages.bytes.getInt((id>>>8)*4);
        return base==0?0:(pages.bytes.getInt((base+((id&255)>>>3))*4)>>>((id&7)*4))&7;
    }
    private static void rejects(Runnable action){boolean failed=false;try{action.run();}catch(IllegalArgumentException e){failed=true;}require(failed);}
    public static void main(String[] args) throws Exception {
        int[] counts={1,257,257,1_048_576,1_000_003,65_536,1_048_576};
        for(int test=0;test<counts.length;test++){
            int count=counts[test];TreeMap<Integer,Integer> edits=new TreeMap<>();
            if(test==1){edits.put(0,0);edits.put(256,0);}
            if(test==2){int[] ids={0,7,8,255,256};for(int id:ids)edits.put(id,1+id%7);}
            if(test==3)for(int id=0;id<64;id++)edits.put(id,id%8);
            if(test==4)for(int j=0;j<4096;j++)edits.put((j*7919)%count,j%8);
            if(test==5||test==6)for(int id=0;id<count;id++)edits.put(id,(id*13+5)%8);
            int max=4096;
            EditPages pages=EditPages.encode(count,edits,max);
            require(pages.bytes.position()==0);
            if(!edits.isEmpty())for(int id=0;id<count;id++)require(decode(pages,id)==edits.getOrDefault(id,0));
            require(pages.bytes.capacity()<=557056);
            if(args.length>0){byte[] data=new byte[pages.bytes.capacity()];pages.bytes.duplicate().get(data);Files.write(Paths.get(args[0],test+".bin"),data);}
            System.out.println("{\"case\":"+test+",\"count\":"+count+",\"width\":"+pages.width+",\"height\":"+pages.height+",\"bytes\":"+pages.bytes.capacity()+"}");
        }
        TreeMap<Integer,Integer> invalid=new TreeMap<>();invalid.put(0,8);rejects(()->EditPages.encode(1,invalid,4096));
        invalid.put(0,-1);rejects(()->EditPages.encode(1,invalid,4096));
        invalid.clear();invalid.put(-1,2);rejects(()->EditPages.encode(1,invalid,4096));
        invalid.clear();invalid.put(1,2);rejects(()->EditPages.encode(1,invalid,4096));
        invalid.clear();rejects(()->EditPages.encode(0,invalid,4096));rejects(()->EditPages.encode(1_048_577,invalid,4096));
        rejects(()->EditPages.encode(1,invalid,0));invalid.put(0,1);rejects(()->EditPages.encode(1_048_576,invalid,1));
        System.out.println("{\"checks\":"+checks+",\"status\":\"passed\"}");
    }
}
