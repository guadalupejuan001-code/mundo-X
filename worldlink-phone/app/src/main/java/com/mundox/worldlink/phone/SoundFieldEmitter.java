package com.mundox.worldlink.phone;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.os.Handler;
import android.os.Looper;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Generates genuine pressure waves with the phone loudspeaker.
 * The acoustic emission is time-limited and cannot generate ultrasound.
 * No physical entanglement or arbitrary material transformation is implied.
 */
final class SoundFieldEmitter {
    interface Completion { void onDone(String message); }
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final Handler main=new Handler(Looper.getMainLooper());
    private final Completion completion;
    private volatile Session current;
    private volatile boolean closed;
    private static final class Session {
        final boolean sweep;
        final double hz;
        final AtomicBoolean cancelled=new AtomicBoolean(false);
        Session(boolean sweep,double hz){this.sweep=sweep;this.hz=hz;}
    }
    SoundFieldEmitter(Completion completion) {this.completion=completion;}

    synchronized String emitTone(double hz) {
        if(!AcousticWave.validFrequency(hz))return "Frecuencia inválida (120–1200 Hz)";
        return begin(new Session(false,hz));
    }
    synchronized String emitSweep() {
        return begin(new Session(true,0));
    }
    private String begin(Session session) {
        if(closed)return "Audio cerrado";
        if(current!=null)current.cancelled.set(true);
        current=session;
        worker.execute(()->generate(session));
        return session.sweep
                ? "Barrido físico 180–1200 Hz · 10 segundos"
                : "Onda física "+Math.round(session.hz)+" Hz · 4,5 segundos";
    }
    synchronized void stop() {
        if(current!=null)current.cancelled.set(true);
        current=null;
    }
    boolean isActive(){Session s=current;return s!=null&&!s.cancelled.get();}
    void close(){closed=true;stop();worker.shutdown();}

    private void generate(Session session) {
        if(session.cancelled.get() || closed)return;
        int durationMs=session.sweep?AcousticWave.DURATION_SWEEP_MS:AcousticWave.DURATION_TONE_MS;
        long total=(long)AcousticWave.SAMPLE_RATE*durationMs/1000;
        int block=1024;
        short[] buffer=new short[block];
        AudioTrack player=null;
        String ending;
        try {
            AudioFormat format=new AudioFormat.Builder()
                .setSampleRate(AcousticWave.SAMPLE_RATE)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build();
            AudioAttributes attrs=new AudioAttributes.Builder()
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .setUsage(AudioAttributes.USAGE_MEDIA).build();
            int bytes=Math.max(block*4,
                    AudioTrack.getMinBufferSize(AcousticWave.SAMPLE_RATE,
                        AudioFormat.CHANNEL_OUT_MONO,AudioFormat.ENCODING_PCM_16BIT));
            player=new AudioTrack.Builder().setAudioFormat(format)
                .setAudioAttributes(attrs).setBufferSizeInBytes(bytes)
                .setTransferMode(AudioTrack.MODE_STREAM).build();
            if(player.getState()!=AudioTrack.STATE_INITIALIZED)
                throw new IllegalStateException("Altavoz no disponible");
            player.play();
            double phase=0;
            for(long start=0;start<total&&!session.cancelled.get()&&!closed;start+=block) {
                int n=(int)Math.min(block,total-start);
                for(int i=0;i<n;i++){
                    long sample=start+i;
                    double freq=session.sweep
                        ? AcousticWave.sweepFrequency(sample/(double)Math.max(1,total-1))
                        : session.hz;
                    phase+=(2*Math.PI*freq)/AcousticWave.SAMPLE_RATE;
                    if(phase>=Math.PI*2)phase-=Math.PI*2;
                    buffer[i]=AcousticWave.pcm(phase,AcousticWave.fade(sample,total));
                }
                int written=0;
                while(written<n&&!session.cancelled.get()&&!closed) {
                    int count=player.write(buffer,written,n-written,AudioTrack.WRITE_BLOCKING);
                    if(count<=0)throw new IllegalStateException("Falló la salida de audio");
                    written+=count;
                }
            }
            ending=session.cancelled.get()?"Emisión acústica detenida":"Emisión acústica terminada";
        } catch(Exception error) {
            ending="No se pudo emitir audio: "+error.getClass().getSimpleName();
        } finally {
            if(player!=null) {
                try{player.pause();player.flush();player.release();}
                catch(Exception ignored){}
            }
        }
        final String result=ending;
        synchronized(this) {
            if(current==session) {
                current=null;
                main.post(()->completion.onDone(result));
            }
        }
    }
}
