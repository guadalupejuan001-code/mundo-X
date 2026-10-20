package com.mundox.graphics;

import java.util.HashSet;

/** Portable syntax only. Native restore validates the complete quantum state. */
public final class InfiniteProgram {
    public static final int MAX_ADDRESS_DIGITS=4096,MAX_QUERY_TERMS=4096;
    public static final String FAR_ADDRESS="1"+zeros(100);
    private InfiniteProgram(){}

    public static String address(String input){
        if(input==null||input.length()==0||input.length()>MAX_ADDRESS_DIGITS)
            throw new IllegalArgumentException("La dirección requiere de 1 a 4096 dígitos decimales");
        for(int i=0;i<input.length();i++)if(input.charAt(i)<'0'||input.charAt(i)>'9')
            throw new IllegalArgumentException("La dirección sólo admite dígitos 0–9, sin signos ni espacios");
        int start=0;while(start+1<input.length()&&input.charAt(start)=='0')start++;
        return input.substring(start);
    }
    public static String partner(String input){
        char[] digits=address(input).toCharArray();int last=digits.length-1;
        digits[last]=(char)(digits[last]+((digits[last]-'0')%2==0?1:-1));
        return new String(digits);
    }
    public static String shortAddress(String input){
        String value=address(input);
        return value.length()<=14?value:value.substring(0,6)+"…"+value.substring(value.length()-5);
    }
    /** A fixed program; gates must never replace it with the new stabilizers. */
    public static String[] pair(String first,String second){
        String a=address(first),b=address(second);
        if(a.equals(b))throw new IllegalArgumentException("Selecciona dos direcciones distintas");
        return new String[]{"X:"+a+" X:"+b,"Z:"+a+" Z:"+b,"Y:"+a+" Y:"+b,"X:"+a,"Z:"+a};
    }
    public static void validate(String observable){
        if(observable==null||observable.length()>16*1024*1024)
            throw new IllegalArgumentException("Observable disperso inválido");
        for(int i=0;i<observable.length();i++){
            char c=observable.charAt(i);
            if(c>126||(c<32&&c!='\t'&&c!='\n'&&c!='\r'&&c!='\f'&&c!=11))
                throw new IllegalArgumentException("El observable requiere texto ASCII");
        }
        String text=observable.trim();if(text.length()==0)return;
        String[] words=text.split("[ \\t\\n\\r\\f\\x0B]+");
        int start=words[0].equals("+")||words[0].equals("-")?1:0;
        if(words.length-start>MAX_QUERY_TERMS)throw new IllegalArgumentException("Demasiados términos Pauli");
        HashSet<String> used=new HashSet<>();
        for(int i=start;i<words.length;i++){
            String term=words[i];
            if(term.length()<3||term.charAt(1)!=':'||"IXYZ".indexOf(term.charAt(0))<0)
                throw new IllegalArgumentException("Usa términos como X:0 Z:1, con signo + o − opcional separado");
            if(!used.add(address(term.substring(2))))throw new IllegalArgumentException("Dirección Pauli duplicada");
        }
    }
    /** Locate a useful UI focus without interpreting any address as an integer. */
    public static String[] focus(String[] program){
        String a=null,b=null;
        if(program!=null)for(String observable:program){
            if(observable==null)continue;
            for(String term:observable.trim().split("[ \\t\\n\\r\\f\\x0B]+")){
                if(term.length()<3||term.charAt(1)!=':')continue;
                String q=address(term.substring(2));
                if(a==null)a=q;else if(!q.equals(a)){b=q;break;}
            }
            if(b!=null)break;
        }
        if(a==null)a="0";
        return new String[]{a,b==null?partner(a):b};
    }
    static void snapshot(String text){
        if(text==null||!(text.startsWith("MXI1:")||text.startsWith("MXI2:"))||text.length()<=5||text.length()>16*1024*1024
                ||(text.length()-5)%2!=0)throw new IllegalArgumentException("Snapshot Ether MXI1/MXI2 inválido");
        for(int i=5;i<text.length();i++){
            char c=text.charAt(i);
            if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))
                throw new IllegalArgumentException("Snapshot infinito: texto hexadecimal requerido");
        }
    }
    private static String zeros(int count){char[] out=new char[count];java.util.Arrays.fill(out,'0');return new String(out);}
}
