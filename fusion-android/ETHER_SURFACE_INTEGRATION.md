# Integración de Ether Surface X en Fusion 3D

La ruta de render del visor nuclear 3D de FusionActivity ya utiliza solo
EtherRenderEngine y EtherSurfaceBuffer para cámara, geometría y observación.
ether_fusion_jni.cpp entrega RGBA a un Bitmap Android para la presentación.
El motor no conserva un framebuffer RGBA+Z propio.

Los modelos físicos y el visor de 100 qubits siguen independientes, para
no modificar sus observables MIL. La comparación histórica se restringe
al benchmark, nunca a la ruta de render del visor principal.

fusion-android/tests/ether_surface_fusion.cpp valida ID, profundidad y RGBA
para escenas idénticas y reconstrucción desde el mapa compacto.

Los tiempos de Linux no miden la velocidad de un dispositivo Android.
