package com.mundox.solar;

/**
 * Offline astronomical catalogue and bounded analytical models.
 * Units, source provenance and accuracy limits: docs/solar-science.md.
 * Positions use AU, J2000 ecliptic, mapped to a right-handed Y-up frame:
 * renderer (x,y,z) = ecliptic (x,z,-y).
 */
public final class SolarData {
    private SolarData() {}

    public static final double AU_KM = 149597870.7;
    public static final double J2000 = 2451545.0;
    public static final double MIN_JD = 2378496.5; // 1800-01-01 00:00
    public static final double MAX_JD = 2469807.5; // 2050-01-01 00:00
    public static final double EARTH_MOON_MASS_RATIO = 398600.435507 / 4902.800118; // JPL DE440 GM ratio
    public static final int FIELD_NONE = 0;
    public static final int FIELD_INTRINSIC = 1;
    public static final int FIELD_INDUCED = 2;
    public static final int FIELD_INDUCED_CRUSTAL = 3;
    public static final int FIELD_SOLAR = 4;
    private static final double DEG = Math.PI / 180.0;
    private static final double TWO_PI = Math.PI * 2.0;

    /** Read-only by convention; public arrays are provided for the renderer. */
    public static final class Body {
        public final int id, parent;
        public final String name, type, description, fieldDescription, texture;
        /** Volumetric mean radius; rings are expressed in units of this radius. */
        public final double radiusKm;
        /** Signed catalogue period. Full obliquity already encodes retrograde spin. */
        public final double rotationHours, obliquityDeg;
        /** Equivalent centred-dipole coefficient at radiusKm, not a measured local field. */
        public final double fieldMicroT, magneticTiltDeg;
        /** Semimajor axis relative to parent, not always relative to the Sun. */
        public final double semiMajorAU, orbitalPeriodDays;
        public final int fieldType;
        public final float[] color;
        public final double ringInner, ringOuter;

        private Body(int id, int parent, String name, String type, String description,
                     String fieldDescription, String texture, double radiusKm,
                     double rotationHours, double obliquityDeg, double fieldMicroT,
                     double magneticTiltDeg, double semiMajorAU, double orbitalPeriodDays,
                     int fieldType, float[] color, double ringInner, double ringOuter) {
            this.id = id;
            this.parent = parent;
            this.name = name;
            this.type = type;
            this.description = description;
            this.fieldDescription = fieldDescription;
            this.texture = texture;
            this.radiusKm = radiusKm;
            this.rotationHours = rotationHours;
            this.obliquityDeg = obliquityDeg;
            this.fieldMicroT = fieldMicroT;
            this.magneticTiltDeg = magneticTiltDeg;
            this.semiMajorAU = semiMajorAU;
            this.orbitalPeriodDays = orbitalPeriodDays;
            this.fieldType = fieldType;
            this.color = color;
            this.ringInner = ringInner;
            this.ringOuter = ringOuter;
        }
    }

