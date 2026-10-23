package com.mundox.ghzwatch;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import androidx.webkit.WebViewAssetLoader;
import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailability;
import com.google.android.gms.wearable.MessageClient;
import com.google.android.gms.wearable.MessageEvent;
import com.google.android.gms.wearable.Wearable;
import com.mundox.maplink.MapProtocol;
import org.json.JSONObject;
import org.json.JSONTokener;
import java.io.ByteArrayInputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.concurrent.Executors;

public final class MapActivity extends Activity implements MessageClient.OnMessageReceivedListener {
    private static final String URL="https://appassets.androidplatform.net/assets/map/index.html";
    private static final int PICK=31,SAVE=32;
    private WebView web;
    private TextView connection;
    private boolean active,listenerAttached;
    private ValueCallback<Uri[]> chooser;
    private String pendingCsv;
    private final LinkedHashMap<String,Entry> ledger=new LinkedHashMap<>();
    private static final class Entry { long sequence; byte[] reply; Entry(long n){sequence=n;} }
    interface Reply { void send(byte[] bytes); }

    @SuppressLint({"SetJavaScriptEnabled","AddJavascriptInterface"})
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().setStatusBarColor(Color.BLACK);
        getWindow().setNavigationBarColor(Color.BLACK);
        LinearLayout root=new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(11,13,16));
        root.setOnApplyWindowInsetsListener((v,insets)->{
            if(Build.VERSION.SDK_INT>=30){
                android.graphics.Insets bars=insets.getInsets(android.view.WindowInsets.Type.systemBars());
                v.setPadding(bars.left,bars.top,bars.right,bars.bottom);
            }else v.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),
                insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());
            return insets;
        });
        connection=new TextView(this);
        connection.setTextColor(Color.rgb(238,203,146));
        connection.setTextSize(12);
        connection.setPadding(20,12,20,12);
        connection.setText("Abre «Mapa 4D · Mando» en el reloj y pulsa CONECTAR.");
        root.addView(connection,new LinearLayout.LayoutParams(-1,-2));
        web=new WebView(this);
        web.setBackgroundColor(Color.rgb(11,13,16));
        web.getSettings().setJavaScriptEnabled(true);
        web.getSettings().setAllowFileAccess(false);
        web.getSettings().setAllowContentAccess(true);
        web.getSettings().setMixedContentMode(android.webkit.WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        web.getSettings().setSupportMultipleWindows(false);
        web.addJavascriptInterface(new FileBridge(),"MundoXFiles");
        WebViewAssetLoader loader=new WebViewAssetLoader.Builder()
            .addPathHandler("/assets/",new WebViewAssetLoader.AssetsPathHandler(this)).build();
        web.setWebViewClient(new WebViewClient(){
            @Override public WebResourceResponse shouldInterceptRequest(WebView view,WebResourceRequest request) {
                Uri uri=request.getUrl();
                if("https".equals(uri.getScheme()) && "appassets.androidplatform.net".equals(uri.getHost()) &&
                    uri.getPath()!=null && uri.getPath().startsWith("/assets/map/")) {
                    WebResourceResponse response=loader.shouldInterceptRequest(uri);
                    if(response!=null)return response;
                }
                return new WebResourceResponse("text/plain","UTF-8",403,"Blocked",
                    java.util.Collections.emptyMap(),new ByteArrayInputStream(new byte[0]));
            }
            @Override public boolean shouldOverrideUrlLoading(WebView view,WebResourceRequest request) {
                return !URL.equals(request.getUrl().toString());
            }
        });
        web.setWebChromeClient(new WebChromeClient(){
            @Override public boolean onShowFileChooser(WebView view,ValueCallback<Uri[]> callback,FileChooserParams params) {
                if(chooser!=null)chooser.onReceiveValue(null);
                chooser=callback;
                Intent pick=new Intent(Intent.ACTION_OPEN_DOCUMENT);
                pick.setType("*/*");
                pick.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"text/csv","text/plain","application/octet-stream"});
                pick.addCategory(Intent.CATEGORY_OPENABLE);
                try{startActivityForResult(pick,PICK);}catch(android.content.ActivityNotFoundException e){
                    chooser.onReceiveValue(null);chooser=null;toast("No hay un selector de archivos disponible.");
                }
                return true;
            }
        });
        root.addView(web,new LinearLayout.LayoutParams(-1,0,1));
        setContentView(root);
        web.loadUrl(URL);
    }
    @Override protected void onResume() {
        super.onResume();active=true;web.onResume();
        if(GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(this)==ConnectionResult.SUCCESS) {
            Wearable.getMessageClient(this).addListener(this)
                .addOnSuccessListener(v->{if(active)listenerAttached=true;else Wearable.getMessageClient(this).removeListener(this);})
                .addOnFailureListener(e->connection.setText("El mapa funciona localmente. No se pudo activar el enlace del reloj."));
        }else connection.setText("Mapa local · el enlace necesita Google Play services actualizado.");
    }
    @Override protected void onPause() {
        active=false;
        if(listenerAttached)Wearable.getMessageClient(this).removeListener(this);
        listenerAttached=false;
        connection.setText("Abre «Mapa 4D · Mando» en el reloj y pulsa CONECTAR.");
        web.evaluateJavascript("document.getElementById('play')?.getAttribute('aria-label')==='Pausar capturas' && document.getElementById('play').click()",null);
        web.onPause();
        super.onPause();
    }
    @Override public void onMessageReceived(MessageEvent event) {
        if(!MapProtocol.REQUEST.equals(event.getPath()))return;
        final String node=event.getSourceNodeId();
        final byte[] bytes=event.getData();
        runOnUiThread(()->accept(node,bytes,reply->Wearable.getMessageClient(this)
            .sendMessage(node,MapProtocol.ACK,reply)
            .addOnFailureListener(e->connection.setText("No se pudo enviar la confirmación al reloj."))));
    }
    void accept(String node,byte[] bytes,Reply callback) {
        final MapProtocol.Request request;
        try{request=MapProtocol.parse(bytes);}catch(Exception error){return;}
        if(!active){respond(request,false,"Abre el mapa en el teléfono.",callback);return;}
        String key=node+"|"+request.session;
        Entry previous=ledger.get(key);
        if(previous!=null && request.sequence<=previous.sequence) {
            if(request.sequence==previous.sequence && previous.reply!=null)callback.send(previous.reply);
            else respond(request,false,"Orden antigua o todavía en curso.",callback);
            return;
        }
        Entry current=new Entry(request.sequence);ledger.put(key,current);
        while(ledger.size()>16)ledger.remove(ledger.keySet().iterator().next());
        String code="JSON.stringify(window.MundoXRemote ? window.MundoXRemote.apply("+JSONObject.quote(request.command)+") : {ok:false,message:'El mapa aún está cargando.'})";
        web.evaluateJavascript(code,value->{
            boolean ok=false;String message="No se pudo aplicar la orden.";
            try {
                Object decoded=new JSONTokener(value).nextValue();
                if(decoded instanceof String) {
                    JSONObject result=new JSONObject((String)decoded);
                    ok=result.getBoolean("ok");message=result.optString("message",message);
                }
                current.reply=MapProtocol.ack(request,ok,message);
                callback.send(current.reply);
                if(active)connection.setText(ok?"Reloj conectado · "+message:"Reloj: "+message);
            }catch(Exception ignored){respond(request,false,message,callback);}
        });
    }
    private void respond(MapProtocol.Request request,boolean ok,String message,Reply callback) {
        try{callback.send(MapProtocol.ack(request,ok,message));}catch(Exception ignored){}
    }
    private final class FileBridge {
        @JavascriptInterface public void saveCSV(String text) {
            if(text==null||text.getBytes(StandardCharsets.UTF_8).length>2_000_000)return;
            runOnUiThread(()->{
                if(!active||pendingCsv!=null)return;
                pendingCsv=text;
                Intent save=new Intent(Intent.ACTION_CREATE_DOCUMENT);
                save.addCategory(Intent.CATEGORY_OPENABLE);
                save.setType("text/csv");
                save.putExtra(Intent.EXTRA_TITLE,"MundoX-Campo-4D.csv");
                try{startActivityForResult(save,SAVE);}catch(android.content.ActivityNotFoundException e){
                    pendingCsv=null;toast("No hay una app para guardar documentos.");
                }
            });
        }
    }
    @Override protected void onActivityResult(int request,int result,Intent intent) {
        super.onActivityResult(request,result,intent);
        if(request==PICK && chooser!=null) {
            chooser.onReceiveValue(result==RESULT_OK&&intent!=null&&intent.getData()!=null?new Uri[]{intent.getData()}:null);
            chooser=null;
        }
        if(request==SAVE) {
            final String text=pendingCsv;pendingCsv=null;
            if(result!=RESULT_OK||intent==null||intent.getData()==null||text==null)return;
            final Uri uri=intent.getData();
            java.util.concurrent.ExecutorService io=Executors.newSingleThreadExecutor();
            io.execute(()->{
                try(OutputStream stream=getContentResolver().openOutputStream(uri,"wt")){
                    if(stream==null)throw new java.io.IOException("Sin salida");
                    stream.write(text.getBytes(StandardCharsets.UTF_8));
                    runOnUiThread(()->toast("CSV guardado."));
                }catch(Exception e){runOnUiThread(()->toast("No se pudo guardar el CSV."));}
                finally{io.shutdown();}
            });
        }
    }
    private void toast(String message){if(!isDestroyed())Toast.makeText(this,message,Toast.LENGTH_LONG).show();}
    WebView webForTest(){return web;}
    @Override protected void onDestroy(){
        if(chooser!=null)chooser.onReceiveValue(null);
        web.removeJavascriptInterface("MundoXFiles");web.destroy();
        super.onDestroy();
    }
}

