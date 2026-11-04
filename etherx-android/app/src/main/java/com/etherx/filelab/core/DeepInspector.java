package com.etherx.filelab.core;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.CRC32;

/**
 * ETHER X Deep Inspector v0.3. Non-executing, bounded, evidence-first binary anatomy.
 * Uses only the Java standard library; host-testable and Android-compatible.
 * Format-specific parsers never claim to decode encrypted/unknown structures.
 */
public final class DeepInspector {
    private DeepInspector() {}
    public static final int SAMPLE_CAP = 3 * 1024 * 1024;
    private static final int MAX_REGIONS=180, MAX_NOTES=45;
    private static final String UNKNOWN="Binario no identificado";

    public static final class Region {
        public final long offset, length;
        public final String name, detail, group;
        public final boolean confirmed;
        public Region(long at,long len,String title,String explanation,String category,boolean certainty){
            offset=at;length=len;name=title;detail=explanation;group=category;confirmed=certainty;
        }
        public String range(){return String.format(Locale.ROOT,"0x%08X · %,d B",offset,length);}
    }
    public static final class Analysis {
        public final String format, explanation;
        public final long totalBytes;
        public final byte[] sample;
        public final double entropy, zeros, repeated;
        public final List<Region> regions;
        public final List<String> observations, strings;
        public final float[] landscape;
        Analysis(String format,String explanation,long totalBytes,byte[] sample,
                 double entropy,double zeros,double repeated,List<Region> regions,
                 List<String> observations,List<String> strings,float[] landscape){
            this.format=format;this.explanation=explanation;this.totalBytes=totalBytes;
            this.sample=sample;this.entropy=entropy;this.zeros=zeros;this.repeated=repeated;
            this.regions=Collections.unmodifiableList(new ArrayList<>(regions));
            this.observations=Collections.unmodifiableList(new ArrayList<>(observations));
            this.strings=Collections.unmodifiableList(new ArrayList<>(strings));
            this.landscape=landscape;
        }
        public String overview(){
            return "FORMATO IDENTIFICADO: "+format+"\n"+explanation+"\n\n"+
                "Tamaño declarado: "+(totalBytes<0?"desconocido":String.format(Locale.ROOT,"%,d bytes",totalBytes))+"\n"+
                "Muestra examinada: "+String.format(Locale.ROOT,"%,d bytes",sample.length)+"\n"+
                "Entropía de muestra: "+String.format(Locale.ROOT,"%.2f / 8 bits",entropy)+"\n"+
                "Bytes cero: "+String.format(Locale.ROOT,"%.1f%%",zeros*100)+"\n"+
                "Regiones interpretadas: "+regions.size()+"\n\n"+
                "Esto describe datos observables; no ejecuta, descifra ni modifica el origen.";
        }
        public String report(){
            StringBuilder s=new StringBuilder(overview()).append("\n\nANATOMÍA POR OFFSETS\n");
            for(Region r:regions)s.append("\n").append(r.range()).append(" | ").append(r.name)
                .append(r.confirmed?" [verificado]":" [hipótesis]").append("\n  ").append(r.detail).append("\n");
            s.append("\nHALLAZGOS Y LÍMITES\n");
            for(String note:observations)s.append("• ").append(note).append('\n');
            s.append("\nCADENAS ASCII OBSERVADAS (muestra)\n");
            for(String v:strings)s.append(v).append('\n');
            s.append("\nMÉTODO\nSolo se lee hasta ").append(SAMPLE_CAP)
                .append(" bytes iniciales. Offsets fuera de esa muestra, directorios lejanos y estructuras cifradas pueden permanecer indeterminados. ")
                .append("La entropía no prueba cifrado ni predice de forma exacta la compresión.");
            return s.toString();
        }
        /** Fixed-width hex/ASCII view with exact offsets. Does not invent unseen bytes. */
        public String hex(long position,int lines){
            if(sample.length==0)return "Archivo vacío";
            if(position<0 || position>=sample.length)return "Offset fuera de los "+sample.length+" bytes disponibles en la muestra.";
            int start=(int)(position/16*16), end=Math.min(sample.length,start+16*Math.min(lines,32));
            StringBuilder out=new StringBuilder();
            for(int i=start;i<end;i+=16){
                out.append(String.format(Locale.ROOT,"%08X  ",i));
                for(int j=0;j<16;j++){
                    if(i+j<sample.length)out.append(String.format(Locale.ROOT,"%02X ",sample[i+j]&255));
                    else out.append("   ");
                    if(j==7)out.append(' ');
                }
                out.append(" │ ");
                for(int j=0;j<16&&i+j<sample.length;j++){
                    int ch=sample[i+j]&255;out.append(ch>=32&&ch<=126?(char)ch:'.');
                }
                out.append('\n');
            }
            return out.toString();
        }
    }
    /** Range-local hex dump when the requested offset is outside the initial sample. */
    public static String hexDump(byte[] chunk,long base,int lines){
        if(chunk==null||chunk.length==0)return "Offset no disponible en el origen";
        StringBuilder out=new StringBuilder();
        int end=Math.min(chunk.length,Math.max(1,lines)*16);
        for(int i=0;i<end;i+=16){
            out.append(String.format(Locale.ROOT,"%08X  ",base+i));
            for(int j=0;j<16;j++){
                if(i+j<chunk.length)out.append(String.format(Locale.ROOT,"%02X ",chunk[i+j]&255));
                else out.append("   ");if(j==7)out.append(' ');
            }
            out.append(" │ ");
            for(int j=0;j<16&&i+j<chunk.length;j++){
                int v=chunk[i+j]&255;out.append(v>=32&&v<=126?(char)v:'.');
            }
            out.append('\n');
        }
        return out.toString();
    }
    public static Analysis withObservation(Analysis a,String note){
        List<String> findings=new ArrayList<>(a.observations);findings.add(note);
        return new Analysis(a.format,a.explanation,a.totalBytes,a.sample,a.entropy,
            a.zeros,a.repeated,a.regions,findings,a.strings,a.landscape);
    }

