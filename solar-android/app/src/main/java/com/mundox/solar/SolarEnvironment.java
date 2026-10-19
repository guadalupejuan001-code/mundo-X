package com.mundox.solar;

/**
 * Atmospheric classification and visual navigation parameters for the existing
 * 23-body catalogue. No orbital data, native core or ephemeris is changed.
 *
 * Classification/composition and the few reference pressures are source-backed.
 * Atmosphere tops, scale heights, colors, opacity and camera bounds are explicit
 * visualization settings, not a measured atmospheric profile or physical edge.
 * See docs/atmosphere-science.md for provenance and scope.
 */
public final class SolarEnvironment {
    public static final int NONE=0, EXOSPHERE=1, THIN=2, TERRESTRIAL=3,
            DENSE=4, GAS_GIANT=5, STAR=6, PLUME=7;

    public final int id, type;
    /** True for the continuous-atmosphere visual model, including the solar atmosphere. */
    public final boolean hasAtmosphere, gasGiant, solidSurface;
    public final String label, composition, description, altitudeReference;
    /** Visual shell extent. Zero means no global scattering shell is drawn. */
    public final double atmosphereTopKm;
    /** Camera bounds relative to the catalogue mean radius, not local terrain. */
    public final double minAltitudeKm, minimumAltitudeKm, maxAltitudeKm, entryAltitudeKm;
    /** Constant visual falloff scale; not a complete thermodynamic density model. */
    public final double scaleHeightKm;
    /** Catalogue/mission reference pressure in bar; NaN means not supplied. */
    public final double referencePressureBar;
    /** Linear-looking display RGB and visual opacity; read-only by convention. */
    public final float[] color;
    public final float opacity;

    private SolarEnvironment(int id,int type,String label,String composition,String description,
            double top,double minimum,double maximum,double entry,double scale,double pressure,
            float r,float g,float b,float opacity) {
        this.id=id;this.type=type;this.label=label;this.composition=composition;
        this.description=description;
        gasGiant=type==GAS_GIANT;solidSurface=type!=GAS_GIANT&&type!=STAR;
        hasAtmosphere=type==THIN||type==TERRESTRIAL||type==DENSE||type==GAS_GIANT||type==STAR;
        atmosphereTopKm=top;minAltitudeKm=minimum;minimumAltitudeKm=minimum;
        maxAltitudeKm=maximum;entryAltitudeKm=entry;scaleHeightKm=scale;
        referencePressureBar=pressure;color=new float[]{r,g,b};this.opacity=opacity;
        altitudeReference=gasGiant
            ?"Respecto al nivel de nubes / 1 bar aproximado; sin suelo sólido"
            :type==STAR?"Sobre la fotosfera de referencia; atmósfera solar"
            :"Sobre el radio medio del catálogo; relieve local no resuelto";
    }