    /** Stable identifiers: Sun 0, planets 1..8, Moon 9, selected moons 10..22. */
    public static final Body[] BODIES = {
        new Body(0,-1,"Sol","Estrella",
            "La estrella que ilumina el sistema. Su rotación cambia con la latitud; aquí se usa un período de referencia de 25,38 días.",
            "Campo solar variable: arcos coronales y viento solar se muestran de forma conceptual, sin una intensidad única ni datos en directo.",
            "",695700,609.12,7.25,0,0,0,0,FIELD_SOLAR,rgba(1.0f,.83f,.46f),0,0),
        planet(1,"Mercurio","Rocoso","Superficie craterizada, gran núcleo metálico y una exosfera muy tenue.",
            "Campo interno débil. Se muestra un dipolo; el desplazamiento real del campo hacia el norte no está resuelto en este modelo.",
            "",2439.4,1407.5088,.034,fieldAtMeanRadius(.2,2440,2439.4),0,.38709927,87.969,
            FIELD_INTRINSIC,rgba(.57f,.54f,.50f),0,0),
        planet(2,"Venus","Rocoso","Atmósfera densa y nubes que ocultan la superficie. Tiene rotación retrógrada.",
            "Sin dipolo interno global observado. El viento solar induce una envoltura magnética; sus líneas aquí son esquemáticas.",
            "",6051.8,-5832.432,177.36,0,0,.72333566,224.701,
            FIELD_INDUCED,rgba(.92f,.72f,.43f),0,0),
        planet(3,"Tierra","Rocoso","Océanos, continentes y atmósfera. La posición incluye una corrección aproximada del baricentro Tierra–Luna.",
            "Dipolo interno de referencia inclinado 9,35°. Las líneas muestran el modelo, no el campo terrestre medido de hoy ni sus tormentas.",
            "textures/earth.png",6371.0084,23.93447232,23.44,fieldAtMeanRadius(30.6,6378,6371.0084),9.35,1.00000261,365.256,
            FIELD_INTRINSIC,rgba(.29f,.56f,.91f),0,0),
        planet(4,"Marte","Rocoso","Desiertos, cráteres y una atmósfera tenue. Fobos y Deimos recorren órbitas cercanas.",
            "Sin dipolo global actual. Hay magnetismo de la corteza y un campo inducido por el viento solar; los arcos locales son ilustrativos.",
            "",3389.5,24.62296224,25.19,0,0,1.52371034,686.980,
            FIELD_INDUCED_CRUSTAL,rgba(.77f,.36f,.19f),0,0),
        planet(5,"Júpiter","Gigante gaseoso","Bandas de nubes, la Gran Mancha Roja, anillos tenues y las cuatro grandes lunas galileanas.",
            "Potente campo interno. Dipolo de referencia inclinado 9,4°; se omiten los multipolos y las corrientes de su magnetodisco.",
            "textures/jupiter.jpg",69911,9.92496,3.13,fieldAtMeanRadius(430,71398,69911),9.4,5.202887,4332.589,
            FIELD_INTRINSIC,rgba(.84f,.69f,.52f),123000.0/69911,128940.0/69911),
        planet(6,"Saturno","Gigante gaseoso","Sus anillos de partículas heladas se sitúan en el plano ecuatorial. Titán y Encélado destacan entre sus lunas.",
            "Dipolo casi alineado con el giro. Cassini acotó la inclinación a menos de 0,007°; aquí se aproxima a 0°. El campo real tiene multipolos.",
            "",58232,10.65624,26.73,fieldAtMeanRadius(21.5,60330,58232),0,9.53667594,10755.699,
            FIELD_INTRINSIC,rgba(.88f,.77f,.54f),66900.0/58232,139826.0/58232),
        planet(7,"Urano","Gigante de hielo","Gira casi de costado. Su atmósfera y sus anillos oscuros comparten una geometría muy inclinada.",
            "Campo interno inclinado unos 58,6°. Su gran desplazamiento y sus componentes no dipolares hacen que este dipolo sea solo orientativo.",
            "",25362,-17.23992,97.77,fieldAtMeanRadius(22.8,25600,25362),58.6,19.18916464,30685.400,
            FIELD_INTRINSIC,rgba(.48f,.80f,.82f),41837.0/25362,51149.0/25362),
        planet(8,"Neptuno","Gigante de hielo","Atmósfera azulada, vientos intensos y anillos tenues. Tritón recorre una órbita retrógrada.",
            "Campo interno inclinado 46,9°, desplazado y no puramente dipolar. La vista enseña una aproximación de su geometría.",
            "",24622,16.11,28.32,fieldAtMeanRadius(14.2,24765,24622),46.9,30.06992276,60189.018,
            FIELD_INTRINSIC,rgba(.29f,.51f,.81f),41900.0/24622,62933.0/24622),
        moon(9,3,"Luna","Satélite rocoso",1737.4,384400,27.3217,6.68,
            "Mares oscuros y cráteres. La textura procede de LRO; órbita y fase local aproximadas.",
            "Sin dipolo interno global actual. Existen regiones magnetizadas de la corteza; esta vista no reconstruye sus campos locales.",
            "textures/moon.jpg",FIELD_NONE,0,0,rgba(.68f,.68f,.66f)),
        moon(10,5,"Ío","Satélite volcánico",1821.5,421800,1.769138,0,
            "Luna volcánica cercana a Júpiter. Su material alimenta el entorno de plasma joviano. Fase orbital ilustrativa.",
            "Interactúa con el plasma y el campo de Júpiter. No se calcula aquí un modelo magnético local.",
            "",FIELD_NONE,0,0,rgba(.92f,.79f,.34f)),
        moon(11,5,"Europa","Satélite helado",1560.8,671100,3.551181,0,
            "Superficie de hielo surcada por fracturas. Las observaciones apoyan un océano bajo el hielo. Fase orbital ilustrativa.",
            "El campo variable de Júpiter induce una respuesta asociada a su interior conductor. No se representa mediante un dipolo interno permanente.",
            "",FIELD_NONE,0,0,rgba(.82f,.79f,.65f)),
        moon(12,5,"Ganímedes","Satélite helado",2631.2,1070400,7.154553,0,
            "La mayor luna del sistema solar. Posee un campo magnético propio dentro del entorno de Júpiter. Fase orbital ilustrativa.",
            "Dipolo interno medido por Galileo: coeficiente ecuatorial de referencia 0,719 µT e inclinación 176°. Se omite la respuesta inducida.",
            "",FIELD_INTRINSIC,.719,176,rgba(.54f,.52f,.47f)),
        moon(13,5,"Calisto","Satélite helado",2410.3,1882700,16.689017,0,
            "Superficie antigua y muy craterizada. Es la más exterior de las cuatro grandes lunas galileanas. Fase orbital ilustrativa.",
            "Inmersa en el campo joviano; las respuestas inducidas no se calculan en esta vista.",
            "",FIELD_NONE,0,0,rgba(.43f,.40f,.35f)),
        moon(14,6,"Titán","Satélite con atmósfera",2575,1221870,15.945421,0,
            "Atmósfera espesa y bruma anaranjada. En su superficie hay líquidos de metano y etano. Fase orbital ilustrativa.",
            "Interactúa con la magnetosfera de Saturno; el campo local no se calcula en esta vista.",
            "",FIELD_NONE,0,0,rgba(.85f,.60f,.22f)),
        moon(15,6,"Encélado","Satélite helado",252.1,238020,1.370218,0,
            "Pequeña luna de hielo con un océano interior y chorros cerca del polo sur. Fase orbital ilustrativa.",
            "Su material interactúa con el plasma de Saturno. No se calcula un campo magnético local.",
            "",FIELD_NONE,0,0,rgba(.93f,.96f,.97f)),
        moon(16,8,"Tritón","Satélite helado",1353.4,354760,5.876854,157.345,
            "Gran luna de Neptuno con órbita retrógrada y superficie helada. La inclinación orbital se conserva; la fase es ilustrativa.",
            "Se encuentra en el entorno magnético de Neptuno. No se calcula un campo local.",
            "",FIELD_NONE,0,0,rgba(.77f,.76f,.72f)),
        moon(17,4,"Fobos","Satélite irregular",11.1,9378,.31891,0,
            "Pequeño satélite irregular que orbita Marte en unas 7,65 horas. La esfera y la fase orbital son aproximaciones.",
            "Esta vista no incluye un modelo magnético local.","",FIELD_NONE,0,0,rgba(.41f,.36f,.30f)),
        moon(18,4,"Deimos","Satélite irregular",6.2,23459,1.26244,0,
            "Pequeño satélite irregular exterior de Marte. La esfera y la fase orbital son aproximaciones.",
            "Esta vista no incluye un modelo magnético local.","",FIELD_NONE,0,0,rgba(.53f,.48f,.41f)),
        moon(19,6,"Rea","Satélite helado",763.8,527040,4.5175,0,
            "Luna helada de Saturno con numerosos cráteres. Fase orbital ilustrativa.",
            "Inmersa en el entorno magnético de Saturno; no se calcula un campo local.","",FIELD_NONE,0,0,rgba(.72f,.72f,.69f)),
        moon(20,7,"Titania","Satélite helado",788.9,436300,8.705867,0,
            "La mayor luna de Urano. Su órbita acompaña el plano ecuatorial inclinado del planeta. Fase orbital ilustrativa.",
            "Inmersa en el campo de Urano; no se calcula un campo local.","",FIELD_NONE,0,0,rgba(.60f,.59f,.57f)),
        moon(21,7,"Oberón","Satélite helado",761.4,583500,13.463234,0,
            "Luna exterior de Urano con superficie craterizada. Fase orbital ilustrativa.",
            "El entorno magnético depende de Urano y del viento solar; no se calcula un modelo local.","",FIELD_NONE,0,0,rgba(.51f,.49f,.47f)),
        moon(22,7,"Miranda","Satélite helado",235.8,129900,1.413479,0,
            "Pequeña luna de Urano con terrenos y escarpes muy variados. Fase orbital ilustrativa.",
            "Inmersa en el campo de Urano; no se calcula un campo local.","",FIELD_NONE,0,0,rgba(.67f,.66f,.63f))
    };