    private static final class Builder {
        final byte[] a; final long size;
        String format=UNKNOWN, description="No coincide con una firma reconocida. Podemos inspeccionar su organización binaria sin inventar un formato.";
        final List<Region> r=new ArrayList<>();
        final List<String> notes=new ArrayList<>();
        final List<String> strings=new ArrayList<>();
        Builder(byte[] a,long size){this.a=a;this.size=size;}
        boolean has(long at,long len){return at>=0&&len>=0&&at<=a.length&&len<=a.length-at;}
        int u8(long p){return has(p,1)?a[(int)p]&255:0;}
        int le16(long p){return u8(p)|(u8(p+1)<<8);}
        long le32(long p){return (le16(p)&65535L)|((long)le16(p+2)<<16);}
        long be32(long p){return ((long)u8(p)<<24)|((long)u8(p+1)<<16)|((long)u8(p+2)<<8)|u8(p+3);}
        long le64(long p){return le32(p)|(le32(p+4)<<32);}
        long be64(long p){return be32(p)<<32|be32(p+4);}
        long number(long p,int width,boolean little){
            if(!has(p,width))return -1;
            long n=0;if(little)for(int i=width-1;i>=0;i--)n=(n<<8)|u8(p+i);
            else for(int i=0;i<width;i++)n=(n<<8)|u8(p+i);
            return n;
        }
        boolean at(long p,String t){if(!has(p,t.length()))return false;
            for(int i=0;i<t.length();i++)if(u8(p+i)!=(t.charAt(i)&255))return false;return true;}
        String ascii(long p,int n){if(!has(p,n))return "";StringBuilder sb=new StringBuilder();
            for(int i=0;i<n;i++){int v=u8(p+i);if(v==0)break;sb.append(v>=32&&v<=126?(char)v:'·');}return sb.toString().trim();}
        void add(long off,long len,String title,String why,String group){add(off,len,title,why,group,true);}
        void add(long off,long len,String title,String why,String group,boolean sure){
            if(r.size()>=MAX_REGIONS||off<0||len<0)return;
            r.add(new Region(off,len,title,why,group,sure));
        }
        void note(String s){if(notes.size()<MAX_NOTES)notes.add(s);}
        void strings(){
            Set<String> seen=new HashSet<>();
            for(int i=0;i<a.length&&strings.size()<26;){
                if(u8(i)<32||u8(i)>126){i++;continue;}int at=i;
                while(i<a.length&&u8(i)>=32&&u8(i)<=126&&i-at<130)i++;
                if(i-at>=8){String s=ascii(at,Math.min(i-at,76));
                    if(seen.add(s))strings.add(String.format(Locale.ROOT,"0x%08X  %s",at,s));}
            }
        }
    }
    public static Analysis inspect(InputStream src,long totalSize)throws IOException{
        if(src==null)throw new IllegalArgumentException("Origen nulo");
        ByteArrayOutputStream out=new ByteArrayOutputStream(128*1024);
        byte[] buf=new byte[65536];int n;
        while(out.size()<SAMPLE_CAP&&(n=src.read(buf,0,Math.min(buf.length,SAMPLE_CAP-out.size())))!=-1)
            if(n>0)out.write(buf,0,n);
        byte[] sample=out.toByteArray();
        Builder b=new Builder(sample,totalSize);
        identify(b);
        if(b.r.isEmpty()&&sample.length>0)b.add(0,Math.min(64,sample.length),"Cabecera desconocida","Primeros bytes del archivo; revisar el volcado hexadecimal para buscar un patrón.","general",false);
        b.strings();
        statisticsNotes(b);
        int[] histogram=new int[256];for(byte v:sample)histogram[v&255]++;
        double h=0,zero=0,repeated=0;
        if(sample.length>0){
            for(int c:histogram)if(c>0){double p=c/(double)sample.length;h-=p*Math.log(p)/Math.log(2);repeated=Math.max(repeated,p);}
            zero=histogram[0]/(double)sample.length;
        }
        float[] landscape=new float[64];
        for(int i=0;i<64;i++){
            int start=(int)((long)sample.length*i/64),end=(int)((long)sample.length*(i+1)/64);
            int[] sub=new int[256];for(int j=start;j<end;j++)sub[sample[j]&255]++;
            double entropy=0;for(int c:sub)if(c>0){double p=c/(double)(end-start);entropy-=p*Math.log(p)/Math.log(2);}
            landscape[i]=(float)(entropy/8);
        }
        return new Analysis(b.format,b.description,totalSize,sample,h,zero,repeated,b.r,b.notes,b.strings,landscape);
    }
    /** Reads only requested address ranges; no whole-file RAM image is required. */
    public interface RangeInput {byte[] read(long offset,int count) throws IOException;}

