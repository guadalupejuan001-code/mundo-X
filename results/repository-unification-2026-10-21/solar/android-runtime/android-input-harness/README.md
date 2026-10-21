# Android case 7 input reconstruction

This diagnostic runs Java preparation from the unchanged candidate `SolarRenderer.class` and the existing host JNI. It never calls `onDrawFrame`, GLES, Gradle, or an Android process.

The commands, original JNI SHA-256, unchanged renderer source SHA-256, and class SHA-256 are in `execution.json`. `manifest.json` compares all ten consumed production Java sources in the compiled candidate snapshot with the repository: all are identical. The only adapters are asset access and the three camera matrix operations, whose resulting `vp` is checked against Android.

`reproduce.py` also compiles those unchanged production Java sources directly, so the archived harness does not depend on retaining generated candidate classes. Supply `--repo`, `--jdk`, `--android-jar`, `--jni-dir`, and a separate `--work` directory. This source reconstruction was executed successfully: its fixture SHA-256 is identical to the original extraction, and its comparison again matches 28 of 29 fields. The commands and result are preserved in `source-reproduction/reproduction.json`. A compatible host JNI remains an explicit prerequisite; no generated shared library or class files are archived here.

## Result

`output/fixture-android-case-7.json` is the input packet for the checker diagnostic. It includes `time=7.25`, `atmosphereEnabled=true`, `coupling=1`, `tileSize=128`, and the actual Android texture order (`earth.png`, `jupiter.jpg`, `moon.jpg`). The ordinary checker must respect these optional values before calling this an Android input reproduction.

`android-input-comparison.json` compares every recorded input hash with both Android APK runs. Case 7 matches 28 of 29 fields exactly, including all shader inputs, camera values, the projection/view matrix, native quantum/Ether coefficients, and both float overlay vertex buffers. The remaining difference is the double `orbitCache`; the actual float `lines` buffer sent to GL is identical. This is not a claim that every Java intermediate or the entire Android rendering pipeline was reproduced.

The controls are replayed through cases 0 to 7. This matters because `entryAltitude` survives `focus()`. Moon poles use the same fixed date and code as the instrumentation. Host `Math` may produce different last bits in double intermediates; those are retained and identified by the hash comparison, without rounding or relaxing it.

## Tile-boundary lead

At 360 by 203 pixels the visual plan is a 3 by 2 grid with masks:

`[2097280, 6291584, 4194432, 2097280, 7340160, 5243008]`

For x=153 through 205, the reference's normalized grid switches to row 1 at y=101, while the candidate's integer 128-pixel lookup remains in row 0 until y=128. Mask 1 XOR mask 4 is 1048576, only body 20 (Titania).

Titania's centre projects to approximately (268.35, 278.78), with a projected radius near 7.35 pixels. A CPU diagnostic using the exact float input packet gives sphere discriminants from -89.77 to -71.33 across the 53 affected pixels: the extra candidate should be rejected with a wide margin. The Uranus ring ray denominator is approximately 0.596 to 0.757 there, so this row is not parallel to that ring plane. Uranus's surface branch does not consume longitude and evaluates the same FBM in both source variants. These are localization findings, not yet the cause of the GLES image difference.

The fixture was handed to the owner of the single GLES diagnostic process for shader-variant isolation. No image-equivalence assertion was changed here.
