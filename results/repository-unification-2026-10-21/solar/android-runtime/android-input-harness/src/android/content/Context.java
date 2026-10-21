package android.content;
import android.content.res.AssetManager;
/** Diagnostic host adapter: reads assets, provides no Android lifecycle/GL. */
public class Context {
    private final AssetManager assets;
    public Context(String root){assets=new AssetManager(root);}
    public Context getApplicationContext(){return this;}
    public AssetManager getAssets(){return assets;}
}
