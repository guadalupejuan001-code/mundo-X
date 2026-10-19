package com.mundox.graphics;

import android.content.Context;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import org.json.JSONArray;
import org.json.JSONObject;

/** Value-only project checks. No NativeCore instance or GL context is created. */
public final class ProjectCodecChecks {
    private static final String BELL="{\"format\":\"cosmic-x-engine-project\",\"version\":1,\"name\":\"Bell X\","
        +"\"core\":{\"backend\":\"compact-clifford\",\"initialState\":\"zero\",\"qubits\":2,\"revision\":1,\"gates\":[[\"H\",0],[\"CX\",0,1]]},"
        +"\"graphics\":{\"program\":\"opaque-grid-v1\",\"objects\":1024,\"seed\":2026,\"coupling\":0.25,"
        +"\"observables\":[\"+XX\",\"-YY\",\"+ZZ\",\"XI\",\"IX\",\"ZI\",\"IZ\"],\"edits\":[[0,2],[62,6],[1000,7]],"
        +"\"camera\":{\"mode\":\"lookAt\",\"eye\":[14,8,18],\"target\":[0,1,0],\"near\":0.1,\"far\":5000,\"fov\":48},"
        +"\"time\":0,\"selectedId\":62,\"resolutionLimit\":640}}";
    private static final String ORBIT="{\"mode\":\"orbit\",\"overview\":true,\"yaw\":0.67,\"pitch\":0.65,\"zoom\":1.0}";
    private static final String CAMERA="{\"mode\":\"lookAt\",\"eye\":[14,8,18],\"target\":[0,1,0],\"near\":0.1,\"far\":5000,\"fov\":48}";
    private int checks,rejections;
    private interface Action {void run() throws Exception;}
    private ProjectCodecChecks(){}

    public static JSONObject run(Context unused) throws Exception {
        return new ProjectCodecChecks().execute();
    }

    private void check(boolean condition,String reason){
        if(!condition)throw new IllegalStateException("Project codec: "+reason);
        checks++;
    }
    private void reject(Action action,String reason) throws Exception {
        try{action.run();}catch(IllegalArgumentException|org.json.JSONException expected){checks++;rejections++;return;}
        throw new IllegalStateException("Project codec accepted "+reason);
    }
    private void reject(String text,String reason) throws Exception {
        reject(()->CosmicProject.decode(text.getBytes(StandardCharsets.UTF_8)),reason);
    }
    private static byte[] utf8(String text){return text.getBytes(StandardCharsets.UTF_8);}

