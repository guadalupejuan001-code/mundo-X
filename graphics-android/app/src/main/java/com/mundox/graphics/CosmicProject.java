package com.mundox.graphics;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Portable .cosmic v1 codec. Validation never creates a native core or GL state.
 * JSON is read by the strict reader below: Android's lenient JSONTokener is not
 * used for imported files. No project code or external reference is executed.
 */
public final class CosmicProject {
    public static final String FORMAT="cosmic-x-engine-project";
    public static final int MAX_FILE_BYTES=16*1024*1024;
    private static final int MAX_QUBITS=256,MAX_GATES=100_000,MAX_OBSERVABLES=4096,MAX_EDITS=100_000;
    private static final int MAX_OBJECTS=1_048_576;
    private static final long MAX_REVISION=9_007_199_254_740_991L;
    private static final String[] GATE_NAMES={"H","S","X","Z","CX","CZ","Y"};

    /** A validated description plus detached snapshots for platform consumers.
     * The encoded description is private and immutable to callers: importing and
     * re-exporting it preserves JSON numeric precision. To export a changed live
     * session, call fromSnapshots with new snapshots instead of editing arrays.
     */
    public static final class Document {
        public final String name;
        public final NativeCore.Snapshot core;
        public final QuantumRenderer.Snapshot graphics;
        private final JSONObject description;
        private Document(String name,NativeCore.Snapshot core,QuantumRenderer.Snapshot graphics,JSONObject description){
            this.name=name;this.core=core;this.graphics=graphics;this.description=description;
        }
    }

    private CosmicProject(){}

