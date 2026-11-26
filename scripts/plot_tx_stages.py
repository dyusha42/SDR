#!/usr/bin/env python3
"""Этапы цепочки передатчика по данным lab3_tx --dump DIR."""
import argparse
import os

import numpy as np

from iqtools import finish, get_pyplot, read_iq


def read_info(folder):
    info = {}
    with open(os.path.join(folder, "info.txt"), encoding="utf-8") as f:
        for line in f:
            key, sep, val = line.rstrip("\n").partition("=")
            if sep:
                info[key] = val
    return info


class Dump:
    """Все файлы, которые сохраняет lab3_tx --dump."""

    def __init__(self, folder):
        self.info = read_info(folder)
        self.sym_len = int(self.info["sym_len"])
        self.pre = int(self.info["preamble_symbols"])
        self.guard = int(self.info["guard_samples"])
        with open(os.path.join(folder, "bits.txt"), encoding="utf-8") as f:
            self.bits = np.array([int(c) for c in f.read().strip()])
        self.symbols = np.loadtxt(os.path.join(folder, "symbols.csv"), delimiter=",", skiprows=1)
        self.upsampled = read_iq(os.path.join(folder, "upsampled.pcm"))
        self.shaped = read_iq(os.path.join(folder, "shaped.pcm"))
        self.burst = read_iq(os.path.join(folder, "tx_burst.pcm"))


def fig_bits(plt, d):
    fig, ax = plt.subplots(figsize=(12, 3))
    ax.step(range(d.bits.size), d.bits, where="post", c="black")
    ax.vlines(range(0, d.bits.size, 8), 0, 1, colors="gray", lw=0.4)
    ax.set(title=f"Биты кадра: длина, данные \"{d.info['message']}\", CRC16 ({d.bits.size} бит, без преамбулы)",
           xlabel="номер бита", yticks=[0, 1])
    return fig


def fig_symbols(plt, d):
    fig, (left, right) = plt.subplots(1, 2, figsize=(12, 5))
    mod = d.info["mod"].upper()
    left.scatter(*d.symbols[:d.pre].T, s=40, c="tab:orange", label="преамбула (BPSK)")
    left.scatter(*d.symbols[d.pre:].T, s=25, c="tab:blue", label=f"данные ({mod})")
    left.set(title="Созвездие на передаче", xlabel="I", ylabel="Q")
    left.set_aspect("equal")
    left.grid(alpha=0.4)
    left.legend()
    k = np.arange(len(d.symbols))
    right.stem(k, d.symbols[:, 0], linefmt="b-", markerfmt="bo", basefmt=" ", label="I")
    right.stem(k + 0.25, d.symbols[:, 1], linefmt="r-", markerfmt="ro", basefmt=" ", label="Q")
    right.axvline(d.pre - 0.5, c="k", ls="--")
    right.set(title="Символы по времени (слева от пунктира преамбула)", xlabel="номер символа")
    right.legend()
    return fig


def fig_shaping(plt, d):
    first = d.pre * d.sym_len                      # начало данных
    sl = slice(first, first + 8 * d.sym_len)
    x = np.arange(sl.start, sl.stop)
    fig, (top, bottom) = plt.subplots(2, 1, figsize=(12, 6), sharex=True)
    top.stem(x, d.upsampled[0][sl], linefmt="b-", markerfmt="bo", basefmt=" ", label="I")
    top.stem(x + 0.25, d.upsampled[1][sl], linefmt="r-", markerfmt="ro", basefmt=" ", label="Q")
    top.set_title(f"После вставки нулей: между символами {d.sym_len - 1} нулей")
    bottom.step(x, d.shaped[0][sl], where="post", c="blue", label="I")
    bottom.step(x, d.shaped[1][sl], where="post", c="red", label="Q")
    bottom.set(title="После свёртки с h = {1,...,1}: символ растянут на 10 отсчётов", xlabel="номер отсчёта")
    for ax in (top, bottom):
        ax.grid(alpha=0.4)
        ax.legend()
    return fig


def fig_burst(plt, d):
    fig, ax = plt.subplots(figsize=(12, 4))
    n = np.arange(d.burst[0].size)
    ax.plot(n, d.burst[0], c="blue", lw=0.8, label="I")
    ax.plot(n, d.burst[1], c="red", lw=0.8, label="Q")
    ax.axvspan(0, d.guard, color="gray", alpha=0.25, label="защитный интервал")
    ax.axvline(d.guard + d.pre * d.sym_len, c="k", ls="--", label="конец преамбулы")
    ax.set(title=f"Буфер, отправляемый в ЦАП ({n.size} отсчётов)", xlabel="номер отсчёта")
    ax.grid(alpha=0.4)
    ax.legend(loc="upper right")
    return fig


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("dir")
    p.add_argument("--save", help="префикс PNG-файлов")
    a = p.parse_args()
    plt = get_pyplot(a.save)

    d = Dump(a.dir)
    for name, make in (("bits", fig_bits), ("symbols", fig_symbols), ("upsample", fig_shaping), ("buffer", fig_burst)):
        finish(make(plt, d), plt, f"{a.save}_{name}.png" if a.save else None, show=False)
    if not a.save:
        plt.show()


if __name__ == "__main__":
    main()