    private static final SolarEnvironment[] ENVIRONMENTS={
        new SolarEnvironment(0,STAR,"Atmósfera solar","Plasma; hidrógeno y helio",
            "La fotosfera, la cromosfera y la corona forman la atmósfera solar. La cámara recorre una corona ilustrativa; no hay suelo sólido ni simulación de supervivencia de una nave.",
            700000,1000,1500000,20000,20000,Double.NaN,1f,.60f,.22f,.65f),
        new SolarEnvironment(1,EXOSPHERE,"Exosfera muy tenue","Oxígeno, sodio, hidrógeno, helio y potasio",
            "Mercurio tiene una exosfera, sin el cielo espeso de una atmósfera terrestre. El descenso aproxima su superficie mediante el radio medio; no añade aire denso.",
            0,.5,2500,150,0,Double.NaN,.64f,.60f,.51f,0),
        new SolarEnvironment(2,DENSE,"Atmósfera densa","Principalmente CO₂; nubes de ácido sulfúrico",
            "Las nubes ocultan la superficie de Venus y la presión superficial de referencia es de unos 92 bar. Bruma, iluminación y caída de densidad son aproximaciones visuales.",
            180,.5,1200,140,15.9,92,1f,.70f,.31f,.96f),
        new SolarEnvironment(3,TERRESTRIAL,"Atmósfera terrestre","Nitrógeno y oxígeno",
            "La atmósfera terrestre se hace más tenue gradualmente con la altura. La envoltura visible de esta app no marca el final físico del aire. Nubes y dispersión son aproximaciones.",
            120,.5,2000,35,8.5,1.014,.26f,.56f,1f,.84f),
        new SolarEnvironment(4,THIN,"Atmósfera tenue","Principalmente CO₂; nitrógeno y argón",
            "Marte tiene una atmósfera tenue y polvo suspendido. Los 6,36 mbar de referencia varían con estación y lugar; el cielo de la escena no es meteorología en directo.",
            150,.5,1500,25,11.1,.00636,.89f,.47f,.25f,.38f),
        new SolarEnvironment(5,GAS_GIANT,"Gigante gaseoso","Hidrógeno y helio; capas de nubes",
            "Júpiter carece de una superficie donde aterrizar. El descenso entra en una envoltura nubosa ilustrativa; la altitud usa el radio medio como nivel de nubes / 1 bar aproximado.",
            1500,-200,12000,600,27,1,.85f,.69f,.50f,.91f),
        new SolarEnvironment(6,GAS_GIANT,"Gigante gaseoso","Hidrógeno y helio; capas de nubes",
            "Saturno no tiene una superficie sólida accesible. Las bandas, brumas y profundidad de nubes se representan de forma aproximada, sin un cálculo de presión a cada altura.",
            1800,-200,12000,900,59,1,.91f,.78f,.53f,.89f),
        new SolarEnvironment(7,GAS_GIANT,"Gigante de hielo","Hidrógeno, helio y metano",
            "Urano es un gigante de hielo con una atmósfera de hidrógeno, helio y metano. No hay suelo en la capa recorrida; los fluidos interiores no se reconstruyen.",
            1200,-200,7000,500,28,1,.47f,.79f,.83f,.84f),
        new SolarEnvironment(8,GAS_GIANT,"Gigante de hielo","Hidrógeno, helio y metano",
            "Neptuno no tiene una superficie sólida para aterrizar. Su color visible es más parecido al de Urano que algunas imágenes históricas intensificadas; aquí el tono y las nubes son aproximados.",
            1200,-200,7000,500,20,1,.36f,.66f,.83f,.87f),
        new SolarEnvironment(9,EXOSPHERE,"Exosfera lunar","Helio, neón y argón, con trazas de otros elementos",
            "La Luna posee una exosfera extremadamente tenue. La vista conserva un cielo oscuro; acercarse no crea una atmósfera espesa ni terreno medido en tres dimensiones.",
            0,.5,1500,70,0,Double.NaN,.69f,.71f,.75f,0),
        new SolarEnvironment(10,THIN,"Atmósfera volcánica muy tenue","Principalmente dióxido de azufre",
            "Ío tiene una atmósfera tenue vinculada al dióxido de azufre de su superficie y su actividad volcánica. Su opacidad visual es muy baja; no se calcula la evolución de las plumas.",
            100,.5,1500,40,20,Double.NaN,.91f,.77f,.39f,.018f),
        new SolarEnvironment(11,EXOSPHERE,"Exosfera muy tenue","Oxígeno en una envoltura ligada a la superficie",
            "Europa tiene una exosfera muy tenue, no una atmósfera densa con cielo azul. La superficie helada y su ambiente se muestran de forma aproximada.",
            0,.5,1200,70,0,Double.NaN,.65f,.77f,.89f,0),
        new SolarEnvironment(12,EXOSPHERE,"Exosfera muy tenue","Oxígeno y trazas de otros componentes",
            "Ganímedes tiene una exosfera tenue. Su campo magnético es tratado por el modelo existente; el modo de descenso no añade una capa de aire denso.",
            0,.5,2000,80,0,Double.NaN,.60f,.68f,.78f,0),
        new SolarEnvironment(13,EXOSPHERE,"Exosfera muy tenue","Dióxido de carbono detectado por Galileo",
            "Calisto tiene una exosfera extremadamente tenue. El descenso conserva un cielo oscuro y una superficie de referencia, sin atribuirle una atmósfera terrestre.",
            0,.5,1800,70,0,Double.NaN,.55f,.56f,.58f,0),
        new SolarEnvironment(14,DENSE,"Atmósfera densa y brumosa","Nitrógeno y metano",
            "Titán tiene una atmósfera densa y bruma orgánica. Huygens midió 1,467 bar en su lugar de aterrizaje en 2005; ese valor de referencia no es un pronóstico ni una presión calculada para toda altura.",
            600,.5,2500,350,40,1.467,.99f,.59f,.19f,.96f),
        new SolarEnvironment(15,PLUME,"Exosfera y plumas locales","Vapor de agua y partículas de hielo",
            "Encélado emite material desde su región polar sur. Las plumas locales no se sustituyen por una envoltura global espesa; esta versión permite acercarse a su superficie sin calcular esos chorros.",
            0,.5,500,30,0,Double.NaN,.67f,.88f,1f,0),
        new SolarEnvironment(16,THIN,"Atmósfera muy tenue","Nitrógeno con pequeñas cantidades de metano",
            "Tritón conserva una atmósfera muy tenue. La bruma débil que muestra el descenso es ilustrativa; no se reconstruyen sus cambios estacionales.",
            300,.5,1300,40,20,Double.NaN,.68f,.76f,.86f,.05f),
        new SolarEnvironment(17,NONE,"Sin atmósfera densa modelada","Sin composición atmosférica asignada",
            "Fobos se representa sin una envoltura atmosférica global. Su forma irregular sigue aproximada por una esfera; la altura se mide desde ese radio de referencia.",
            0,.5,150,8,0,Double.NaN,.53f,.47f,.40f,0),
        new SolarEnvironment(18,NONE,"Sin atmósfera densa modelada","Sin composición atmosférica asignada",
            "Deimos se representa sin una envoltura atmosférica global. La aproximación esférica no reproduce la elevación exacta de su superficie irregular.",
            0,.5,120,6,0,Double.NaN,.62f,.57f,.49f,0),
        new SolarEnvironment(19,EXOSPHERE,"Exosfera muy tenue","Oxígeno y dióxido de carbono",
            "Cassini detectó una exosfera tenue de oxígeno y dióxido de carbono alrededor de Rea. No se representa como un cielo espeso.",
            0,.5,800,45,0,Double.NaN,.72f,.77f,.83f,0),
        new SolarEnvironment(20,NONE,"Sin atmósfera densa modelada","Sin composición atmosférica asignada",
            "Titania se muestra sin una envoltura de dispersión global. Esa decisión del modelo no afirma que sea imposible una población muy tenue de gas.",
            0,.5,900,50,0,Double.NaN,.65f,.69f,.73f,0),
        new SolarEnvironment(21,NONE,"Sin atmósfera densa modelada","Sin composición atmosférica asignada",
            "Oberón se muestra sin una atmósfera densa. La app no proporciona un modelo de exosfera ni una topografía local medida.",
            0,.5,900,50,0,Double.NaN,.60f,.64f,.68f,0),
        new SolarEnvironment(22,NONE,"Sin atmósfera densa modelada","Sin composición atmosférica asignada",
            "Miranda se muestra sin una atmósfera densa. Los grandes escarpes y la forma exacta del relieve no se reconstruyen mediante el radio medio.",
            0,.5,450,25,0,Double.NaN,.70f,.73f,.77f,0)
    };

    public static SolarEnvironment environment(int id) {
        if(id<0||id>=ENVIRONMENTS.length)throw new IllegalArgumentException("Cuerpo desconocido: "+id);
        return ENVIRONMENTS[id];
    }

    /** Alias for UI consumers. */
    public static SolarEnvironment body(int id) { return environment(id); }

    public double clampAltitudeKm(double altitude) {
        if(!Double.isFinite(altitude))throw new IllegalArgumentException("Altitud no finita");
        return Math.max(minAltitudeKm,Math.min(maxAltitudeKm,altitude));
    }
}
