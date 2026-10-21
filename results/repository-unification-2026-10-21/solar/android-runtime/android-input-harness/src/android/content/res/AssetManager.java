package android.content.res;
import java.io.*;
import java.nio.file.*;
/** Diagnostic host adapter, only for the production constructor's asset reads. */
public class AssetManager {
    private final Path root;
    public AssetManager(String root){this.root=Paths.get(root);}
    public InputStream open(String name)throws IOException{return Files.newInputStream(root.resolve(name));}
}
