package com.cosmicx.win10;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Build;
import android.os.Process;

import java.io.File;
import java.util.Arrays;

/**
 * Inspect the *installed* native libraries rather than assuming that their
 * presence in the APK means the Android linker can load them.
 *
 * The CPU and Ether implementation remain in the canonical Mundo X C++ core.
 * This is strictly an Android packaging/loading diagnostic.
 */
public final class NativeLibraryDoctor {
    private static final String[] ORDER = {
            "c++_shared", "mx_graphics", "cosmic_pc"
    };

    private NativeLibraryDoctor() { }

    public static final class Result {
        public final boolean loaded;
        public final String diagnosis;
        private Result(boolean success, String details) {
            loaded = success;
            diagnosis = details;
        }
    }

    public static synchronized Result inspectAndLoad(Context context) {
        ApplicationInfo info = context.getApplicationInfo();
        String libraryDir = info.nativeLibraryDir;
        File directory = libraryDir == null ? null : new File(libraryDir);
        StringBuilder report = new StringBuilder("MUNDO X · DIAGNÓSTICO DE BIBLIOTECAS v0.1.3");
        report.append("\nPaquete: ").append(context.getPackageName())
                .append("\nAndroid API: ").append(Build.VERSION.SDK_INT)
                .append("\nProceso: ").append(Process.is64Bit() ? "64 bits" : "32 bits")
                .append("\nABI disponibles: ").append(Arrays.toString(Build.SUPPORTED_ABIS))
                .append("\nABI del proceso: ")
                .append(Process.is64Bit()
                    ? Arrays.toString(Build.SUPPORTED_64_BIT_ABIS)
                    : Arrays.toString(Build.SUPPORTED_32_BIT_ABIS))
                .append("\nDirectorio nativo: ")
                .append(libraryDir == null ? "(no disponible)" : libraryDir);
        if (directory != null && directory.isDirectory()) {
            String[] files = directory.list();
            if (files != null) {
                Arrays.sort(files);
                report.append("\nArchivos instalados: ").append(Arrays.toString(files));
            }
        } else {
            report.append("\nDirectorio nativo no visible en disco; requiere revisar instalación.");
        }

        for (String lib : ORDER) {
            String so = "lib" + lib + ".so";
            File physical = directory == null ? null : new File(directory, so);
            report.append("\n").append(so);
            report.append(" físico=")
                    .append(physical != null && physical.isFile()
                            ? physical.length() + " bytes"
                            : "no extraído");
            try {
                System.loadLibrary(lib);
                report.append(" · CARGADO");
                continue;
            } catch (UnsatisfiedLinkError standardError) {
                report.append("\n  loadLibrary: ")
                        .append(safe(standardError.toString()));
                // Prefer normal class-loader resolution; retry an explicit
                // nativeLibraryDir path only if PackageManager extracted the
                // library and the first attempt failed.
                if (physical != null && physical.isFile()) {
                    try {
                        System.load(physical.getAbsolutePath());
                        report.append("\n  CARGADO por ruta física");
                        continue;
                    } catch (UnsatisfiedLinkError pathError) {
                        report.append("\n  System.load: ")
                                .append(safe(pathError.toString()));
                    } catch (SecurityException pathError) {
                        report.append("\n  Restricción al abrir ELF: ")
                                .append(safe(pathError.toString()));
                    }
                }
                report.append("\nFALLO EN ").append(so)
                        .append("\nEl núcleo x86/Ether no se inicia para evitar diagnósticos falsos.");
                return new Result(false, report.toString());
            } catch (SecurityException error) {
                report.append("\n  SEGURIDAD: ").append(safe(error.toString()));
                return new Result(false, report.toString());
            }
        }
        report.append("\nRESULTADO: LAS 3 BIBLIOTECAS CARGARON");
        return new Result(true, report.toString());
    }

    // Keep the error report readable and copyable even if Android supplies
    // an exceptionally verbose linker error.
    private static String safe(String text) {
        if (text == null) return "(sin detalles)";
        return text.length() <= 1800 ? text : text.substring(0, 1800) + " [recortado]";
    }
}
