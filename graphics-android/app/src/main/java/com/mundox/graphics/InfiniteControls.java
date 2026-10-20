package com.mundox.graphics;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Typeface;
import android.text.InputType;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** Shared Android controls. Callbacks enqueue changes on the active world's GL thread. */
public final class InfiniteControls {
    public interface AddressSelection {void selected(String first,String second);}
    public interface Command {void execute(String script);}
    private InfiniteControls(){}

    public static final class ModeChoice {
        /** 0 legacy finite; 1 Bell; 2 zero; 3 Ether cluster. */
        public int mode=3;
        public final Button button;
        public ModeChoice(Activity activity,EditText finiteQubits){
            button=new Button(activity);button.setAllCaps(false);button.setText("Núcleo: Ether ▾");button.setTag("new_project_backend");
            finiteQubits.setEnabled(false);finiteQubits.setAlpha(.4f);
            button.setOnClickListener(v->new AlertDialog.Builder(activity).setTitle("Núcleo del nuevo mundo")
                .setSingleChoiceItems(new String[]{"Finito · cantidad de qubits","Infinito · pares Bell","Infinito · fondo cero","Ether · fondo entrelazado"},mode,(dialog,which)->{
                    mode=which;button.setText(new String[]{"Núcleo: finito ▾","Núcleo: Infinito · Bell ▾","Núcleo: Infinito · cero ▾","Núcleo: Ether ▾"}[which]);
                    finiteQubits.setEnabled(mode==0);finiteQubits.setAlpha(mode==0?1f:.4f);dialog.dismiss();
                }).show());
        }
        public int background(){return mode==1?NativeCore.BELL_PAIRS:NativeCore.ZERO;}
    }

