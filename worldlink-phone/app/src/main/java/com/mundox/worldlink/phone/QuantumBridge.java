package com.mundox.worldlink.phone;

import org.json.JSONObject;

/**
 * JNI bridge to canonical Mundo X MIL 1000 + QuantumRuntime/Ether Memory COW.
 * Only call from a background worker; never update physics on the Android UI thread.
 * All quantum expectations describe a simulated state, not physical entanglement.
 */
final class QuantumBridge {
    private static final boolean LOADED;
    private static final String LOAD_ERROR;
    static {
        boolean ready=false;
        String problem="";
        try {
            System.loadLibrary("worldlink_quantum");
            ready=true;
        } catch (LinkageError error) {
            problem=error.toString();
        }
        LOADED=ready;
        LOAD_ERROR=problem;
    }

    private QuantumBridge() {}
    static boolean available() {return LOADED;}
    static String problem() {return LOAD_ERROR;}
    static native String nativeEvolve(float magnetic_uT,float angular_rad_s,float lux);
    static native String nativeStatus();
    static native String nativeReset();

    static JSONObject evolve(float magnetic,float angular,float lux) throws Exception {
        if(!LOADED)throw new IllegalStateException("No se pudo cargar MIL/Ether: "+LOAD_ERROR);
        JSONObject packet=new JSONObject(nativeEvolve(magnetic,angular,lux));
        if(!packet.optBoolean("ok",false))
            throw new IllegalStateException(packet.optString("error","Evolución del núcleo fallida"));
        if(packet.optInt("logical_qubits")!=1000 || !packet.has("zz") || !packet.has("xx"))
            throw new IllegalStateException("Núcleo devolvió un resultado incompleto");
        return packet;
    }
}
