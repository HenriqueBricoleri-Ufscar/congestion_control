import matplotlib
matplotlib.use("Agg")
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import sys
import glob
import os

# ─── Configuração ────────────────────────────────────────────────
MSS         = 1024
DATA_DIR    = "data"
OUTPUT_DIR  = "plot_graphic"
# ─────────────────────────────────────────────────────────────────

def plot_file(filepath: str):
    df = pd.read_csv(filepath)

    # Normaliza para MSS
    df["cwnd_mss"]     = df["cwnd"]     / MSS
    df["ssthresh_mss"] = df["ssthresh"] / MSS
    if "rwnd" in df.columns:
        df["rwnd_mss"] = df["rwnd"] / MSS

    fig, ax = plt.subplots(figsize=(14, 6))
    fig.patch.set_facecolor("#f9f7f2")
    ax.set_facecolor("#f9f7f2")

    # ── Linhas principais ────────────────────────────────────────
    ax.plot(df["time_ms"], df["cwnd_mss"],
            color="#2471a3", lw=2, label="CWND", zorder=3)

    ax.plot(df["time_ms"], df["ssthresh_mss"],
            color="#c0392b", lw=1.5, ls="--", alpha=0.8, label="SSTHRESH", zorder=3)

    if "rwnd" in df.columns:
        ax.plot(df["time_ms"], df["rwnd_mss"],
                color="#1e8449", lw=1.5, ls=":", alpha=0.85, label="rwnd", zorder=3)

    # ── Eventos pontuais ─────────────────────────────────────────
    eventos = [
        ("TIMEOUT",     "X", "#c0392b", 120, "Timeout"),
        ("FR_ENTER",    "v", "#e67e22", 100, "Fast Retransmit"),
        ("FR_EXIT",     "^", "#1e8449",  90, "Saída FR (Full ACK)"),
        ("PARTIAL_ACK", "s", "#8e44ad",  80, "Partial ACK"),
    ]

    for event, marker, color, size, label in eventos:
        sub = df[df["event"] == event]
        if not sub.empty:
            ax.scatter(sub["time_ms"], sub["cwnd_mss"],
                       marker=marker, color=color, s=size,
                       zorder=5, label=label, edgecolors="white", linewidths=0.5)

    phase_colors = {
        "ACK_SS":  ("#d6eaf8", "Slow Start"),
        "ACK_CA":  ("#d5f5e3", "Cong. Avoidance"),
        "FR_ENTER": ("#fdebd0", "Fast Recovery"),
    }
    prev_time  = df["time_ms"].iloc[0]
    prev_phase = None

    for _, row in df.iterrows():
        if row["event"] in phase_colors:
            color, _ = phase_colors[row["event"]]
            if prev_phase != row["event"]:
                ax.axvspan(prev_time, row["time_ms"],
                           alpha=0.15, color=color if prev_phase and prev_phase in phase_colors
                           else "#d6eaf8", lw=0)
            prev_time  = row["time_ms"]
            prev_phase = row["event"]

    for t in df[df["event"] == "TIMEOUT"]["time_ms"]:
        ax.axvline(x=t, color="#c0392b", lw=0.8, alpha=0.4, ls="--")

    ax.set_xlabel("Tempo (ms)", fontsize=12)
    ax.set_ylabel("CWND (MSS)", fontsize=12)

    payload_label = os.path.basename(filepath).replace("cwnd_log_", "").replace(".csv", "")
    ax.set_title(f"CWND × Tempo  —  payload {payload_label}", fontsize=13, fontweight="bold")

    ax.legend(loc="upper left", framealpha=0.9, fontsize=9)
    ax.grid(True, alpha=0.3, linestyle="--")
    ax.set_xlim(left=0)
    ax.set_ylim(bottom=0)

    # Legenda de fases no rodapé
    fase_patches = [
        mpatches.Patch(color="#d6eaf8", alpha=0.6, label="Slow Start"),
        mpatches.Patch(color="#d5f5e3", alpha=0.6, label="Cong. Avoidance"),
        mpatches.Patch(color="#fdebd0", alpha=0.6, label="Fast Recovery"),
    ]
    ax.legend(handles=ax.get_legend_handles_labels()[0] + fase_patches,
              loc="upper left", framealpha=0.9, fontsize=9)

    plt.tight_layout()

    out = os.path.join(OUTPUT_DIR, os.path.basename(filepath).replace(".csv", ".png"))
    plt.savefig(out, dpi=150)
    print(f"Salvo: {out}")
    plt.show()
    plt.close()


def plot_comparativo(filepaths: list):
    """Plota todos os arquivos numa figura só para comparar payloads."""
    fig, ax = plt.subplots(figsize=(14, 6))
    fig.patch.set_facecolor("#f9f7f2")
    ax.set_facecolor("#f9f7f2")

    colors = ["#2471a3", "#c0392b", "#1e8449", "#8e44ad", "#e67e22", "#17a589"]

    for i, fp in enumerate(filepaths):
        df = pd.read_csv(fp)
        df["cwnd_mss"] = df["cwnd"] / MSS
        label = os.path.basename(fp).replace("cwnd_log_", "").replace(".csv", "")
        color = colors[i % len(colors)]
        ax.plot(df["time_ms"], df["cwnd_mss"], color=color, lw=1.8, label=label)

        # Marca timeouts
        for t in df[df["event"] == "TIMEOUT"]["time_ms"]:
            ax.axvline(x=t, color=color, lw=0.6, alpha=0.3, ls="--")

    ax.set_xlabel("Tempo (ms)", fontsize=12)
    ax.set_ylabel("CWND (MSS)", fontsize=12)
    ax.set_title("CWND × Tempo — comparativo de payloads", fontsize=13, fontweight="bold")
    ax.legend(fontsize=9, framealpha=0.9)
    ax.grid(True, alpha=0.3, linestyle="--")
    ax.set_xlim(left=0)
    ax.set_ylim(bottom=0)

    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, "cwnd_comparativo.png")
    plt.savefig(out, dpi=150)
    print(f"Salvo: {out}")
    plt.show()

if __name__ == "__main__":
    files = sorted(glob.glob(os.path.join(DATA_DIR, "cwnd_log_*.csv")))

    if not files:
        print(f"Nenhum arquivo encontrado em '{DATA_DIR}/'")
        sys.exit(1)

    for f in files:
        plot_file(f)

    if len(files) > 1:
        plot_comparativo(files)