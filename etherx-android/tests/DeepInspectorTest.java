import com.etherx.filelab.core.DeepInspector;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.CRC32;

public final class DeepInspectorTest {
    static int checks=0;
    static void ok(boolean yes,String info){checks++;if(!yes)throw new AssertionError("FAIL: "+info);}
    static void le16(byte[] a,int off,int n){a[off]=(byte)n;a[off+1]=(byte)(n>>8);}
    static void le32(byte[] a,int off,long n){for(int i=0;i<4;i++)a[off+i]=(byte)(n>>(8*i));}
    static void be32(byte[] a,int off,long n){for(int i=0;i<4;i++)a[off+3-i]=(byte)(n>>(8*i));}
    static void str(byte[] a,int off,String t){byte[] b=t.getBytes(StandardCharsets.ISO_8859_1);System.arraycopy(b,0,a,off,b.length);}
    static DeepInspector.Analysis run(byte[] a){try{return DeepInspector.inspect(new ByteArrayInputStream(a),a.length);}catch(IOException e){throw new RuntimeException(e);}}
    static boolean region(DeepInspector.Analysis a,String fragment){for(DeepInspector.Region r:a.regions)if(r.name.contains(fragment))return true;return false;}
    static void testEmpty(){DeepInspector.Analysis a=run(new byte[0]);ok(a.format.contains("vacío"),"empty");ok(a.hex(0,5).contains("vacío"),"empty hex");}
    static void testUnknown(){byte[] b=new byte[997];new Random(9).nextBytes(b);DeepInspector.Analysis a=run(b);
      ok(a.format.contains("Binario"),"unknown");ok(a.entropy>6,"entropy");ok(a.hex(0,1).startsWith("00000000"),"offset in hex");ok(a.hex(9999,1).contains("fuera"),"hex out of bounds");}
    static void testPE(){byte[] b=new byte[1300];str(b,0,"MZ");le32(b,0x3c,128);str(b,128,"PE\000\000");
      le16(b,132,0x8664);le16(b,134,2);le16(b,148,240);le16(b,152,0x20b);le32(b,168,0x1000);
      str(b,392,".text");le32(b,392+12,0x1000);le32(b,392+16,0x200);le32(b,392+20,0x400);
      DeepInspector.Analysis a=run(b);ok(a.format.startsWith("PE"),"pe detected");ok(region(a,"Sección .text"),"pe section");ok(a.report().contains("0x00000188"),"section offset");}
    static void testElf(){byte[] b=new byte[256];str(b,0,"\177ELF");b[4]=2;b[5]=1;
      le16(b,16,2);le16(b,18,62);le32(b,32,64);le16(b,54,56);le16(b,56,1);le32(b,64,1);
      DeepInspector.Analysis a=run(b);ok(a.format.contains("ELF"),"elf detected");ok(region(a,"Program header 0"),"elf segment");}
    static void testPng(){byte[] b=new byte[8+25+12];byte[] sig={(byte)137,80,78,71,13,10,26,10};System.arraycopy(sig,0,b,0,8);
      be32(b,8,13);str(b,12,"IHDR");be32(b,16,123);be32(b,20,456);b[24]=8;b[25]=2;
      CRC32 crc=new CRC32();crc.update(b,12,17);be32(b,29,crc.getValue());str(b,37,"IEND");
      DeepInspector.Analysis a=run(b);ok(a.format.contains("PNG"),"png");ok(region(a,"IHDR"),"png ihdr");ok(a.report().contains("123 × 456"),"png size");ok(a.report().contains("CRC32 válido"),"png crc");
      b[30]=(byte)0xff;ok(run(b).report().contains("CRC NO COINCIDE"),"png crc corruption");}
    static void testZip(){byte[] b=new byte[120];str(b,0,"PK\003\004");le16(b,8,8);le32(b,18,4);le32(b,22,8);le16(b,26,8);str(b,30,"data.txt");
      b[38]=1;b[39]=2;b[40]=3;b[41]=4;str(b,42,"PK\001\002");le16(b,42+28,8);str(b,88,"data.txt");
      DeepInspector.Analysis a=run(b);ok(a.format.contains("ZIP"),"zip");ok(region(a,"Entrada ZIP"),"local header");ok(region(a,"Índice central"),"central header");}
    static void testIso(){byte[] b=new byte[81920];int p=16*2048;b[p]=1;str(b,p+1,"CD001");b[p+6]=1;str(b,p+40,"ETHER_DEMO");le32(b,p+80,40);le32(b,p+156+2,20);le32(b,p+156+10,2048);
      int d=20*2048;b[d]=40;le32(b,d+2,21);le32(b,d+10,32);b[d+32]=7;str(b,d+33,"A.TXT;1");
      DeepInspector.Analysis a=run(b);ok(a.format.contains("ISO"),"iso");ok(region(a,"Descriptor"),"iso descriptor");ok(region(a,"Archivo /A.TXT"),"iso entry");}
    static void testPdf(){byte[] b="%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n%%EOF".getBytes(StandardCharsets.ISO_8859_1);
      DeepInspector.Analysis a=run(b);ok(a.format.contains("PDF"),"pdf");ok(region(a,"Objeto PDF"),"pdf object");}
    static void testSqlite(){byte[] b=new byte[1024];str(b,0,"SQLite format 3\000");le16(b,16,512);le32(b,28,2);b[100]=13;le16(b,103,1);
      DeepInspector.Analysis a=run(b);ok(a.format.contains("SQLite"),"sqlite");ok(region(a,"B-tree"),"sqlite B-tree");}
    static void testMp4(){byte[] b=new byte[48];be32(b,0,24);str(b,4,"ftyp");str(b,8,"isom");be32(b,24,24);str(b,28,"moov");be32(b,32,16);str(b,36,"mvhd");
      DeepInspector.Analysis a=run(b);ok(a.format.contains("MP4"),"mp4");ok(region(a,"ftyp"),"ftyp");ok(region(a,"moov"),"moov");ok(region(a,"mvhd"),"nested mvhd");}
    static void testRiff(){byte[] b=new byte[56];str(b,0,"RIFF");le32(b,4,48);str(b,8,"WAVE");str(b,12,"fmt ");le32(b,16,16);
      le16(b,20,1);le16(b,22,2);le32(b,24,44100);le16(b,34,16);
      DeepInspector.Analysis a=run(b);ok(a.format.contains("WAVE"),"riff");ok(region(a,"fmt"),"fmt");ok(a.report().contains("44100"),"audio rate");}
    static void testByteCaps()throws Exception {byte[] b=new byte[DeepInspector.SAMPLE_CAP+7777];Arrays.fill(b,(byte)0);
      DeepInspector.Analysis a=run(b);ok(a.sample.length==DeepInspector.SAMPLE_CAP,"memory cap");ok(a.landscape.length==64,"landscape");ok(a.zeros==1,"zeros");ok(a.hex(0,2).contains("00000010"),"hex offset");}
    static void testDeepIsoRange()throws Exception{
      byte[] b=new byte[9*1024*1024];int p=16*2048;b[p]=1;str(b,p+1,"CD001");b[p+6]=1;
      le32(b,p+156+2,4096);le32(b,p+156+10,2048);
      int directory=4096*2048;b[directory]=44;le32(b,directory+2,4097);le32(b,directory+10,19);
      b[directory+32]=11;str(b,directory+33,"WINDOWS.BIN");
      DeepInspector.Analysis a=run(b);
      ok(!region(a,"WINDOWS.BIN"),"ISO directory outside initial sample");
      DeepInspector.Analysis deep=DeepInspector.enrich(a,(off,len)->Arrays.copyOfRange(b,(int)off,(int)Math.min((long)b.length,off+len)));
      ok(region(deep,"WINDOWS.BIN"),"distant ISO record inspected without allocating original");
      ok(deep.report().contains("LBA 4097"),"ISO content physical address");
    }
    static void testZipEndRange()throws Exception{
      byte[] b=new byte[6*1024*1024];str(b,0,"PK\003\004");
      int central=b.length-1024;str(b,central,"PK\001\002");le16(b,central+28,6);str(b,central+46,"OS.BIN");
      int eocd=b.length-100;str(b,eocd,"PK\005\006");le16(b,eocd+10,1);le32(b,eocd+12,52);le32(b,eocd+16,central);
      DeepInspector.Analysis scan=run(b);
      ok(!region(scan,"OS.BIN"),"ZIP index outside initial sample");
      DeepInspector.Analysis deep=DeepInspector.enrich(scan,(off,len)->Arrays.copyOfRange(b,(int)off,(int)Math.min((long)b.length,off+len)));
      ok(region(deep,"OS.BIN"),"ZIP tail index inspected");
      ok(region(deep,"fin de índice central"),"ZIP EOCD physical address");
    }
    static void testPdfEndRange()throws Exception{
      byte[] b=new byte[4*1024*1024];str(b,0,"%PDF-1.7");str(b,b.length-37,"startxref\n123\n%%EOF");
      DeepInspector.Analysis a=DeepInspector.enrich(run(b),(off,len)->Arrays.copyOfRange(b,(int)off,(int)Math.min((long)b.length,off+len)));
      ok(region(a,"marca de fin"),"PDF EOF from tail");
    }
    static void fuzz(){Random r=new Random(42);for(int i=0;i<700;i++){
      byte[] a=new byte[r.nextInt(3000)];r.nextBytes(a);
      if(a.length>5&&(i%5)==0)str(a,0,"MZ");
      if(a.length>5&&(i%6)==0)str(a,0,"%PDF-");
      DeepInspector.Analysis z=run(a);
      ok(z.regions.size()<=180,"bounded regions");ok(z.observations.size()<=45,"bounded notes");
    }}
    public static void main(String[] argv)throws Exception{
      testEmpty();testUnknown();testPE();testElf();testPng();testZip();testIso();testPdf();testSqlite();testMp4();testRiff();testByteCaps();testDeepIsoRange();testZipEndRange();testPdfEndRange();fuzz();
      System.out.println("DEEP INSPECTOR: "+checks+" checks PASS; randomized cases 700; no file execution");
    }
}