    /**
     * Current JPL gravitational parameters (km^3/s^2) for the bodies in this
     * catalogue. Planet values use DE/planetary satellite solutions and moon
     * values use the corresponding satellite ephemerides. These parameters
     * drive local gravity only; the existing JPL analytical orbit model stays
     * unchanged.
     */
    private static final double[] GM_KM3_S2 = {
        132712440041.279419, // Sun
        22031.868551,        // Mercury
        324858.592000,       // Venus
        398600.436,          // Earth, DE440
        42828.37362,         // Mars, MAR097
        126686531.9,         // Jupiter, JUP365
        37931206.23,         // Saturn, SAT441
        5793951.3,           // Uranus, URA111
        6835099.97,          // Neptune, NEP097
        4902.800,            // Moon, DE440
        5959.91547,          // Io, JUP365
        3202.71210,          // Europa, JUP365
        9887.83275,          // Ganymede, JUP365
        7179.28340,          // Callisto, JUP365
        8978.13710,          // Titan, SAT441
        7.21037,             // Enceladus, SAT441
        1428.49546,          // Triton, NEP097
        .0007087,            // Phobos, MAR097
        .0000962,            // Deimos, MAR097
        153.94175,           // Rhea, SAT441
        226.9,               // Titania, URA111
        205.3,               // Oberon, URA111
        4.3                  // Miranda, URA111
    };

