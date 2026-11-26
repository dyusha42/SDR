#!/usr/bin/env python3
"""I/Q по времени из бинарного файла. --start и --length в комплексных отсчётах."""
import argparse

import numpy as np

from iqtools import finish, get_pyplot, read_iq, window_of


def parse():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("file")
    p.add_argument("--start", type=int, default=0)
    p.add_argument("--length", type=int, default=0, help="0 = до конца записи")
    p.add_argument("--abs", action="store_true", help="добавить модуль сигнала")
    p.add_argument("--save", help="имя PNG-файла")
    return p.parse_args()


def main():
    a = parse()
    plt = get_pyplot(a.save)

    i_all, q_all = read_iq(a.file)
    lo, hi = window_of(a.start, a.length, i_all.size)
    x = np.arange(lo, hi)
    i_part, q_part = i_all[lo:hi], q_all[lo:hi]

    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(x, i_part, c="tab:blue", lw=0.8, label="I")
    ax.plot(x, q_part, c="tab:red", lw=0.8, label="Q")
    if a.abs:
        ax.plot(x, np.hypot(i_part, q_part), c="gray", lw=0.8, label="|I+jQ|")
    ax.set(xlabel="номер отсчёта", ylabel="амплитуда", title=f"{a.file}: отсчёты {lo}…{hi - 1}")
    ax.grid(alpha=0.4)
    ax.legend(loc="upper right")
    finish(fig, plt, a.save)


if __name__ == "__main__":
    main()
