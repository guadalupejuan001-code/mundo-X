#!/usr/bin/env python3
"""Export measured product-state growth from a stress_architecture JSONL run.

Requires matplotlib. Usage: python3 tools/plot_architecture_limits.py
  /path/to/results.jsonl /path/to/output/limits
"""
import argparse
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("output_prefix", type=Path)
    args = parser.parse_args()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    rows = [json.loads(line) for line in args.results.read_text().splitlines() if line.strip()]
    selected = [r for r in rows if r.get("family") == "product_ht" and r["status"] == "ok"]
    if not selected:
        parser.error("No successful product_ht measurements found")
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.7), layout="constrained")
    methods = [("reduced", "Reduced", "#7132a8"), ("dense", "Denso double", "#127b86")]
    for method, label, color in methods:
        data = [r for r in selected if r.get("method") == method]
        ns = sorted({r["qubits"] for r in data})
        if not ns:
            continue
        median = lambda key, n: statistics.median(r[key] for r in data if r["qubits"] == n)
        if method == "reduced":
            axes[0].plot(ns, [median("component_count", n) for n in ns], "o-", color=color)
        axes[1].plot(ns, [median("process_peak_rss_bytes", n) / 1024**2 for n in ns],
                     "o-", label=label, color=color)
        axes[2].plot(ns, [median("evolution_ms", n) for n in ns],
                     "o-", label=label, color=color)
    axes[0].set(title="Componentes almacenados", ylabel="Número de componentes")
    axes[1].set(title="Memoria total del proceso", ylabel="Pico RSS (MiB)")
    axes[2].set(title="Tiempo de evolución", ylabel="Milisegundos")
    axes[0].set_yscale("log", base=2)
    axes[1].set_yscale("log", base=2)
    axes[2].set_yscale("log")
    for ax in axes:
        ax.set_xlabel("Qubits")
        ax.grid(True, alpha=0.2)
    axes[1].legend(frameon=False)
    axes[2].legend(frameon=False)
    frontiers = [r for r in rows if r.get("family") == "product_ht" and
                 r.get("method") == "reduced" and r["status"] not in ("ok", "adaptive_skipped")]
    if frontiers:
        first = min(frontiers, key=lambda r: r["qubits"])
        label = f"Reduced, {first['qubits']} qubits:\n{first['status']}\nbajo el presupuesto de la prueba"
        axes[0].text(0.04, 0.96, label, transform=axes[0].transAxes, va="top", fontsize=9)
    fig.suptitle("Mundo X: crecimiento en estados producto H → T", fontsize=14)
    fig.supxlabel("Mismo circuito · Mediana si hay repeticiones · Evolución excluye inicialización y validación\n"
                  "Los puntos fallidos no se presentan como tiempos completados. RSS incluye el worker completo.",
                  fontsize=9)
    args.output_prefix.parent.mkdir(parents=True, exist_ok=True)
    for suffix in (".png", ".pdf", ".svg"):
        path = args.output_prefix.with_suffix(suffix)
        fig.savefig(path, dpi=180)
        if suffix == ".svg":
            path.write_text("\n".join(line.rstrip() for line in path.read_text().splitlines()) + "\n")
        print(path)


if __name__ == "__main__":
    main()