    /** Gravitational parameter of the selected body, km^3/s^2. */
    public static double gmKm3S2(int id) {
        checkId(id);
        return GM_KM3_S2[id];
    }

    /**
     * Spherical two-body local gravity at altitude above the catalogue mean
     * radius. Returns m/s^2. Rotation, J2, tides and terrain are deliberately
     * outside this local model.
     */
    public static double gravityMs2(int id, double altitudeKm) {
        checkId(id); requireFinite(altitudeKm,"altitude");
        final double radius = BODIES[id].radiusKm + altitudeKm;
        if (!(radius > 0)) throw new IllegalArgumentException("altitude reaches the body centre");
        return GM_KM3_S2[id] / (radius * radius) * 1000.0;
    }

    /** Two-body escape speed at the same spherical radius, km/s. */
    public static double escapeVelocityKmS(int id, double altitudeKm) {
        checkId(id); requireFinite(altitudeKm,"altitude");
        final double radius = BODIES[id].radiusKm + altitudeKm;
        if (!(radius > 0)) throw new IllegalArgumentException("altitude reaches the body centre");
        return Math.sqrt(2.0 * GM_KM3_S2[id] / radius);
    }

    /** Circular-orbit speed around an isolated spherical body, km/s. */
    public static double circularVelocityKmS(int id, double altitudeKm) {
        checkId(id); requireFinite(altitudeKm,"altitude");
        final double radius = BODIES[id].radiusKm + altitudeKm;
        if (!(radius > 0)) throw new IllegalArgumentException("altitude reaches the body centre");
        return Math.sqrt(GM_KM3_S2[id] / radius);
    }

