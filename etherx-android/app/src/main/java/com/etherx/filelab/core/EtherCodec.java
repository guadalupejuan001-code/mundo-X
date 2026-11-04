package com.etherx.filelab.core;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;
import java.util.zip.*;

/**
 * ETHER X archive engine. Native Java, no Python, backend service or third-party JARs.
 * Compatible with the documented ETHERX01 container for RAW/ZLIB/FILL/SPARSE/REFERENCE.
 * Legacy LZMA records (kind 2) are explicitly rejected, never silently corrupted.
 * Rule-first compression is a classical data representation inspired by Mundo X.
 */
public final class EtherCodec {
    private EtherCodec() {}
    public static final byte[] MAGIC = "ETHERX01".getBytes(StandardCharsets.US_ASCII);
    public static final int RAW=0, ZLIB=1, LZMA=2, FILL=3, SPARSE=4, REFERENCE=5, END=255;
    public static final int BLOCK_SIZE=1024*1024;
    public static final int MAX_META=8192;
    public interface Progress { void update(long done, long total); }
    public enum Level { RAPIDO, EQUILIBRADO, MAXIMO }

    public static final class Report {
        public final String action, hash;
        public final long originalBytes, resultBytes, blocks;
        public final Map<Integer,Long> kinds;
        Report(String action, String hash, long o, long r, long b, Map<Integer,Long> k) {
            this.action=action; this.hash=hash; originalBytes=o; resultBytes=r; blocks=b;
            kinds=Collections.unmodifiableMap(new TreeMap<>(k));
        }
        public String explanation() {
            StringBuilder s = new StringBuilder();
            s.append(action).append(" | Origen: ").append(originalBytes)
             .append(" B | Resultado: ").append(resultBytes).append(" B\n");
            for (Map.Entry<Integer,Long> e : kinds.entrySet()) {
                String tag;
                switch(e.getKey()) {
                    case RAW: tag="literal (sin compactar)"; break;
                    case ZLIB: tag="patrones zlib"; break;
                    case FILL: tag="regla uniforme"; break;
                    case SPARSE: tag="regla + excepciones"; break;
                    case REFERENCE: tag="bloque repetido"; break;
                    default: tag="tipo "+e.getKey();
                }
                s.append(tag).append(": ").append(e.getValue()).append(" bloques\n");
            }
            s.append("SHA-256: ").append(hash);
            return s.toString();
        }
    }
    public static final class Header {
        public final String filename;
        public final long size;
        public final int chunkSize;
        Header(String n,long s,int c){filename=n;size=s;chunkSize=c;}
    }
    private static MessageDigest sha() {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException e) { throw new IllegalStateException(e); }
    }
    private static String hex(byte[] b) {
        StringBuilder s = new StringBuilder(b.length*2);
        for(byte v:b)s.append(Character.forDigit((v>>>4)&15,16)).append(Character.forDigit(v&15,16));
        return s.toString();
    }
    private static String cleanName(String input) {
        if(input==null) return "reconstruido.bin";
        String name=input.replace('\\','/');
        name=name.substring(name.lastIndexOf('/')+1).trim();
        StringBuilder b=new StringBuilder();
        for(int i=0;i<name.length();i++) {
            char c=name.charAt(i);
            if(c>=32 && "<>:\"|?*".indexOf(c)<0) b.append(c);
        }
        String s=b.toString();
        return s.isEmpty() || s.equals(".") || s.equals("..")?"reconstruido.bin":s;
    }
    private static String jsonString(String input) {
        StringBuilder b=new StringBuilder("\"");
        for(int i=0;i<input.length();i++) {
            char c=input.charAt(i);
            switch(c) {
                case '"': b.append("\\\""); break;
                case '\\':b.append("\\\\");break;
                case '\n':b.append("\\n");break;
                case '\r':b.append("\\r");break;
                case '\t':b.append("\\t");break;
                default:
                    if(c<32)b.append(String.format(Locale.ROOT,"\\u%04x",(int)c));
                    else b.append(c);
            }
        }
        return b.append('"').toString();
    }
    public static Header readHeader(InputStream in) throws IOException {
        if(!Arrays.equals(readExactly(in,8),MAGIC)) throw new IOException("No es un contenedor ETHER X");
        int metaLen=readIntLE(in);
        if(metaLen<10 || metaLen>MAX_META)throw new IOException("Cabecera fuera de límite");
        String m=new String(readExactly(in,metaLen),StandardCharsets.UTF_8);
        JsonParser parser=new JsonParser(m);
        Map<String,Object> data=parser.parseObject();
        if(!Long.valueOf(1).equals(data.get("version")))throw new IOException("Versión ETHER X no soportada");
        Object sz=data.get("size"),chunk=data.get("chunk_size");
        if(!(sz instanceof Long) || !(chunk instanceof Long))throw new IOException("Metadatos incompletos");
        long size=(Long)sz, ck=(Long)chunk;
        if(size<0 || ck<4096 || ck>8L*1024*1024)throw new IOException("Dimensiones de contenedor inválidas");
        Object nm=data.get("name");
        return new Header(cleanName(nm instanceof String?(String)nm:"reconstruido.bin"),size,(int)ck);
    }
    public static Report pack(InputStream input, OutputStream output, String sourceName,
                              long originalLength, Level level, Progress progress) throws IOException {
        if(originalLength<0)throw new IllegalArgumentException("Se requiere tamaño conocido del archivo");
        String name=cleanName(sourceName);
        String mode=level==Level.RAPIDO?"rapido":level==Level.EQUILIBRADO?"equilibrado":"maximo";
        // JSON keys and layout deliberately match the original Python codec.
        String metadata="{\"version\":1,\"name\":"+jsonString(name)+",\"size\":"+originalLength+
                ",\"chunk_size\":"+BLOCK_SIZE+",\"mode\":\""+mode+
                "\",\"strategy\":\"inverse-ether-rule-first\",\"hash\":\"sha256\"}";
        byte[] mb=metadata.getBytes(StandardCharsets.UTF_8);
        if(mb.length>MAX_META)throw new IOException("Nombre de origen excesivo");
        CountingOutput out=new CountingOutput(new BufferedOutputStream(output,64*1024));
        out.write(MAGIC);putIntLE(out,mb.length);out.write(mb);
        MessageDigest digest=sha();
        LinkedHashMap<String,Long> references=new LinkedHashMap<>(128,0.75f,true);
        Map<Integer,Long> counts=new TreeMap<>();
        long processed=0, index=0;
        while(processed<originalLength) {
            int n=(int)Math.min(BLOCK_SIZE,originalLength-processed);
            byte[] raw=readExactly(input,n);
            digest.update(raw);
            byte[] hash=sha().digest(raw);
            String key=hex(hash)+":"+n;
            Integer kind=null;
            byte[] payload=null;
            Long prev=references.get(key);
            if(prev!=null) {
                kind=REFERENCE;
                ByteArrayOutputStream b=new ByteArrayOutputStream(8);
                putLongLE(b,prev);
                payload=b.toByteArray();
            } else if(isUniform(raw)) {
                kind=FILL;payload=new byte[]{raw[0]};
            } else {
                kind=RAW;payload=raw;
                byte[] sparse=encodeSparse(raw);
                if(sparse!=null && sparse.length<payload.length) {kind=SPARSE;payload=sparse;}
                byte[] zip=deflate(raw,level);
                if(zip.length<payload.length) {kind=ZLIB;payload=zip;}
            }
            out.write(kind);putIntLE(out,n);putIntLE(out,payload.length);
            CRC32 crc=new CRC32();crc.update(raw);putIntLE(out,(int)crc.getValue());
            out.write(payload);
            counts.put(kind,counts.getOrDefault(kind,0L)+1);
            references.put(key,index);
            while(references.size()>4096)references.remove(references.keySet().iterator().next());
            index++; processed+=n;
            if(progress!=null)progress.update(processed,originalLength);
        }
        // Fail if the provider has grown since determining file size.
        if(input.read()!=-1) throw new IOException("El archivo cambió durante la compactación");
        out.write(END); putIntLE(out,0);putIntLE(out,32);putIntLE(out,0);
        byte[] hash=digest.digest();out.write(hash);out.flush();
        return new Report("Compactación",hex(hash),originalLength,out.count,index,counts);
    }
    /** Destination must be an application-private scratch file; no unverified user document is published. */
    public static Report unpack(InputStream archive, RandomAccessFile scratch, Progress progress) throws IOException {
        CountingInput in=new CountingInput(new BufferedInputStream(archive,64*1024));
        Header header=readHeader(in);
        scratch.setLength(0);scratch.seek(0);
        MessageDigest digest=sha();
        Map<Integer,Long> counts=new TreeMap<>();
        long written=0, index=0;
        while(true) {
            int kind=in.read();if(kind<0)throw new EOFException("Sin marcador de cierre");
            long original=readUnsignedIntLE(in),payloadLen=readUnsignedIntLE(in),crcWanted=readUnsignedIntLE(in);
            if(kind==END) {
                if(original!=0 || payloadLen!=32 || crcWanted!=0) throw new IOException("Cierre EXF inválido");
                byte[] expected=readExactly(in,32);
                if(written!=header.size || !MessageDigest.isEqual(expected,digest.digest()))
                    throw new IOException("SHA-256 incorrecto o tamaño diferente");
                if(in.read()!=-1)throw new IOException("Contenedor con datos adicionales");
                return new Report("Reconstrucción verificada",hex(expected),header.size,in.count,index,counts);
            }
            int rawLen=(int)original;
            if(kind!=RAW && kind!=ZLIB && kind!=LZMA && kind!=FILL && kind!=SPARSE && kind!=REFERENCE)
                throw new IOException("Tipo de bloque desconocido: "+kind);
            if(original==0 || original>header.chunkSize || written>header.size-original ||
                original!=Math.min((long)header.chunkSize,header.size-written))
                throw new IOException("Bloque de longitud inconsistente");
            if(payloadLen>Math.max(header.chunkSize*2L+1024,8192))throw new IOException("Bloque comprimido excesivo");
            byte[] payload=readExactly(in,(int)payloadLen);
            byte[] raw;
            switch(kind) {
                case RAW:
                    if(payloadLen!=original)throw new IOException("Bloque literal malformado");
                    raw=payload;break;
                case ZLIB:raw=inflate(payload,rawLen);break;
                case LZMA:throw new IOException("Este EXF usa LZMA (modo máximo Python). Falta importar el decodificador XZ; no se modificó el origen.");
                case FILL:
                    if(payloadLen!=1)throw new IOException("Regla uniforme malformada");
                    raw=new byte[rawLen];Arrays.fill(raw,payload[0]);break;
                case SPARSE:raw=decodeSparse(payload,rawLen);break;
                case REFERENCE:
                    if(payloadLen!=8)throw new IOException("Referencia malformada");
                    long prev=longLE(payload);
                    if(prev<0 || prev>=index || prev>Long.MAX_VALUE/header.chunkSize)throw new IOException("Referencia inválida");
                    long current=scratch.getFilePointer();
                    scratch.seek(prev*header.chunkSize);
                    raw=new byte[rawLen];scratch.readFully(raw);
                    scratch.seek(current);break;
                default:throw new IOException("Bloque no implementado");
            }
            CRC32 crc=new CRC32();crc.update(raw);
            if(crc.getValue()!=crcWanted)throw new IOException("CRC32 incorrecto en bloque "+index);
            scratch.write(raw);digest.update(raw);written+=rawLen;index++;
            counts.put(kind,counts.getOrDefault(kind,0L)+1);
            if(progress!=null)progress.update(written,header.size);
        }
    }
    public static void transfer(InputStream src,OutputStream dst) throws IOException {
        byte[] buf=new byte[128*1024];int n;
        while((n=src.read(buf))!=-1) {if(n>0)dst.write(buf,0,n);}
        dst.flush();
    }
    private static boolean isUniform(byte[] raw) {
        for(int i=1;i<raw.length;i++) if(raw[i]!=raw[0])return false;
        return true;
    }
    private static byte[] deflate(byte[] raw,Level level) {
        Deflater d=new Deflater(level==Level.RAPIDO?1:level==Level.EQUILIBRADO?6:9);
        try {
            d.setInput(raw);d.finish();
            ByteArrayOutputStream out=new ByteArrayOutputStream(raw.length/2);
            byte[] b=new byte[32*1024];
            while(!d.finished()) {
                int n=d.deflate(b);
                if(n<=0)break;
                out.write(b,0,n);
            }
            return out.toByteArray();
        } finally {d.end();}
    }
    private static byte[] inflate(byte[] payload,int length) throws IOException {
        Inflater d=new Inflater();
        try {
            d.setInput(payload);
            byte[] out=new byte[length];int total=0;
            while(!d.finished() && total<=length) {
                int n;
                try {n=d.inflate(out,total,length-total);}catch(DataFormatException e){throw new IOException("Zlib dañado",e);}
                if(n==0) {
                    if(d.finished())break;
                    if(d.needsInput() || d.needsDictionary() || total==length)break;
                    throw new IOException("Zlib sin progreso");
                }
                total+=n;
            }
            if(total!=length || !d.finished() || d.getRemaining()!=0)throw new IOException("Zlib truncado o excesivo");
            return out;
        } finally {d.end();}
    }
    private static byte[] encodeSparse(byte[] data) {
        if(data.length<128)return null;
        int[] freq=new int[256];for(byte b:data)freq[b&255]++;
        int bg=0;
        for(int choice:new int[]{0,255,data[0]&255})if(freq[choice]>freq[bg])bg=choice;
        if(data.length-freq[bg]>data.length/100)return null;
        ByteArrayOutputStream out=new ByteArrayOutputStream(Math.min(data.length,8192));out.write(bg);
        int last=-1;
        for(int i=0;i<data.length;i++) if((data[i]&255)!=bg) {
            putVarint(out,i-last);out.write(data[i]);last=i;
        }
        return out.toByteArray();
    }
    private static byte[] decodeSparse(byte[] encoded,int length) throws IOException {
        if(encoded.length==0)throw new IOException("Regla dispersa vacía");
        byte[] result=new byte[length];Arrays.fill(result,encoded[0]);
        int last=-1,offset=1;
        while(offset<encoded.length) {
            long delta=0;int shift=0;boolean ok=false;
            for(int i=0;i<10;i++) {
                if(offset>=encoded.length)throw new IOException("Varint truncado");
                int b=encoded[offset++]&255;
                delta|=(long)(b&127)<<shift;
                if((b&128)==0){ok=true;break;}
                shift+=7;
            }
            if(!ok || delta<1 || delta>=length || last+delta>=length || offset>=encoded.length)
                throw new IOException("Excepción dispersa inválida");
            last+=(int)delta;result[last]=encoded[offset++];
        }
        return result;
    }
    private static void putVarint(OutputStream out,int n) {
        while(n>=128){try{out.write((n&127)|128);}catch(IOException e){throw new IllegalStateException(e);}n>>>=7;}
        try {out.write(n);}catch(IOException e){throw new IllegalStateException(e);}
    }
    private static byte[] readExactly(InputStream in,int len) throws IOException {
        byte[] r=new byte[len];int off=0;
        while(off<len) {int n=in.read(r,off,len-off);if(n<0)throw new EOFException("Archivo truncado");if(n==0)continue;off+=n;}
        return r;
    }
    private static long readUnsignedIntLE(InputStream in) throws IOException {return readIntLE(in)&0xffffffffL;}
    private static int readIntLE(InputStream in) throws IOException {
        int a=in.read(),b=in.read(),c=in.read(),d=in.read();
        if((a|b|c|d)<0)throw new EOFException("Cabecera truncada");
        return a|(b<<8)|(c<<16)|(d<<24);
    }
    private static void putIntLE(OutputStream out,int n) throws IOException {
        out.write(n);out.write(n>>>8);out.write(n>>>16);out.write(n>>>24);
    }
    private static void putLongLE(OutputStream out,long v) throws IOException {
        for(int i=0;i<8;i++)out.write((int)(v>>>(i*8)));
    }
    private static long longLE(byte[] b) {
        long v=0;for(int i=0;i<8;i++)v|=(long)(b[i]&255)<<(8*i);return v;
    }
    private static final class CountingInput extends FilterInputStream {
        long count=0;CountingInput(InputStream in){super(in);}
        public int read() throws IOException {int b=in.read();if(b>=0)count++;return b;}
        public int read(byte[] b,int o,int l) throws IOException {int n=in.read(b,o,l);if(n>0)count+=n;return n;}
    }
    private static final class CountingOutput extends FilterOutputStream {
        long count=0;CountingOutput(OutputStream out){super(out);}
        public void write(int b)throws IOException{out.write(b);count++;}
        public void write(byte[] b,int o,int l)throws IOException{out.write(b,o,l);count+=l;}
    }
    /** Small strict flat JSON parser, independent of org.json for host verification. */
    private static final class JsonParser {
        private final String s;private int p=0;
        JsonParser(String s){this.s=s;}
        private void spaces(){while(p<s.length()&&Character.isWhitespace(s.charAt(p)))p++;}
        private boolean next(char c){spaces();if(p<s.length()&&s.charAt(p)==c){p++;return true;}return false;}
        private char take() throws IOException {if(p>=s.length())throw new IOException("JSON truncado");return s.charAt(p++);}
        Map<String,Object> parseObject() throws IOException {
            if(!next('{'))throw new IOException("JSON sin objeto");
            Map<String,Object> m=new HashMap<>();
            if(!next('}')) while(true) {
                spaces();if(p>=s.length()||s.charAt(p)!='"')throw new IOException("Clave JSON inválida");
                String key=string();if(!next(':'))throw new IOException("Falta dos puntos");
                spaces();Object value;
                if(p<s.length()&&s.charAt(p)=='"')value=string();
                else {
                    int st=p;if(p<s.length()&&s.charAt(p)=='-')p++;
                    while(p<s.length()&&s.charAt(p)>='0'&&s.charAt(p)<='9')p++;
                    if(p==st)throw new IOException("Tipo JSON no admitido");
                    try{value=Long.parseLong(s.substring(st,p));}catch(NumberFormatException e){throw new IOException("Entero JSON inválido");}
                }
                if(m.put(key,value)!=null)throw new IOException("Metadato duplicado");
                if(next('}'))break;
                if(!next(','))throw new IOException("Separador JSON inválido");
            }
            spaces();if(p!=s.length())throw new IOException("Caracteres después del JSON");
            return m;
        }
        private String string() throws IOException {
            if(take()!='"')throw new IOException("Cadena JSON inválida");
            StringBuilder b=new StringBuilder();
            while(true) {
                char c=take();if(c=='"')return b.toString();
                if(c=='\\') {
                    char e=take();switch(e) {
                        case '"':case '\\':case '/': b.append(e);break;
                        case 'n':b.append('\n');break;case 't':b.append('\t');break;
                        case 'r':b.append('\r');break;case 'b':b.append('\b');break;
                        case 'f':b.append('\f');break;
                        case 'u':
                            if(p+4>s.length())throw new IOException("Escape unicode truncado");
                            try {b.append((char)Integer.parseInt(s.substring(p,p+4),16));}
                            catch(NumberFormatException ex){throw new IOException("Escape Unicode inválido");}
                            p+=4;break;
                        default: throw new IOException("Escape JSON no reconocido");
                    }
                } else {if(c<32)throw new IOException("Control en JSON");b.append(c);}
            }
        }
    }
}
