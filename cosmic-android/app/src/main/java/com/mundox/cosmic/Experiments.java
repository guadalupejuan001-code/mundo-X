package com.mundox.cosmic;

import android.app.Activity;

/** Existing Mundo X Android experiments packaged directly in COSMIC X Engine. */
public final class Experiments {
    private Experiments() { }

    public static final class Item {
        public final String id, title, subtitle, category;
        public final Class<? extends Activity> activity;
        public final int accent;

        private Item(String id, String title, String subtitle, String category,
                     Class<? extends Activity> activity, int accent) {
            this.id = id;
            this.title = title;
            this.subtitle = subtitle;
            this.category = category;
            this.activity = activity;
            this.accent = accent;
        }
    }

    public static final Item[] ALL = {
        new Item("circuitos", "Circuitos", "XLab · puertas, protocolos y memoria", "Núcleo",
                com.mundox.lab.XLabActivity.class, 0xFFD6ED67),
        new Item("micromundo", "Micromundo", "Partículas, colonias y cuatro qubits", "Visual",
                com.mundox.lab.MicroLabActivity.class, 0xFF69E7B3),
        new Item("objetos", "Objetos", "Regiones e identidades en la escena", "Visual",
                com.mundox.lab.ObjectLabActivity.class, 0xFF91BEFF),
        new Item("ecos", "Ecos", "Evolución, perturbación y reversión", "Núcleo",
                com.mundox.echoes.MainActivity.class, 0xFF72E8DB),
        new Item("vm8", "Máquina de 8 bits", "Programas, registros y RAM en CompactCore", "Núcleo",
                com.mundox.lab.BasisVmActivity.class, 0xFFFFC789),
        new Item("cuantica", "Interfaz cuántica", "Explora las operaciones de tu núcleo", "Núcleo",
                com.mundox.lab.QuantumInterfaceActivity.class, 0xFFC3B2FF),
        new Item("visual", "Campo visual", "Clásico, generativo y compacto → visual", "Visual",
                com.mundox.lab.VisualExperimentActivity.class, 0xFF9CC9FF),
        new Item("gpu", "Partículas GPU", "Una escena viva con controles táctiles", "Visual",
                com.mundox.lab.GpuVisualActivity.class, 0xFFFFABCB),
        new Item("juego", "Ciudad procedural", "Recorre tu experimento de juego", "Visual",
                com.mundox.lab.ProceduralGameDemoActivity.class, 0xFFF3D87F),
        new Item("sincronizada", "Escena sincronizada", "GPU y estado compartido entre teléfonos", "Conexión",
                com.mundox.lab.SyncedGpuActivity.class, 0xFF83D5FA),
        new Item("red", "Red de estados", "Experimento de conexión y relay", "Conexión",
                com.mundox.lab.InternetSyncActivity.class, 0xFF7BD6C1),
        new Item("dispositivo", "Dispositivo", "Tu lanzador experimental de Android", "Herramientas",
                com.mundox.lab.DeviceLauncherActivity.class, 0xFFC5CBD8),
        new Item("rendimiento", "Rendimiento", "Controles y acceso a tus aplicaciones", "Herramientas",
                com.mundox.lab.PerformanceLauncherActivity.class, 0xFFE4BB92),
        new Item("clasico", "Laboratorio original", "GHZ, cluster, teleportación y Exact T", "Núcleo",
                com.mundox.lab.MainActivity.class, 0xFFB5C8FF)
    };
}