    // JPL Table 1: a,e,I,L,longitude of perihelion,longitude of ascending node.
    // First row is J2000; second row is change per Julian century.
    private static final double[][][] ELEMENTS = {
        null,
        {{.38709927,.20563593,7.00497902,252.25032350,77.45779628,48.33076593},
         {.00000037,.00001906,-.00594749,149472.67411175,.16047689,-.12534081}},
        {{.72333566,.00677672,3.39467605,181.97909950,131.60246718,76.67984255},
         {.00000390,-.00004107,-.00078890,58517.81538729,.00268329,-.27769418}},
        {{1.00000261,.01671123,-.00001531,100.46457166,102.93768193,0},
         {.00000562,-.00004392,-.01294668,35999.37244981,.32327364,0}},
        {{1.52371034,.09339410,1.84969142,-4.55343205,-23.94362959,49.55953891},
         {.00001847,.00007882,-.00813131,19140.30268499,.44441088,-.29257343}},
        {{5.202887,.04838624,1.30439695,34.39644051,14.72847983,100.47390909},
         {-.00011607,-.00013253,-.00183714,3034.74612775,.21252668,.20469106}},
        {{9.53667594,.05386179,2.48599187,49.95424423,92.59887831,113.66242448},
         {-.00125060,-.00050991,.00193609,1222.49362201,-.41897216,-.28867794}},
        {{19.18916464,.04725744,.77263783,313.23810451,170.95427630,74.01692503},
         {-.00196176,-.00004397,-.00242939,428.48202785,.40805281,.04240589}},
        {{30.06992276,.00859048,1.77004347,-55.12002969,44.96476227,131.78422574},
         {.00026291,.00005105,.00035372,218.45945325,-.32241464,-.00508664}}
    };

    // e, inclination to parent equator (Moon: ecliptic), illustrative phase degrees.
    // Inclination >90 degrees, rather than negative mean motion, encodes Triton's retrograde orbit.
    private static final double[][] MOON_ELEMENTS = {
        {.0549,5.145,40}, {.004,.04,15}, {.009,.47,112}, {.001,.18,231},
        {.007,.19,312}, {.0292,.33,83}, {.0045,0,270}, {.000016,157.345,160},
        {.0151,1.08,30}, {.0005,1.79,220}, {.001,.35,340},
        {.0011,.08,55}, {.0014,.07,205}, {.0013,4.34,135}
    };

    // IAU north pole RA/declination at J2000, degrees; used to orient satellite planes.
    // Pole motion/nutation and satellite node/apsidal precession are omitted.
    private static final double[][] NORTH_POLES = {
        {286.13,63.87}, {281.0103,61.4155}, {272.76,67.16}, {0,90},
        {317.681,52.887}, {268.057,64.495}, {40.589,83.537},
        {257.311,-15.175}, {299.36,43.46}
    };

    private static Body planet(int id, String name, String type, String description,
                               String fieldDescription, String texture, double radius,
                               double rotation, double tilt, double field, double fieldTilt,
                               double a, double period, int fieldType, float[] color,
                               double ringInner, double ringOuter) {
        return new Body(id,0,name,type,description,fieldDescription,texture,radius,rotation,
            tilt,field,fieldTilt,a,period,fieldType,color,ringInner,ringOuter);
    }

    private static Body moon(int id, int parent, String name, String type, double radius,
                             double aKm, double period, double tilt, String description,
                             String fieldDescription, String texture, int fieldType,
                             double field, double fieldTilt, float[] color) {
        double spin = period * 24.0 * (id == 16 ? -1 : 1);
        return new Body(id,parent,name,type,description,fieldDescription,texture,radius,spin,
            tilt,field,fieldTilt,aKm/AU_KM,period,fieldType,color,0,0);
    }

    private static float[] rgba(float r, float g, float b) { return new float[]{r,g,b,1}; }

    private static double fieldAtMeanRadius(double coefficient, double referenceKm, double meanKm) {
        double scale = referenceKm / meanKm;
        return coefficient * scale * scale * scale;
    }

    public static double boundedDate(double jd) {
        requireFinite(jd,"Julian date");
        return Math.max(MIN_JD,Math.min(MAX_JD,jd));
    }

    /** Heliocentric analytical position in AU. Dates are clamped to model validity. */
    public static double[] position(int id, double jd) {
        checkId(id);
        return toRenderer(positionEcliptic(id,boundedDate(jd)));
    }

