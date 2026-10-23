import com.mundox.worldlink.phone.FieldExperiment;
import java.util.*;

/** Synthetic fixtures test the algorithm, not physical measurements. */
public final class FieldExperimentCheck {
    static final String SID="synthetic-test-only";
    static int checks;
    static void check(boolean value,String message){checks++;if(!value)throw new AssertionError(message);}
    static FieldExperiment.Sample sample(int i,boolean changed,boolean fixed) {
        double t=i*.1,x=fixed?0:Math.sin(t*.7),y=fixed?0:Math.cos(t*.43),z=fixed?0:Math.sin(t*.31),w=1.1;
        double n=Math.sqrt(x*x+y*y+z*z+w*w);double[] q={x/n,y/n,z/n,w/n};
        long mt=1_000_000_000L+i*100_000_000L;
        FieldExperiment.Sample blank=new FieldExperiment.Sample(SID,i,mt,mt-10_000_000,q,new double[3],3);
        double[][] r=blank.rotation();double[] field={21,-8,42},bias={4,-3,2},m=new double[3];
        for(int a=0;a<3;a++){m[a]=bias[a]+.05*Math.sin(i*(a+1)*1.71);for(int k=0;k<3;k++)m[a]+=r[k][a]*field[k];}
        if(changed&&i>=300)m[0]+=20;
        return new FieldExperiment.Sample(SID,i,mt,mt-10_000_000,q,m,3);
    }
    public static void main(String[] args) {
        // Independent orientation convention check: +90deg around z moves reference x to device -y.
        List<FieldExperiment.Sample> train=new ArrayList<>();for(int i=0;i<200;i++)train.add(sample(i,false,false));
        FieldExperiment.Model model=FieldExperiment.Model.fit(train);
        double[] p=model.parameters();double[] expected={21,-8,42,4,-3,2};
        for(int i=0;i<6;i++)check(Math.abs(p[i]-expected[i])<.03,"six parameter recovery "+i);
        FieldExperiment.Sample z90=new FieldExperiment.Sample(SID,999,100,100,new double[]{0,0,Math.sqrt(.5),Math.sqrt(.5)},new double[3],3);
        double[] predicted=model.predict(z90);
        check(Math.abs(predicted[0]-(-8+4))<.03 && Math.abs(predicted[1]-(-21-3))<.03,"rotation convention");
        FieldExperiment good=new FieldExperiment(SID);double[] frozen=null;
        for(int i=0;i<=500;i++){good.accept(sample(i,false,false));if(i==200)frozen=good.model().parameters();}
        check(good.stage()==FieldExperiment.Stage.DONE,"complete session");
        check(good.testCount()==200,"chronological held-out 200 samples");
        check(good.rmse()<.1,"held-out accurate prediction");
        check(good.constantRmse()>10*good.rmse(),"beats constant reference");
        check(Arrays.equals(frozen,good.model().parameters()),"model frozen during calibration and test");
        check(good.exportCsv().contains("TEST,")&&good.exportCsv().contains("CALIBRATE,"),"export raw held-out observations");
        FieldExperiment changed=new FieldExperiment(SID);for(int i=0;i<=500;i++)changed.accept(sample(i,true,false));
        check(changed.rmse()>19,"field intervention detected, not fitted away");
        check(changed.coverage()<.1,"changed environment fails empirical range");
        check(Arrays.equals(good.model().parameters(),changed.model().parameters()),"test targets cannot leak into fit");
        FieldExperiment staticWatch=new FieldExperiment(SID);for(int i=0;i<=200;i++)staticWatch.accept(sample(i,false,true));
        check(staticWatch.stage()==FieldExperiment.Stage.ABORTED,"unidentifiable constant orientation rejected");
        FieldExperiment missing=new FieldExperiment(SID);missing.accept(sample(0,false,false));missing.accept(sample(15,false,false));
        check(missing.stage()==FieldExperiment.Stage.ABORTED,"timestamp gap aborts");
        FieldExperiment duplicate=new FieldExperiment(SID);duplicate.accept(sample(0,false,false));duplicate.accept(sample(0,false,false));
        check(duplicate.count()==1,"duplicate ignored");
        FieldExperiment reset=new FieldExperiment(SID);reset.accept(sample(0,false,false));
        FieldExperiment.Sample a=sample(1,false,false);
        reset.accept(new FieldExperiment.Sample("other-watch-session",a.seq,a.mt,a.qt,a.q,a.m,3));
        check(reset.stage()==FieldExperiment.Stage.ABORTED,"frame reset aborts");
        boolean bad=false;try{new FieldExperiment.Sample(SID,1,100_000_000,1,new double[]{0,0,0,1},new double[3],3);}catch(IllegalArgumentException e){bad=true;}
        check(bad,"stale sensor pair rejected");
        bad=false;try{new FieldExperiment.Sample(SID,1,100,100,new double[]{0,0,0,1},new double[]{Double.NaN,0,0},3);}catch(IllegalArgumentException e){bad=true;}
        check(bad,"nonfinite sample rejected");
        System.out.printf(Locale.US,"PASS %d checks; SYNTHETIC fixtures only; held-out RMS=%.5f uT; changed-field RMS=%.3f uT%n",checks,good.rmse(),changed.rmse());
    }
}
