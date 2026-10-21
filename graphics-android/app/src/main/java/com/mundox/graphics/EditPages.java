package com.mundox.graphics;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Map;
import java.util.SortedMap;

/** Derived GPU index; the sparse edits remain the authoritative project state. */
final class EditPages {
    final int width,height;
    final ByteBuffer bytes;

    private EditPages(int width,int height){
        this.width=width;this.height=height;
        bytes=ByteBuffer.allocateDirect(width*height*4).order(ByteOrder.nativeOrder());
    }

    static EditPages encode(int count,SortedMap<Integer,Integer> edits,int maxTexture){
        if(count<1||count>1_048_576||maxTexture<1)
            throw new IllegalArgumentException("Invalid edit index extent");
        int directory=edits.isEmpty()?1:(count+255)/256;
        int pages=0,previous=-1;
        for(Map.Entry<Integer,Integer> entry:edits.entrySet()){
            int id=entry.getKey(),state=entry.getValue();
            if(id<0||id>=count||state<0||state>7)
                throw new IllegalArgumentException("Invalid scene edits");
            if(state!=0&&id/256!=previous){previous=id/256;pages++;}
        }
        // Fixed rows agree with the shader's shifts and avoid per-query division.
        int words=directory+pages*32,width=edits.isEmpty()?1:256;
        int height=(words+width-1)/width;
        if(width>maxTexture||height>maxTexture)throw new IllegalArgumentException("Edit texture exceeds device capability");
        EditPages result=new EditPages(width,height);
        int cursor=directory;
        for(Map.Entry<Integer,Integer> entry:edits.entrySet()){
            int id=entry.getKey(),state=entry.getValue();
            if(state==0)continue;
            int pageByte=(id>>>8)*4,offset=result.bytes.getInt(pageByte);
            if(offset==0){offset=cursor;cursor+=32;result.bytes.putInt(pageByte,offset);}
            int wordByte=(offset+((id&255)>>>3))*4;
            result.bytes.putInt(wordByte,result.bytes.getInt(wordByte)|(state<<((id&7)*4)));
        }
        return result;
    }
}
