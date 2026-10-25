import com.etherx.filelab.core.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class EtherCodecTest {
    static int count=0;
    static void ok(boolean cond, String name){ if(!cond)throw new AssertionError(name); System.out.println("OK: "+name); count++; }
    static byte[] pack(byte[] data, EtherCodec.Level level) throws Exception {
        ByteArrayOutputStream o=new ByteArrayOutputStream();
        EtherCodec.Report r=EtherCodec.pack(new ByteArrayInputStream(data),o,"prueba.iso",data.length,level,null);
        ok(r.originalBytes==data.length,"pack length "+data.length);
        return o.toByteArray();
    }
    static byte[] unpack(byte[] archive) throws Exception {
        Path path=Files.createTempFile("etherx_test_",".bin");
        try(RandomAccessFile raf=new RandomAccessFile(path.toFile(),"rw")){
            EtherCodec.Report r=EtherCodec.unpack(new ByteArrayInputStream(archive),raf,null);
            ok(r.originalBytes==raf.length(),"unpack length "+r.originalBytes);
            return Files.readAllBytes(path);
        }finally{Files.deleteIfExists(path);}
    }
    static void roundtrip(byte[] data, EtherCodec.Level level) throws Exception {
        byte[] exf=pack(data,level);ok(Arrays.equals(data,unpack(exf)),"roundtrip "+level+" / "+data.length);
    }
    public static void main(String[] args)throws Exception {
        roundtrip(new byte[0],EtherCodec.Level.RAPIDO);
        roundtrip(new byte[100_000],EtherCodec.Level.EQUILIBRADO);
        byte[] many=new byte[2*1024*1024];Arrays.fill(many,(byte)255);roundtrip(many,EtherCodec.Level.MAXIMO);
        byte[] sparse=new byte[1024*1024];Arrays.fill(sparse,(byte)0x7f);sparse[3]=14;sparse[66_000]=11;
        byte[] encoded=pack(sparse,EtherCodec.Level.EQUILIBRADO);
        ok((encoded[encoded.length-1]&255)>=0,"fixture sparse");
        ok(Arrays.equals(sparse,unpack(encoded)),"sparse exact");
        byte[] rng=new byte[1024*1024+171];new Random(123).nextBytes(rng);
        roundtrip(rng,EtherCodec.Level.MAXIMO);
        byte[] repeated=new byte[4*1024*1024];byte[] one=new byte[1024*1024];new Random(56).nextBytes(one);
        for(int i=0;i<4;i++)System.arraycopy(one,0,repeated,i*one.length,one.length);
        byte[] rep=pack(repeated,EtherCodec.Level.RAPIDO);
        ok(Arrays.equals(repeated,unpack(rep)),"reference exact");
        byte[] corrupted=rep.clone();corrupted[corrupted.length-1]^=1;
        try {unpack(corrupted);throw new AssertionError("corruption undetected");}
        catch(IOException expected){ok(true,"corruption detected");}
        try {unpack(Arrays.copyOf(rep,rep.length-3));throw new AssertionError("truncation undetected");}
        catch(IOException expected){ok(true,"truncation detected");}
        byte[] iso=Files.exists(Paths.get("tests/demo.iso"))?Files.readAllBytes(Paths.get("tests/demo.iso")):new byte[64*1024];
        FileInspector.Result inspected=FileInspector.analyze(new ByteArrayInputStream(iso),iso.length);
        System.out.println("Inspector: "+inspected.format);
        var q=QuantumToyLab.train();ok(q.samples==200&&q.epochs==90,"QLab reproducible");
        System.out.println(q.report());
        if(args.length>0) {
            byte[] src=Files.readAllBytes(Paths.get(args[0]));
            roundtrip(src,EtherCodec.Level.EQUILIBRADO);
            if(args.length>1){
                byte[] pythonArchive=Files.readAllBytes(Paths.get(args[1]));
                ok(Arrays.equals(src,unpack(pythonArchive)),"Python EXF to native Java");
                Files.write(Paths.get("tests/native_interop.exf"),pack(src,EtherCodec.Level.EQUILIBRADO));
            }
        }
        System.out.println("Total tests: "+count);
    }
}
