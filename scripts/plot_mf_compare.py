#!/usr/bin/env python3
"""Один участок записи до и после согласованного фильтра (rxdata.pcm и mf_out.pcm)."""
import argparse

import numpy as np

from iqtools import finish, get_pyplot, read_iq, window_of


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--raw", default="build/rxdata.pcm")
    p.add_argument("--mf", default="build/mf_out.pcm")
    p.add_argument("--start", type=int, required=True, help="начало участка (отсчёт кадра из вывода lab4_rx минус 50)")
    p.add_argument("--length", type=int, default=1200)
    p.add_argument("--save", help="имя PNG-файла")
    a = p.parse_args()
    plt = get_pyplot(a.save)

    sources = [("Принятый сигнал", read_iq(a.raw)), ("После согласованного фильтра (сумма 10 отсчётов)", read_iq(a.mf))]
    lo, hi = window_of(a.start, a.length, sources[0][1][0].size)
    x = np.arange(lo, hi)

    fig, axes = plt.subplots(2, 1, figsize=(12, 7), sharex=True)
    for ax, (title, (ci, cq)) in zip(axes, sources):
        ax.plot(x, ci[lo:hi], c="tab:blue", lw=0.9, label="I")
        ax.plot(x, cq[lo:hi], c="tab:red", lw=0.9, label="Q")
        ax.set_title(title)
        ax.grid(alpha=0.4)
        ax.legend(loc="upper right")
    axes[-1].set_xlabel("номер отсчёта")
    finish(fig, plt, a.save)


if __name__ == "__main__":
    main()
