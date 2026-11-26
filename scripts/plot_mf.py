#!/usr/bin/env python3
"""Сигнал после согласованного фильтра (файл mf_out.pcm создаёт lab4_rx)."""
import argparse

import numpy as np

from iqtools import finish, get_pyplot, read_iq, window_of


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("file", nargs="?", default="build/mf_out.pcm")
    p.add_argument("--start", type=int, default=0)
    p.add_argument("--length", type=int, default=10000)
    p.add_argument("--save", help="имя PNG-файла")
    a = p.parse_args()
    plt = get_pyplot(a.save)

    re_part, im_part = read_iq(a.file)
    lo, hi = window_of(a.start, a.length, re_part.size)

    fig, ax = plt.subplots(figsize=(12, 4))
    n = np.arange(lo, hi)
    ax.plot(n, re_part[lo:hi], c="tab:blue", lw=1.1, label="I после фильтра")
    ax.plot(n, im_part[lo:hi], c="tab:red", lw=1.1, label="Q после фильтра")
    ax.set(xlabel="номер отсчёта", ylabel="сумма 10 отсчётов")
    ax.grid(alpha=0.4)
    ax.legend(loc="upper right")
    finish(fig, plt, a.save)


if __name__ == "__main__":
    main()
