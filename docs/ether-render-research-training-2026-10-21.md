# Ether Render X · research-trained evolution

Date: 2026-10-21

This pass does not train neural-network weights. It turns public rendering research into measurable policies and then lets Ether Render X tune itself on the machine that executes it.

## External ideas used

1. **Tile-based mobile rendering.** Khronos and Arm document that mobile GPUs bin work into screen tiles and are often bandwidth-bound. Ether Render X already starts from the same high-level principle: resolve screen regions first, then shade only what can affect them.
   - https://github.khronos.org/Vulkan-Site/guide/latest/tile_based_rendering_best_practices.html
   - https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/console-quality-game-rendering-on-mobile

2. **Front-to-back / early visibility.** Arm recommends front-to-back ordering for opaque work so hidden work can be rejected early. Ether Render X now sorts candidate bodies by a conservative camera-distance lower bound. Once the nearest exact hit is closer than every remaining lower bound, later candidates are skipped without changing the brute-force result.
   - https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-performance-5-an-application-s-performance-responsibilities

3. **Hierarchical demand-driven detail.** Nanite demonstrates hierarchical clusters, demand-driven detail and candidate/visible-cluster accounting. Ether Render X does not copy Nanite or require its hardware/API. The reusable principle is to keep candidate work explicit and bounded before shading.
   - https://dev.epicgames.com/documentation/unreal-engine/nanite-in-unreal-engine

4. **Visibility before expensive shading.** The visibility-buffer literature separates cheap visibility from expensive material work and emphasizes bandwidth at high resolution. Ether Render X v0.1 already separates candidate/nearest-hit resolution from shading; a compact visibility/id-depth stage is a future GPU target.
   - https://jcgt.org/published/0002/02/04/

5. **Cull in useful batches.** GPUOpen's mesh-shader guidance notes that culling itself has latency and should process useful batches rather than create excessive scheduling overhead. Ether Render X therefore does not assume that the smallest possible region is automatically fastest.
   - https://gpuopen.com/learn/mesh_shaders/mesh_shaders-optimization_and_best_practices/

## What changed in code

- Camera-distance lower bounds are computed for every body.
- Candidate traversal is ordered front-to-back.
- Traversal stops only when all remaining bodies are mathematically unable to beat the current nearest hit.
- Equal-depth ties still resolve by body id exactly like the brute-force oracle.
- `render_train` benchmarks tile sizes 8, 16, 32, 64, 128 and 256 on the current machine.
- Each tile candidate uses one warm-up and three measured frames; the median is used.
- All tile candidates must produce the same frame hash.
- The chosen tile is finally checked against the brute-force oracle with zero allowed mismatches.

## What `render_train` learns

It learns one local policy parameter: the screen-region tile size for the current scene, resolution, implementation and machine. It does not claim that the same tile is optimal on a different GPU or Android driver.

Example:

```xasm
render 320 180 32
render_camera 0 0 10 0 0 0 47
render_body 0 0 0 0 1.4 0.2 0.6 1.0
render_train
render_info
```

Training remains subordinate to correctness: no faster policy is accepted if it changes the frame.

## Next research-backed stages

- hierarchical regions instead of one fixed grid;
- compact visibility/id-depth representation before material shading;
- GPU Android implementation that cooperates with tile memory instead of forcing full-frame intermediate traffic;
- opaque front-to-back path that keeps early-Z available; avoid `discard`/`gl_FragDepth` where not required;
- screen-space LOD / demand-driven geometry when mesh support is added;
- physical Galaxy A16 profile so the learned policy is based on the target device, not host timing.
