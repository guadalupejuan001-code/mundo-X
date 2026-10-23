package com.mundox.worldlink.phone;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/** Classical system-identification experiment. No Android or simulated sensor inputs. */
public final class FieldExperiment {
    public interface RuleStore {void save(double[] parameters);double[] load();String name();}
    private static final class LocalRuleStore implements RuleStore {
        private double[] values;
        public void save(double[] p){values=p.clone();}
        public double[] load(){return values.clone();}
        public String name(){return "in_process_test_store";}
    }
    public enum Stage { TRAIN, CALIBRATE, TEST, DONE, ABORTED }
    public static final long TRAIN_NS=20_000_000_000L, CAL_NS=10_000_000_000L,
            TOTAL_NS=50_000_000_000L, MAX_PAIR_NS=50_000_000L;
    public static final int MIN_TRAIN=100, MIN_CAL=50, MIN_TEST=100, MAX_RECORDS=650;
    private static final String HEADER="stage,watch_magnetic_ns,watch_rotation_ns,sequence,qx,qy,qz,qw,mx_uT,my_uT,mz_uT,accuracy,pred_x_uT,pred_y_uT,pred_z_uT,error_uT,constant_error_uT,persistence_error_uT,empirical_radius_uT\n";

    public static final class Sample {
        public final String session;
        public final long seq, mt, qt;
        public final double[] q, m;
        public final int accuracy;
        public Sample(String session,long seq,long mt,long qt,double[] q,double[] m,int accuracy) {
            if(session==null || session.length()<6 || session.length()>80 || seq<0 || mt<=0 || qt<=0
                    || qt>mt || mt-qt>MAX_PAIR_NS || q==null || q.length!=4 || m==null || m.length!=3)
                throw new IllegalArgumentException("Muestra incompleta o sensores desincronizados");
            double n=0;
            for(double x:q) { if(!Double.isFinite(x))throw new IllegalArgumentException("Orientación inválida"); n+=x*x; }
            if(Math.abs(n-1)>0.02)throw new IllegalArgumentException("Cuaternión no unitario");
            for(double x:m)if(!Double.isFinite(x)||Math.abs(x)>5000)
                throw new IllegalArgumentException("Campo fuera de rango");
            this.session=session;this.seq=seq;this.mt=mt;this.qt=qt;this.q=q.clone();this.m=m.clone();
            for(int i=0;i<4;i++)this.q[i]/=Math.sqrt(n);
            this.accuracy=accuracy;
        }
        // Android game rotation: device -> arbitrary inertial reference, never magnetic north.
        public double[][] rotation() {
            double x=q[0],y=q[1],z=q[2],w=q[3];
            return new double[][]{
                {1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
                {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
                {2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
        }
    }

    public static final class Model {
        // m_device = transpose(R) * B_reference + b_device. Six fitted coefficients.
        private final double[] p, mean;
        public final double condition;
        private Model(double[] p,double[] mean,double condition){this.p=p.clone();this.mean=mean.clone();this.condition=condition;}
        public double[] parameters(){return p.clone();}
        public double[] predict(Sample s) {
            return predict(s,p);
        }
        private double[] predict(Sample s,double[] rule) {
            double[][] r=s.rotation();double[] y=new double[3];
            for(int a=0;a<3;a++){y[a]=rule[a+3];for(int k=0;k<3;k++)y[a]+=r[k][a]*rule[k];}
            return y;
        }
        public static Model fit(List<Sample> samples) {
            if(samples.size()<MIN_TRAIN)throw new IllegalArgumentException("Faltan lecturas para el ajuste");
            double[][] g=new double[6][6];double[] h=new double[6],mean=new double[3];
            for(Sample s:samples) {
                double[][] r=s.rotation();
                for(int a=0;a<3;a++) {
                    double[] row={r[0][a],r[1][a],r[2][a],a==0?1:0,a==1?1:0,a==2?1:0};
                    mean[a]+=s.m[a]/samples.size();
                    for(int i=0;i<6;i++){h[i]+=row[i]*s.m[a];for(int j=0;j<6;j++)g[i][j]+=row[i]*row[j];}
                }
            }
            double[] eigen=eigenvalues(g);Arrays.sort(eigen);
            if(eigen[0]<=0 || eigen[5]/eigen[0]>10_000)
                throw new IllegalArgumentException("Gira en varios ejes: las orientaciones no separan campo y sesgo");
            return new Model(solve(g,h),mean,eigen[5]/eigen[0]);
        }
    }

    private Stage stage=Stage.TRAIN;
    private final String session;
    private final RuleStore ruleStore;
    private final List<Sample> training=new ArrayList<>();
    private final List<Double> calibration=new ArrayList<>();
    private final List<String> records=new ArrayList<>();
    private long first=-1,last=-1,seq=-1;
    private Model model;
    private double radius=Double.NaN, error2, constant2, persistence2;
    private double[] previousTest;
    private int testN,persistenceN,covered,rejected;
    private String reason="";

    public FieldExperiment(String session) {this(session,new LocalRuleStore());}
    public FieldExperiment(String session,RuleStore store) {this.session=session;this.ruleStore=store;}
    public Stage stage(){return stage;}
    public Model model(){return model;}
    public boolean active(){return stage!=Stage.DONE && stage!=Stage.ABORTED;}
    public void reject(){rejected++;}
    public void abort(String reason){if(active()){stage=Stage.ABORTED;this.reason=reason;}}
    public int testCount(){return testN;}
    public int count(){return records.size();}
    public double rmse(){return testN==0?Double.NaN:Math.sqrt(error2/testN);}
    public double constantRmse(){return testN==0?Double.NaN:Math.sqrt(constant2/testN);}
    public double persistenceRmse(){return persistenceN==0?Double.NaN:Math.sqrt(persistence2/persistenceN);}
    public double coverage(){return testN==0?Double.NaN:(double)covered/testN;}
    public double radius(){return radius;}
    public double elapsed(){return first<0?0:(last-first)/1e9;}

    public void accept(Sample s) {
        if(!active())return;
        if(!session.equals(s.session)){abort("El reloj reinició su referencia. Repite la sesión.");return;}
        if(s.seq<=seq || s.mt<=last){reject();return;}
        if(last>0 && s.mt-last>1_000_000_000L){abort("Hueco de más de un segundo en los sensores");return;}
        if(records.size()>=MAX_RECORDS){abort("Se excedió el límite de registros");return;}
        if(first<0)first=s.mt;
        last=s.mt;seq=s.seq;
        long t=s.mt-first;
        if(stage==Stage.TRAIN && t>=TRAIN_NS) {
            try {model=Model.fit(training);ruleStore.save(model.parameters());stage=Stage.CALIBRATE;}
            catch(RuntimeException e){abort(e.getMessage());return;}
        }
        if(stage==Stage.CALIBRATE && t>=TRAIN_NS+CAL_NS) {
            if(calibration.size()<MIN_CAL){abort("Faltan lecturas de calibración");return;}
            double[] errors=new double[calibration.size()];
            for(int i=0;i<errors.length;i++)errors[i]=calibration.get(i);
            Arrays.sort(errors);
            // Empirical radius only; temporally correlated data do not guarantee 95% coverage.
            radius=Math.max(1e-6,errors[(int)Math.ceil(.95*errors.length)-1]);
            stage=Stage.TEST;
        }
        if(t>=TOTAL_NS) {
            if(testN<MIN_TEST)abort("Faltan lecturas de prueba");else stage=Stage.DONE;
            return;
        }
        double[] pred=null;double e=Double.NaN,c=Double.NaN,p=Double.NaN;
        if(stage==Stage.TRAIN)training.add(s);
        else {
            try {
                double[] rule=ruleStore.load();
                if(!Arrays.equals(rule,model.p))throw new IllegalStateException("El modelo cambió después del ajuste");
                pred=model.predict(s,rule); // Query the frozen canonical rule, not the target reading.
            }catch(RuntimeException failure){abort("Memoria de reglas: "+failure.getMessage());return;}
            e=distance(pred,s.m);
            if(stage==Stage.CALIBRATE)calibration.add(e);
            else if(stage==Stage.TEST) {
                c=distance(model.mean,s.m);
                if(previousTest!=null){p=distance(previousTest,s.m);persistence2+=p*p;persistenceN++;}
                previousTest=s.m.clone();error2+=e*e;constant2+=c*c;testN++;
                if(e<=radius)covered++;
            }
        }
        records.add(csvRow(stage,s,pred,e,c,p,radius));
    }

    public String status() {
        if(stage==Stage.ABORTED)return "SESIÓN INCOMPLETA\n"+reason;
        if(stage==Stage.DONE)return report();
        String label=stage==Stage.TRAIN?"1/3 · APRENDER (20 s)":stage==Stage.CALIBRATE?
                "2/3 · CALIBRAR ERROR (10 s)":"3/3 · PREDECIR SIN REAJUSTAR (20 s)";
        return String.format(Locale.US,"%s\n%.1f / 50 s · %d registros\nGira despacio en varios ejes, en el mismo lugar.",label,elapsed(),count());
    }
    public String report() {
        if(stage!=Stage.DONE)return status();
        double gain=constantRmse()>1e-6?100*(1-rmse()/constantRmse()):Double.NaN;
        String verdict=Double.isFinite(gain)&&gain>=20&&coverage()>=.8
                ?"Predicción reproducida en esta sesión"
                :"Resultado no concluyente o modelo insuficiente";
        return String.format(Locale.US,
            "%s\n\nError vectorial RMS: %.3f µT\nReferencia constante: %.3f µT\nÚltima lectura: %.3f µT\nMejora frente a constante: %.1f %%\nDentro del radio empírico: %.1f %%\nRadio calibrado: %.3f µT\nPrueba: %d lecturas · descartes: %d\n\nModelo fijo de 6 parámetros. Resultado exploratorio de un campo clásico; no demuestra entrelazamiento ni una lattice universal.",
            verdict,rmse(),constantRmse(),persistenceRmse(),gain,100*coverage(),radius,testN,rejected);
    }
    public String exportCsv() {
        StringBuilder b=new StringBuilder("# WorldLink Field v1; source=watch_physical; protocol=20s_train_10s_calibration_20s_test\n");
        b.append("# orientation=TYPE_GAME_ROTATION_VECTOR; magnetic=TYPE_MAGNETIC_FIELD_UNCALIBRATED; frame=initial_inertial\n");
        b.append("# session=").append(safe(session)).append("; state=").append(stage).append("; rejected=").append(rejected).append('\n');
        b.append("# reason=").append(safe(reason)).append('\n');
        b.append("# model=m_device=transpose(R)*B_reference+b_device; radius=empirical_95th_percentile_not_confidence_guarantee\n");
        b.append("# rule_store=").append(safe(ruleStore.name())).append('\n');
        if(model!=null)b.append("# parameters_Bxyz_bxyz=").append(Arrays.toString(model.p)).append("; normal_matrix_condition=").append(model.condition).append('\n');
        b.append(HEADER);for(String line:records)b.append(line);
        return b.toString();
    }
    private static String safe(String text){return text.replace('\n',' ').replace('\r',' ').replace(',',';');}
    private static String csvRow(Stage stage,Sample s,double[] y,double e,double c,double p,double radius) {
        StringBuilder b=new StringBuilder(stage.name());
        b.append(',').append(s.mt).append(',').append(s.qt).append(',').append(s.seq);
        for(double v:s.q)b.append(',').append(v);for(double v:s.m)b.append(',').append(v);
        b.append(',').append(s.accuracy);
        for(int i=0;i<3;i++)b.append(',').append(y==null?"":Double.toString(y[i]));
        for(double v:new double[]{e,c,p,radius})b.append(',').append(Double.isFinite(v)?Double.toString(v):"");
        return b.append('\n').toString();
    }
    private static double distance(double[] a,double[] b){double z=0;for(int i=0;i<3;i++){double d=a[i]-b[i];z+=d*d;}return Math.sqrt(z);}
    private static double[] solve(double[][] matrix,double[] rhs) {
        double[][] a=new double[6][7];
        for(int i=0;i<6;i++){System.arraycopy(matrix[i],0,a[i],0,6);a[i][6]=rhs[i];}
        for(int k=0;k<6;k++) {
            int best=k;for(int i=k+1;i<6;i++)if(Math.abs(a[i][k])>Math.abs(a[best][k]))best=i;
            double[] tmp=a[k];a[k]=a[best];a[best]=tmp;
            if(Math.abs(a[k][k])<1e-10)throw new IllegalArgumentException("Ajuste singular");
            double pivot=a[k][k];for(int j=k;j<7;j++)a[k][j]/=pivot;
            for(int i=0;i<6;i++)if(i!=k){double factor=a[i][k];for(int j=k;j<7;j++)a[i][j]-=factor*a[k][j];}
        }
        double[] x=new double[6];for(int i=0;i<6;i++)x[i]=a[i][6];return x;
    }
    private static double[] eigenvalues(double[][] source) {
        double[][] a=new double[6][6];for(int i=0;i<6;i++)a[i]=source[i].clone();
        for(int iteration=0;iteration<200;iteration++) {
            int p=0,q=1;double max=0;
            for(int i=0;i<6;i++)for(int j=i+1;j<6;j++)if(Math.abs(a[i][j])>max){max=Math.abs(a[i][j]);p=i;q=j;}
            if(max<1e-9)break;
            double angle=.5*Math.atan2(2*a[p][q],a[q][q]-a[p][p]),c=Math.cos(angle),s=Math.sin(angle);
            double ap=a[p][p],aq=a[q][q],x=a[p][q];
            a[p][p]=c*c*ap-2*c*s*x+s*s*aq;a[q][q]=s*s*ap+2*c*s*x+c*c*aq;a[p][q]=a[q][p]=0;
            for(int k=0;k<6;k++)if(k!=p&&k!=q){double kp=a[k][p],kq=a[k][q];a[k][p]=a[p][k]=c*kp-s*kq;a[k][q]=a[q][k]=s*kp+c*kq;}
        }
        double[] out=new double[6];for(int i=0;i<6;i++)out[i]=a[i][i];return out;
    }
}