    /**
     * Heliocentric point on the instantaneous orbital ellipse, fraction in turns.
     * The parent remains at its position at jd. Phase zero is the periapsis.
     * Earth receives its instantaneous, small Earth-Moon barycentric correction.
     */
    public static double[] orbitPosition(int id, double fraction, double jd) {
        checkId(id);
        requireFinite(fraction,"orbit fraction");
        double t = boundedDate(jd);
        if (id == 0) return new double[]{0,0,0};
        double m = TWO_PI * (fraction - Math.floor(fraction));
        if (id <= 8) {
            double[] p = planetPositionEcliptic(id,t,m,true);
            if (id == 3) subtractEarthBarycentricOffset(p,t);
            return toRenderer(p);
        }
        double[] p = positionEcliptic(BODIES[id].parent,t);
        addInPlace(p,moonRelativeEcliptic(id,t,m,true));
        return toRenderer(p);
    }

    private static double[] positionEcliptic(int id, double jd) {
        if (id == 0) return new double[]{0,0,0};
        if (id <= 8) {
            double[] p = planetPositionEcliptic(id,jd,0,false);
            if (id == 3) subtractEarthBarycentricOffset(p,jd);
            return p;
        }
        double[] p = positionEcliptic(BODIES[id].parent,jd);
        addInPlace(p,moonRelativeEcliptic(id,jd,0,false));
        return p;
    }

    private static void subtractEarthBarycentricOffset(double[] earth, double jd) {
        double[] relative = moonRelativeEcliptic(9,jd,0,false);
        for (int k=0;k<3;k++) earth[k] -= relative[k]/(1.0+EARTH_MOON_MASS_RATIO);
    }

    private static double[] planetPositionEcliptic(int id, double jd, double m, boolean overrideM) {
        double t = (jd-J2000)/36525.0;
        double[] e = new double[6];
        for (int k=0;k<6;k++) e[k] = ELEMENTS[id][0][k]+ELEMENTS[id][1][k]*t;
        if (!overrideM) m = (e[3]-e[4])*DEG;
        return ellipse(e[0],e[1],e[2]*DEG,(e[4]-e[5])*DEG,e[5]*DEG,m);
    }

    private static double[] moonRelativeEcliptic(int id, double jd, double m, boolean overrideM) {
        Body b = BODIES[id];
        double[] e = MOON_ELEMENTS[id-9];
        if (!overrideM) m = (e[2]/360.0+(jd-J2000)/b.orbitalPeriodDays)*TWO_PI;
        // Fixed nodes/apsides are illustrative. For the Moon the reference plane is ecliptic.
        double node = (id == 9 ? 125.08 : (id*29)%360)*DEG;
        double omega = (id == 9 ? 318.15 : (id*17)%360)*DEG;
        double[] local = ellipse(b.semiMajorAU,e[0],e[1]*DEG,omega,node,m);
        if (id == 9) return local;
        double[] pole = poleEcliptic(b.parent);
        // Regular Uranian moons orbit with the planet's retrograde angular momentum.
        if (BODIES[b.parent].rotationHours < 0) {
            for (int k=0;k<3;k++) pole[k] = -pole[k];
        }
        double[] x = cross(new double[]{0,0,1},pole);
        if (norm(x)<1e-12) x = new double[]{1,0,0};
        else normalizeInPlace(x);
        double[] y = cross(pole,x);
        return new double[]{
            x[0]*local[0]+y[0]*local[1]+pole[0]*local[2],
            x[1]*local[0]+y[1]*local[1]+pole[1]*local[2],
            x[2]*local[0]+y[2]*local[1]+pole[2]*local[2]
        };
    }

    private static double[] ellipse(double a, double e, double inc, double omega,
                                     double node, double meanAnomaly) {
        double m = Math.IEEEremainder(meanAnomaly,TWO_PI);
        double eccentricAnomaly = m+e*Math.sin(m);
        for (int k=0;k<16;k++) {
            double delta = (eccentricAnomaly-e*Math.sin(eccentricAnomaly)-m)
                /(1-e*Math.cos(eccentricAnomaly));
            eccentricAnomaly -= delta;
            if (Math.abs(delta)<1e-13) break;
        }
        double x = a*(Math.cos(eccentricAnomaly)-e);
        double y = a*Math.sqrt(1-e*e)*Math.sin(eccentricAnomaly);
        double cw=Math.cos(omega),sw=Math.sin(omega),cn=Math.cos(node),sn=Math.sin(node);
        double ci=Math.cos(inc),si=Math.sin(inc);
        return new double[]{
            (cw*cn-sw*sn*ci)*x+(-sw*cn-cw*sn*ci)*y,
            (cw*sn+sw*cn*ci)*x+(-sw*sn+cw*cn*ci)*y,
            sw*si*x+cw*si*y
        };
    }

