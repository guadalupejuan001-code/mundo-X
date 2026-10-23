package com.mundox.maplink;

import org.json.JSONObject;
import org.json.JSONException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

public final class MapProtocol {
    public static final String REQUEST = "/mundox/map/v1/request";
    public static final String ACK = "/mundox/map/v1/ack";
    public static final String CAPABILITY = "mundox_map_phone_v1";
    public static final Set<String> COMMANDS = java.util.Collections.unmodifiableSet(new HashSet<>(Arrays.asList(
        "HELLO","LEFT","RIGHT","UP","DOWN","ZOOM_IN","ZOOM_OUT","Z_UP","Z_DOWN",
        "NEXT","PREVIOUS","PLAY","FIELD_UP","FIELD_DOWN","RESET")));
    private MapProtocol() {}
    public static final class Request {
        public final String session, command;
        public final long sequence;
        Request(String s, long n, String c) { session=s; sequence=n; command=c; }
    }
    public static byte[] request(String session,long sequence,String command) throws JSONException {
        JSONObject json = new JSONObject().put("v",1).put("session",session).put("seq",sequence).put("cmd",command);
        byte[] bytes=json.toString().getBytes(StandardCharsets.UTF_8);
        parse(bytes);
        return bytes;
    }
    public static Request parse(byte[] bytes) throws JSONException {
        if(bytes==null||bytes.length==0||bytes.length>2048)throw new JSONException("Tamaño inválido");
        JSONObject json=new JSONObject(new String(bytes,StandardCharsets.UTF_8));
        if(json.length()!=4 || !(json.get("v") instanceof Number) || json.getDouble("v")!=1 ||
            !(json.get("session") instanceof String) || !(json.get("cmd") instanceof String) ||
            !(json.get("seq") instanceof Number))throw new JSONException("Formato inválido");
        String session=json.getString("session"),command=json.getString("cmd");
        double seq=json.getDouble("seq");
        if(!session.matches("[a-f0-9-]{36}")||seq<1||seq>1000000000||seq!=Math.floor(seq)||!COMMANDS.contains(command))
            throw new JSONException("Orden inválida");
        return new Request(session,(long)seq,command);
    }
    public static byte[] ack(Request request,boolean ok,String message) throws JSONException {
        if(message==null)message="";
        if(message.length()>200)message=message.substring(0,200);
        return new JSONObject().put("v",1).put("session",request.session).put("seq",request.sequence)
            .put("ok",ok).put("message",message).toString().getBytes(StandardCharsets.UTF_8);
    }
    public static JSONObject parseAck(byte[] bytes,String session,long sequence) throws JSONException {
        if(bytes==null||bytes.length>2048)throw new JSONException("Respuesta inválida");
        JSONObject json=new JSONObject(new String(bytes,StandardCharsets.UTF_8));
        if(json.length()!=5 || !(json.get("v") instanceof Number) || json.getDouble("v")!=1 ||
            !(json.get("seq") instanceof Number) || json.getDouble("seq")!=sequence ||
            !session.equals(json.getString("session"))||
            !(json.get("ok") instanceof Boolean)||!(json.get("message") instanceof String)||
            json.getString("message").length()>200)throw new JSONException("Respuesta fuera de sesión");
        return json;
    }
}
