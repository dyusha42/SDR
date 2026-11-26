#!/usr/bin/env python3
"""Выбор символьных отсчётов и созвездие. Позиции кадров берутся из frames.csv (создаёт lab4_rx)."""
import argparse
import csv
import os

import numpy as np

from iqtools import finish, get_pyplot, read_iq

SYM_LEN = 10
PREAMBLE = 32
MAX_FRAMES = 20


def frame_regions(path):
    with open(path, encoding="utf-8") as f:
        return [(int(r["start_sample"]), int(r["end_sample"])) for r in csv.DictReader(f)][:MAX_FRAMES]


def mf_output(i_arr, q_arr):
    """Скользящее среднее по длине символа, как в приёмнике."""
    z = i_arr + 1j * q_arr
    return np.convolve(z, np.ones(SYM_LEN) / SYM_LEN)[: z.size]


def offsets_figure(plt, y, regions):
    idx = np.concatenate([np.arange(a, min(b, y.size)) for a, b in regions])
    best = (regions[0][0] + SYM_LEN - 1) % SYM_LEN       # символ заканчивается на (старт + длина - 1)
    lim = 1.15 * np.abs(y[idx]).max()
    fig, axes = plt.subplots(2, 5, figsize=(15, 6.5), sharex=True, sharey=True)
    for shift, ax in enumerate(axes.ravel()):
        pick = idx[idx % SYM_LEN == shift]
        ax.scatter(y[pick].real, y[pick].imag, s=4, alpha=0.5, c="tab:green" if shift == best else "tab:blue")
        ax.set_title(f"сдвиг k = {shift}" + (" (лучший)" if shift == best else ""))
        ax.set_aspect("equal")
        ax.set_xlim(-lim, lim)
        ax.set_ylim(-lim, lim)
        ax.grid(alpha=0.4)
    fig.suptitle(f"Выход согласованного фильтра, прореженный в {SYM_LEN} раз (кадров: {len(regions)})")
    return fig


def read_symbols(path):
    """Точки данных (без преамбулы) до и после коррекции."""
    pts = {"raw": [], "corr": []}
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if int(r["k"]) >= PREAMBLE:
                pts[r["kind"]].append(complex(float(r["I"]), float(r["Q"])))
    return {k: np.array(v) for k, v in pts.items()}


def constellation_figure(plt, pts):
    fig, axes = plt.subplots(1, 2, figsize=(11, 5.5), sharex=True, sharey=True)
    for ax, key, title in zip(axes, ("raw", "corr"), ("До коррекции", "После коррекции частоты и фазы")):
        ax.scatter(pts[key].real, pts[key].imag, s=10, alpha=0.6)
        ax.set(title=title, xlabel="I", ylabel="Q")
        ax.set_aspect("equal")
        ax.grid(alpha=0.4)
    return fig


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("file", help="запись IQ (int16)")
    p.add_argument("--frames", help="frames.csv (по умолчанию рядом с записью)")
    p.add_argument("--symbols", help="symbols.csv (по умолчанию рядом с записью)")
    p.add_argument("--start", type=int, help="вручную: начало участка, если нет frames.csv")
    p.add_argument("--length", type=int, help="вручную: длина участка")
    p.add_argument("--save", help="префикс PNG: PREFIX_offsets.png и PREFIX_constellation.png")
    a = p.parse_args()
    plt = get_pyplot(a.save)

    folder = os.path.dirname(os.path.abspath(a.file))
    frames_csv = a.frames or os.path.join(folder, "frames.csv")
    symbols_csv = a.symbols or os.path.join(folder, "symbols.csv")

    if a.start is not None and a.length:
        regions = [(a.start, a.start + a.length)]
    elif os.path.exists(frames_csv):
        regions = frame_regions(frames_csv)
    else:
        raise SystemExit(f"Нет {frames_csv}. Сначала запустите lab4_rx или задайте --start и --length.")
    if not regions:
        raise SystemExit("В frames.csv нет кадров")

    y = mf_output(*read_iq(a.file))
    out = (lambda suffix: a.save + suffix if a.save else None)

    fig_offsets = offsets_figure(plt, y, regions)
    fig_offsets.tight_layout()
    fig_offsets.subplots_adjust(hspace=0.35)
    finish(fig_offsets, plt, out("_offsets.png"), tight=False, show=False)

    if os.path.exists(symbols_csv):
        finish(constellation_figure(plt, read_symbols(symbols_csv)), plt, out("_constellation.png"), show=False)
    else:
        print(f"Нет {symbols_csv}: созвездие не построено.")

    if not a.save:
        plt.show()


if __name__ == "__main__":
    main()
