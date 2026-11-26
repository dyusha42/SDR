"""Общие функции скриптов: чтение IQ-файлов и вывод рисунка."""
import numpy as np


def read_iq(path):
    """Возвращает (I, Q) как массивы float из файла int16 с чередованием I/Q."""
    raw = np.fromfile(path, dtype=np.int16)
    n = raw.size // 2
    return raw[0:2 * n:2].astype(np.float64), raw[1:2 * n:2].astype(np.float64)


def window_of(start, length, total):
    """Границы участка записи; length <= 0 означает до конца."""
    stop = total if length <= 0 else min(total, start + length)
    if start >= stop:
        raise SystemExit("В выбранном диапазоне нет данных")
    return start, stop


def get_pyplot(save):
    """matplotlib без окна, если рисунок нужно сохранить в файл."""
    import matplotlib
    if save:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    return plt


def finish(fig, plt, save, tight=True, show=True):
    """Сохраняет рисунок в PNG; без имени файла показывает окно (если show)."""
    if tight:
        fig.tight_layout()
    if save:
        fig.savefig(save, dpi=110)
        print("Сохранено:", save)
    elif show:
        plt.show()
