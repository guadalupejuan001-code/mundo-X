package com.etherx.filelab.core;

import java.util.*;

/**
 * Educational variational classifier using an EXACT classical simulation of two qubits.
 * It is NOT a physical quantum computer or a proven compression speedup.
 * Trains on synthetic compressible/incompressible blocks; cannot touch user files.
 */
public final class QuantumToyLab {
    private QuantumToyLab() {}
    public static final class Experiment {
        public final int samples,epochs,correct;
        public final double loss,accuracy,baselineAccuracy;
        public final double[] weights;
        Experiment(int n,int e,int c,double l,double a,double baseline,double[] w) {
            samples=n;epochs=e;correct=c;loss=l;accuracy=a;baselineAccuracy=baseline;weights=w;
        }
        public String report(){
            return "EXPERIMENTO Q-LAB · 2 QUBITS SIMULADOS\n"+
                "Muestras sintéticas: "+samples+" (mitad entrenamiento, mitad evaluación)\n"+
                "Iteraciones: "+epochs+"\n"+
                String.format(Locale.ROOT,"Aciertos evaluación: %d/%d (%.1f%%)\n",correct,samples/2,accuracy*100)+
                String.format(Locale.ROOT,"Clasificador clásico referencia: %.1f%%\n",baselineAccuracy*100)+
                String.format(Locale.ROOT,"Pérdida de entrenamiento: %.5f\n\n",loss)+
                "Circuito clásico de amplitudes complejas de 2 qubits con rotaciones Ry y CNOT. " +
                "No entrena una red neuronal grande, no usa hardware cuántico y no ha demostrado ahorro real de espacio. " +
                "La compresión EXF permanece determinista e independiente de este laboratorio.";
        }
    }
    private static final class Case {
        final double entropy,zeros;final int label;
        Case(double e,double z,int label){entropy=e;zeros=z;this.label=label;}
    }
    private static List<Case> generate() {
        Random r=new Random(42);List<Case> cases=new ArrayList<>();
        for(int i=0;i<200;i++){
            // A synthetic example. Entropy in [0,8], zero fraction in [0,1].
            double e=i%2==0? r.nextDouble()*5.8 : 6.7+r.nextDouble()*1.3;
            double z=i%2==0?0.2+r.nextDouble()*0.8:r.nextDouble()*.03;
            cases.add(new Case(e,z,i%2==0?1:0));
        }
        Collections.shuffle(cases,new Random(69));return cases;
    }
    public static Experiment train() {
        List<Case> d=generate();double[] weights={0.7,0.5,-0.25,0.25};
        int epochs=90;double step=.13;double loss=loss(d,weights,0,100);
        Random rnd=new Random(2026);
        for(int ep=0;ep<epochs;ep++){
            for(int t=0;t<100;t++){
                double[] proposal=weights.clone();
                int j=rnd.nextInt(weights.length);proposal[j]+=rnd.nextGaussian()*step;
                double next=loss(d,proposal,0,100);
                if(next<loss){weights=proposal;loss=next;}
            }
            step*=.983;
        }
        int correct=0,classic=0;
        for(int i=100;i<d.size();i++){
            Case x=d.get(i);
            if((prob(x,weights)>=0.5?1:0)==x.label)correct++;
            if((x.entropy<6.3?1:0)==x.label)classic++;
        }
        return new Experiment(d.size(),epochs,correct,loss,correct/100.,classic/100.,weights);
    }
    private static double loss(List<Case> d,double[] w,int begin,int end) {
        double s=0;
        for(int i=begin;i<end;i++){
            Case c=d.get(i);double p=prob(c,w);s+=(p-c.label)*(p-c.label);
        }
        return s/(end-begin);
    }
    /** Exact 2-qubit statevector, Ry(a) on q0 and Ry(b) on q1, CNOT(0,1), Ry(c) on q1, Ry(d) on q0. */
    private static double prob(Case x,double[] w) {
        double[] a={1,0,0,0};
        ry(a,0,(x.entropy/8)*Math.PI+w[0]);
        ry(a,1,x.zeros*Math.PI+w[1]);
        double tmp=a[2];a[2]=a[3];a[3]=tmp;
        ry(a,1,w[2]);ry(a,0,w[3]);
        return a[2]*a[2]+a[3]*a[3];
    }
    private static void ry(double[] a,int qubit,double theta){
        double c=Math.cos(theta/2),s=Math.sin(theta/2);
        for(int k=0;k<4;k++)if((k&(qubit==0?2:1))==0){
            int other=k|(qubit==0?2:1);double x=a[k],y=a[other];
            a[k]=c*x-s*y;a[other]=s*x+c*y;
        }
    }
}
