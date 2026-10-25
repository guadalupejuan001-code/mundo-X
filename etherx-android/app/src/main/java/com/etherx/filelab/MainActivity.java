package com.etherx.filelab;

import android.app.*;
import android.os.*;
import android.content.*;
import android.database.Cursor;
import android.graphics.*;
import android.graphics.drawable.*;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.view.*;
import android.widget.*;

import com.etherx.filelab.core.*;

import java.io.*;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Native Android client. No embedded web server, Termux or runtime downloads. */
public final class MainActivity extends Activity {
    private static final int PICK_SOURCE=100, PICK_DEST=101;
    private static final int BG=0xff0b1020, PANEL=0xff151d30, ACCENT=0xff50e3cd, MUTED=0xff9fb1c9, WHITE=0xfff1f6ff;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private Uri inputUri;
    private String inputName;
    private long inputSize=-1;
    private boolean active=false;
    private enum Action { PACK, RESTORE }
    private Action pending;
    private EtherCodec.Level mode=EtherCodec.Level.EQUILIBRADO;
    private TextView selectedLabel,resultText,stageLabel;
    private Button chooseButton,analyzeButton,packButton,restoreButton,trainButton;
    private ProgressBar progress;
    private long lastUiUpdate=0;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        getWindow().getDecorView().setSystemUiVisibility(0);
        buildInterface();
        if(state!=null) {
            String uri=state.getString("file_uri");
            if(uri!=null){inputUri=Uri.parse(uri);inputName=state.getString("file_name");inputSize=state.getLong("file_size",-1);}
            setSelected();
        }
    }
    @Override protected void onSaveInstanceState(Bundle b){
        super.onSaveInstanceState(b);
        if(inputUri!=null){b.putString("file_uri",inputUri.toString());b.putString("file_name",inputName);b.putLong("file_size",inputSize);}
    }
    @Override protected void onDestroy(){super.onDestroy();worker.shutdown();}
    private int px(float dp){return (int)(getResources().getDisplayMetrics().density*dp+.5f);}
    private GradientDrawable bg(int color,int radius){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(px(radius));return d;}
    private GradientDrawable outline(int color,int stroke){GradientDrawable d=bg(color,18);d.setStroke(px(1),stroke);return d;}
    private TextView text(String value,int size,int color,boolean bold){
        TextView v=new TextView(this);v.setText(value);v.setTextColor(color);v.setTextSize(size);
        if(bold)v.setTypeface(android.graphics.Typeface.DEFAULT,android.graphics.Typeface.BOLD);
        v.setLineSpacing(px(3),1f);return v;
    }
    private LinearLayout column(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;}
    private LinearLayout.LayoutParams margin(int top){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.topMargin=px(top);return p;}
    private Button button(String label, boolean primary) {
        Button b=new Button(this);b.setText(label);b.setAllCaps(false);b.setTextSize(15);
        b.setTextColor(primary?BG:WHITE);b.setTypeface(null,1);
        b.setBackground(outline(primary?ACCENT:PANEL,primary?ACCENT:0xff354661));
        b.setPadding(px(12),px(8),px(12),px(8));
        b.setMinimumHeight(px(50));b.setElevation(px(1));
        return b;
    }
    private void buildInterface(){
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setBackgroundColor(BG);
        LinearLayout body=column();body.setPadding(px(22),px(28),px(22),px(32));
        scroll.addView(body);setContentView(scroll);
        TextView brand=text("E T H E R   X",16,ACCENT,true);body.addView(brand);
        TextView heading=text("File Laboratory",32,WHITE,true);body.addView(heading,margin(6));
        TextView lead=text("Deconstruye • Compacta • Reconstruye",14,MUTED,false);body.addView(lead,margin(4));
        LinearLayout details=column();details.setPadding(px(16),px(16),px(16),px(16));
        details.setBackground(outline(PANEL,0xff26344b));body.addView(details,margin(26));
        details.addView(text("ARCHIVO SELECCIONADO",11,ACCENT,true));
        selectedLabel=text("Ningún archivo seleccionado",15,WHITE,false);details.addView(selectedLabel,margin(8));
        chooseButton=button("Elegir archivo o imagen ISO",true);body.addView(chooseButton,margin(16));
        chooseButton.setOnClickListener(v->openSource());
        analyzeButton=button("01   Analizar estructura",false);body.addView(analyzeButton,margin(20));
        analyzeButton.setOnClickListener(v->analyze());
        packButton=button("02   Compactar a .exf",false);body.addView(packButton,margin(10));
        packButton.setOnClickListener(v->chooseMode());
        restoreButton=button("03   Reconstruir desde .exf",false);body.addView(restoreButton,margin(10));
        restoreButton.setOnClickListener(v->prepareRestore());
        trainButton=button("Laboratorio Q · Entrenar 2 qubits",false);body.addView(trainButton,margin(16));
        trainButton.setOnClickListener(v->train());
        stageLabel=text("Listo para explorar archivos locales · No se suben datos a Internet",12,MUTED,false);
        body.addView(stageLabel,margin(20));
        progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);progress.setProgressTintList(android.content.res.ColorStateList.valueOf(ACCENT));
        progress.setProgress(0);body.addView(progress,margin(8));
        LinearLayout results=column();results.setPadding(px(16),px(18),px(16),px(18));
        results.setBackground(outline(PANEL,0xff26344b));body.addView(results,margin(22));
        results.addView(text("ANÁLISIS / INFORME",12,ACCENT,true));
        resultText=text("Selecciona un archivo para empezar.\n\nMotor reversible con reglas uniformes, excepciones dispersas, referencias, zlib y SHA-256.",14,WHITE,false);
        resultText.setTextIsSelectable(true);results.addView(resultText,margin(10));
        TextView footer=text("MUNDO X · motor clásico inspirado en Inverse Ether Memory\n"+
                             "Q-Lab es simulación experimental, no hardware cuántico.",11,MUTED,false);
        body.addView(footer,margin(22));setSelected();
    }
    private void show(String message){resultText.setText(message);}
    private void notice(String message){Toast.makeText(this,message,Toast.LENGTH_LONG).show();}
    private void setSelected(){
        if(selectedLabel==null)return;
        selectedLabel.setText(inputUri==null?"Ningún archivo seleccionado":
            inputName+"\n"+(inputSize>=0?human(inputSize):"Tamaño pendiente de lectura"));
        boolean ready=inputUri!=null&&!active;
        analyzeButton.setEnabled(ready);packButton.setEnabled(ready);restoreButton.setEnabled(ready);
        chooseButton.setEnabled(!active);trainButton.setEnabled(!active);
        analyzeButton.setAlpha(ready?1f:.55f);packButton.setAlpha(ready?1f:.55f);restoreButton.setAlpha(ready?1f:.55f);
    }
    private static String human(long size){
        String[] units={"B","KiB","MiB","GiB","TiB"};double amount=size;int u=0;
        while(amount>=1024&&u<4){amount/=1024;u++;}
        return String.format(Locale.ROOT,"%.2f %s (%,d bytes)",amount,units[u],size);
    }
    private void openSource(){
        Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i,PICK_SOURCE);
    }
    private String name(Uri uri){
        try(Cursor c=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){
            if(c!=null&&c.moveToFirst()&&!c.isNull(0))return c.getString(0);
        }catch(Exception ignored){}
        return "archivo.bin";
    }
    private long size(Uri uri){
        try(Cursor c=getContentResolver().query(uri,new String[]{OpenableColumns.SIZE},null,null,null)){
            if(c!=null&&c.moveToFirst()&&!c.isNull(0))return c.getLong(0);
        }catch(Exception ignored){}
        return -1;
    }
    @Override protected void onActivityResult(int req,int code,Intent data){
        super.onActivityResult(req,code,data);
        if(code!=RESULT_OK || data==null || data.getData()==null)return;
        Uri uri=data.getData();
        if(req==PICK_SOURCE){
            inputUri=uri;inputName=name(uri);inputSize=size(uri);
            try{getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);}catch(Exception ignored){}
            setSelected();progress.setProgress(0);stageLabel.setText("Archivo cargado de almacenamiento Android");
            show("Origen: "+inputName+"\n\nUsa Analizar para identificar su estructura. La fuente es de solo lectura.");
        }else if(req==PICK_DEST){
            if(uri.equals(inputUri)){notice("El destino no puede ser el mismo que el origen");return;}
            Action action=pending;
            if(action==Action.PACK)doPack(uri);else if(action==Action.RESTORE)doRestore(uri);
        }
    }
    private void createDest(String suggestion,Action action){
        pending=action;
        Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/octet-stream");
        i.putExtra(Intent.EXTRA_TITLE,suggestion);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        startActivityForResult(i,PICK_DEST);
    }
    private void chooseMode(){
        if(inputUri==null||active)return;
        String[] options={"Rápido · zlib nivel 1", "Equilibrado · zlib nivel 6", "Máximo · zlib nivel 9 (sin LZMA)"};
        new AlertDialog.Builder(this).setTitle("Nivel de compactación")
            .setSingleChoiceItems(options,1,(dialog,selected)->{
                mode=selected==0?EtherCodec.Level.RAPIDO:selected==1?EtherCodec.Level.EQUILIBRADO:EtherCodec.Level.MAXIMO;
                dialog.dismiss();createDest(inputName+".exf",Action.PACK);
            }).setNegativeButton("Cancelar",null).show();
    }
    private void prepareRestore(){
        if(inputUri==null||active)return;
        runTask("Leyendo cabecera ETHER X…",()->{
            EtherCodec.Header hdr;
            try(InputStream in=getContentResolver().openInputStream(inputUri)) {
                if(in==null)throw new IOException("No se abrió el origen");
                hdr=EtherCodec.readHeader(in);
            }
            final EtherCodec.Header h=hdr;
            runOnUiThread(()->new AlertDialog.Builder(this)
                .setTitle("Reconstruir "+h.filename)
                .setMessage("Tamaño original: "+human(h.size)+".\n\n"+
                  "Se utilizará un archivo temporal local para validar SHA-256 antes de escribir el documento final. " +
                  "Debes tener espacio para el temporal y la salida.")
                .setNegativeButton("Cancelar",null)
                .setPositiveButton("Elegir destino",(di,w)->createDest(h.filename,Action.RESTORE))
                .show());
            return null;
        });
    }
    private interface Task {String perform() throws Exception;}
    private void runTask(String description,Task job){
        if(active)return;
        active=true;setSelected();lastUiUpdate=0;
        stageLabel.setText(description);progress.setIndeterminate(true);
        worker.execute(()->{
            try {
                String result=job.perform();
                runOnUiThread(()->{
                    active=false;setSelected();progress.setIndeterminate(false);progress.setProgress(1000);
                    stageLabel.setText("Operación completada");
                    if(result!=null)show(result);
                });
            }catch(Exception failure){
                String message=failure.getMessage()==null?failure.getClass().getSimpleName():failure.getMessage();
                runOnUiThread(()->{active=false;setSelected();progress.setIndeterminate(false);
                    stageLabel.setText("Operación detenida: archivo fuente intacto");
                    show("ERROR\n"+message+"\n\nNo se modificó el archivo de entrada.");
                });
            }
        });
    }
    private void update(long done,long total) {
        long t=SystemClock.elapsedRealtime();
        if(done!=total&&t-lastUiUpdate<120)return;
        lastUiUpdate=t;
        runOnUiThread(()->{
            progress.setIndeterminate(total<=0);
            if(total>0)progress.setProgress((int)Math.min(1000,done*1000.0/total));
            stageLabel.setText("Procesado: "+human(done)+(total>0?" / "+human(total):""));
        });
    }
    private long knownSize()throws IOException {
        if(inputSize>=0)return inputSize;
        long n=0;
        try(InputStream in=getContentResolver().openInputStream(inputUri)) {
            if(in==null)throw new IOException("No se puede abrir el origen");
            byte[] b=new byte[256*1024];int r;
            while((r=in.read(b))!=-1) if(r>0)n+=r;
        }
        final long count=n;
        runOnUiThread(()->{inputSize=count;setSelected();});
        return count;
    }
    private void cleanup(Uri uri){
        try{DocumentsContract.deleteDocument(getContentResolver(),uri);}catch(Exception ignored){}
    }
    private void doPack(Uri outputUri){
        runTask("Compactando con ETHER X…",()->{
            boolean complete=false;
            try {
                long sourceBytes=knownSize();
                EtherCodec.Report r;
                try(InputStream in=getContentResolver().openInputStream(inputUri);
                    OutputStream out=getContentResolver().openOutputStream(outputUri,"wt")){
                    if(in==null || out==null)throw new IOException("Origen o destino inaccesible");
                    r=EtherCodec.pack(in,out,inputName,sourceBytes,mode,this::update);
                }
                complete=true;
                double saved=sourceBytes==0?0:100d*(1d-r.resultBytes/sourceBytes);
                return "ARCHIVO .EXF CREADO\n\n"+r.explanation()+"\n\n"+
                    String.format(Locale.ROOT,"Reducción de tamaño: %.2f%%\n",saved)+
                    "Se conservó intacto el archivo original.\n"+
                    "Los bloques usan reglas, excepciones, referencias y/o zlib según su menor tamaño.";
            } finally {if(!complete)cleanup(outputUri);}
        });
    }
    private void doRestore(Uri outputUri){
        runTask("Reconstruyendo y verificando SHA-256…",()->{
            File scratch=File.createTempFile("etherx-restore-",".tmp",getCacheDir());
            boolean complete=false;
            try {
                EtherCodec.Report r;
                try(InputStream in=getContentResolver().openInputStream(inputUri);
                    RandomAccessFile temp=new RandomAccessFile(scratch,"rw")){
                    if(in==null)throw new IOException("No se pudo abrir EXF");
                    r=EtherCodec.unpack(in,temp,this::update);
                    temp.getFD().sync();
                }
                try(InputStream recovered=new BufferedInputStream(new FileInputStream(scratch),128*1024);
                    OutputStream out=getContentResolver().openOutputStream(outputUri,"wt")){
                    if(out==null)throw new IOException("El destino no permite escritura");
                    EtherCodec.transfer(recovered,out);
                }
                complete=true;
                return "RECONSTRUCCIÓN COMPLETA\n\n"+r.explanation()+"\n\n"+
                    "SHA-256 validado antes de publicar. Formato de origen conservado.";
            }finally{
                //noinspection ResultOfMethodCallIgnored
                scratch.delete();if(!complete)cleanup(outputUri);
            }
        });
    }
    private void analyze(){
        if(inputUri==null)return;
        runTask("Analizando estructura del archivo…",()->{
            FileInspector.Result report;
            try(InputStream in=getContentResolver().openInputStream(inputUri)) {
                if(in==null)throw new IOException("No se pudo leer el archivo");
                report=FileInspector.analyze(in,inputSize);
            }
            return report.report();
        });
    }
    private void train(){
        runTask("Entrenando circuito de 2 qubits simulado…",()->QuantumToyLab.train().report());
    }
}
