#!/usr/bin/env python3
"""Plot historical measured runs from saved JSONL; never reruns benchmarks.

The controller has been removed from active source. Legacy method labels identify
archived measurements; this plotter does not import or execute controller code.

Example:
  python3 tools/plot_factorized.py --input /path/full-01/results.jsonl \
      --overlay /path/range-label-02/results.jsonl \
      --extended /path/extended-memory-03/results.jsonl \
      --output-dir results/factorized_exams_figures

Each point is an observed worker run, not an estimate with confidence bounds.
Overlay rows replace matching (family, method, qubits, scenario, repeat) rows.
Payload plots count complex<double> coefficients only; RSS is plotted separately.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import LogLocator, NullFormatter

MIB = 1024 ** 2
METHODS = ("factorized", "computasis", "reduced", "dense")
STYLES = {
    "factorized": dict(label="Factorizado", color="#2563eb", marker="o", linestyle="-"),
    "computasis": dict(label="Computasis", color="#0f766e", marker="s", linestyle="--"),
    "reduced": dict(label="Reduced", color="#d97706", marker="D", linestyle="--"),
    "dense": dict(label="Denso double", color="#7c3aed", marker="x", linestyle="-"),
}


def key(row):
    return tuple(row.get(name, default) for name, default in
                 (("family", None), ("method", None), ("qubits", None),
                  ("scenario", "default"), ("repeat", 1)))


def load(paths):
    records = {}
    sources = []
    for path in paths:
        payload = path.read_bytes()
        sources.append({"path": str(path), "sha256": hashlib.sha256(payload).hexdigest()})
        seen = set()
        for line in payload.decode().splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            identity = key(row)
            if identity in seen:
                raise ValueError(f"duplicate run identity within {path}: {identity}")
            seen.add(identity)
            records[identity] = row
    return list(records.values()), sources


def family_rows(rows, family, method, scenario="default"):
    return sorted((r for r in rows if r["family"] == family and r["method"] == method
                   and r["status"] == "ok" and r.get("scenario", "default") == scenario),
                  key=lambda r: r["qubits"])


def decimal(value, places=2):
    return f"{value:.{places}f}".replace(".", ",")


def style_axis(ax, *, logx=False, logy=True):
    if logx:
        ax.set_xscale("log")
        ax.xaxis.set_minor_formatter(NullFormatter())
    if logy:
        ax.set_yscale("log")
        ax.yaxis.set_minor_locator(LogLocator(base=10, subs=(2, 5)))
        ax.yaxis.set_minor_formatter(NullFormatter())
    ax.grid(True, which="major", linewidth=0.7, color="#d1d5db")
    ax.grid(True, which="minor", linewidth=0.4, color="#e5e7eb", alpha=0.65)
    ax.spines[["top", "right"]].set_visible(False)
    ax.set_xlabel("Número de qubits, N")
    ax.tick_params(labelsize=10)


def plot_series(ax, rows, family, field, divisor=1.0):
    for method in METHODS:
        data = [r for r in family_rows(rows, family, method) if r.get(field) is not None]
        if not data:
            continue
        ax.plot([r["qubits"] for r in data], [r[field] / divisor for r in data],
                **STYLES[method], markersize=5, linewidth=1.55,
                markerfacecolor="white" if method in {"computasis", "reduced"} else STYLES[method]["color"],
                markeredgewidth=1.1)


def save(fig, destination, name, source_label):
    outputs = []
    for extension in ("png", "pdf"):
        path = destination / f"{name}.{extension}"
        metadata = {"Title": name, "Creator": "tools/plot_factorized.py", "Subject": source_label}
        if extension == "png":
            metadata = {"Title": name, "Source": source_label, "Software": "tools/plot_factorized.py"}
        fig.savefig(path, dpi=180, facecolor="white", metadata=metadata)
        outputs.append(str(path))
    plt.close(fig)
    return outputs


def resources(rows, destination, source_label):
    fig, axes = plt.subplots(1, 2, figsize=(12.6, 6.2))
    fig.subplots_adjust(left=0.085, right=0.98, bottom=0.25, top=0.80, wspace=0.30)
    fig.suptitle("Producto H→T: conservar la independencia cambia el costo", fontsize=16, x=0.52, y=0.96)
    fig.text(0.52, 0.895, "Puntos observados · una ejecución por configuración · sin exportar el estado completo", ha="center", fontsize=10.5, color="#4b5563")
    for ax in axes:
        style_axis(ax, logx=True)
    plot_series(axes[0], rows, "product_ht", "logical_amplitude_bytes", MIB)
    plot_series(axes[1], rows, "product_ht", "process_peak_rss_bytes", MIB)
    axes[0].set_title("Carga lógica de amplitudes", loc="left", fontsize=12, pad=12)
    axes[1].set_title("Pico de memoria del proceso (RSS)", loc="left", fontsize=12, pad=12)
    axes[0].set_ylabel("Amplitudes complejas double [MiB]")
    axes[1].set_ylabel("Memoria residente observada [MiB]")
    factor = {r["qubits"]: r for r in family_rows(rows, "product_ht", "factorized")}
    if 22 in factor:
        payload = factor[22]["logical_amplitude_bytes"]
        axes[0].annotate(f"N=22: {payload} bytes\nFactorizado y Computasis", xy=(22, payload / MIB),
                         xytext=(110, 0.007), fontsize=9,
                         arrowprops=dict(arrowstyle="-", color="#6b7280"), color="#1f2937")
    if 100000 in factor:
        rss = factor[100000]["process_peak_rss_bytes"] / MIB
        logical = factor[100000]["logical_amplitude_bytes"] / MIB
        axes[1].annotate(f"N=100.000: {decimal(rss)} MiB RSS\n{decimal(logical)} MiB de amplitudes", xy=(100000, rss),
                         xytext=(160, 55), fontsize=9,
                         arrowprops=dict(arrowstyle="-", color="#6b7280"), color="#1f2937")
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=4, loc="lower center", bbox_to_anchor=(0.52, 0.125), frameon=False, fontsize=10)
    fig.text(0.085, 0.085, "La carga lógica excluye índices, metadatos, asignador y temporales; no equivale a RAM.", fontsize=10, color="#374151")
    fig.text(0.085, 0.045, "Reduced y Denso coinciden en carga lógica; Factorizado y Computasis también. MiB = 2²⁰ bytes.", fontsize=9, color="#4b5563")
    return save(fig, destination, "producto_ht_memoria", source_label)


def ghz_limits(rows, destination, source_label):
    fig, axes = plt.subplots(1, 2, figsize=(12.6, 6.2))
    fig.subplots_adjust(left=0.085, right=0.98, bottom=0.25, top=0.80, wspace=0.30)
    fig.suptitle("GHZ: el prototipo de bloques densos conserva un límite exponencial", fontsize=15, x=0.52, y=0.96)
    fig.text(0.52, 0.895, "Una ejecución por punto · tiempos observados, sin estimación estadística del rendimiento", ha="center", fontsize=10.5, color="#4b5563")
    complete_factor = family_rows(rows, "ghz", "factorized")
    cap = complete_factor[-1]["max_block_qubits"] if complete_factor else 20
    for ax in axes:
        style_axis(ax)
        ax.set_xlim(1.5, 24.5)
        ax.set_xticks((2, 4, 8, 12, 16, 20, 24))
        ax.axvspan(cap + 0.5, 24.5, facecolor="#fee2e2", alpha=0.55, zorder=-5)
        ax.axvline(cap + 0.5, color="#b91c1c", linewidth=1.0, linestyle=":")
    plot_series(axes[0], rows, "ghz", "logical_amplitude_bytes", MIB)
    plot_series(axes[1], rows, "ghz", "evolution_ms")
    axes[0].set_title("Carga lógica de amplitudes", loc="left", fontsize=12, pad=12)
    axes[1].set_title("Tiempo de evolución observado", loc="left", fontsize=12, pad=12)
    axes[0].set_ylabel("Amplitudes complejas double [MiB]")
    axes[1].set_ylabel("Tiempo de evolución [ms]")
    axes[0].annotate("Reduced: un componente\nen toda esta familia", xy=(12, 16 / MIB),
                     xytext=(5.5, 0.00015), fontsize=9,
                     arrowprops=dict(arrowstyle="-", color="#6b7280"))
    if complete_factor:
        last = complete_factor[-1]
        logical = last["logical_amplitude_bytes"] / MIB
        axes[0].annotate(f"N={last['qubits']}: {decimal(logical, 0)} MiB\npor bloque denso", xy=(last["qubits"], logical),
                         xytext=(10, 2), fontsize=9,
                         arrowprops=dict(arrowstyle="-", color="#6b7280"))
    axes[1].text(22.4, 0.010, f"Factorizado /\nComputasis:\nrechazo por\nlímite de\n{cap} qubits", ha="center", va="center", fontsize=8.5, color="#991b1b")
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=4, loc="lower center", bbox_to_anchor=(0.52, 0.125), frameon=False, fontsize=10)
    fig.text(0.085, 0.085, "Solo se grafican ejecuciones completadas. El rechazo conserva el estado previo; no completa GHZ.", fontsize=10, color="#374151")
    fig.text(0.085, 0.045, "Los métodos representan el mismo circuito con estructuras diferentes. Carga lógica ≠ RSS; no incluye consultas.", fontsize=9, color="#4b5563")
    return save(fig, destination, "ghz_limites", source_label)


def extended_memory(rows, destination, source_label):
    extended = [r for r in rows if r.get("scenario") == "extended_memory"]
    if not extended or any(r["status"] != "ok" for r in extended):
        raise ValueError("Extended figure requires completed, validated runs; failures must be shown explicitly before plotting.")
    product = {method: family_rows(rows, "product_ht", method, "extended_memory")
               for method in ("factorized", "computasis")}
    ghz = family_rows(rows, "ghz", "factorized", "extended_memory")
    if not all(product.values()) or not ghz:
        raise ValueError("Extended figure requires both product methods and the separate GHZ annex.")
    factor = product["factorized"]
    controller = product["computasis"]
    if [(r["qubits"], r["logical_amplitude_bytes"]) for r in factor] != [
            (r["qubits"], r["logical_amplitude_bytes"]) for r in controller]:
        raise ValueError("Shared payload curve requires matching measured payloads.")

    fig, axes = plt.subplots(1, 3, figsize=(16.4, 7.1))
    fig.subplots_adjust(left=0.065, right=0.985, bottom=0.34, top=0.78, wspace=0.34)
    fig.suptitle("Anexo con más RAM: 10 millones de qubits producto y GHZ de 25 qubits", fontsize=16, y=0.96)
    limit_gib = extended[0]["worker_address_space_limit_bytes"] / (1024 ** 3)
    logical_mib = extended[0]["logical_amplitude_budget_bytes"] / MIB
    cap = extended[0]["max_block_qubits"]
    fig.text(0.52, 0.895,
             f"Límite de proceso: {decimal(limit_gib, 0)} GiB · carga lógica: {decimal(logical_mib, 0)} MiB · bloque máximo: {cap} qubits · {len(extended)}/{len(extended)} ensayos completados",
             ha="center", fontsize=11, color="#4b5563")

    ax = axes[0]
    style_axis(ax, logx=True)
    ax.set_title("Producto H→T: amplitudes y RSS", loc="left", fontsize=12, pad=13)
    ax.set_xlabel("Número de qubits [millones]")
    ax.set_ylabel("Memoria [MiB]")
    x = [r["qubits"] / 1e6 for r in factor]
    ax.plot(x, [r["logical_amplitude_bytes"] / MIB for r in factor],
            color="#2563eb", marker="o", label="Amplitudes: ambos motores")
    for method, data in product.items():
        ax.plot([r["qubits"] / 1e6 for r in data],
                [r["process_peak_rss_bytes"] / MIB for r in data],
                color=STYLES[method]["color"], marker="s" if method == "computasis" else "^",
                markerfacecolor="white", linestyle="--" if method == "factorized" else ":",
                label=f"RSS: {STYLES[method]['label']}")
    ax.set_xticks(x, [decimal(value, 0) for value in x])
    last = factor[-1]
    ax.annotate(f"{decimal(last['logical_amplitude_bytes'] / MIB)} MiB lógicos\n{decimal(last['process_peak_rss_bytes'] / MIB)} MiB RSS",
                xy=(x[-1], last["process_peak_rss_bytes"] / MIB), xytext=(1.15, 850),
                fontsize=9, arrowprops=dict(arrowstyle="-", color="#6b7280"))
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.15), fontsize=9, frameon=False)

    ax = axes[1]
    style_axis(ax, logy=False)
    ax.set_yscale("log", base=2)
    ax.set_title(f"GHZ: presupuesto de bloque {cap}", loc="left", fontsize=12, pad=13)
    ax.set_ylabel("Memoria [MiB]")
    ax.set_xticks([r["qubits"] for r in ghz])
    ax.set_yticks([32, 64, 128, 256, 512, 1024], ["32", "64", "128", "256", "512", "1024"])
    ax.plot([r["qubits"] for r in ghz], [r["logical_amplitude_bytes"] / MIB for r in ghz],
            color="#2563eb", marker="o", label="Amplitudes Factorizado")
    ax.plot([r["qubits"] for r in ghz], [r["process_peak_rss_bytes"] / MIB for r in ghz],
            color="#7c3aed", marker="^", linestyle="--", label="RSS Factorizado")
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.15), fontsize=9, frameon=False)

    ax = axes[2]
    style_axis(ax, logy=False)
    ax.set_title("Producto H→T: 10 millones", loc="left", fontsize=12, pad=13)
    ax.set_xlabel("Motor")
    ax.set_ylabel("Tiempo observado [ms]")
    final_runs = [product[method][-1] for method in ("factorized", "computasis")]
    if any(r["qubits"] != 10000000 for r in final_runs):
        raise ValueError("Time panel requires the measured 10-million-qubit runs.")
    bottoms = [0.0, 0.0]
    for field, label, color in (("initialization_ms", "Inicialización", "#94a3b8"),
                                ("evolution_ms", "Evolución", "#2563eb"),
                                ("query_ms", "Consultas", "#d97706")):
        values = [r[field] for r in final_runs]
        ax.bar([0, 1], values, bottom=bottoms, width=0.55, color=color, label=label)
        bottoms = [a + b for a, b in zip(bottoms, values)]
    ax.set_xticks([0, 1], ["Factorizado", "Computasis"])
    ax.set_ylim(0, max(bottoms) * 1.15)
    for position, total in enumerate(bottoms):
        ax.text(position, total + 60, f"{decimal(total, 0)} ms", ha="center", fontsize=9)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.15), fontsize=9, frameon=False)

    fig.text(0.065, 0.135, "Barras: inicialización + evolución + consultas; excluyen preparación, hash y destrucción del proceso.", fontsize=10, color="#374151")
    fig.text(0.065, 0.09, "Validación mediante consultas locales y amplitudes seleccionadas; sin exportar el estado completo. Tolerancia logarítmica: 10⁻⁸.", fontsize=10, color="#374151")
    fig.text(0.065, 0.045, "Una observación por configuración; no es un benchmark estadístico. Amplitudes ≠ RAM. GHZ permanece separado del ensayo original con límite de 20 qubits.", fontsize=9.5, color="#4b5563")
    return save(fig, destination, "anexo_memoria_ampliada", source_label)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--overlay", action="append", default=[], type=Path)
    parser.add_argument("--extended", action="append", default=[], type=Path,
                        help="Additional annex JSONL; kept separate from default-budget curves")
    parser.add_argument("--output-dir", default=Path("results/factorized_exams_figures"), type=Path)
    args = parser.parse_args()
    rows, sources = load([args.input, *args.overlay, *args.extended])
    args.output_dir.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.family": "DejaVu Sans", "text.color": "#111827", "axes.labelcolor": "#111827", "axes.titleweight": "medium", "pdf.fonttype": 42})
    source_label = json.dumps(sources, ensure_ascii=False)
    files = resources(rows, args.output_dir, source_label)
    files += ghz_limits(rows, args.output_dir, source_label)
    if args.extended:
        files += extended_memory(rows, args.output_dir, source_label)
    print(json.dumps({"sources": sources, "files": files, "merged_records": len(rows), "note": "Single observations; logical payload excludes metadata and differs from RSS."}, indent=2))


if __name__ == "__main__":
    main()