    /**
     * Enrich an initial header scan with optical directory and ZIP footer records
     * that may reside far beyond the first sample. Individual reads are bounded.
     */
    public static Analysis enrich(Analysis first, RangeInput reader) throws IOException {
        if(first==null||reader==null)return first;
        if(first.totalBytes<=0)return first;
        Builder b=new Builder(first.sample,first.totalBytes);
        b.format=first.format;b.description=first.explanation;
        b.r.addAll(first.regions);b.notes.addAll(first.observations);b.strings.addAll(first.strings);
        if(first.format.contains("ISO9660"))enrichIso(b,reader);
        if(first.format.startsWith("ZIP"))enrichZip(b,reader);
        if(first.format.startsWith("PDF"))enrichPdf(b,reader);
        return new Analysis(b.format,b.description,first.totalBytes,first.sample,
            first.entropy,first.zeros,first.repeated,b.r,b.notes,b.strings,first.landscape);
    }
    private static long u32(byte[] a,int at) {
        if(at<0||at+4>a.length)return -1;
        return (a[at]&255L)|((a[at+1]&255L)<<8)|((a[at+2]&255L)<<16)|((a[at+3]&255L)<<24);
    }
    private static int u16(byte[] a,int at){
        if(at<0||at+2>a.length)return -1;
        return (a[at]&255)|((a[at+1]&255)<<8);
    }
    private static String safeName(byte[] a,int at,int len,boolean unicode){
        if(at<0||len<0||at+len>a.length)return "?";
        String name=unicode?new String(a,at,len,StandardCharsets.UTF_16BE):new String(a,at,len,StandardCharsets.US_ASCII);
        return name.replaceFirst(";[0-9]+$","").replaceAll("[\\p{Cntrl}]","?");
    }
    private static void enrichIso(Builder b,RangeInput reader) throws IOException {
        long p=-1;boolean unicode=false;
        for(int i=16;i<=64;i++){
            int pos=i*2048;
            if(!b.has(pos,2048))break;
            if(b.at(pos+1,"CD001")&&b.u8(pos)==1&&p<0)p=pos;
            if(b.at(pos+1,"CD001")&&b.u8(pos)==2 &&
                (b.at(pos+88,"%/@")||b.at(pos+88,"%/C")||b.at(pos+88,"%/E"))){
                p=pos;unicode=true;
            }
        }
        if(p<0)return;
        long root=p+156;if(!b.has(root,34))return;
        long lba=b.le32(root+2),len=b.le32(root+10);
        if(lba<=0||len<=0)return;
        Set<Long> visited=new HashSet<>();
        farDirectory(b,reader,lba,len,unicode,"/",visited,0);
        b.note("Directorios ISO consultados mediante offsets físicos, sin materializar toda la imagen de disco.");
    }
    private static void farDirectory(Builder b,RangeInput reader,long lba,long len,boolean unicode,
                                     String prefix,Set<Long> visited,int depth)throws IOException {
        if(depth>4||len<=0||len>512*1024||lba>Long.MAX_VALUE/2048||visited.size()>45)return;
        long start=lba*2048;
        if(start<0||start>=b.size||!visited.add(start))return;
        int count=(int)Math.min(len,256*1024);
        byte[] sector=reader.read(start,count);
        if(sector==null||sector.length<34)return;
        long pos=0;int entries=0;
        while(pos<sector.length&&entries++<160&&b.r.size()<MAX_REGIONS){
            int n=sector[(int)pos]&255;
            if(n==0){pos=((pos/2048)+1)*2048;continue;}
            if(n<34||pos+n>sector.length)break;
            int sl=sector[(int)pos+32]&255;
            if(sl>0&&sl+33<=n){
                int marker=sector[(int)pos+33]&255;
                if(!(sl==1&&(marker==0||marker==1))){
                    long entryLba=u32(sector,(int)pos+2),entryLen=u32(sector,(int)pos+10);
                    boolean folder=((sector[(int)pos+25]&255)&2)!=0;
                    String name=safeName(sector,(int)pos+33,sl,unicode);
                    long recordPos=start+pos;
                    // Entries already discovered in the first sample are not duplicated.
                    boolean duplicated=false;
                    for(Region reg:b.r)if(reg.offset==recordPos && reg.group.equals("archivo")){duplicated=true;break;}
                    if(!duplicated)b.add(recordPos,n,(folder?"Carpeta ":"Archivo ")+prefix+name,
                        "ISO9660: registro real encontrado mediante lectura en LBA "+lba+". Datos en LBA "+entryLba+
                        ", offset 0x"+Long.toHexString(entryLba*2048)+", longitud "+entryLen+" bytes.","archivo");
                    if(folder&&depth<3&&entryLba>0&&entryLen>0)farDirectory(b,reader,entryLba,entryLen,unicode,
                        prefix+name+"/",visited,depth+1);
                }
            }
            pos+=n;
        }
    }
    private static void enrichZip(Builder b,RangeInput reader)throws IOException {
        long start=Math.max(0,b.size-128*1024);
        int want=(int)Math.min(128*1024,b.size-start);
        byte[] tail=reader.read(start,want);
        if(tail==null)return;
        int eocd=-1;
        for(int i=tail.length-22;i>=0;i--){
            if((tail[i]&255)==80&&(tail[i+1]&255)==75&&(tail[i+2]&255)==5&&(tail[i+3]&255)==6){
                int comment=u16(tail,i+20);
                if(comment>=0&&i+22+comment<=tail.length){eocd=i;break;}
            }
        }
        if(eocd<0){b.note("No se localizó el fin de directorio ZIP (puede ser ZIP64, truncado o no convencional).");return;}
        long centralStart=u32(tail,eocd+16),size=u32(tail,eocd+12);
        int count=u16(tail,eocd+10);
        b.add(start+eocd,22,"ZIP · fin de índice central",
            "Total de entradas en este disco: "+count+". Directorio declarado en offset 0x"+
                Long.toHexString(centralStart)+" con "+size+" bytes.","índice");
        if(centralStart<0||centralStart>=b.size||size<=0||size>128L*1024*1024)return;
        int toRead=(int)Math.min(size,256*1024);
        byte[] dir=reader.read(centralStart,toRead);
        if(dir==null)return;
        int pos=0;int n=0;
        while(pos+46<=dir.length&&n++<125&&b.r.size()<MAX_REGIONS){
            if((dir[pos]&255)!=80||(dir[pos+1]&255)!=75||(dir[pos+2]&255)!=1||(dir[pos+3]&255)!=2)break;
            int name=u16(dir,pos+28),extra=u16(dir,pos+30),comment=u16(dir,pos+32);
            long recordSize=46L+name+extra+comment;
            if(recordSize<46||pos+recordSize>dir.length)break;
            String label=safeName(dir,pos+46,Math.min(name,180),false);
            long packed=u32(dir,pos+20),unpacked=u32(dir,pos+24);
            b.add(centralStart+pos,recordSize,"Índice ZIP: "+label,
                "Entrada real del directorio final; algoritmo "+u16(dir,pos+10)+", comprimidos "+packed+
                " bytes, originales "+unpacked+" bytes. Cabecera local declarada en 0x"+
                Long.toHexString(u32(dir,pos+42))+".","archivo");
            pos+=(int)recordSize;
        }
        b.note("Directorio ZIP de cola inspeccionado; no se extraen entradas ni se ejecuta código.");
    }
    private static void enrichPdf(Builder b,RangeInput reader)throws IOException{
        int window=(int)Math.min(96*1024,b.size);long offset=b.size-window;
        if(offset<=b.a.length)return;
        byte[] bytes=reader.read(offset,window);if(bytes==null)return;
        String tail=new String(bytes,StandardCharsets.ISO_8859_1);
        int eof=tail.lastIndexOf("%%EOF"),xref=tail.lastIndexOf("startxref");
        if(eof>=0)b.add(offset+eof,5,"PDF · marca de fin","Fin de documento visible al inspeccionar la cola real del archivo.","índice");
        if(xref>=0)b.add(offset+xref,9,"PDF · puntero a tabla xref","Referencia al índice de objetos PDF. No se ha interpretado su árbol completo.","índice");
        b.note("Cola PDF examinada por dirección para buscar punteros e índice final.");
    }

