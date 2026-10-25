package com.etherx.filelab;

import android.app.*;
import android.os.*;
import android.os.ParcelFileDescriptor;
import android.content.*;
import android.content.res.ColorStateList;
import android.database.Cursor;
import android.graphics.*;
import android.graphics.drawable.*;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.*;
import android.view.inputmethod.InputMethodManager;
import android.widget.*;

import com.etherx.filelab.core.*;

import java.io.*;
import java.util.Locale;
import java.util.Arrays;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** ETHER X Reverse Lab. Fully native, offline, bounded, read-only inspection. */
public final class MainActivity extends Activity {
    private static final int PICK_SOURCE=100,PICK_DEST=101;
    private static final int BG=0xff080d1b, PANEL=0xff121b30, PANEL2=0xff18243c;
    private static final int AQUA=0xff52ead0, PURPLE=0xffae82ff, BLUE=0xff72aeff, AMBER=0xffffc477, CORAL=0xffff8b9b;
    private static final int INK=0xffedf5ff, MUTED=0xff9aadc7, STROKE=0xff304260;
    private static final int[] TONES={AQUA,PURPLE,BLUE,AMBER,CORAL};
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private Uri inputUri;
    private String inputName;
    private long inputSize=-1;
    private volatile boolean active=false;
    private volatile long lastUiUpdate=0;
    private enum Action{PACK,RESTORE}
    private Action pending;
    private EtherCodec.Level mode=EtherCodec.Level.EQUILIBRADO;
    private TextView selectedLabel,status,reportHeadline,progressPercent;
    private Button chooseButton,analyzeButton,packButton,restoreButton,trainButton;
    private ProgressBar progress;
    private LinearLayout analysisRoot,tabArea,tabBar;
    private DeepInspector.Analysis current;
    private String shareText;
    private String selectedTab="ESTRUCTURA";
    private long hexOffset=0;

    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        buildUi();
        if(state!=null){
            String uri=state.getString("source_uri");
            if(uri!=null){inputUri=Uri.parse(uri);inputName=state.getString("source_name");inputSize=state.getLong("source_size",-1);}
            setSelected();
        }
    }
    @Override public void onSaveInstanceState(Bundle b){
        super.onSaveInstanceState(b);
        if(inputUri!=null){b.putString("source_uri",inputUri.toString());b.putString("source_name",inputName);b.putLong("source_size",inputSize);}
    }
    @Override public void onDestroy(){super.onDestroy();worker.shutdownNow();}
    private int dp(float v){return (int)(v*getResources().getDisplayMetrics().density+.5f);}
    private GradientDrawable fill(int color,float rad){GradientDrawable g=new GradientDrawable();g.setColor(color);g.setCornerRadius(dp(rad));return g;}
    private GradientDrawable border(int color,int line,float rad){GradientDrawable g=fill(color,rad);g.setStroke(dp(1),line);return g;}
    private GradientDrawable grad(int c1,int c2,int line){
        GradientDrawable g=new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{c1,c2});
        g.setCornerRadius(dp(22));if(line!=0)g.setStroke(dp(1),line);return g;
    }
    private TextView tx(String str,int sp,int color,boolean bold){
        TextView t=new TextView(this);t.setText(str);t.setTextColor(color);t.setTextSize(sp);t.setLineSpacing(dp(2),1.12f);
        if(bold)t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        return t;
    }
    private LinearLayout col(){LinearLayout l=new LinearLayout(this);l.setOrientation(1);return l;}
    private LinearLayout row(){LinearLayout l=new LinearLayout(this);l.setOrientation(0);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    private LinearLayout.LayoutParams lp(int w,int h){return new LinearLayout.LayoutParams(w==-1?-1:w==-2?-2:dp(w),h==-1?-1:h==-2?-2:dp(h));}
    private LinearLayout.LayoutParams top(int t){LinearLayout.LayoutParams p=lp(-1,-2);p.topMargin=dp(t);return p;}
    private LinearLayout.LayoutParams weight(float w){return new LinearLayout.LayoutParams(0,-2,w);}
    private LinearLayout box(int surface,int stroke,int radius,int pad){
        LinearLayout l=col();l.setPadding(dp(pad),dp(pad),dp(pad),dp(pad));l.setBackground(border(surface,stroke,radius));return l;
    }
    private Button btn(String title,int color,boolean solid){
        Button b=new Button(this);b.setAllCaps(false);b.setText(title);b.setTextSize(13);b.setTypeface(null,Typeface.BOLD);
        b.setTextColor(solid?BG:color);b.setBackground(border(solid?color:PANEL2,solid?color:STROKE,16));
        b.setMinHeight(dp(50));b.setMinimumHeight(dp(50));b.setPadding(dp(9),dp(9),dp(9),dp(9));
        b.setStateListAnimator(null);return b;
    }
    private LinearLayout iconRow(String symbol,String heading,String sub,int color){
        LinearLayout horizontal=row();
        TextView badge=tx(symbol,23,color,true);badge.setGravity(Gravity.CENTER);
        badge.setBackground(border(PANEL2,color,14));horizontal.addView(badge,lp(50,50));
        LinearLayout c=col();c.setPadding(dp(12),0,0,0);c.addView(tx(heading,15,INK,true));
        c.addView(tx(sub,12,MUTED,false));horizontal.addView(c,new LinearLayout.LayoutParams(0,-2,1));
        return horizontal;
    }
    private void buildUi(){
        ScrollView scroll=new ScrollView(this);scroll.setClipToPadding(false);scroll.setFillViewport(true);scroll.setBackgroundColor(BG);
        LinearLayout root=col();root.setPadding(dp(17),dp(21),dp(17),dp(46));scroll.addView(root);setContentView(scroll);

        LinearLayout brand=row();
        TextView mark=tx("✧",28,AQUA,true);mark.setGravity(Gravity.CENTER);mark.setBackground(grad(0xff1a3651,0xff1b1742,AQUA));
        brand.addView(mark,lp(48,48));
        LinearLayout brandText=col();brandText.setPadding(dp(11),0,0,0);
        brandText.addView(tx("ETHER X",18,INK,true));brandText.addView(tx("R E V E R S E   L A B",10,AQUA,true));
        brand.addView(brandText,new LinearLayout.LayoutParams(0,-2,1));
        TextView version=tx("v0.3",11,AQUA,true);version.setGravity(Gravity.CENTER);version.setBackground(border(0xff133a3b,0xff285a61,20));
        version.setPadding(dp(12),dp(8),dp(12),dp(8));brand.addView(version);root.addView(brand);

        LinearLayout hero=col();hero.setPadding(dp(20),dp(21),dp(20),dp(18));hero.setBackground(grad(0xff162f42,0xff241737,0xff3f5677));
        root.addView(hero,top(23));
        TextView eyebrow=tx("INGENIERÍA INVERSA · LABORATORIO BINARIO",10,AQUA,true);hero.addView(eyebrow);
        hero.addView(tx("Mira dentro\nde cualquier archivo.",26,INK,true),top(10));
        hero.addView(tx("Descompón estructuras, inspecciona cada byte y entiende cómo se construyó.",13,0xffbdd0e4,false),top(10));
        VectorField field=new VectorField();hero.addView(field,top(12));
        LinearLayout heroStats=row();heroStats.setGravity(Gravity.CENTER);
        TextView one=tx("  ◈  OFFSETS",10,AQUA,true),two=tx("  ⌁  ESTRUCTURAS",10,PURPLE,true),three=tx("  ▦  HEX",10,AMBER,true);
        heroStats.addView(one,new LinearLayout.LayoutParams(0,-2,1));heroStats.addView(two,new LinearLayout.LayoutParams(0,-2,1));heroStats.addView(three,new LinearLayout.LayoutParams(0,-2,1));
        hero.addView(heroStats,top(6));

        LinearLayout source=box(PANEL,STROKE,22,17);root.addView(source,top(18));
        source.addView(iconRow("⌁","MATERIAL DE ESTUDIO","Archivo local · lectura segura",BLUE));
        selectedLabel=tx("Ningún archivo elegido",14,INK,true);source.addView(selectedLabel,top(14));
        chooseButton=btn("＋   SELECCIONAR ARCHIVO / ISO",AQUA,true);source.addView(chooseButton,top(14));
        chooseButton.setOnClickListener(v->openSource());

        LinearLayout buttons=row();analyzeButton=btn("⌕  DISECCIONAR",AQUA,true);packButton=btn("▣  COMPACTAR",PURPLE,false);
        LinearLayout.LayoutParams equalA=weight(1),equalB=weight(1);equalB.leftMargin=dp(10);
        buttons.addView(analyzeButton,equalA);buttons.addView(packButton,equalB);root.addView(buttons,top(14));
        LinearLayout bottom=row();restoreButton=btn("↺  RECONSTRUIR",BLUE,false);trainButton=btn("✦  Q-LAB",AMBER,false);
        LinearLayout.LayoutParams equalC=weight(1),equalD=weight(1);equalD.leftMargin=dp(10);
        bottom.addView(restoreButton,equalC);bottom.addView(trainButton,equalD);root.addView(bottom,top(10));
        analyzeButton.setOnClickListener(v->analyze());packButton.setOnClickListener(v->chooseMode());
        restoreButton.setOnClickListener(v->prepareRestore());trainButton.setOnClickListener(v->train());

        LinearLayout state=row();state.setPadding(dp(5),0,dp(5),0);
        status=tx("●  LOCAL · NO SE ENVÍAN ARCHIVOS",11,MUTED,true);
        state.addView(status,new LinearLayout.LayoutParams(0,-2,1));
        progressPercent=tx("",11,AQUA,true);state.addView(progressPercent);root.addView(state,top(22));
        progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);progress.setProgressTintList(ColorStateList.valueOf(AQUA));
        progress.setProgressBackgroundTintList(ColorStateList.valueOf(PANEL2));root.addView(progress,top(6));

        analysisRoot=col();root.addView(analysisRoot,top(20));
        emptyState();
        root.addView(tx("MUNDO X  /  ETHER X     ·     ANÁLISIS SIN EJECUCIÓN",10,0xff6f819d,true),top(28));
        setSelected();
    }
    private void emptyState(){
        analysisRoot.removeAllViews();LinearLayout card=box(PANEL,STROKE,20,18);
        card.addView(iconRow("◇","MESA DE ANÁLISIS","Esperando material para inspección",PURPLE));
        card.addView(tx("Elige un archivo y pulsa DISECCIONAR. Verás su anatomía por offsets, regiones verificadas, entropía y una vista hexadecimal navegable.",13,MUTED,false),top(15));
        analysisRoot.addView(card);
    }
    private void setSelected(){
        if(selectedLabel==null)return;
        selectedLabel.setText(inputUri==null?"Ningún archivo elegido":inputName+"  ·  "+(inputSize>=0?human(inputSize):"Tamaño por determinar"));
        boolean available=!active&&inputUri!=null;
        chooseButton.setEnabled(!active);trainButton.setEnabled(!active);
        analyzeButton.setEnabled(available);packButton.setEnabled(available);restoreButton.setEnabled(available);
        analyzeButton.setAlpha(available?1:.5f);packButton.setAlpha(available?1:.5f);restoreButton.setAlpha(available?1:.5f);
    }
    private static String human(long bytes){
        if(bytes<0)return "desconocido";String[] units={"B","KiB","MiB","GiB","TiB"};double v=bytes;int i=0;
        while(v>=1024&&i<4){v/=1024;i++;}return String.format(Locale.ROOT,"%.2f %s",v,units[i]);
    }
    private void note(String msg){Toast.makeText(this,msg,Toast.LENGTH_LONG).show();}
    private void openSource(){
        Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i,PICK_SOURCE);
    }
    private String name(Uri uri){
        try(Cursor c=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){
            if(c!=null&&c.moveToFirst()&&!c.isNull(0))return c.getString(0);
        }catch(Exception ignored){}return "archivo.bin";
    }
    private long size(Uri uri){
        try(Cursor c=getContentResolver().query(uri,new String[]{OpenableColumns.SIZE},null,null,null)){
            if(c!=null&&c.moveToFirst()&&!c.isNull(0))return c.getLong(0);
        }catch(Exception ignored){}return -1;
    }
    @Override protected void onActivityResult(int req,int code,Intent data){
        super.onActivityResult(req,code,data);
        if(code!=RESULT_OK||data==null||data.getData()==null)return;
        Uri uri=data.getData();
        if(req==PICK_SOURCE){
            inputUri=uri;inputName=name(uri);inputSize=size(uri);current=null;hexOffset=0;
            try{getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);}catch(Exception ignored){}
            setSelected();progress.setProgress(0);status.setText("●  FUENTE ABIERTA · SOLO LECTURA");
            emptyState();
        }else if(req==PICK_DEST){
            if(uri.equals(inputUri)){note("El destino debe ser distinto del original");return;}
            Action requested=pending;
            if(requested==Action.PACK)doPack(uri);else if(requested==Action.RESTORE)doRestore(uri);
        }
    }
    private void createDest(String suggestion,Action action){
        pending=action;
        Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/octet-stream");
        i.putExtra(Intent.EXTRA_TITLE,suggestion);i.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        startActivityForResult(i,PICK_DEST);
    }
    private void chooseMode(){
        if(inputUri==null||active)return;
        String[] choices={"Rápido · zlib 1","Equilibrado · zlib 6","Máximo · zlib 9"};
        new AlertDialog.Builder(this).setTitle("Selecciona la compactación")
            .setSingleChoiceItems(choices,1,(dialog,selected)->{
                mode=selected==0?EtherCodec.Level.RAPIDO:selected==1?EtherCodec.Level.EQUILIBRADO:EtherCodec.Level.MAXIMO;
                dialog.dismiss();createDest(inputName+".exf",Action.PACK);
            }).setNegativeButton("Cancelar",null).show();
    }
    private void prepareRestore(){
        if(inputUri==null||active)return;
        runTask("LEYENDO CONTENEDOR",()->{
            EtherCodec.Header h;
            try(InputStream in=getContentResolver().openInputStream(inputUri)){
                if(in==null)throw new IOException("No se pudo abrir el documento");h=EtherCodec.readHeader(in);
            }
            runOnUiThread(()->new AlertDialog.Builder(this).setTitle("Reconstruir "+h.filename)
                .setMessage("Origen EXF → "+human(h.size)+" recuperados. Se crea un temporal privado, se comprueba SHA-256 y después se copia. Se necesita espacio para ambas copias.")
                .setNegativeButton("Cancelar",null)
                .setPositiveButton("Elegir destino",(d,w)->createDest(h.filename,Action.RESTORE)).show());
            return null;
        });
    }
    private static final class Result {
        final String message;final DeepInspector.Analysis analysis;
        Result(String s,DeepInspector.Analysis a){message=s;analysis=a;}
    }
    private interface Job {Result execute()throws Exception;}
    private void runTask(String description,Job job){
        if(active)return;active=true;lastUiUpdate=0;setSelected();
        progress.setIndeterminate(true);progressPercent.setText("");status.setText("◉  "+description);
        worker.execute(()->{
            try{
                Result r=job.execute();
                runOnUiThread(()->{
                    active=false;setSelected();progress.setIndeterminate(false);progress.setProgress(1000);progressPercent.setText("100%");
                    status.setText("●  OPERACIÓN FINALIZADA");
                    if(r!=null){if(r.analysis!=null){current=r.analysis;shareText=r.analysis.report();renderAnalysis();}
                        else if(r.message!=null){shareText=r.message;showMessage(r.message);}}
                });
            }catch(Exception error){
                String msg=error.getMessage()==null?error.getClass().getSimpleName():error.getMessage();
                runOnUiThread(()->{
                    active=false;setSelected();progress.setIndeterminate(false);status.setText("●  OPERACIÓN DETENIDA");
                    showMessage("ERROR DE PROCESAMIENTO\n\n"+msg+"\n\nEl archivo original no fue modificado.");
                });
            }
        });
    }
    private void update(long done,long total){
        long now=SystemClock.elapsedRealtime();if(done!=total&&now-lastUiUpdate<170)return;lastUiUpdate=now;
        runOnUiThread(()->{progress.setIndeterminate(total<=0);
            if(total>0){int v=(int)Math.min(1000,done*1000.0/total);progress.setProgress(v);progressPercent.setText((v/10)+"%");}
            status.setText("◉  PROCESANDO "+human(done)+(total>0?" / "+human(total):""));
        });
    }
    private long knownSize()throws IOException{
        if(inputSize>=0)return inputSize;
        long n=0;try(InputStream in=getContentResolver().openInputStream(inputUri)){
            if(in==null)throw new IOException("No se puede abrir el archivo");
            byte[] chunk=new byte[131072];int read;
            while((read=in.read(chunk))!=-1)if(read>0)n+=read;
        }
        inputSize=n;return n;
    }
    private void cleanup(Uri uri){try{DocumentsContract.deleteDocument(getContentResolver(),uri);}catch(Exception ignored){}}
    private void doPack(Uri dst){
        runTask("CREANDO ETHER X",()->{
            boolean done=false;try{
                long len=knownSize();EtherCodec.Report r;
                try(InputStream in=getContentResolver().openInputStream(inputUri);
                    OutputStream out=getContentResolver().openOutputStream(dst,"wt")){
                    if(in==null||out==null)throw new IOException("El proveedor de archivos rechazó la operación");
                    r=EtherCodec.pack(in,out,inputName,len,mode,this::update);
                }
                done=true;
                String reduction=String.format(Locale.ROOT,"%.2f%%",len==0?0:100*(1-(double)r.resultBytes/len));
                return new Result("COMPACTACIÓN REVERSIBLE\n\n"+r.explanation()+"\n\nReducción respecto al original: "+reduction+
                    "\n\nModo: "+mode+". Archivo fuente intacto.\nLos bytes se restauran y verifican con SHA-256.",null);
            }finally{if(!done)cleanup(dst);}
        });
    }
    private void doRestore(Uri dst){
        runTask("RECONSTRUYENDO ARCHIVO",()->{
            File tmp=File.createTempFile("etherx-recovery-",".tmp",getCacheDir());boolean done=false;
            try{
                EtherCodec.Report r;
                try(InputStream in=getContentResolver().openInputStream(inputUri);RandomAccessFile raw=new RandomAccessFile(tmp,"rw")){
                    if(in==null)throw new IOException("No se pudo abrir el EXF");
                    r=EtherCodec.unpack(in,raw,this::update);raw.getFD().sync();
                }
                try(InputStream in=new BufferedInputStream(new FileInputStream(tmp),131072);
                    OutputStream out=getContentResolver().openOutputStream(dst,"wt")){
                    if(out==null)throw new IOException("Destino inaccesible");EtherCodec.transfer(in,out);
                }
                done=true;
                return new Result("RECONSTRUCCIÓN TERMINADA\n\n"+r.explanation()+"\n\nSHA-256 validado antes de escribir el resultado. Original sin cambios.",null);
            }finally{tmp.delete();if(!done)cleanup(dst);}
        });
    }
    /** Bounded random reads. No ISO-sized RAM buffer and no network permission. */
    private byte[] readRange(long offset,int count)throws IOException{
        if(inputUri==null||offset<0||count<0||count>512*1024)throw new IOException("Intervalo inválido");
        if(count==0)return new byte[0];
        IOException directFailure=null;
        try(ParcelFileDescriptor file=getContentResolver().openFileDescriptor(inputUri,"r")){
            if(file==null)throw new IOException("Descriptor de archivo inaccesible");
            try(FileInputStream stream=new FileInputStream(file.getFileDescriptor())){
                stream.getChannel().position(offset);
                byte[] found=new byte[count];int filled=0;
                while(filled<count){int r=stream.read(found,filled,count-filled);if(r<0)break;if(r==0)continue;filled+=r;}
                return Arrays.copyOf(found,filled);
            }
        }catch(IOException error){directFailure=error;}
        // Some document providers expose pipes rather than seekable descriptors.
        // Restrict slow stream skipping instead of scanning multi-GB ISOs by accident.
        if(offset>128L*1024*1024)throw new IOException("El proveedor no permite lectura aleatoria en offsets altos: "+directFailure.getMessage());
        try(InputStream stream=getContentResolver().openInputStream(inputUri)){
            if(stream==null)throw new IOException("Origen inaccesible");
            byte[] discard=new byte[32768];long remaining=offset;
            while(remaining>0){int r=stream.read(discard,0,(int)Math.min(discard.length,remaining));
                if(r<0)return new byte[0];remaining-=r;}
            byte[] bytes=new byte[count];int filled=0;
            while(filled<count){int r=stream.read(bytes,filled,count-filled);if(r<0)break;filled+=r;}
            return Arrays.copyOf(bytes,filled);
        }
    }

    private void analyze(){
        if(inputUri==null||active)return;
        runTask("DISECCIONANDO BINARIO",()->{
            DeepInspector.Analysis r;
            try(InputStream in=getContentResolver().openInputStream(inputUri)){
                if(in==null)throw new IOException("No se pudo abrir el archivo");
                r=DeepInspector.inspect(in,inputSize);
            }
            if(inputSize>r.sample.length){
                try{r=DeepInspector.enrich(r,this::readRange);}
                catch(IOException failure){r=DeepInspector.withObservation(r,"Lectura extendida omitida: "+failure.getMessage());}
            }
            return new Result(null,r);
        });
    }
    private void train(){
        runTask("Q-LAB · SIMULACIÓN",()->new Result("Q-LAB · DOS QUBITS SIMULADOS\n\n"+
            QuantumToyLab.train().report()+"\n\nExperimento independiente. No comprime con hardware cuántico ni aprende de tus archivos reales.",null));
    }
    private void showMessage(String report){
        analysisRoot.removeAllViews();LinearLayout card=box(PANEL,STROKE,20,18);
        card.addView(iconRow("▣","REPORTE DE OPERACIÓN","Registros del motor ETHER X",AQUA));
        TextView t=tx(report,14,INK,false);t.setTextIsSelectable(true);card.addView(t,top(20));analysisRoot.addView(card);
        Button share=btn("↗   COMPARTIR REPORTE",BLUE,false);share.setOnClickListener(v->share());analysisRoot.addView(share,top(12));
    }
    private void renderAnalysis(){
        if(current==null){emptyState();return;}
        DeepInspector.Analysis a=current;
        analysisRoot.removeAllViews();
        LinearLayout head=box(PANEL,STROKE,22,17);
        head.addView(tx("DIAGNÓSTICO ESTRUCTURAL",11,AQUA,true));
        reportHeadline=tx(a.format,23,INK,true);head.addView(reportHeadline,top(9));
        head.addView(tx(a.explanation,13,MUTED,false),top(8));
        LinearLayout stats=row();
        addStat(stats,String.format(Locale.ROOT,"%.2f",a.entropy),"ENTROPÍA",PURPLE);
        addStat(stats,String.valueOf(a.regions.size()),"REGIONES",AQUA);
        addStat(stats,String.format(Locale.ROOT,"%.0f%%",a.zeros*100),"CEROS",AMBER);
        head.addView(stats,top(16));analysisRoot.addView(head);

        LinearLayout nav=col();nav.setBackground(border(PANEL,STROKE,19));nav.setPadding(dp(6),dp(7),dp(6),dp(7));
        analysisRoot.addView(nav,top(14));tabBar=row();HorizontalScrollView hs=new HorizontalScrollView(this);hs.setHorizontalScrollBarEnabled(false);
        hs.addView(tabBar);nav.addView(hs);tabArea=col();nav.addView(tabArea,top(12));
        renderTabs();
        Button export=btn("↗   EXPORTAR INFORME COMPLETO",BLUE,false);export.setOnClickListener(v->share());
        analysisRoot.addView(export,top(12));
    }
    private void addStat(LinearLayout parent,String value,String label,int tone){
        LinearLayout s=col();s.setGravity(Gravity.CENTER);s.addView(tx(value,19,tone,true));
        s.addView(tx(label,10,MUTED,true),top(3));parent.addView(s,new LinearLayout.LayoutParams(0,-2,1));
    }
    private void renderTabs(){
        tabBar.removeAllViews();
        String[] names={"ESTRUCTURA","HEX","MAPA","INDICIOS"};
        for(String n:names){
            final boolean selected=selectedTab.equals(n);
            TextView tab=tx(n,11,selected?BG:MUTED,true);
            tab.setGravity(Gravity.CENTER);tab.setPadding(dp(12),dp(12),dp(12),dp(12));
            tab.setBackground(border(selected?AQUA:PANEL,selected?AQUA:PANEL,14));
            LinearLayout.LayoutParams p=lp(-2,-2);p.rightMargin=dp(5);tabBar.addView(tab,p);
            tab.setOnClickListener(v->{selectedTab=n;renderTabs();});
        }
        tabArea.removeAllViews();
        if(current==null)return;
        switch(selectedTab){
            case "HEX":renderHex();break;
            case "MAPA":renderMap();break;
            case "INDICIOS":renderEvidence();break;
            default:renderRegions();break;
        }
    }
    private void renderRegions(){
        DeepInspector.Analysis a=current;
        tabArea.addView(tx(a.regions.size()+" REGIONES DETECTADAS",12,AQUA,true),top(7));
        tabArea.addView(tx("Toca una estructura para abrir sus bytes y su interpretación.",12,MUTED,false),top(5));
        int i=0;
        for(DeepInspector.Region region:a.regions){
            final int tone=TONES[i++%TONES.length];
            LinearLayout card=box(PANEL2,STROKE,15,12);
            LinearLayout nameRow=row();
            TextView bullet=tx("◆",13,tone,true);nameRow.addView(bullet);
            TextView name=tx(region.name,13,INK,true);name.setPadding(dp(9),0,dp(7),0);
            nameRow.addView(name,new LinearLayout.LayoutParams(0,-2,1));
            nameRow.addView(tx("›",22,tone,true));card.addView(nameRow);
            card.addView(tx(region.range()+"   ·   "+(region.confirmed?"VERIFICADO":"INDICIO"),10,tone,true),top(4));
            TextView detail=tx(region.detail,12,MUTED,false);detail.setMaxLines(3);card.addView(detail,top(6));
            tabArea.addView(card,top(7));
            card.setOnClickListener(v->openRegion(region));
        }
        tabArea.addView(tx("Los offsets no presentes en la muestra no se interpretan como si hubieran sido leídos.",12,MUTED,false),top(12));
    }
    private void openRegion(DeepInspector.Region r){
        String about=r.name+"\n\n"+r.detail+"\n\nOffset: 0x"+Long.toHexString(r.offset).toUpperCase(Locale.ROOT)+
            "\nLongitud: "+r.length+" bytes\nEstado: "+(r.confirmed?"estructura verificada":"indicio sin confirmar")+
            "\n\nHEX + ASCII (muestra):\n"+current.hex(r.offset,11);
        TextView info=tx(about,13,INK,false);info.setTypeface(Typeface.MONOSPACE);info.setTextIsSelectable(true);
        ScrollView sc=new ScrollView(this);sc.setPadding(dp(13),dp(7),dp(13),dp(7));sc.addView(info);
        new AlertDialog.Builder(this).setTitle("MICROSCOPIO · "+r.group).setView(sc)
            .setNegativeButton("Cerrar",null)
            .setPositiveButton("Abrir en HEX",(d,w)->{hexOffset=Math.max(0,r.offset);selectedTab="HEX";renderTabs();}).show();
    }
    private void renderHex(){
        LinearLayout layout=col();layout.setPadding(dp(8),dp(7),dp(8),dp(8));tabArea.addView(layout);
        layout.addView(tx("MICROSCOPIO HEXADECIMAL",12,AQUA,true));
        layout.addView(tx("Cada línea representa 16 bytes originales. Izquierda: dirección; centro: hexadecimal; derecha: caracteres ASCII.",12,MUTED,false),top(6));
        LinearLayout controls=row();
        EditText off=new EditText(this);off.setSingleLine(true);off.setTextSize(13);off.setTextColor(INK);
        off.setHintTextColor(MUTED);off.setHint("0x00000000");off.setInputType(InputType.TYPE_CLASS_TEXT);
        off.setText("0x"+Long.toHexString(hexOffset).toUpperCase(Locale.ROOT));
        controls.addView(off,new LinearLayout.LayoutParams(0,dp(51),1));
        Button jump=btn("IR",AQUA,true);controls.addView(jump,lp(65,49));layout.addView(controls,top(8));
        boolean outside=hexOffset>=current.sample.length;
        TextView code=tx(outside?"Consultando offset real 0x"+Long.toHexString(hexOffset)+"…":current.hex(hexOffset,24),10,0xffcbf6ef,false);
        code.setTypeface(Typeface.MONOSPACE);code.setTextIsSelectable(true);
        code.setHorizontallyScrolling(true);
        code.setPadding(dp(10),dp(14),dp(10),dp(14));code.setBackground(border(0xff081522,STROKE,14));
        HorizontalScrollView hs=new HorizontalScrollView(this);hs.addView(code);layout.addView(hs,top(8));
        if(outside){
            long at=hexOffset;
            worker.execute(()->{
                try{byte[] extra=readRange(at,384);String hex=DeepInspector.hexDump(extra,at,24);
                    runOnUiThread(()->{if(current!=null&&"HEX".equals(selectedTab)&&hexOffset==at)code.setText(hex);});
                }catch(IOException e){runOnUiThread(()->{if(hexOffset==at)code.setText("No se puede leer offset: "+e.getMessage());});}
            });
        }
        jump.setOnClickListener(v->{
            try{
                String raw=off.getText().toString().trim();long target;
                if(raw.toLowerCase(Locale.ROOT).startsWith("0x"))target=Long.parseLong(raw.substring(2),16);
                else target=Long.parseLong(raw);
                if(target<0||(current.totalBytes>=0&&target>=current.totalBytes)){
                    note("Dirección fuera del tamaño del archivo");return;
                }
                hexOffset=target;renderTabs();
            }catch(NumberFormatException e){note("Escribe offset decimal o 0x hexadecimal");}
        });
        LinearLayout row=row();Button back=btn("←  ANTERIOR",BLUE,false),next=btn("SIGUIENTE  →",AQUA,false);
        row.addView(back,weight(1));LinearLayout.LayoutParams n=weight(1);n.leftMargin=dp(7);row.addView(next,n);
        layout.addView(row,top(10));
        back.setOnClickListener(v->{hexOffset=Math.max(0,hexOffset-384);renderTabs();});
        next.setOnClickListener(v->{
            long max=current.totalBytes>=0?current.totalBytes-1:current.sample.length-1;
            hexOffset=Math.min(Math.max(0,max),hexOffset+384);renderTabs();
        });
        layout.addView(tx("Muestra inicial: "+human(current.sample.length)+". Los offsets posteriores se solicitan directamente al proveedor de archivos, sin cargar toda la ISO en RAM.",12,MUTED,false),top(13));
    }
    private void renderMap(){
        LinearLayout panel=col();panel.setPadding(dp(10),dp(8),dp(10),dp(8));tabArea.addView(panel);
        panel.addView(tx("MAPA DE ENTROPÍA",12,AQUA,true));
        panel.addView(tx("64 ventanas consecutivas en la muestra. Bajo: repetición probable; alto: distribución más diversa. No prueba cifrado.",12,MUTED,false),top(8));
        panel.addView(new EntropyMap(current.landscape),top(10));
        LinearLayout spread=row();spread.addView(tx("Baja variedad",11,AQUA,false),weight(1));
        TextView high=tx("Alta variedad",11,PURPLE,false);high.setGravity(Gravity.RIGHT);spread.addView(high,weight(1));panel.addView(spread);
        panel.addView(tx("COBERTURA",12,AQUA,true),top(20));
        panel.addView(tx("Analizados: "+human(current.sample.length)+"  /  Origen: "+human(current.totalBytes)+
            "\n"+(current.totalBytes>current.sample.length?"Muestra parcial: no representa obligatoriamente todo el archivo.":"El origen cabe dentro de la muestra disponible."),12,MUTED,false),top(6));
        panel.addView(tx("DISTRIBUCIÓN",12,AQUA,true),top(20));
        panel.addView(tx("Ceros: "+String.format(Locale.ROOT,"%.2f%%",current.zeros*100)+
            "\nValor más frecuente: "+String.format(Locale.ROOT,"%.2f%%",current.repeated*100)+
            "\nEntropía: "+String.format(Locale.ROOT,"%.3f bits/byte",current.entropy),13,INK,false),top(8));
    }
    private void renderEvidence(){
        LinearLayout panel=col();panel.setPadding(dp(8),dp(8),dp(8),dp(8));tabArea.addView(panel);
        panel.addView(tx("EVIDENCIA Y LIMITACIONES",12,AQUA,true));
        for(String note:current.observations){
            LinearLayout c=box(PANEL2,STROKE,12,11);
            c.addView(tx("◎  "+note,12,INK,false));panel.addView(c,top(7));
        }
        panel.addView(tx("CADENAS ASCII EN LA MUESTRA",12,PURPLE,true),top(18));
        panel.addView(tx("Estas cadenas no revelan automáticamente su significado; son evidencias textuales con posición.",12,MUTED,false),top(5));
        if(current.strings.isEmpty())panel.addView(tx("No se detectaron cadenas ASCII largas en la muestra.",12,MUTED,false),top(10));
        else for(String value:current.strings){TextView t=tx(value,11,INK,false);t.setTypeface(Typeface.MONOSPACE);panel.addView(t,top(7));}
        panel.addView(tx("Importante: se inspeccionan hasta 3 MiB iniciales. No sustituye decompilación, reconstrucción de formatos propietarios ni extracción de cifrados.",12,AMBER,false),top(19));
    }
    private void share(){
        if(shareText==null){note("No hay informe disponible");return;}
        Intent send=new Intent(Intent.ACTION_SEND);send.setType("text/plain");send.putExtra(Intent.EXTRA_TEXT,shareText);
        startActivity(Intent.createChooser(send,"Compartir informe ETHER X"));
    }
    private final class VectorField extends View{
        Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        VectorField(){super(MainActivity.this);setMinimumHeight(dp(120));}
        @Override protected void onMeasure(int w,int h){setMeasuredDimension(MeasureSpec.getSize(w),dp(118));}
        @Override protected void onDraw(Canvas c){
            super.onDraw(c);float w=getWidth(),h=getHeight();
            paint.setStrokeWidth(dp(.7f));paint.setStyle(Paint.Style.STROKE);
            paint.setColor(0x2252ead0);
            for(int x=0;x<16;x++){float xx=w*x/15;c.drawLine(xx,0,xx,h,paint);}
            for(int y=0;y<5;y++){float yy=h*y/4;c.drawLine(0,yy,w,yy,paint);}
            for(int layer=0;layer<3;layer++){
                paint.setColor(TONES[layer]);paint.setStrokeWidth(dp(layer==0?2.5f:1.2f));
                Path path=new Path();
                for(int j=0;j<=128;j++){
                    float x=w*j/128f, phase=j/10f+layer*1.3f;
                    float y=(float)(h*.49+Math.sin(phase)*h*(.12+.055*layer)+Math.cos(phase*.44)*h*.10);
                    if(j==0)path.moveTo(x,y);else path.lineTo(x,y);
                }
                c.drawPath(path,paint);
            }
            paint.setStyle(Paint.Style.FILL);paint.setColor(AQUA);
            c.drawCircle(w*.62f,h*.57f,dp(4),paint);
        }
    }
    private final class EntropyMap extends View{
        final float[] points;Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
        EntropyMap(float[] map){super(MainActivity.this);points=map;setMinimumHeight(dp(145));}
        @Override protected void onMeasure(int w,int h){setMeasuredDimension(MeasureSpec.getSize(w),dp(148));}
        @Override protected void onDraw(Canvas c){
            super.onDraw(c);float w=getWidth(),h=getHeight();
            p.setColor(0xff0b1729);c.drawRoundRect(0,0,w,h,dp(12),dp(12),p);
            for(int i=0;i<4;i++){p.setColor(0xff23304a);p.setStrokeWidth(dp(1));float y=h*(i+1)/5f;c.drawLine(dp(7),y,w-dp(7),y,p);}
            float step=(w-dp(17))/points.length;
            for(int i=0;i<points.length;i++){
                float x=dp(8)+i*step, v=Math.max(.03f,Math.min(1f,points[i]));
                p.setColor(v>.75?PURPLE:v>.42?BLUE:AQUA);
                c.drawRoundRect(x,h-dp(9)-v*(h-dp(22)),x+Math.max(1f,step*.68f),h-dp(9),dp(2),dp(2),p);
            }
        }
    }
}