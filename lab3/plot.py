import numpy as np
import matplotlib.pyplot as plt

def main():
    filename = "txdata.pcm"
    
    # Чтение 16-битных целых чисел из файла PCM
    raw_data = np.fromfile(filename, dtype=np.int16)
    
    # Разделение на I и Q компоненты
    i_samples = raw_data[0::2]
    q_samples = raw_data[1::2]
    
    indices = np.arange(len(i_samples))
    
    plt.figure(figsize=(9, 6))
    plt.plot(indices, i_samples, label="In-Phase (I)", color="blue", linewidth=1.2)
    plt.plot(indices, q_samples, label="Quadrature (Q)", color="red", linewidth=1.2)
    
    plt.title("Визуализация принятых IQ-отсчетов (Adalm-Pluto SDR)")
    plt.xlabel("Отсчеты (Samples)")
    plt.ylabel("Амплитуда")
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.legend(loc="upper right")
    
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    main()