package com.etherx.filelab.core;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Bounded stream-oriented format inspector. No code execution or image mounting. */
public final class FileInspector {
    private FileInspector() {}
    private static final int SECTOR=2048, LIMIT=512*1024;
    public static final class Result {
        public final String format, explanation;
        public final double entropy, zeros, ff;
        public final long size;
        public final List<String> entries;
        Result(String f,String explain,double e,double z,double ff,long size,List<String> entries){
            this.format=f;explanation=explain;entropy=e;zeros=z;this.ff=ff;this.size=size;
            this.entries=Collections.unmodifiableList(entries);
        }
        public String report() {
            StringBuilder b=new StringBuilder();
            b.append("FORMATO: ").append(format).append("\n");
            b.append("TAMAÑO: ").append(size>=0?size+" bytes":"desconocido").append("\n");
            b.append("ENTROPÍA DE MUESTRA: ").append(String.format(Locale.ROOT,"%.3f/8 bits por byte",entropy)).append("\n");
            b.append("CEROS: ").append(String.format(Locale.ROOT,"%.1f%%",zeros*100)).append("\n");
            b.append("VALOR FF: ").append(String.format(Locale.ROOT,"%.1f%%",ff*100)).append("\n\n");
            b.append(explanation).append("\n\n");
            if(!entries.isEmpty()) {
                b.append("ENTRADAS ISO EN MUESTRA\n");for(String v:entries)b.append("• ").append(v).append('\n');
                b.append("Solo se enumeran los registros de directorio accesibles en los primeros 512 KiB.\n");
            }
            b.append("La estimación de entropía utiliza únicamente el inicio del archivo, no todos sus sectores.");
            return b.toString();
        }
    }
    public static Result analyze(InputStream source,long size) throws IOException {
        ByteArrayOutputStream sample=new ByteArrayOutputStream(LIMIT);
        byte[] buf=new byte[64*1024];int n;
        while(sample.size()<LIMIT && (n=source.read(buf,0,Math.min(buf.length,LIMIT-sample.size())))!=-1) {
            if(n>0)sample.write(buf,0,n);
        }
        byte[] a=sample.toByteArray();int[] counts=new int[256];
        for(byte v:a)counts[v&255]++;
        double entropy=0;
        for(int c:counts)if(c>0){double p=(double)c/a.length;entropy-=p*(Math.log(p)/Math.log(2));}
        double zero=a.length==0?0:(double)counts[0]/a.length;
        double ff=a.length==0?0:(double)counts[255]/a.length;
        String f="Binario / sin firma conocida";String explain="Inspección no destructiva de cabeceras y patrones. No se ejecutan ni modifican sus datos.";
        List<String> entries=new ArrayList<>();
        boolean iso=false,joliet=false,udf=false;
        int pvd=-1,jvd=-1;
        for(int i=16;i<256;i++) {
            int off=i*SECTOR;
            if(off+SECTOR>a.length)break;
            if(at(a,off+1,"CD001") && (a[off+6]&255)==1) {
                if((a[off]&255)==1)pvd=off;
                if((a[off]&255)==2 && (at(a,off+88,"%/@")||at(a,off+88,"%/C")||at(a,off+88,"%/E")))jvd=off;
                iso=true;
            }
            if(at(a,off+1,"NSR02")||at(a,off+1,"NSR03"))udf=true;
        }
        if(iso||udf) {
            joliet=jvd>=0;f=udf&&!iso?"UDF":joliet?"ISO9660 + Joliet":udf?"ISO9660 / UDF":"ISO9660";
            int base=joliet?jvd:pvd;
            explain="Disco óptico virtual: sectores de 2048 bytes. Posición física = LBA × 2048. "+
                   "La compactación preserva todos los bytes; antes de arrancar o montar debes restaurar la ISO.";
            if(base>=0) {
                String volume;
                if(joliet)volume=new String(a,base+40,32,StandardCharsets.UTF_16BE).trim();
                else volume=new String(a,base+40,32,StandardCharsets.US_ASCII).trim();
                explain+="\nEtiqueta de volumen: "+volume;
                int ro=base+156;
                if(ro+34<=a.length) {
                    long lba=u32(a,ro+2),bytes=u32(a,ro+10);
                    explain+="\nDirectorio raíz: LBA "+lba+", "+bytes+" bytes.";
                    if(bytes>0&&bytes<=131072&&lba*SECTOR+bytes<=a.length) {
                        int pos=(int)(lba*SECTOR),end=pos+(int)bytes;
                        while(pos<end&&entries.size()<80) {
                            int rec=a[pos]&255;
                            if(rec==0){pos=((pos/SECTOR)+1)*SECTOR;continue;}
                            if(rec<34 || pos+rec>end)break;
                            int len=a[pos+32]&255;
                            if(len>0 && pos+33+len<=pos+rec && !(len==1&&(a[pos+33]==0||a[pos+33]==1))) {
                                String name;
                                if(joliet)name=new String(a,pos+33,len,StandardCharsets.UTF_16BE);
                                else name=new String(a,pos+33,len,StandardCharsets.US_ASCII);
                                entries.add(name.replaceAll(";[0-9]+$","") + (((a[pos+25]&2)!=0)?"/":"")+
                                            " · "+u32(a,pos+10)+" B");
                            }
                            pos+=rec;
                        }
                    }
                }
            }
            if(udf)explain+="\nFirma UDF detectada; su árbol no se decodifica en esta versión.";
        } else if(at(a,0,"ETHERX01")) {
            f="ETHER X (.exf)";explain="Contenedor reversible ETHER X: cabecera + bloques tipados + CRC32 y SHA-256 global.";
        } else if(at(a,0,"PK\003\004")||at(a,0,"PK\005\006")) {
            f="ZIP / APK";explain="Contenedor de entradas ZIP, habitualmente ya comprimidas. No se enumeran entradas ZIP en este inspector.";
        } else if(at(a,0,"\211PNG\r\n\032\n")){
            f="PNG";explain="Imagen PNG generalmente comprimida con Deflate.";
        } else if(at(a,0,"\377\330\377")) {
            f="JPEG";explain="Imagen JPEG ya comprimida; volver a comprimir suele ahorrar poco.";
        } else if(at(a,0,"%PDF-")) {
            f="PDF";explain="Documento PDF: mezcla de objetos, imágenes, fuentes y streams potencialmente comprimidos.";
        } else if(at(a,0,"MZ")) {
            f="Windows EXE / MZ";explain="Cabecera DOS MZ; puede contener un ejecutable PE.";
        } else if(at(a,0,"MSWIM\000\000\000")) {
            f="Windows WIM";explain="Imagen de archivos de Windows; puede estar comprimida internamente.";
        } else if(at(a,0,"7z\274\257'\034")) {
            f="7z";explain="Archivo 7z; normalmente ya comprimido.";
        } else if(at(a,0,"\037\213")) {
            f="GZIP";explain="Stream GZIP con compresión Deflate.";
        } else if(at(a,0,"\3757zXZ\000")) {
            f="XZ";explain="Stream XZ con compresión LZMA2.";
        } else if(at(a,0,"\050\265\057\375")) {
            f="Zstandard";explain="Stream Zstandard comprimido.";
        } else if(at(a,4,"ftyp")) {
            f="MP4 / ISO Base Media";explain="Contenedor multimedia con flujos que suelen estar comprimidos.";
        }
        if(!iso&&!udf) {
            if(zero>.85 || ff>.85)explain+="\nDominan valores repetidos: buen candidato para regla + excepciones.";
            else if(entropy>7.75)explain+="\nLa muestra tiene alta entropía: la ganancia de compresión podría ser pequeña.";
            else explain+="\nHay patrones candidatos; se compararán codificadores por bloque.";
        }
        return new Result(f,explain,entropy,zero,ff,size,entries);
    }
    private static boolean at(byte[] a,int off,String signature) {
        if(off<0||off+signature.length()>a.length)return false;
        for(int i=0;i<signature.length();i++)if((a[off+i]&255)!=(signature.charAt(i)&255))return false;
        return true;
    }
    private static long u32(byte[] a,int off) {
        return (a[off]&255L)|((a[off+1]&255L)<<8)|((a[off+2]&255L)<<16)|((a[off+3]&255L)<<24);
    }
}