    public static Document decode(byte[] data) throws Exception {
        if(data==null||data.length>MAX_FILE_BYTES)throw invalid("El proyecto supera el límite de 16 MiB");
        final String text;
        try {
            text=StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(data)).toString();
        } catch(CharacterCodingException error){
            throw new IllegalArgumentException("El proyecto no contiene UTF-8 válido",error);
        }
        return validate(object(new StrictJson(text).read(),"project"));
    }

    /** Read at most the documented limit; the caller owns and closes the stream. */
    public static Document read(InputStream stream) throws Exception {
        if(stream==null)throw invalid("Falta el archivo del proyecto");
        ByteArrayOutputStream out=new ByteArrayOutputStream();
        byte[] chunk=new byte[8192];
        int count;
        while((count=stream.read(chunk))!=-1){
            if(count==0){
                int one=stream.read();
                if(one<0)break;
                if(out.size()==MAX_FILE_BYTES)throw invalid("El proyecto supera el límite de 16 MiB");
                out.write(one);
                continue;
            }
            if(count>MAX_FILE_BYTES-out.size())throw invalid("El proyecto supera el límite de 16 MiB");
            out.write(chunk,0,count);
        }
        return decode(out.toByteArray());
    }

    public static byte[] encode(Document document) throws Exception {
        if(document==null)throw invalid("Falta el proyecto");
        byte[] result=(document.description.toString(2)+"\n").getBytes(StandardCharsets.UTF_8);
        if(result.length>MAX_FILE_BYTES)throw invalid("El proyecto supera el límite de 16 MiB");
        return result;
    }

    public static Document fromSnapshots(String name,NativeCore.Snapshot core,QuantumRenderer.Snapshot graphics) throws Exception {
        if(core==null||graphics==null)throw invalid("Falta el estado del núcleo o del observador");
        if(core.gateHistory==null||core.gateHistory.length%3!=0||core.gateHistory.length>MAX_GATES*3)
            throw invalid("core.gates: historial inválido");
        if(graphics.projectVersion!=0&&graphics.projectVersion!=1)throw invalid("Versión visual no admitida");
        if(graphics.coupling!=(graphics.quantumStrength!=0f))throw invalid("Acoplamiento visual inconsistente");
        if(graphics.observables==null||graphics.observables.length<1||graphics.observables.length>MAX_OBSERVABLES
            ||graphics.editIds==null||graphics.editValues==null||graphics.editIds.length!=graphics.editValues.length
            ||graphics.editIds.length>MAX_EDITS)throw invalid("Observables o ediciones inválidos");
        // Hidden orbit parameters must still be safe for a later touch gesture.
        number(Float.valueOf(graphics.yaw),-1_000_000,1_000_000,"camera.yaw");
        number(Float.valueOf(graphics.pitch),.04,1.52,"camera.pitch");
        number(Float.valueOf(graphics.zoom),.12,6,"camera.zoom");

        JSONArray gates=new JSONArray();
        for(int i=0;i<core.gateHistory.length;i+=3){
            long operation=core.gateHistory[i];
            if(operation<0||operation>=GATE_NAMES.length)throw invalid("core.gates: operación no admitida");
            JSONArray gate=new JSONArray();gate.put(GATE_NAMES[(int)operation]);gate.put(core.gateHistory[i+1]);
            if(operation==NativeCore.CX||operation==NativeCore.CZ)gate.put(core.gateHistory[i+2]);
            gates.put(gate);
        }
        JSONObject coreJson=new JSONObject();
        coreJson.put("backend","compact-clifford");coreJson.put("initialState","zero");
        coreJson.put("qubits",core.qubits);coreJson.put("revision",core.version);coreJson.put("gates",gates);

        JSONObject camera=new JSONObject();
        final boolean legacy=graphics.projectVersion==0;
        if(graphics.overrideEye==null&&graphics.overrideTarget==null){
            camera.put("mode","orbit");camera.put("overview",graphics.overview);
            camera.put("yaw",Float.valueOf(graphics.yaw));camera.put("pitch",Float.valueOf(graphics.pitch));
            camera.put("zoom",Float.valueOf(graphics.zoom));
        }else{
            camera.put("mode","lookAt");camera.put("eye",floatVector(graphics.overrideEye));
            camera.put("target",floatVector(graphics.overrideTarget));
            camera.put("near",Float.valueOf(legacy?.1f:graphics.cameraNear));
            camera.put("far",Float.valueOf(legacy?10000f:graphics.cameraFar));
            camera.put("fov",Float.valueOf(legacy?48f:graphics.cameraFov));
        }
        JSONArray observables=new JSONArray(),edits=new JSONArray();
        for(String pauli:graphics.observables){
            if(pauli==null)throw invalid("Observable nulo");
            observables.put(pauli);
        }
        for(int i=0;i<graphics.editIds.length;i++){
            JSONArray edit=new JSONArray();edit.put(graphics.editIds[i]);edit.put(graphics.editValues[i]);edits.put(edit);
        }
        JSONObject graphicsJson=new JSONObject();
        graphicsJson.put("program","opaque-grid-v1");graphicsJson.put("objects",graphics.objectCount);
        graphicsJson.put("seed",legacy?2026L:graphics.seed);
        graphicsJson.put("coupling",Float.valueOf(graphics.quantumStrength));graphicsJson.put("observables",observables);
        graphicsJson.put("edits",edits);graphicsJson.put("camera",camera);
        graphicsJson.put("time",Float.valueOf(graphics.time));graphicsJson.put("selectedId",graphics.selectedId);
        graphicsJson.put("resolutionLimit",graphics.resolutionLimit);
        JSONObject description=new JSONObject();
        description.put("format",FORMAT);description.put("version",1);description.put("name",name==null?JSONObject.NULL:name);
        description.put("core",coreJson);description.put("graphics",graphicsJson);
        return validate(description);
    }

    private static Document validate(JSONObject document) throws JSONException {
        keys(document,"project","format","version","name","core","graphics");
        if(!FORMAT.equals(document.get("format"))||integer(document.get("version"),1,1,"version")!=1)
            throw invalid("Formato o versión .cosmic no admitido");
        String name=string(document.get("name"),"project.name");
        unicode(name);
        int length=name.codePointCount(0,name.length());
        if(length<1||length>128)throw invalid("project.name: se requieren de 1 a 128 caracteres");
        for(int i=0;i<name.length();i++)if(name.charAt(i)<32)throw invalid("project.name: carácter de control");

        JSONObject core=object(document.get("core"),"core");
        keys(core,"core","backend","initialState","qubits","revision","gates");
        if(!"compact-clifford".equals(core.get("backend"))||!"zero".equals(core.get("initialState")))
            throw invalid("Esta versión admite compact-clifford desde el estado cero");
        int qubits=(int)integer(core.get("qubits"),1,MAX_QUBITS,"core.qubits");
        long revision=integer(core.get("revision"),0,MAX_REVISION,"core.revision");
        JSONArray gates=array(core.get("gates"),"core.gates");
        if(gates.length()>MAX_GATES)throw invalid("core.gates: máximo 100.000 operaciones");
        long[] history=new long[gates.length()*3];
        for(int i=0;i<gates.length();i++){
            JSONArray gate=array(gates.get(i),"core.gates");
            if(gate.length()<1)throw invalid("core.gates: operación vacía");
            String gateName=string(gate.get(0),"core.gates");
            int operation=-1;
            for(int j=0;j<GATE_NAMES.length;j++)if(GATE_NAMES[j].equals(gateName)){operation=j;break;}
            if(operation<0)throw invalid("core.gates: se requiere H/S/X/Y/Z/CX/CZ");
            boolean pair=operation==NativeCore.CX||operation==NativeCore.CZ;
            if(gate.length()!=(pair?3:2))throw invalid("core.gates: cantidad de operandos inválida");
            long first=integer(gate.get(1),0,qubits-1,"core.gates operand");
            long second=pair?integer(gate.get(2),0,qubits-1,"core.gates operand"):0;
            if(pair&&first==second)throw invalid("core.gates: se requieren dos qubits distintos");
            history[3*i]=operation;history[3*i+1]=first;history[3*i+2]=second;
        }

        JSONObject graphics=object(document.get("graphics"),"graphics");
        keys(graphics,"graphics","program","objects","seed","coupling","observables","edits","camera","time","selectedId","resolutionLimit");
        if(!"opaque-grid-v1".equals(graphics.get("program")))throw invalid("Programa gráfico no admitido");
        int count=(int)integer(graphics.get("objects"),1,MAX_OBJECTS,"graphics.objects");
        long seed=integer(graphics.get("seed"),0,0xffffffffL,"graphics.seed");
        float strength=(float)number(graphics.get("coupling"),-.25,.25,"graphics.coupling");
        JSONArray observables=array(graphics.get("observables"),"graphics.observables");
        if(observables.length()<1||observables.length()>MAX_OBSERVABLES)throw invalid("graphics.observables: se requieren de 1 a 4096 observables");
        String[] program=new String[observables.length()];
        for(int i=0;i<observables.length();i++){
            String pauli=string(observables.get(i),"graphics.observables");
            int start=pauli.startsWith("+")||pauli.startsWith("-")?1:0;
            if(pauli.length()-start!=qubits)throw invalid("graphics.observables: longitud Pauli inválida");
            for(int q=start;q<pauli.length();q++)if("IXYZ".indexOf(pauli.charAt(q))<0)throw invalid("graphics.observables: se requiere IXYZ");
            program[i]=pauli;
        }
        JSONArray edits=array(graphics.get("edits"),"graphics.edits");
        if(edits.length()>MAX_EDITS)throw invalid("graphics.edits: máximo 100.000 ediciones");
        int[] ids=new int[edits.length()],values=new int[edits.length()];
        int previous=-1;
        for(int i=0;i<edits.length();i++){
            JSONArray edit=array(edits.get(i),"graphics.edits");
            if(edit.length()!=2)throw invalid("graphics.edits: se requieren pares [id, estado]");
            ids[i]=(int)integer(edit.get(0),0,MAX_OBJECTS-1,"graphics.edits id");
            values[i]=(int)integer(edit.get(1),1,7,"graphics.edits state");
            if(ids[i]<=previous)throw invalid("graphics.edits: IDs únicos y ordenados requeridos");
            previous=ids[i];
        }
        JSONObject camera=object(graphics.get("camera"),"graphics.camera");
        String mode=string(camera.get("mode"),"graphics.camera.mode");
        boolean overview=true;
        float yaw=.67f,pitch=.65f,zoom=1f,near=.1f,far=10000f,fov=48f;
        float[] eye=null,target=null;
        if(mode.equals("orbit")){
            keys(camera,"graphics.camera","mode","overview","yaw","pitch","zoom");
            Object view=camera.get("overview");
            if(!(view instanceof Boolean))throw invalid("graphics.camera.overview: booleano requerido");
            overview=(Boolean)view;
            yaw=(float)number(camera.get("yaw"),-1_000_000,1_000_000,"graphics.camera.yaw");
            pitch=(float)number(camera.get("pitch"),.04,1.52,"graphics.camera.pitch");
            zoom=(float)number(camera.get("zoom"),.12,6,"graphics.camera.zoom");
        }else if(mode.equals("lookAt")){
            keys(camera,"graphics.camera","mode","eye","target","near","far","fov");
            double[] preciseEye=vector(camera.get("eye"),"graphics.camera.eye");
            double[] preciseTarget=vector(camera.get("target"),"graphics.camera.target");
            if(distanceSquared(preciseEye,preciseTarget)<1e-8)throw invalid("La cámara y su objetivo deben ser distintos");
            double preciseNear=number(camera.get("near"),.001,1_000_000,"graphics.camera.near");
            double preciseFar=number(camera.get("far"),.001,1_000_000,"graphics.camera.far");
            if(preciseNear>=preciseFar)throw invalid("camera.near debe ser menor que camera.far");
            near=(float)preciseNear;far=(float)preciseFar;
            fov=(float)number(camera.get("fov"),1,170,"graphics.camera.fov");
            eye=new float[]{(float)preciseEye[0],(float)preciseEye[1],(float)preciseEye[2]};
            target=new float[]{(float)preciseTarget[0],(float)preciseTarget[1],(float)preciseTarget[2]};
            double dx=(double)eye[0]-target[0],dy=(double)eye[1]-target[1],dz=(double)eye[2]-target[2];
            if(near>=far||dx*dx+dy*dy+dz*dz<1e-8)
                throw invalid("La cámara pierde separación con la precisión gráfica compartida");
        }else throw invalid("graphics.camera.mode: orbit o lookAt requerido");
        float time=(float)number(graphics.get("time"),-1_000_000,1_000_000,"graphics.time");
        int selected=(int)integer(graphics.get("selectedId"),-1,count-1,"graphics.selectedId");
        int resolution=(int)integer(graphics.get("resolutionLimit"),64,4096,"graphics.resolutionLimit");
        // These are detached value objects; no native library or renderer is initialized.
        NativeCore.Snapshot coreState=NativeCore.Snapshot.of(qubits,revision,history);
        QuantumRenderer.Snapshot graphicState=new QuantumRenderer.Snapshot(count,selected,resolution,seed,overview,
            yaw,pitch,zoom,time,strength,eye,target,near,far,fov,program,ids,values);
        return new Document(name,coreState,graphicState,document);
    }

    private static IllegalArgumentException invalid(String message){return new IllegalArgumentException(message);}
    private static JSONObject object(Object value,String where){
        if(!(value instanceof JSONObject))throw invalid(where+": objeto requerido");
        return (JSONObject)value;
    }
    private static JSONArray array(Object value,String where){
        if(!(value instanceof JSONArray))throw invalid(where+": lista requerida");
        return (JSONArray)value;
    }
    private static String string(Object value,String where){
        if(!(value instanceof String))throw invalid(where+": texto requerido");
        return (String)value;
    }
    private static void keys(JSONObject value,String where,String... expected){
        Set<String> allowed=new HashSet<>(Arrays.asList(expected));
        if(value.length()!=allowed.size())throw invalid(where+": campos ausentes o adicionales");
        Iterator<String> iterator=value.keys();
        while(iterator.hasNext())if(!allowed.contains(iterator.next()))throw invalid(where+": campo no admitido");
    }
    private static long integer(Object value,long low,long high,String where){
        if(!(value instanceof Long)&&!(value instanceof Integer))throw invalid(where+": entero requerido");
        long result=((Number)value).longValue();
        if(result<low||result>high)throw invalid(where+": entero fuera de rango");
        return result;
    }
    private static double number(Object value,double low,double high,String where){
        if(!(value instanceof Number))throw invalid(where+": número finito requerido");
        // A Float's decimal JSON spelling is the portable value, rather than
        // the longer double spelling created by implicit float promotion.
        double result=value instanceof Float?Double.parseDouble(value.toString()):((Number)value).doubleValue();
        if(!Double.isFinite(result)||result<low||result>high)throw invalid(where+": número fuera de rango");
        return result;
    }
    private static double[] vector(Object value,String where) throws JSONException {
        JSONArray coordinates=array(value,where);
        if(coordinates.length()!=3)throw invalid(where+": tres coordenadas requeridas");
        double[] result=new double[3];
        for(int i=0;i<3;i++)result[i]=number(coordinates.get(i),-1_000_000,1_000_000,where);
        return result;
    }
    private static JSONArray floatVector(float[] value){
        if(value==null||value.length!=3)throw invalid("La cámara necesita tres coordenadas");
        JSONArray out=new JSONArray();
        for(float coordinate:value)out.put(Float.valueOf(coordinate));
        return out;
    }
    private static double distanceSquared(double[] a,double[] b){
        double sum=0;for(int i=0;i<3;i++){double delta=a[i]-b[i];sum+=delta*delta;}return sum;
    }
    private static void unicode(String value){
        for(int i=0;i<value.length();i++){
            char ch=value.charAt(i);
            if(Character.isHighSurrogate(ch)){
                if(i+1>=value.length()||!Character.isLowSurrogate(value.charAt(i+1)))throw invalid("Unicode: sustituto alto sin pareja");
                i++;
            }else if(Character.isLowSurrogate(ch))throw invalid("Unicode: sustituto bajo sin pareja");
        }
    }

    /** Small strict JSON grammar owned by this project. The valid project
     * schema never approaches depth64, string1024 or node750000; these guards
     * reject malformed resource-heavy documents before allocation grows freely.
     */
    private static final class StrictJson {
        private final String source;
        private int cursor,nodes;
        StrictJson(String source){this.source=source;}
        Object read() throws JSONException {
            Object result=value(0);space();
            if(cursor!=source.length())throw syntax("Texto después del documento");
            return result;
        }
        private Object value(int depth) throws JSONException {
            if(depth>64||++nodes>750_000)throw syntax("Documento demasiado complejo");
            space();if(cursor==source.length())throw syntax("Valor ausente");
            char ch=source.charAt(cursor);
            if(ch=='{')return objectValue(depth+1);
            if(ch=='[')return arrayValue(depth+1);
            if(ch=='\"')return stringValue();
            if(ch=='t'){literal("true");return Boolean.TRUE;}
            if(ch=='f'){literal("false");return Boolean.FALSE;}
            if(ch=='n'){literal("null");return JSONObject.NULL;}
            if(ch=='-'||(ch>='0'&&ch<='9'))return numberValue();
            throw syntax("Valor JSON inválido");
        }
        private JSONObject objectValue(int depth) throws JSONException {
            cursor++;JSONObject result=new JSONObject();space();
            if(take('}'))return result;
            while(true){
                space();if(cursor==source.length()||source.charAt(cursor)!='\"')throw syntax("Clave de objeto requerida");
                String key=stringValue();
                if(result.has(key))throw syntax("Clave JSON duplicada: "+key);
                space();if(!take(':'))throw syntax("Falta ':'");
                result.put(key,value(depth));space();
                if(take('}'))return result;
                if(!take(','))throw syntax("Falta ',' entre campos");
            }
        }
        private JSONArray arrayValue(int depth) throws JSONException {
            cursor++;JSONArray result=new JSONArray();space();
            if(take(']'))return result;
            while(true){
                result.put(value(depth));space();
                if(take(']'))return result;
                if(!take(','))throw syntax("Falta ',' entre valores");
            }
        }
        private String stringValue(){
            cursor++;StringBuilder result=new StringBuilder();
            while(cursor<source.length()){
                char ch=source.charAt(cursor++);
                if(ch=='\"'){String text=result.toString();unicode(text);return text;}
                if(ch<32)throw syntax("Carácter de control sin escape");
                if(ch=='\\'){
                    if(cursor==source.length())throw syntax("Escape incompleto");
                    char escaped=source.charAt(cursor++);
                    switch(escaped){
                        case '\"':ch='\"';break;case '\\':ch='\\';break;case '/':ch='/';break;
                        case 'b':ch='\b';break;case 'f':ch='\f';break;case 'n':ch='\n';break;
                        case 'r':ch='\r';break;case 't':ch='\t';break;
                        case 'u':
                            if(source.length()-cursor<4)throw syntax("Escape Unicode incompleto");
                            int code=0;
                            for(int i=0;i<4;i++){
                                char digit=source.charAt(cursor++);
                                int number=digit>='0'&&digit<='9'?digit-'0':digit>='a'&&digit<='f'?digit-'a'+10:digit>='A'&&digit<='F'?digit-'A'+10:-1;
                                if(number<0)throw syntax("Escape Unicode inválido");
                                code=(code<<4)|number;
                            }
                            ch=(char)code;break;
                        default:throw syntax("Escape JSON no admitido");
                    }
                }
                result.append(ch);
                if(result.length()>1024)throw syntax("Texto demasiado largo para un proyecto");
            }
            throw syntax("Texto sin comillas finales");
        }
        private Number numberValue(){
            int start=cursor;take('-');
            if(cursor==source.length())throw syntax("Número incompleto");
            if(!take('0')){
                if(source.charAt(cursor)<'1'||source.charAt(cursor)>'9')throw syntax("Número inválido");
                digits();
            }
            boolean decimal=false;
            if(take('.')){decimal=true;int before=cursor;digits();if(before==cursor)throw syntax("Falta la fracción");}
            if(cursor<source.length()&&(source.charAt(cursor)=='e'||source.charAt(cursor)=='E')){
                decimal=true;cursor++;
                if(!take('+'))take('-');
                int before=cursor;digits();if(before==cursor)throw syntax("Falta el exponente");
            }
            String token=source.substring(start,cursor);
            try {
                if(!decimal)return Long.valueOf(token);
                double result=Double.parseDouble(token);
                if(!Double.isFinite(result))throw syntax("Número no finito");
                return Double.valueOf(result);
            }catch(NumberFormatException error){throw syntax("Número fuera de rango");}
        }
        private void digits(){while(cursor<source.length()&&source.charAt(cursor)>='0'&&source.charAt(cursor)<='9')cursor++;}
        private void literal(String expected){
            if(!source.startsWith(expected,cursor))throw syntax("Literal JSON inválido");cursor+=expected.length();
        }
        private boolean take(char ch){if(cursor<source.length()&&source.charAt(cursor)==ch){cursor++;return true;}return false;}
        private void space(){
            while(cursor<source.length()){
                char ch=source.charAt(cursor);if(ch!=' '&&ch!='\t'&&ch!='\r'&&ch!='\n')return;cursor++;
            }
        }
        private IllegalArgumentException syntax(String message){return invalid(message+" (posición "+cursor+")");}
    }
}
