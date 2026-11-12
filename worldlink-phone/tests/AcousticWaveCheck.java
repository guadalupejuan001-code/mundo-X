import com.mundox.worldlink.phone.AcousticWave;
public final class AcousticWaveCheck {
    private static void check(boolean b,String message) {if(!b)throw new AssertionError(message);}
    public static void main(String[] args) {
        check(AcousticWave.validFrequency(220),"valid 220Hz");
        check(!AcousticWave.validFrequency(0),"reject DC");
        check(!AcousticWave.validFrequency(20000),"reject ultrasound");
        check(!AcousticWave.validFrequency(Double.NaN),"reject NaN");
        check(AcousticWave.sweepFrequency(0)==180.0,"start sweep");
        check(AcousticWave.sweepFrequency(1)==1200.0,"end sweep");
        check(AcousticWave.sweepFrequency(0.5)>600.0,"middle sweep");
        check(AcousticWave.fade(0,22050)==0,"fade-in begins silent");
        check(AcousticWave.fade(22050,22050)==0,"fade-out ends silent");
        check(Math.abs(AcousticWave.pcm(Math.PI/2,1))<9000,"bounded output");
        check(AcousticWave.DURATION_SWEEP_MS<=10000,"sweep time bounded");
        System.out.println("Acoustic actuator pure tests passed");
    }
}
