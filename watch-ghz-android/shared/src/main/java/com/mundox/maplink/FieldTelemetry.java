package com.mundox.maplink;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.nio.charset.StandardCharsets;

/** Local sensor observations, deliberately separate from actuator commands. */
public final class FieldTelemetry {
    public static final String PATH="/mundox/fields/v1/telemetry";
    private FieldTelemetry(){}
    public static JSONObject parse(byte[] bytes)throws JSONException {
        if(bytes==null||bytes.length==0||bytes.length>2048)throw new JSONException("Invalid telemetry size");
        JSONObject j=new JSONObject(new String(bytes,StandardCharsets.UTF_8));
        if(j.length()!=12||number(j,"v")!=1||!(j.get("session") instanceof String)||
            !j.getString("session").matches("[a-f0-9-]{36}")||!(j.get("mode") instanceof String)||
            !(j.get("model") instanceof String)||j.getString("model").length()>80||
            !(j.get("emulator") instanceof Boolean))throw new JSONException("Invalid telemetry header");
        double seq=number(j,"seq");
        if(seq<1||seq>1e9||seq!=Math.floor(seq))throw new JSONException("Invalid sequence");
        String mode=j.getString("mode");
        if(!mode.equals("measuring")&&!mode.equals("stopped"))throw new JSONException("Invalid mode");
        for(String key:new String[]{"magnetic","gravity"}){
            double age=number(j,key+"AgeMs"),accuracy=number(j,key+"Accuracy");
            if(age<0||age>3000||age!=Math.floor(age)||accuracy< -1||accuracy>3||accuracy!=Math.floor(accuracy))throw new JSONException("Invalid sample metadata");
            if(!j.has(key))throw new JSONException("Missing sample key");
            if(j.isNull(key))continue;
            if(mode.equals("stopped")||!(j.get(key) instanceof JSONArray))throw new JSONException("Invalid sample");
            JSONArray vector=j.getJSONArray(key);
            if(vector.length()!=3)throw new JSONException("Invalid vector");
            for(int i=0;i<3;i++){
                Object value=vector.get(i);
                if(!(value instanceof Number))throw new JSONException("Non numeric vector");
                double n=((Number)value).doubleValue();
                if(!Double.isFinite(n)||Math.abs(n)>(key.equals("magnetic")?200000:100))throw new JSONException("Invalid sensor value");
            }
        }
        return j;
    }
    private static double number(JSONObject j,String key)throws JSONException {
        Object value=j.get(key);
        if(!(value instanceof Number)||!Double.isFinite(((Number)value).doubleValue()))throw new JSONException("Invalid number");
        return ((Number)value).doubleValue();
    }
}