    private static void identify(Builder b){
        if(b.a.length==0){b.format="Archivo vacío";b.description="El origen contiene cero bytes, por lo que no hay estructuras que interpretar.";return;}
        boolean iso=false,udf=false;
        for(int i=16;i<=64;i++){
            int pos=i*2048;
            if(!b.has(pos,7))break;
            if(b.at(pos+1,"CD001"))iso=true;
            if(b.at(pos+1,"NSR02")||b.at(pos+1,"NSR03"))udf=true;
        }
        if(iso||udf){iso(b,iso,udf);return;}
        if(b.at(0,"ETHERX01")){b.format="ETHER X · contenedor EXF";
            b.description="Archivo compactado por reglas, excepciones y codificadores reversibles. Restaurar y verificar SHA-256 antes de utilizar el original.";
            b.add(0,8,"Firma EXF","Identificador de contenedor ETHERX01, no es el contenido original.","cabecera");
            b.note("Para ver el contenido interno, reconstruye una copia del archivo original.");return;}
        if(b.at(0,"\211PNG\r\n\032\n")){png(b);return;}
        if(b.at(0,"PK\003\004")||b.at(0,"PK\005\006")||b.at(0,"PK\007\010")){zip(b);return;}
        if(b.at(0,"\177ELF")){elf(b);return;}
        if(b.at(0,"MZ")){pe(b);return;}
        if(b.at(0,"SQLite format 3\000")){sqlite(b);return;}
        if(b.at(0,"%PDF-")){pdf(b);return;}
        if(b.at(0,"\377\330\377")){jpeg(b);return;}
        if(b.at(0,"RIFF")&&b.has(8,4)){riff(b);return;}
        if(b.at(4,"ftyp")){mp4(b);return;}
        if(b.at(0,"MSWIM\000\000\000")){b.format="WIM · imagen de Windows";
            b.description="Contenedor de imágenes de Windows. La cabecera permite identificar metadatos; la extracción integral y los recursos internos requieren un decodificador WIM.";
            b.add(0,Math.min(208,b.a.length),"Cabecera WIM","Firma MSWIM, versión y parámetros del contenedor Windows Imaging Format.","cabecera");
            if(b.has(8,16))b.note("Longitud declarada de cabecera: "+b.le32(8)+" bytes; versión: 0x"+Long.toHexString(b.le32(12)));
            return;}
        if(b.at(0,"\037\213")){b.format="GZIP";b.description="Flujo Deflate con cabecera GZIP. Los bytes siguientes pueden estar comprimidos.";
            b.add(0,Math.min(10,b.a.length),"Cabecera GZIP","Método de compresión y banderas de metadatos; el contenido no se descomprime automáticamente.","cabecera");return;}
        if(b.at(0,"7z\274\257'\034")){b.format="7-Zip";b.description="Archivo 7z con cabeceras y bloques potencialmente comprimidos.";
            b.add(0,Math.min(32,b.a.length),"Firma 7z","Versión y encabezado inicial de archivo 7z.","cabecera");return;}
        if(b.at(0,"\3757zXZ\000")){b.format="XZ / LZMA2";b.description="Flujo XZ con compresión LZMA2; no se desmonta sin decodificar.";
            b.add(0,Math.min(12,b.a.length),"Cabecera XZ","Identificador de flujo y opciones.","cabecera");return;}
        if(b.at(0,"\050\265\057\375")){b.format="Zstandard";b.description="Flujo Zstandard comprimido; no confundir bytes codificados con la estructura del contenido.";
            b.add(0,Math.min(12,b.a.length),"Cabecera Zstd","Firma de flujo comprimido.","cabecera");return;}
        b.add(0,Math.min(b.a.length,64),"Cabecera sin firma","Datos crudos, sin interpretación verificada de formato.","general",false);
        b.note("No se reconoció un formato. El escaneo de firmas internas y cadenas es exploratorio, no una prueba de semántica.");
    }
    private static void iso(Builder b,boolean iso,boolean udf){
        b.format=iso?(udf?"ISO9660 + UDF":"ISO9660"):("UDF");
        b.description="Imagen de disco óptico; bloques lógicos típicos de 2048 bytes. Una dirección LBA corresponde al desplazamiento LBA × 2048. Esto no arranca ni ejecuta la imagen.";
        if(b.a.length>=32768)b.add(0,32768,"Área de sistema","Los primeros 16 sectores están reservados para usos del sistema/arranque. No son archivos del directorio.","sistema");
        long primary=-1,joliet=-1;
        for(int i=16;i<=64;i++){
            long p=i*2048;if(!b.has(p,7))break;
            if(b.at(p+1,"CD001")){
                int type=b.u8(p);
                b.add(p,2048,"Descriptor de volumen tipo "+type,"Sector "+i+": firma CD001, versión "+b.u8(p+6)+". Los tipos 1, 2 y 255 describen volumen primario, suplementario y terminación.","descriptor");
                if(type==1)primary=p;
                if(type==2&&(b.at(p+88,"%/@")||b.at(p+88,"%/C")||b.at(p+88,"%/E")))joliet=p;
                if(type==255)break;
            }else if(b.at(p+1,"NSR02")||b.at(p+1,"NSR03")){
                b.add(p,2048,"Ancla de reconocimiento UDF","Identificador NSR encontrado: evidencia de estructura UDF.","descriptor");
            }
        }
        long base=joliet>=0?joliet:primary;
        if(base>=0){
            boolean uni=joliet>=0;
            String volume=uni?new String(b.a,(int)base+40,32,StandardCharsets.UTF_16BE).trim():b.ascii(base+40,32);
            long sectors=b.le32(base+80);
            b.note("Volumen: "+volume+". Sectores declarados: "+sectors+" ("+(sectors*2048L)+" bytes lógicos).");
            long root=base+156;
            if(b.has(root,34)){
                long lba=b.le32(root+2),len=b.le32(root+10);
                b.add(root,34,"Registro de directorio raíz","Apunta a LBA "+lba+" (offset "+(lba*2048)+"), "+len+" bytes.","directorio");
                Set<Long> visited=new HashSet<>();
                isoDirectory(b,lba,len,uni,"/",visited,0);
            }
        }
        if(udf)b.note("UDF reconocido por firma; NO se interpreta el árbol completo de archivos UDF.");
        b.note("Si el directorio está fuera de la muestra, solo se informa su dirección; no se inventa su contenido.");
    }
    private static void isoDirectory(Builder b,long lba,long len,boolean unicode,String prefix,Set<Long> visited,int depth){
        if(depth>2||len<=0||len>256*1024||lba>Long.MAX_VALUE/2048)return;
        long start=lba*2048;
        if(!b.has(start,len)){b.note("Directorio "+prefix+" ubicado en 0x"+Long.toHexString(start)+" fuera de la muestra.");return;}
        if(!visited.add(start))return;
        long p=start,end=start+len;
        while(p<end&&b.r.size()<MAX_REGIONS){
            int rl=b.u8(p);
            if(rl==0){p=((p/2048)+1)*2048;continue;}
            if(rl<34||!b.has(p,rl)||p+rl>end)break;
            int nameLen=b.u8(p+32);
            if(nameLen>0&&33L+nameLen<=rl){
                int raw=b.u8(p+33);
                if(!(nameLen==1&&(raw==0||raw==1))){
                    String name=unicode?new String(b.a,(int)p+33,nameLen,StandardCharsets.UTF_16BE):b.ascii(p+33,nameLen);
                    name=name.replaceFirst(";[0-9]+$","");
                    boolean folder=(b.u8(p+25)&2)!=0;
                    long subLba=b.le32(p+2),subLen=b.le32(p+10);
                    b.add(p,rl,(folder?"Directorio ":"Archivo ")+prefix+name,
                        "Registro de "+rl+" bytes; contenido en LBA "+subLba+" (offset "+(subLba*2048)+"), longitud "+subLen+" bytes.","archivo");
                    if(folder&&depth<1)isoDirectory(b,subLba,subLen,unicode,prefix+name+"/",visited,depth+1);
                }
            }
            p+=rl;
        }
    }
    private static void png(Builder b){
        b.format="PNG · imagen";b.description="Secuencia de chunks PNG; cada bloque tiene longitud, tipo, contenido y CRC32. Se revisa la integridad de los bloques completamente presentes en la muestra.";
        b.add(0,8,"Firma PNG","Ocho bytes de identificación PNG.","cabecera");
        long pos=8;int count=0;
        while(b.has(pos,12)&&count++<120){
            long len=b.be32(pos);String typ=b.ascii(pos+4,4);
            if(len>512L*1024*1024){b.note("Chunk "+typ+" declara longitud inusualmente grande; análisis suspendido.");break;}
            long total=12+len;
            String description="Chunk "+typ+": carga de "+len+" bytes.";
            if("IHDR".equals(typ)&&b.has(pos+8,13))
                description="Cabecera: "+b.be32(pos+8)+" × "+b.be32(pos+12)+" píxeles, profundidad "+b.u8(pos+16)+" bits, color tipo "+b.u8(pos+17)+".";
            if("IDAT".equals(typ))description="Datos de imagen comprimidos mediante zlib/Deflate; no son píxeles directos.";
            if("IEND".equals(typ))description="Marca de fin PNG; no contiene imagen.";
            if(b.has(pos,total)){
                CRC32 crc=new CRC32();crc.update(b.a,(int)pos+4,(int)len+4);
                long expected=b.be32(pos+8+len);
                if(crc.getValue()!=expected){description+=" CRC NO COINCIDE (esperado "+Long.toHexString(expected)+").";b.note("Integridad CRC32 no coincide en chunk "+typ+" en "+pos+".");}
                else description+=" CRC32 válido.";
            }else description+=" El chunk excede la muestra; CRC no comprobado.";
            b.add(pos,total,typ,description,"chunk");
            if(!b.has(pos,total)||"IEND".equals(typ))break;
            pos+=total;
        }
    }
    private static void zip(Builder b){
        b.format="ZIP / APK / contenedor PK";
        b.description="Archivo ZIP de cabeceras locales, datos comprimidos y directorio central. Un APK Android también es ZIP; no ejecutamos su contenido.";
        long p=0;int count=0;
        while(b.at(p,"PK\003\004")&&b.has(p,30)&&count++<110){
            int flags=b.le16(p+6),method=b.le16(p+8),nameLen=b.le16(p+26),extra=b.le16(p+28);
            long packed=b.le32(p+18),original=b.le32(p+22);
            if(!b.has(p+30,(long)nameLen+extra))break;
            String name=b.ascii(p+30,Math.min(nameLen,120));
            String detail="Método "+(method==0?"almacenado":method==8?"Deflate":"código "+method)+
                "; "+original+" bytes originales, "+packed+" bytes guardados. Banderas 0x"+Integer.toHexString(flags)+".";
            if((flags&8)!=0)detail+=" Tamaños pueden estar en descriptor posterior.";
            if((flags&1)!=0)detail+=" Entrada cifrada; no se decodifica.";
            b.add(p,30L+nameLen+extra,"Entrada ZIP: "+name,detail,"archivo");
            if((flags&8)!=0||!b.has(p+30L+nameLen+extra,packed))break;
            p+=30L+nameLen+extra+packed;
        }
        int central=0;
        for(int i=0;i+46<=b.a.length&&central<90;i++){
            if(!b.at(i,"PK\001\002"))continue;
            int nameLen=b.le16(i+28),extra=b.le16(i+30),comment=b.le16(i+32);
            long total=46L+nameLen+extra+comment;
            if(!b.has(i,total))continue;
            String name=b.ascii(i+46,Math.min(nameLen,120));
            b.add(i,total,"Índice central: "+name,"Entrada de catálogo; offset local relativo: "+b.le32(i+42)+", método de compresión "+b.le16(i+10)+".","índice");
            central++;i+=(int)total-1;
        }
        if(central==0)b.note("El directorio central suele estar al FINAL del ZIP; podría quedar fuera de los primeros 3 MiB.");
        b.note("Detección de entradas no equivale a descompresión, validación de firmas APK ni análisis de código Dalvik.");
    }
    private static void elf(Builder b){
        b.format="ELF · ejecutable/objeto Unix";
        boolean wide=b.u8(4)==2,little=b.u8(5)==1;
        b.description="Formato ELF "+(wide?"64":"32")+" bits, orden "+(little?"little-endian":"big-endian")+". Muestra cabeceras y segmentos; no desensambla instrucciones.";
        int eh=wide?64:52;
        b.add(0,Math.min(eh,b.a.length),"Cabecera ELF","Tipo "+b.number(16,2,little)+", arquitectura e_machine="+b.number(18,2,little)+".","cabecera");
        long entry=b.number(wide?24:24,wide?8:4,little);
        long phoff=b.number(wide?32:28,wide?8:4,little);
        int phents=(int)b.number(wide?54:42,2,little),phnum=(int)b.number(wide?56:44,2,little);
        b.note("Punto de entrada virtual: 0x"+Long.toHexString(entry)+"; tabla de segmentos: offset "+phoff+"; registros: "+phnum+".");
        if(phents>0&&phents<256)for(int i=0;i<Math.min(phnum,64);i++){
            long off=phoff+(long)i*phents;if(!b.has(off,phents))break;
            long type=b.number(off,4,little), file= b.number(off+(wide?8:4),wide?8:4,little);
            long length=b.number(off+(wide?32:16),wide?8:4,little);
            b.add(off,phents,"Program header "+i+" · tipo "+type,
                "Apunta al offset de archivo 0x"+Long.toHexString(file)+" y describe "+length+" bytes. Es un segmento, no necesariamente una sección.","segmento");
        }
    }
    private static void pe(Builder b){
        b.format="MZ / Windows ejecutable";b.description="Cabecera DOS MZ y, si existe, cabecera PE/COFF y tabla de secciones. No se ejecuta ni desensambla el programa.";
        b.add(0,Math.min(64,b.a.length),"Cabecera DOS MZ","Compatibilidad DOS; e_lfanew en offset 0x3C indica dónde empieza la cabecera PE.","cabecera");
        if(!b.has(0x3c,4))return;
        long pe=b.le32(0x3c);
        if(!b.at(pe,"PE\000\000")||!b.has(pe+4,20)){b.note("No se verificó una cabecera PE/COFF dentro de la muestra.");return;}
        int arch=b.le16(pe+4),sections=b.le16(pe+6),optional=b.le16(pe+20);
        b.format="PE / ejecutable Windows";
        b.add(pe,24,"Firma PE + COFF","Máquina 0x"+Integer.toHexString(arch)+", "+sections+" secciones; COFF gestiona la tabla y sus banderas.","cabecera");
        long opt=pe+24;
        if(b.has(opt,Math.min(32,optional))){
            int magic=b.le16(opt);
            long entry=b.le32(opt+16);
            b.add(opt,optional,"Cabecera opcional PE","Formato "+(magic==0x20b?"PE32+ (64-bit)":magic==0x10b?"PE32":"otro")+"; RVA de entrada 0x"+Long.toHexString(entry)+".","cabecera");
        }
        long table=opt+optional;
        for(int i=0;i<Math.min(sections,65);i++){
            long off=table+i*40L;if(!b.has(off,40))break;
            String name=b.ascii(off,8);long virt=b.le32(off+12),size=b.le32(off+16),file=b.le32(off+20);
            b.add(off,40,"Sección "+name,"RVA 0x"+Long.toHexString(virt)+", contenido en archivo 0x"+Long.toHexString(file)+", "+size+" bytes físicos.","sección");
        }
    }
    private static void sqlite(Builder b){
        b.format="SQLite · base de datos";
        b.description="Cabecera SQLite con organización por páginas. No interpreta tablas ni extrae registros sin analizar las páginas B-tree.";
        int page=b.le16(16);if(page==1)page=65536;
        long declared=b.le32(28);
        b.add(0,Math.min(100,b.a.length),"Cabecera SQLite","Tamaño de página "+page+" bytes, páginas declaradas "+declared+", versión de formato "+b.u8(18)+"/"+b.u8(19)+".","cabecera");
        b.note("Codificación de texto código "+b.le32(56)+"; la cabecera no contiene filas de usuario legibles directamente.");
        if(page>=512&&page<=65536&&b.has(100,8)){
            int typ=b.u8(100);b.add(100,8,"Primera página B-tree","Tipo "+typ+", celdas declaradas "+b.le16(103)+". Estructura por páginas y offsets.","índice");
        }
    }
    private static void pdf(Builder b){
        b.format="PDF · documento";b.description="Archivo PDF basado en objetos numerados, streams y referencias. Se localizan encabezados de objetos visibles sin interpretar contenido comprimido ni cifrado.";
        b.add(0,Math.min(16,b.a.length),"Versión PDF",b.ascii(0,Math.min(12,b.a.length))+"; la firma solo indica formato, no valida el documento entero.","cabecera");
        int objects=0, streams=0;
        for(int i=4;i<b.a.length-8&&objects<95;i++){
            if(b.at(i,"stream"))streams++;
            if(!b.at(i," obj")||i==0)continue;
            int p=i-1;while(p>=0&&b.u8(p)==' ')p--;
            int e=p+1;while(p>=0&&b.u8(p)>='0'&&b.u8(p)<='9')p--;
            if(e-p<2||e-p>8)continue;
            int bEnd=p;while(p>=0&&b.u8(p)==' ')p--;
            int e2=p+1;while(p>=0&&b.u8(p)>='0'&&b.u8(p)<='9')p--;
            if(e2-p<2||e2-p>10)continue;
            String id=b.ascii(p+1,e2-p-1)+" "+b.ascii(bEnd+1,e-bEnd-1);
            b.add(p+1,i+4-(p+1),"Objeto PDF "+id,"Marcador de objeto visible en la muestra. Puede contener diccionarios, referencias y streams.","objeto");objects++;
        }
        b.note(objects+" marcadores de objeto y "+streams+" cadenas 'stream' observadas en la muestra; los conteos NO son totales del PDF.");
    }
    private static void jpeg(Builder b){
        b.format="JPEG · imagen";b.description="Segmentos JPEG con tablas, metadatos y flujo de coeficientes comprimidos; no son píxeles RGB almacenados directamente.";
        b.add(0,2,"SOI","Inicio de imagen JPEG.","cabecera");
        int p=2,count=0;
        while(p+4<=b.a.length&&count++<90){
            if(b.u8(p)!=255){p++;continue;}int m=b.u8(p+1);while(m==255&&p+2<b.a.length){p++;m=b.u8(p+1);}
            if(m==0xD9){b.add(p,2,"EOI","Fin de JPEG.","marcador");break;}
            if(m==0xD8||m==1||(m>=0xD0&&m<=0xD7)){p+=2;continue;}
            int len=(b.u8(p+2)<<8)|b.u8(p+3);if(len<2||!b.has(p+2,len))break;
            String name=(m>=0xE0&&m<=0xEF?"APP"+(m-0xE0):m==0xDB?"DQT":m==0xC4?"DHT":m==0xDA?"SOS":"Marker FF"+Integer.toHexString(m));
            String info="Segmento JPEG de "+len+" bytes incluyendo longitud.";
            if((m==0xC0||m==0xC2)&&len>=7)info="Resolución "+((b.u8(p+7)<<8)|b.u8(p+8))+" × "+((b.u8(p+5)<<8)|b.u8(p+6))+"; componentes "+b.u8(p+9)+".";
            b.add(p,2L+len,name,info,"segmento");p+=2+len;
            if(m==0xDA){b.note("Desde SOS comienza el flujo de imagen codificado; las marcas internas requieren interpretar el framing JPEG.");break;}
        }
    }
    private static void riff(Builder b){
        String kind=b.ascii(8,4);b.format="RIFF / "+kind;
        b.description="Contenedor RIFF little-endian organizado en subchunks. Muchos formatos (WAVE, WEBP, AVI) comparten RIFF.";
        b.add(0,12,"Encabezado RIFF","Tamaño declarado "+b.le32(4)+" bytes más firma; tipo "+kind+".","cabecera");
        long p=12;int n=0;
        while(b.has(p,8)&&n++<90){
            String tag=b.ascii(p,4);long len=b.le32(p+4);String why="Subchunk "+tag+" de "+len+" bytes.";
            if(kind.equals("WAVE")&&tag.equals("fmt")&&b.has(p+8,16))
                why="Audio: formato "+b.le16(p+8)+", canales "+b.le16(p+10)+", frecuencia "+b.le32(p+12)+" Hz, bits "+b.le16(p+22)+".";
            b.add(p,8+len,tag,why,"chunk");
            if(!b.has(p+8,len))break;
            p+=8+len+(len&1);
        }
    }
    private static void mp4(Builder b){
        b.format="MP4 / ISO Base Media";b.description="Cajas anidadas ISO BMFF. ftyp declara compatibilidad; moov aporta estructura, mdat contiene medios codificados que no se decodifican aquí.";
        mp4Boxes(b,0,b.a.length,0);
    }
    private static String indent(int n){StringBuilder s=new StringBuilder();for(int i=0;i<n;i++)s.append("  ");return s.toString();}
    private static void mp4Boxes(Builder b,long start,long end,int depth){
        if(depth>3)return;long p=start;int count=0;
        Set<String> parents=new HashSet<>(Arrays.asList("moov","trak","mdia","minf","stbl","edts","dinf","udta","meta","moof","traf","mvex"));
        while(p+8<=end&&count++<65&&b.r.size()<MAX_REGIONS){
            long size=b.be32(p);String kind=b.ascii(p+4,4);int head=8;
            if(size==1){if(!b.has(p,16))break;size=b.be64(p+8);head=16;}
            else if(size==0)size=end-p;
            if(size<head||size>Long.MAX_VALUE-p)break;
            String info="Caja "+kind+", "+size+" bytes.";
            if(kind.equals("ftyp")&&b.has(p+head,8))info+=" Marca principal: "+b.ascii(p+head,4)+".";
            if(kind.equals("mdat"))info+=" Carga audiovisual codificada; no es una secuencia de píxeles crudos.";
            b.add(p,size,indent(depth)+kind,info,"caja");
            if(p+size>end){b.note("Caja "+kind+" continúa fuera de la muestra.");break;}
            if(parents.contains(kind)&&!kind.equals("meta"))mp4Boxes(b,p+head,p+size,depth+1);
            p+=size;
        }
    }
    private static void statisticsNotes(Builder b){
        int[] counts=new int[256];for(byte v:b.a)counts[v&255]++;
        if(b.a.length==0)return;
        double zeros=counts[0]/(double)b.a.length;
        int max=0;for(int c:counts)max=Math.max(max,c);
        if(zeros>=0.65)b.note(String.format(Locale.ROOT,"%.1f%% de los bytes examinados son cero: candidato a representación por regla de fondo + excepciones.",zeros*100));
        if(max/(double)b.a.length>=0.8)b.note("Un valor de byte domina al menos el 80% de la muestra: probable redundancia local.");
        int hits=0;String[] signatures={"PK\003\004","\211PNG\r\n\032\n","%PDF-","\177ELF","SQLite format 3\000","MZ"};
        String[] names={"ZIP","PNG","PDF","ELF","SQLite","MZ"};
        for(int i=1;i<b.a.length-16&&hits<12;i++){
            if(i%4!=0)continue;
            for(int j=0;j<signatures.length;j++)if(b.at(i,signatures[j])){
                b.add(i,signatures[j].length(),"Firma interna candidata: "+names[j],"Patrón de identificación hallado dentro de otros bytes. Podría ser un archivo incrustado o coincidencia accidental; no se asume validez.","indicio",false);
                hits++;break;
            }
        }
        b.note("Análisis no ejecutable y de solo lectura: sin montar ISO, sin interpretar instrucciones de CPU y sin descifrar contenido.");
    }
}