    public static void addresses(Activity activity,String first,String second,AddressSelection selection){
        LinearLayout form=column(activity);
        TextView explanation=new TextView(activity);explanation.setText("Elige las direcciones de las puertas y de la imagen. Se observan XX, ZZ, YY, X(A) y Z(A), sin modificar el estado.");
        form.addView(explanation);
        EditText a=addressField(activity,"Dirección A",first,"infinite_address_a"),b=addressField(activity,"Dirección B · destino CX/CZ",second,"infinite_address_b");
        form.addView(a);form.addView(b);
        Button partner=new Button(activity);partner.setAllCaps(false);partner.setText("Usar pareja Bell de A");partner.setTag("infinite_partner");
        partner.setOnClickListener(v->{try{b.setText(InfiniteProgram.partner(a.getText().toString()));}catch(Exception error){a.setError(error.getMessage());}});form.addView(partner);
        Button far=new Button(activity);far.setAllCaps(false);far.setText("Ir a 10¹⁰⁰ y su pareja");far.setTag("infinite_far");
        far.setOnClickListener(v->{a.setText(InfiniteProgram.FAR_ADDRESS);b.setText(InfiniteProgram.partner(InfiniteProgram.FAR_ADDRESS));});form.addView(far);
        ScrollView scroll=new ScrollView(activity);scroll.addView(form);
        AlertDialog dialog=new AlertDialog.Builder(activity).setTitle("Direcciones del núcleo infinito").setView(scroll)
            .setNegativeButton("Cancelar",null).setPositiveButton("Observar",null).create();
        dialog.setOnShowListener(ignored->{Button apply=dialog.getButton(AlertDialog.BUTTON_POSITIVE);apply.setTag("infinite_observe");
            apply.setOnClickListener(v->{try{
                String qa=InfiniteProgram.address(a.getText().toString()),qb=InfiniteProgram.address(b.getText().toString());
                InfiniteProgram.pair(qa,qb);selection.selected(qa,qb);dialog.dismiss();
            }catch(Exception error){b.setError(error.getMessage());}});
        });dialog.show();
    }
    private static EditText addressField(Activity activity,String hint,String value,String tag){
        EditText field=new EditText(activity);field.setHint(hint);field.setText(value);field.setTag(tag);
        field.setContentDescription(hint);field.setSingleLine(true);field.setSelectAllOnFocus(true);
        // Text stays a decimal string; no numeric conversion or silent length filter.
        field.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        field.setTypeface(Typeface.MONOSPACE);return field;
    }
    private static LinearLayout column(Activity activity){
        LinearLayout form=new LinearLayout(activity);form.setOrientation(LinearLayout.VERTICAL);
        int pad=(int)(20*activity.getResources().getDisplayMetrics().density+.5f);form.setPadding(pad,pad/2,pad,pad/2);return form;
    }
    public static void actions(Activity activity,NativeCore core,String address,Command command){
        String q=InfiniteProgram.address(address);
        new AlertDialog.Builder(activity).setTitle("Núcleo infinito").setItems(new String[]{
            "Estado y recursos activos","Medir Z en A","Compactar componentes que vuelven al fondo",
            "Reiniciar con fondo cero","Reiniciar con pares Bell","Ejecutar programa .xi"},(dialog,which)->{
                if(which==0){NativeCore.InfiniteStats s=core.infiniteStats();
                    new AlertDialog.Builder(activity).setTitle("Infinito · "+s.backgroundName())
                        .setMessage("Direcciones decimales sin tamaño total finito. El fondo se calcula por reglas.\n\n"+
                            s.activeQubits+" qubits activos · "+s.components+" componentes\n"+
                            "Componente mayor: "+s.largestComponent+" qubits\n"+
                            "Tableau activo: "+s.tableauBytes+" bytes\nTexto de direcciones: "+s.addressBytes+" bytes\n"+
                            "Reglas Ether: "+core.etherStats().rules+"\nRevisión: "+Long.toUnsignedString(s.revision)+"\n\n"+
                            "Estos bytes describen los datos lógicos; la aplicación también necesita memoria para índices, copias y gráficos. " +
                            "Esta sesión admite hasta 4096 qubits activos, 1024 por componente y 4096 dígitos por dirección.")
                        .setPositiveButton("Cerrar",null).show();
                }else if(which==1)command.execute("measure "+q+"\n");
                else if(which==2)command.execute("compact\n");
                else if(which==3||which==4)new AlertDialog.Builder(activity).setTitle("Reiniciar núcleo infinito")
                    .setMessage("Se sustituirá el estado cuántico por el fondo "+(which==3?"cero":"de pares Bell")+". Se conserva el mundo y el programa observado.")
                    .setNegativeButton("Cancelar",null).setPositiveButton("Reiniciar",(d,w)->command.execute("background "+(which==3?"zero":"bell")+"\n")).show();
                else program(activity,q,command);
            }).show();
    }
    private static void program(Activity activity,String address,Command command){
        LinearLayout form=column(activity);TextView help=new TextView(activity);
        help.setText("Una instrucción por línea: h, s, x, y, z, cx, cz, h_all, cz_neighbors, expect, probz, measure, compact. Las reglas globales y las puertas locales actúan sobre el mismo estado. La petición completa se aplica de forma atómica.");form.addView(help);
        EditText input=new EditText(activity);input.setTypeface(Typeface.MONOSPACE);input.setMinLines(4);input.setTag("infinite_script");
        input.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_MULTI_LINE|InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        input.setText("expect X:"+address+" X:"+InfiniteProgram.partner(address)+"\n");form.addView(input);
        new AlertDialog.Builder(activity).setTitle("Programa infinito").setView(form).setNegativeButton("Cancelar",null)
            .setPositiveButton("Ejecutar",(d,w)->command.execute(input.getText().toString())).show();
    }
    public static void result(Activity activity,String result){
        if(result==null||result.trim().length()==0)return;
        TextView text=new TextView(activity);text.setText(result);text.setTextIsSelectable(true);
        int pad=(int)(20*activity.getResources().getDisplayMetrics().density+.5f);text.setPadding(pad,pad/2,pad,pad/2);
        ScrollView scroll=new ScrollView(activity);scroll.addView(text);
        new AlertDialog.Builder(activity).setTitle("Resultado del núcleo infinito").setView(scroll).setPositiveButton("Cerrar",null).show();
    }
}
