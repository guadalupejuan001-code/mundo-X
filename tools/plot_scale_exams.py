#!/usr/bin/env python3
"""Plot historical measured scale/storage runs without rerunning computation.

The controller has been removed from active source. Legacy method labels identify
archived measurements; --controller reads saved JSON evidence, not controller code.

python3 tools/plot_scale_exams.py --stream /path/full-stream-01/results.jsonl \
    --controller /path/controller-scale/results.json \
    --output-dir results/scale_exams_figures

The baseline and candidate must use the same streaming benchmark driver. Skips,
failed validations and uncompleted workers never become successful data points.
All curves are single observations, not performance estimates with error bars.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.ticker import NullFormatter

GIB = 1024 ** 3
KIB = 1024
COLORS = {"baseline": "#9a3412", "candidate": "#2563eb"}
VARIANT_NAMES = {"baseline": "Anterior", "candidate": "Plano"}
METHOD_NAMES = {"factorized": "Factorizado", "computasis": "Computasis"}
FAMILY_NAMES = {"product_ht": "Producto H→T", "heterogeneous_product": "Producto heterogéneo",
                "bell_pairs": "Pares de Bell", "ghz_blocks": "Bloques GHZ de 4"}


def read(path, *, jsonl=False):
    payload = path.read_bytes()
    data = [json.loads(line) for line in payload.decode().splitlines() if line.strip()] if jsonl else json.loads(payload)
    return data, {"path": str(path), "sha256": hashlib.sha256(payload).hexdigest()}


def decimal(value, places=2):
    return f"{value:.{places}f}".replace(".", ",")


def successful(rows, *, family, variant="candidate", method="factorized"):
    selected = [r for r in rows if r.get("family") == family and r.get("variant") == variant
                and r.get("method") == method and r.get("status") == "ok"
                and r.get("scaled_tolerance_pass", True)]
    selected.sort(key=lambda r: r["qubits"])
    if len({r["qubits"] for r in selected}) != len(selected):
        raise ValueError("One observation per configuration required; aggregate repeats explicitly before plotting.")
    return selected


def axis_style(ax, *, logx=False, logy=False):
    if logx:
        ax.set_xscale("log")
        ax.xaxis.set_minor_formatter(NullFormatter())
    if logy:
        ax.set_yscale("log")
        ax.yaxis.set_minor_formatter(NullFormatter())
    ax.spines[["top", "right"]].set_visible(False)
    ax.grid(True, color="#d1d5db", linewidth=0.65)
    ax.set_axisbelow(True)
    ax.tick_params(labelsize=9)


def save(fig, directory, name, sources):
    paths = []
    source = json.dumps(sources, ensure_ascii=False)
    for extension in ("png", "pdf"):
        path = directory / f"{name}.{extension}"
        metadata = ({"Title": name, "Source": source, "Software": "tools/plot_scale_exams.py"}
                    if extension == "png" else {"Title": name, "Subject": source, "Creator": "tools/plot_scale_exams.py"})
        fig.savefig(path, dpi=180, facecolor="white", metadata=metadata)
        paths.append(str(path))
    plt.close(fig)
    return paths


def rss_comparison(ax, rows, family):
    for variant in ("baseline", "candidate"):
        for method in ("factorized", "computasis"):
            data = successful(rows, family=family, variant=variant, method=method)
            if not data:
                continue
            ax.plot([r["qubits"] / 1e6 for r in data],
                    [r["process_peak_rss_bytes"] / GIB for r in data],
                    label=f"{VARIANT_NAMES[variant]} · {METHOD_NAMES[method]}",
                    color=COLORS[variant], linestyle="-" if method == "factorized" else "--",
                    marker="o" if method == "factorized" else "s", markersize=4.5,
                    markerfacecolor=COLORS[variant] if method == "factorized" else "white")
    axis_style(ax, logx=True, logy=True)
    ax.set_title(FAMILY_NAMES[family], loc="left", fontsize=12, pad=11)
    ax.set_xlabel("Número de qubits [millones]")
    ax.set_ylabel("Pico RSS del proceso [GiB]")
    ax.set_xticks((1, 10, 30, 100), ("1", "10", "30", "100"))


def memory_figure(rows, directory, sources):
    fig, axes = plt.subplots(2, 2, figsize=(13.9, 10.0))
    fig.subplots_adjust(left=0.085, right=0.98, bottom=0.17, top=0.84, hspace=0.54, wspace=0.29)
    fig.suptitle("Almacenamiento plano: el ahorro depende de la estructura", fontsize=17, y=0.965)
    fig.text(0.52, 0.92, "Anterior y plano ejecutan el mismo generador de circuitos; una observación por configuración", ha="center", fontsize=11, color="#4b5563")
    for ax, family in zip(axes[0], ("product_ht", "heterogeneous_product")):
        rss_comparison(ax, rows, family)
    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", bbox_to_anchor=(0.52, 0.895), ncol=4, frameon=False, fontsize=9)

    ax = axes[1, 0]
    data = successful(rows, family="product_ht")
    x = [r["qubits"] / 1e6 for r in data]
    ax.plot(x, [r["logical_amplitude_bytes"] / GIB for r in data], color="#0f766e", marker="o", label="Amplitudes activas (lógicas)")
    estimates = [r for r in data if r.get("storage_estimate")]
    ax.plot([r["qubits"] / 1e6 for r in estimates],
            [r["storage_estimate"]["estimated_reserved_bytes"] / GIB for r in estimates],
            color="#7c3aed", marker="^", linestyle="--", label="Capacidad y metadatos estimados")
    ax.plot(x, [r["process_peak_rss_bytes"] / GIB for r in data], color="#2563eb", marker="s", linestyle=":", label="RSS observado")
    axis_style(ax, logx=True, logy=True)
    ax.set_title("Plano / Factorizado: lógica, capacidad y RAM", loc="left", fontsize=11.5, pad=11)
    ax.set_xlabel("Número de qubits [millones]")
    ax.set_ylabel("Memoria [GiB]")
    ax.set_xticks((1, 10, 30, 100), ("1", "10", "30", "100"))
    ax.legend(fontsize=8.5, frameon=False, loc="upper left")

    ax = axes[1, 1]
    positions = []
    for family in ("bell_pairs", "ghz_blocks"):
        old = {r["qubits"]: r for r in successful(rows, family=family, variant="baseline")}
        new = {r["qubits"]: r for r in successful(rows, family=family)}
        common = sorted(old.keys() & new.keys())
        if common:
            n = common[-1]
            positions.append((family, n, old[n], new[n]))
    for index, (family, n, old, new) in enumerate(positions):
        for offset, variant, row in ((-0.19, "baseline", old), (0.19, "candidate", new)):
            ax.bar(index + offset, row["process_peak_rss_bytes"] / GIB, width=0.36,
                   color=COLORS[variant], label=VARIANT_NAMES[variant] if index == 0 else None)
    axis_style(ax)
    ax.set_title("Factores conectados: también medir el costo", loc="left", fontsize=11.5, pad=11)
    ax.set_ylabel("Pico RSS Factorizado [GiB]")
    ax.set_xticks(range(len(positions)), [f"{FAMILY_NAMES[f]}\nN={n:,}".replace(",", ".") for f, n, _, _ in positions])
    ax.legend(frameon=False, fontsize=9)
    fig.text(0.085, 0.105, "Las amplitudes singleton siguen reservadas después de unir qubits; guardar factores densos puede aumentar la RAM.", fontsize=10, color="#374151")
    fig.text(0.085, 0.065, "Anterior se ensaya hasta 10 millones; no se extrapolan ejecuciones ausentes. Capacidades estimadas ≠ RSS; GiB = 2³⁰ bytes.", fontsize=9.5, color="#4b5563")
    fig.text(0.085, 0.025, "Sin listas de puertas almacenadas ni materialización del estado global. Solo se grafican ejecuciones completadas y aceptadas por el criterio de escala.", fontsize=9, color="#4b5563")
    return save(fig, directory, "almacenamiento_memoria", sources)


def time_error_figure(rows, directory, sources):
    fig, axes = plt.subplots(1, 2, figsize=(14.5, 7.5), gridspec_kw={"width_ratios": (1.3, 1.0)})
    fig.subplots_adjust(left=0.19, right=0.98, bottom=0.35, top=0.79, wspace=0.32)
    fig.suptitle("Escalar también cuesta preparación, consultas y error numérico", fontsize=16, x=0.53, y=0.965)
    fig.text(0.53, 0.91, "Tiempos observados en un proceso; criterios numéricos registrados antes de la ejecución", ha="center", fontsize=10.5, color="#4b5563")
    final = []
    for family in ("product_ht", "heterogeneous_product"):
        for method in ("factorized", "computasis"):
            data = successful(rows, family=family, method=method)
            final += [r for r in data if r["qubits"] == 100000000]
    ax = axes[0]
    if final:
        phases = []
        for row in final:
            prep = sum(row.get(k, 0) or 0 for k in ("circuit_hash_preparation_ms", "basis_preparation_ms", "query_hash_preparation_ms"))
            components = [prep, row["initialization_ms"], row["evolution_ms"], row["query_ms"]]
            other = row["worker_total_ms"] - sum(components)
            if other < -1.0:
                raise ValueError("Timer phases overlap or total clock is incompatible; do not stack.")
            phases.append(components + [max(0, other)])
        bottoms = [0.0] * len(final)
        for stage, (name, color) in enumerate((("Preparación y hash", "#94a3b8"), ("Inicialización", "#64748b"),
                                               ("Evolución", "#2563eb"), ("Consultas", "#d97706"),
                                               ("Resto, incluida destrucción", "#cbd5e1"))):
            values = [p[stage] / 1000 for p in phases]
            ax.barh(range(len(final)), values, left=bottoms, color=color, label=name, height=0.62)
            bottoms = [a + b for a, b in zip(bottoms, values)]
        ax.set_yticks(range(len(final)), [f"{'H→T' if r['family'] == 'product_ht' else 'Heterogéneo'}\n{METHOD_NAMES[r['method']]}" for r in final])
        ax.invert_yaxis()
        ax.plot([r["process_wall_ms"] / 1000 for r in final], range(len(final)), "kD", markersize=4, label="Proceso completo (supervisor)")
        ax.legend(loc="upper left", bbox_to_anchor=(-0.28, -0.18), ncol=2, fontsize=8.5, frameon=False)
    else:
        ax.text(0.5, 0.5, "No hay ejecución validada de 100 millones", ha="center", transform=ax.transAxes)
    axis_style(ax)
    ax.set_title("100 millones de qubits producto", loc="left", fontsize=11.5, pad=12)
    ax.set_xlabel("Tiempo [s]")

    ax = axes[1]
    series = (("product_ht", "factorized", "#2563eb", "o"),
              ("product_ht", "computasis", "#0f766e", "s"),
              ("heterogeneous_product", "factorized", "#d97706", "D"),
              ("heterogeneous_product", "computasis", "#7c3aed", "x"))
    tolerances = {}
    for family, method, color, marker in series:
        data = successful(rows, family=family, method=method)
        data = [r for r in data if max(r.get("max_log_magnitude_error", 0), r.get("max_log_phase_error", 0)) > 0]
        values = [max(r["max_log_magnitude_error"], r["max_log_phase_error"]) for r in data]
        ax.plot([r["qubits"] / 1e6 for r in data], values, color=color, marker=marker, markersize=4,
                label=f"{'H→T' if family == 'product_ht' else 'Heter.'} · {METHOD_NAMES[method]}")
        for row, value in zip(data, values):
            tolerances[row["qubits"]] = row["scaled_log_tolerance"]
            if not row["legacy_tolerance_pass"]:
                ax.scatter(row["qubits"] / 1e6, value, marker="o", facecolors="none", edgecolors="#b91c1c", linewidths=1.6, s=110, zorder=5)
    ax.axhline(1e-8, color="#b91c1c", linestyle="--", linewidth=1.2, label="Umbral legado: 10⁻⁸")
    ordered = sorted(tolerances.items())
    ax.plot([n / 1e6 for n, _ in ordered], [t for _, t in ordered], color="#111827", linestyle=":", linewidth=1.4, label="Criterio de escala registrado")
    axis_style(ax, logx=True, logy=True)
    ax.set_title("Máximo error de log-magnitud o fase", loc="left", fontsize=11.5, pad=12)
    ax.set_xlabel("Número de qubits [millones]")
    ax.set_ylabel("Error absoluto [log natural o radianes]")
    ax.set_xticks((1, 10, 30, 100), ("1", "10", "30", "100"))
    ax.legend(fontsize=7.8, frameon=False, loc="lower right")
    fig.text(0.065, 0.115, "Anillos rojos: incumple el umbral legado aunque acepte el criterio de escala. Este criterio es heurístico; no es una cota de error demostrada.", fontsize=9.5, color="#374151")
    fig.text(0.065, 0.065, "Barras: fases sin solapamiento + residuo hasta worker_total; el diamante agrega arranque, salida y cierre observados por el supervisor.", fontsize=9, color="#4b5563")
    fig.text(0.065, 0.025, "Una observación por configuración; sin intervalos de confianza. Consultas seleccionadas, sin enumerar las 2ᴺ amplitudes.", fontsize=9, color="#4b5563")
    return save(fig, directory, "tiempos_y_criterios", sources)


def controller_figure(data, directory, sources):
    if data.get("status") != "pass":
        raise ValueError("Controller evidence must pass its checks before plotting.")
    fig, axes = plt.subplots(1, 2, figsize=(12.8, 6.5))
    fig.subplots_adjust(left=0.085, right=0.98, bottom=0.29, top=0.78, wspace=0.32)
    fig.suptitle("Computasis: la recuperación depende de la historia disponible", fontsize=16, y=0.96)
    fig.text(0.52, 0.89, f"{data['qubits']} qubits · 40 bloques · mismo circuito de {data['identical_circuit_gates']} puertas · cortes numéricamente estrictos", ha="center", fontsize=11, color="#4b5563")
    names = ("Historial 32", "Historial 4", "Cortes completos\nexternos")
    variants = [data[name] for name in ("history32", "history4", "explicit_cuts")]
    ax = axes[0]
    recovered = [r["recovered_blocks"] for r in variants]
    remaining = [r["unrecovered_blocks"] for r in variants]
    ax.bar(range(3), recovered, color="#2563eb", label="Bloques recuperados")
    ax.bar(range(3), remaining, bottom=recovered, color="#e5e7eb", label="Sin recuperar")
    for position, value in enumerate(recovered):
        ax.text(position, value / 2 if value >= 4 else value + 1.3, str(value), ha="center", color="white" if value >= 4 else "#111827", fontweight="bold")
    ax.set_title("Recuperación de 40 bloques", loc="left", fontsize=12, pad=12)
    ax.set_ylabel("Número de bloques")
    ax.set_ylim(0, 43)
    ax.set_xticks(range(3), names)
    axis_style(ax)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.22), ncol=2, frameon=False, fontsize=9)
    ax = axes[1]
    for offset, field, name, color in ((-0.19, "logical_amplitude_bytes", "Amplitudes activas", "#0f766e"),
                                      (0.19, "estimated_reserved_bytes", "Reserva estimada", "#7c3aed")):
        ax.bar([i + offset for i in range(3)], [r[field] / KIB for r in variants], width=0.36, color=color, label=name)
    ax.set_title("Memoria lógica y capacidad estimada", loc="left", fontsize=12, pad=12)
    ax.set_ylabel("Memoria [KiB]")
    ax.set_xticks(range(3), names)
    axis_style(ax)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.22), ncol=2, frameon=False, fontsize=9)
    fig.text(0.085, 0.115, "Los cortes completos reciben información adicional sobre la partición; no son otro controlador con historial acotado.", fontsize=10, color="#374151")
    fig.text(0.085, 0.065, "Carga lógica y capacidades estimadas no representan RSS. Más memoria de historial también exige trabajo de recuperación.", fontsize=9.5, color="#4b5563")
    fig.text(0.085, 0.025, "Una observación por configuración. La reducción conserva coeficientes representados; no usa poda aproximada.", fontsize=9, color="#4b5563")
    return save(fig, directory, "historial_y_recuperacion", sources)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stream", required=True, type=Path)
    parser.add_argument("--controller", required=True, type=Path)
    parser.add_argument("--output-dir", default=Path("results/scale_exams_figures"), type=Path)
    args = parser.parse_args()
    rows, stream_source = read(args.stream, jsonl=True)
    controller, controller_source = read(args.controller)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.family": "DejaVu Sans", "pdf.fonttype": 42, "text.color": "#111827", "axes.labelcolor": "#111827"})
    sources = [stream_source, controller_source]
    outputs = memory_figure(rows, args.output_dir, sources)
    outputs += time_error_figure(rows, args.output_dir, sources)
    outputs += controller_figure(controller, args.output_dir, sources)
    print(json.dumps({"sources": sources, "files": outputs, "observed_rows": len(rows),
                      "status_counts": {status: sum(r.get("status") == status for r in rows)
                                        for status in sorted({r.get("status", "unknown") for r in rows})}}, indent=2))


if __name__ == "__main__":
    main()