    private JSONObject execute() throws Exception {
        CosmicProject.Document bell=CosmicProject.decode(utf8(BELL));
        check(bell.core.qubits==2&&bell.core.version==1&&bell.core.gateHistory.length==6,"Bell core");
        check(bell.graphics.objectCount==1024&&bell.graphics.seed==2026&&bell.graphics.selectedId==62
            &&bell.graphics.quantumStrength==.25f&&bell.graphics.overrideEye[0]==14f,"Bell scene");
        check(Arrays.equals(bell.graphics.observables,new String[]{"+XX","-YY","+ZZ","XI","IX","ZI","IZ"}),"original observables");
        check(Arrays.equals(bell.graphics.editIds,new int[]{0,62,1000})&&bell.graphics.editValues[2]==7,"stable sorted edits");
        CosmicProject.Document saved=CosmicProject.decode(CosmicProject.encode(bell));
        check(Arrays.equals(saved.core.gateHistory,bell.core.gateHistory)&&saved.core.version==bell.core.version
            &&Arrays.equals(saved.graphics.observables,bell.graphics.observables),"decode/encode core roundtrip");

        String orbit=BELL.replace(CAMERA,ORBIT).replace("\"seed\":2026","\"seed\":4000000001")
            .replace("\"coupling\":0.25","\"coupling\":-0.125");
        CosmicProject.Document ghz=CosmicProject.decode(utf8(orbit));
        check(ghz.graphics.overrideEye==null&&ghz.graphics.overview&&ghz.graphics.seed==4000000001L
            &&ghz.graphics.quantumStrength==-.125f,"orbit/unsigned seed/signed coupling");
        CosmicProject.Document snapshot=CosmicProject.fromSnapshots(ghz.name,ghz.core,ghz.graphics);
        String snapshotJson=new String(CosmicProject.encode(snapshot),StandardCharsets.UTF_8);
        check(snapshotJson.contains("0.67")&&!snapshotJson.contains("0.670000016"),"Float JSON spelling");
        CosmicProject.Document snapshotBack=CosmicProject.decode(utf8(snapshotJson));
        check(snapshotBack.graphics.yaw==ghz.graphics.yaw&&snapshotBack.graphics.pitch==ghz.graphics.pitch,"snapshot float roundtrip");

        String precise=orbit.replace("\"yaw\":0.67","\"yaw\":0.1234567891234567");
        String preciseOut=new String(CosmicProject.encode(CosmicProject.decode(utf8(precise))),StandardCharsets.UTF_8);
        check(preciseOut.contains("0.1234567891234567"),"imported JSON precision retention");
        byte[] originalBell=CosmicProject.encode(bell);
        for(int oldQubits:new int[]{2,8,20,60}){
            CosmicProject.Document legacy=oldQubits==2?bell:CosmicProject.expandQubits(bell,oldQubits);
            byte[] original=CosmicProject.encode(legacy);
            CosmicProject.Document expanded=CosmicProject.expandQubits(legacy,1000);
            check(expanded.core.qubits==1000&&expanded.core.version==legacy.core.version
                &&Arrays.equals(expanded.core.gateHistory,legacy.core.gateHistory),"1000-qubit expansion preserves legacy history "+oldQubits);
            char[] padding=new char[1000-oldQubits];Arrays.fill(padding,'I');String suffix=new String(padding);
            for(int i=0;i<legacy.graphics.observables.length;i++)
                check(expanded.graphics.observables[i].equals(legacy.graphics.observables[i]+suffix),"expansion preserves signed Pauli "+oldQubits);
            check(Arrays.equals(original,CosmicProject.encode(legacy)),"expansion does not mutate original "+oldQubits);
            CosmicProject.Document expandedBack=CosmicProject.decode(CosmicProject.encode(expanded));
            check(expandedBack.core.qubits==1000&&Arrays.equals(expandedBack.graphics.observables,expanded.graphics.observables)
                &&Arrays.equals(expandedBack.graphics.editIds,legacy.graphics.editIds)
                &&expandedBack.graphics.seed==legacy.graphics.seed&&expandedBack.graphics.selectedId==legacy.graphics.selectedId,
                "1000-qubit portable roundtrip preserves scene "+oldQubits);
        }
        check(Arrays.equals(originalBell,CosmicProject.encode(bell)),"original Bell fixture remains two qubits");
        String expandedPrecise=new String(CosmicProject.encode(CosmicProject.expandQubits(CosmicProject.decode(utf8(precise)),1000)),StandardCharsets.UTF_8);
        check(expandedPrecise.contains("0.1234567891234567"),"expansion retains imported JSON number precision");
        JSONObject highGate=new JSONObject(new String(CosmicProject.encode(CosmicProject.expandQubits(bell,1000)),StandardCharsets.UTF_8));
        highGate.getJSONObject("core").getJSONArray("gates").put(new JSONArray().put("H").put(999))
            .put(new JSONArray().put("CX").put(999).put(32)).put(new JSONArray().put("Y").put(53));
        CosmicProject.Document high=CosmicProject.decode(utf8(highGate.toString()));
        check(Arrays.equals(high.core.gateHistory,new long[]{0,0,0,4,0,1,0,999,0,4,999,32,6,53,0}),"gate999 and cross-word operands roundtrip");
        for(int target:new int[]{-1,0,1,2,1001})reject(()->CosmicProject.expandQubits(bell,target),"invalid expansion target "+target);
        CosmicProject.Document boundary=CosmicProject.decode(utf8(orbit.replace("\"pitch\":0.65","\"pitch\":0.04")
            .replace("\"zoom\":1.0","\"zoom\":0.12")));
        check(CosmicProject.decode(CosmicProject.encode(CosmicProject.fromSnapshots(boundary.name,boundary.core,boundary.graphics)))
            .graphics.pitch==.04f,"float boundary export");
        CosmicProject.Document gates=CosmicProject.decode(utf8(BELL.replace("[[\"H\",0],[\"CX\",0,1]]",
            "[[\"H\",0],[\"S\",0],[\"X\",0],[\"Z\",0],[\"CX\",0,1],[\"CZ\",0,1],[\"Y\",0]]")));
        check(Arrays.equals(gates.core.gateHistory,new long[]{0,0,0,1,0,0,2,0,0,3,0,0,4,0,1,5,0,1,6,0,0}),"stable native gate IDs");

        JSONObject infiniteJson=new JSONObject(BELL);
        infiniteJson.put("version",2).put("core",new JSONObject().put("backend","infinite-clifford").put("snapshot","MXI1:00"));
        infiniteJson.getJSONObject("graphics").put("observables",new JSONArray().put("X:"+InfiniteProgram.FAR_ADDRESS+" X:"+InfiniteProgram.partner(InfiniteProgram.FAR_ADDRESS)).put("- Y:0 Y:1").put(""));
        CosmicProject.Document infinite=CosmicProject.decode(utf8(infiniteJson.toString()));
        check(infinite.core.infinite&&infinite.core.qubits==0&&infinite.core.infiniteSnapshot.equals("MXI1:00"),"v2 structurally retains opaque state without native creation");
        check(CosmicProject.decode(CosmicProject.encode(infinite)).core.infinite,"v2 decode/encode backend");
        check(CosmicProject.decode(CosmicProject.encode(CosmicProject.fromSnapshots(infinite.name,infinite.core,infinite.graphics))).graphics.observables[0].equals(infinite.graphics.observables[0]),"v2 current snapshot retains full decimal addresses");
        check(InfiniteProgram.address("000001").equals("1")&&InfiniteProgram.partner("18446744073709551616").equals("18446744073709551617"),"address canonicalization beyond 64 bits");
        reject(()->CosmicProject.expandQubits(infinite,1000),"finite expansion of infinite domain");
        for(String badAddress:new String[]{"","-1","+1"," 1","1 ","1e100","１"})reject(()->InfiniteProgram.address(badAddress),"invalid decimal address");
        char[] maximum=new char[4096];Arrays.fill(maximum,'9');check(InfiniteProgram.address(new String(maximum)).length()==4096,"full 4096-digit address");
        reject(()->InfiniteProgram.address("0"+new String(maximum)),"raw input budget bypass");
        for(String bad:new String[]{"X:0 Z:00","+X:0","X:+1","X:1e100","X:１","X:1 # comment","X:1\u0000"}){
            JSONObject copy=new JSONObject(infiniteJson.toString());copy.getJSONObject("graphics").put("observables",new JSONArray().put(bad));
            reject(copy.toString(),"invalid sparse observable");
        }
        for(String bad:new String[]{"","MXI2:00","MXI1:0","MXI1:0z"}){
            JSONObject copy=new JSONObject(infiniteJson.toString());copy.getJSONObject("core").put("snapshot",bad);reject(copy.toString(),"invalid v2 snapshot envelope");
        }
        JSONObject extra=new JSONObject(infiniteJson.toString());extra.getJSONObject("core").put("qubits",2);reject(extra.toString(),"v2 duplicated finite metadata");
        JSONObject wrongBackend=new JSONObject(infiniteJson.toString());wrongBackend.getJSONObject("core").put("backend","compact-clifford");reject(wrongBackend.toString(),"v2 finite backend");

        String escapedRocket="\\ud83d\\ude80";
        check(CosmicProject.decode(utf8(BELL.replace("Bell X",escapedRocket))).name.codePointCount(0,2)==1,"escaped Unicode pair");
        StringBuilder name=new StringBuilder();for(int i=0;i<128;i++)name.appendCodePoint(0x1f680);
        check(CosmicProject.decode(utf8(BELL.replace("Bell X",name.toString()))).name.codePointCount(0,name.length())==128,"128 Unicode codepoints");
        reject(BELL.replace("Bell X",name+"a"),"129 name codepoints");
        check(CosmicProject.decode(utf8(BELL.replace("Bell X","\\u007f\\u0085"))).name.length()==2,"documented DEL/C1 name policy");
        reject(BELL.replace("Bell X","\\ud800"),"unpaired high surrogate");
        reject(BELL.replace("Bell X","\\udc00"),"unpaired low surrogate");
        reject(BELL.replace("Bell X","\\ud800x\\udc00"),"separated surrogate pair");
        reject(BELL.replace("Bell X","\\u000a"),"control in name");
        reject(BELL.replace("Bell X",""),"empty name");
        reject(()->CosmicProject.decode(new byte[]{(byte)0xc3,0x28}),"invalid UTF-8");
        reject(()->CosmicProject.decode(new byte[]{(byte)0xed,(byte)0xa0,(byte)0x80}),"UTF-8 encoded surrogate");
        reject("\ufeff"+BELL,"UTF-8 BOM");

        String[] malformed={
            BELL.replace("\"version\":1","\"version\":1,\"version\":1"),
            BELL.replace("\"seed\":2026","\"seed\":2026,\"\\u0073eed\":2026"),
            BELL.substring(0,BELL.length()-1)+",}",
            BELL.replace("[[\"H\",0],[\"CX\",0,1]]","[[\"H\",0],[\"CX\",0,1],]"),
            BELL.replace("\"version\":1","\"version\":01"),
            BELL.replace("\"version\":1","\"version\":+1"),
            BELL.replace("\"time\":0","\"time\":.5"),
            BELL.replace("\"time\":0","\"time\":1."),
            BELL.replace("\"time\":0","\"time\":1e"),
            BELL.replace("\"time\":0","\"time\":1e+"),
            BELL.replace("\"time\":0","\"time\":NaN"),
            BELL.replace("\"time\":0","\"time\":Infinity"),
            BELL.replace("\"time\":0","\"time\":1e309"),
            BELL.replace("\"name\":\"Bell X\"","name:'Bell X'"),
            BELL.replace("Bell X","Bell\\qX"),
            BELL.replace("Bell X","Bell\nX"),
            "/* comment */"+BELL,
            BELL+"// comment",
            BELL+" {}",
            BELL.replace("\"version\":1","\"version\"=1"),
            BELL.replace("\"time\":0,","\"time\":0;"),
            BELL.substring(0,BELL.length()-1)
        };
        for(String malformedJson:malformed)reject(malformedJson,"lenient/malformed JSON");
        for(String value:new String[]{"1.0","1e0","true","\"1\"","null"})
            reject(BELL.replace("\"version\":1","\"version\":"+value),"wrong version type");
        for(String value:new String[]{"2.0","2e0","true","\"2\"","null","0","1001"})
            reject(BELL.replace("\"qubits\":2","\"qubits\":"+value),"wrong qubit type/range");
        reject(BELL.replace("\"revision\":1","\"revision\":9007199254740992"),"revision overflow");
        reject(BELL.replace("\"revision\":1","\"revision\":-1"),"negative revision");
        reject(BELL.replace("\"seed\":2026","\"seed\":4294967296"),"seed overflow");
        reject(BELL.replace("\"seed\":2026","\"seed\":2026.0"),"floating seed");
        reject(BELL.replace("compact-clifford","dense-reference"),"unsupported backend");
        reject(BELL.replace("\"zero\"","\"random\""),"unsupported initial state");
        reject(BELL.replace("opaque-grid-v1","opaque-grid-v2"),"unsupported graphic program");
        reject(BELL.replace("\"version\":1","\"version\":2"),"future project version");
        reject(BELL.replace("\"format\":","\"extra\":0,\"format\":"),"unknown field");
        reject(BELL.replace("\"seed\":2026,",""),"missing field");
        for(String gate:new String[]{"[\"T\",0]","[\"CNOT\",0,1]","[\"H\",0,1]","[\"CX\",0]","[\"CX\",0,0]","[\"X\",2]","[\"X\",-1]","[\"X\",0.0]","[\"X\",true]"})
            reject(BELL.replace("[\"H\",0]",gate),"gate validation");
        reject(BELL.replace("\"+XX\"","\"+XA\""),"invalid Pauli alphabet");
        reject(BELL.replace("\"+XX\"","\"+X\""),"invalid Pauli width");
        reject(BELL.replace("[[0,2],[62,6],[1000,7]]","[[62,6],[0,2]]"),"unsorted edit IDs");
        reject(BELL.replace("[[0,2],[62,6],[1000,7]]","[[62,6],[62,2]]"),"duplicate edit IDs");
        reject(BELL.replace("[[0,2],[62,6],[1000,7]]","[[0,0]]"),"invalid edit state");
        reject(BELL.replace("[[0,2],[62,6],[1000,7]]","[[1048576,1]]"),"invalid edit id");
        CosmicProject.Document retained=CosmicProject.decode(utf8(BELL.replace("\"objects\":1024","\"objects\":64")));
        check(retained.graphics.editIds[2]==1000,"edits retained beyond current population");
        reject(BELL.replace("\"selectedId\":62","\"selectedId\":1024"),"selection out of visible range");
        reject(BELL.replace("\"resolutionLimit\":640","\"resolutionLimit\":63"),"resolution lower limit");
        reject(BELL.replace("\"coupling\":0.25","\"coupling\":0.2500001"),"coupling limit");
        reject(orbit.replace("\"overview\":true","\"overview\":1"),"orbit boolean type");
        reject(orbit.replace("\"pitch\":0.65","\"pitch\":0.039"),"orbit pitch range");
        reject(BELL.replace("\"eye\":[14,8,18]","\"eye\":[0,1,0]"),"coincident camera");
        reject(BELL.replace("\"near\":0.1","\"near\":5000"),"near/far order");
        reject(BELL.replace("\"near\":0.1,\"far\":5000","\"near\":1,\"far\":1.000000001"),"float32 projection collapse");
        reject(BELL.replace("\"eye\":[14,8,18],\"target\":[0,1,0]",
            "\"eye\":[1000000,0,0],\"target\":[999999.999,0,0]"),"float32 eye/target collapse");

        // Limits are enforced before snapshots/native/GL can be constructed.
        StringBuilder longGates=new StringBuilder("[");
        for(int i=0;i<100001;i++){if(i>0)longGates.append(',');longGates.append("[\"H\",0]");}
        longGates.append(']');
        reject(BELL.replace("[[\"H\",0],[\"CX\",0,1]]",longGates),"gate count limit");
        StringBuilder manyPaulis=new StringBuilder("[");
        for(int i=0;i<4097;i++){if(i>0)manyPaulis.append(',');manyPaulis.append("\"XX\"");}
        manyPaulis.append(']');
        reject(BELL.replace("[\"+XX\",\"-YY\",\"+ZZ\",\"XI\",\"IX\",\"ZI\",\"IZ\"]",manyPaulis),"observable count limit");
        reject(()->CosmicProject.decode(new byte[CosmicProject.MAX_FILE_BYTES+1]),"file byte limit");
        StringBuilder deep=new StringBuilder();for(int i=0;i<66;i++)deep.append('[');deep.append('0');for(int i=0;i<66;i++)deep.append(']');
        reject(deep.toString(),"excessive nesting");
        final byte[] source=utf8(BELL);
        InputStream partial=new ByteArrayInputStream(source){
            boolean first=true;
            @Override public synchronized int read(byte[] bytes,int offset,int length){
                if(first){first=false;return 0;}return super.read(bytes,offset,Math.min(length,7));
            }
        };
        check(CosmicProject.read(partial).name.equals("Bell X"),"partial/zero-length stream reads");
        reject(()->CosmicProject.fromSnapshots("X",null,bell.graphics),"missing core snapshot");
        reject(()->CosmicProject.encode(null),"missing document");

        JSONObject result=new JSONObject();
        result.put("passed",true);result.put("checks",checks);result.put("rejectedInvalidDocuments",rejections);
        result.put("nativeOrGlCreated",false);result.put("format",CosmicProject.FORMAT);
        result.put("expandedQubits",1000);result.put("legacyQubitCounts",new JSONArray(Arrays.asList(2,8,20,60)));
        return result;
    }
}