    /**
     * Centred vacuum dipole field in microteslas, expressed in the input frame.
     * r is in reference-body radii. At r=1 equator, |B|=equatorialMicroT.
     * For r<1 returns zero because the exterior model does not apply there;
     * this sentinel does not assert that the physical interior has zero field.
     * The supplied axis is normalized. Zero axes or invalid values are rejected.
     */
    public static double[] dipoleField(double[] rInPlanetRadii, double[] unitAxis,
                                       double equatorialMicroT) {
        checkVector(rInPlanetRadii,"position");
        checkVector(unitAxis,"dipole axis");
        requireFinite(equatorialMicroT,"dipole coefficient");
        double axisNorm = norm(unitAxis);
        if (axisNorm<1e-30 || !Double.isFinite(axisNorm))
            throw new IllegalArgumentException("Dipole axis must have nonzero finite length");
        double radius = norm(rInPlanetRadii);
        if (radius<1 || equatorialMicroT==0) return new double[]{0,0,0};
        if (!Double.isFinite(radius)) return new double[]{0,0,0};
        double[] rhat = new double[3], axis = new double[3];
        for (int k=0;k<3;k++) {
            rhat[k]=rInPlanetRadii[k]/radius;
            axis[k]=unitAxis[k]/axisNorm;
        }
        double dot = rhat[0]*axis[0]+rhat[1]*axis[1]+rhat[2]*axis[2];
        double scale = equatorialMicroT/radius/radius/radius;
        return new double[]{scale*(3*dot*rhat[0]-axis[0]),
            scale*(3*dot*rhat[1]-axis[1]),scale*(3*dot*rhat[2]-axis[2])};
    }

    /** Angular-momentum direction at J2000 in renderer coordinates; planets/Sun only. */
    public static double[] spinAxis(int id) {
        checkId(id);
        if (id>8) throw new IllegalArgumentException("Moon spin poles are not modelled");
        double[] axis = poleEcliptic(id);
        if (BODIES[id].rotationHours<0) for(int k=0;k<3;k++) axis[k]=-axis[k];
        return toRenderer(axis);
    }

    private static double[] poleEcliptic(int id) {
        double ra=NORTH_POLES[id][0]*DEG,dec=NORTH_POLES[id][1]*DEG;
        double x=Math.cos(dec)*Math.cos(ra),y=Math.cos(dec)*Math.sin(ra),z=Math.sin(dec);
        double ob=23.43928*DEG;
        return new double[]{x,Math.cos(ob)*y+Math.sin(ob)*z,-Math.sin(ob)*y+Math.cos(ob)*z};
    }

    private static double[] toRenderer(double[] p) { return new double[]{p[0],p[2],-p[1]}; }
    private static double norm(double[] p) { return Math.hypot(Math.hypot(p[0],p[1]),p[2]); }
    private static void normalizeInPlace(double[] p) {
        double length=norm(p);
        for(int k=0;k<3;k++) p[k]/=length;
    }
    private static double[] cross(double[] a,double[] b) {
        return new double[]{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    }
    private static void addInPlace(double[] a,double[] b) { for(int k=0;k<3;k++) a[k]+=b[k]; }
    private static void checkId(int id) {
        if(id<0 || id>=BODIES.length) throw new IllegalArgumentException("Unknown body id: "+id);
    }
    private static void checkVector(double[] v,String label) {
        if(v==null || v.length<3) throw new IllegalArgumentException(label+" needs three components");
        for(int k=0;k<3;k++) requireFinite(v[k],label);
    }
    private static void requireFinite(double n,String label) {
        if(!Double.isFinite(n)) throw new IllegalArgumentException(label+" must be finite");
    }
}
