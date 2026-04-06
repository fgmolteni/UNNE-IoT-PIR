"""
UNNE-IoT-PIR — Visualizador de imagen recibida por LoRa
========================================================
Lee los bytes volcados por el firmware RX (lora_rx_image)
desde el puerto serie y reconstruye la imagen 16×16.

Uso:
    python visualize_rx.py --port COM3          # Windows
    python visualize_rx.py --port /dev/ttyUSB0  # Linux
    python visualize_rx.py --port /dev/cu.usbserial-0001  # macOS

Dependencias:
    pip install pyserial numpy matplotlib

El firmware RX envía bloques delimitados así:
    IMG_START
    42
    255
    0
    ...  (256 enteros, uno por línea)
    IMG_END

También podés correr en modo --demo para probar
sin hardware (genera una imagen de prueba local).
"""

import argparse
import sys
import time
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.colors import Normalize

# ── Dimensiones ───────────────────────────────────────────────────────────────
IMG_W = 16
IMG_H = 16
IMG_SIZE = IMG_W * IMG_H

# ── Imagen de referencia TX (checkerboard 4×4) ────────────────────────────────
def gen_checkerboard():
    buf = np.zeros(IMG_SIZE, dtype=np.uint8)
    for y in range(IMG_H):
        for x in range(IMG_W):
            buf[y * IMG_W + x] = 255 if ((x // 4 + y // 4) % 2) else 0
    return buf.reshape(IMG_H, IMG_W)

def gen_gradient():
    buf = np.zeros(IMG_SIZE, dtype=np.uint8)
    for y in range(IMG_H):
        for x in range(IMG_W):
            buf[y * IMG_W + x] = int(((x + y) * 255) / (IMG_W + IMG_H - 2))
    return buf.reshape(IMG_H, IMG_W)

# ── Lectura por UART ──────────────────────────────────────────────────────────
def read_image_from_serial(port: str, baudrate: int = 115200, timeout_s: float = 60.0):
    """
    Espera el bloque IMG_START...IMG_END y devuelve ndarray (16, 16) uint8.
    timeout_s: tiempo máximo de espera desde el inicio.
    """
    try:
        import serial
    except ImportError:
        print("ERROR: pyserial no instalado. Ejecutá: pip install pyserial")
        sys.exit(1)

    print(f"Abriendo {port} @ {baudrate} baud...")
    ser = serial.Serial(port, baudrate, timeout=1.0)
    print(f"Puerto abierto. Esperando imagen del receptor LoRa (timeout {timeout_s}s)...")

    pixels = []
    in_image = False
    deadline = time.time() + timeout_s

    while time.time() < deadline:
        try:
            raw = ser.readline()
        except serial.SerialException as e:
            print(f"Error de serial: {e}")
            break

        if not raw:
            continue

        line = raw.decode("utf-8", errors="ignore").strip()

        if line == "IMG_START":
            print("  ← IMG_START detectado, leyendo pixels...")
            pixels = []
            in_image = True
            continue

        if line == "IMG_END":
            print(f"  ← IMG_END — {len(pixels)} bytes recibidos")
            in_image = False
            if len(pixels) == IMG_SIZE:
                ser.close()
                arr = np.array(pixels, dtype=np.uint8).reshape(IMG_H, IMG_W)
                return arr
            else:
                print(f"  ADVERTENCIA: se esperaban {IMG_SIZE} bytes, llegaron {len(pixels)}. Reintentando...")
            continue

        if in_image:
            try:
                val = int(line)
                if 0 <= val <= 255:
                    pixels.append(val)
                # Las líneas de log ESP_LOGI que no son números se ignoran
            except ValueError:
                pass  # línea de log, ignorar

    ser.close()
    print("TIMEOUT: no se recibió imagen completa.")
    return None

# ── Métricas de calidad ───────────────────────────────────────────────────────
def compute_metrics(img_tx: np.ndarray, img_rx: np.ndarray):
    """
    Calcula MSE y PSNR entre imagen transmitida y recibida.
    PSNR > 30 dB → calidad buena. PSNR infinito → imagen idéntica.
    """
    mse = np.mean((img_tx.astype(float) - img_rx.astype(float)) ** 2)
    if mse == 0:
        psnr = float("inf")
    else:
        psnr = 10 * np.log10(255.0 ** 2 / mse)
    
    # Error por píxel
    err = np.abs(img_tx.astype(int) - img_rx.astype(int))
    max_err = int(err.max())
    n_diff = int(np.sum(err > 0))

    return mse, psnr, max_err, n_diff

# ── Visualización ─────────────────────────────────────────────────────────────
def plot_results(img_tx: np.ndarray, img_rx: np.ndarray, img_count: int = 1):
    mse, psnr, max_err, n_diff = compute_metrics(img_tx, img_rx)
    err_map = np.abs(img_tx.astype(int) - img_rx.astype(int)).astype(np.uint8)

    fig = plt.figure(figsize=(14, 5))
    fig.suptitle(
        f"Imagen #{img_count} — MSE={mse:.2f}  PSNR={'∞' if np.isinf(psnr) else f'{psnr:.1f} dB'}  "
        f"Píxeles erróneos: {n_diff}/{IMG_SIZE}  Error máx: {max_err}",
        fontsize=12, fontweight="bold"
    )

    gs = gridspec.GridSpec(1, 4, wspace=0.4)

    # ── TX ──
    ax0 = fig.add_subplot(gs[0])
    ax0.imshow(img_tx, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
    ax0.set_title("TX (referencia)")
    ax0.axis("off")
    _add_pixel_grid(ax0, img_tx)

    # ── RX ──
    ax1 = fig.add_subplot(gs[1])
    ax1.imshow(img_rx, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
    ax1.set_title("RX (recibida)")
    ax1.axis("off")
    _add_pixel_grid(ax1, img_rx)

    # ── Diferencia absoluta ──
    ax2 = fig.add_subplot(gs[2])
    im2 = ax2.imshow(err_map, cmap="hot", vmin=0, vmax=255, interpolation="nearest")
    ax2.set_title("Diferencia |TX - RX|")
    ax2.axis("off")
    plt.colorbar(im2, ax=ax2, shrink=0.8)

    # ── Histograma de valores RX ──
    ax3 = fig.add_subplot(gs[3])
    ax3.hist(img_rx.flatten(), bins=32, range=(0, 256), color="steelblue", edgecolor="k")
    ax3.set_title("Histograma RX")
    ax3.set_xlabel("Valor de gris")
    ax3.set_ylabel("Frecuencia")

    plt.tight_layout()
    plt.show()

def _add_pixel_grid(ax, img: np.ndarray):
    """Superpone los valores numéricos en cada celda de la imagen."""
    for y in range(IMG_H):
        for x in range(IMG_W):
            val = img[y, x]
            color = "white" if val < 128 else "black"
            ax.text(x, y, str(val), ha="center", va="center",
                    fontsize=4, color=color)

# ── Modo continuo ─────────────────────────────────────────────────────────────
def run_continuous(port: str, baudrate: int):
    """
    Espera imágenes en loop. Cada vez que llega una, la muestra.
    Útil para dejar corriendo mientras el TX retransmite cada 10s.
    Presioná Ctrl+C para salir.
    """
    print(f"\nModo continuo — esperando imágenes en {port}")
    print("Presioná Ctrl+C para salir.\n")

    try:
        import serial
    except ImportError:
        print("ERROR: pip install pyserial")
        sys.exit(1)

    img_tx = gen_checkerboard()
    img_count = 0

    ser = serial.Serial(port, baudrate, timeout=1.0)
    pixels = []
    in_image = False

    plt.ion()  # modo interactivo

    try:
        while True:
            try:
                raw = ser.readline()
            except serial.SerialException as e:
                print(f"Error serial: {e}")
                break

            if not raw:
                continue

            line = raw.decode("utf-8", errors="ignore").strip()

            if line == "IMG_START":
                pixels = []
                in_image = True
                print(f"  ← Nueva imagen comenzando...")
                continue

            if line == "IMG_END":
                in_image = False
                if len(pixels) == IMG_SIZE:
                    img_count += 1
                    img_rx = np.array(pixels, dtype=np.uint8).reshape(IMG_H, IMG_W)
                    mse, psnr, max_err, n_diff = compute_metrics(img_tx, img_rx)
                    psnr_str = "∞" if np.isinf(psnr) else f"{psnr:.1f} dB"
                    print(f"  ✓ Imagen #{img_count}: MSE={mse:.2f}, PSNR={psnr_str}, "
                          f"erróneos={n_diff}/{IMG_SIZE}")
                    plot_results(img_tx, img_rx, img_count)
                    plt.pause(0.1)
                else:
                    print(f"  ✗ Imagen incompleta ({len(pixels)} bytes)")
                continue

            if in_image:
                try:
                    val = int(line)
                    if 0 <= val <= 255:
                        pixels.append(val)
                except ValueError:
                    pass

    except KeyboardInterrupt:
        print("\nSaliendo.")
    finally:
        ser.close()

# ── Demo sin hardware ─────────────────────────────────────────────────────────
def run_demo():
    """
    Simula TX=checkerboard, RX=checkerboard con algunos bytes
    corrompidos al azar para demostrar las métricas.
    """
    print("Modo DEMO — sin hardware")
    img_tx = gen_checkerboard()

    # Simular 5 bits corrompidos (probabilidad de error baja = buena transmisión)
    img_rx = img_tx.copy()
    rng = np.random.default_rng(42)
    corrupt_idx = rng.choice(IMG_SIZE, size=5, replace=False)
    for idx in corrupt_idx:
        img_rx.flat[idx] = rng.integers(0, 256)

    print("Imagen TX (checkerboard 4×4):")
    print(img_tx)
    print("\nImagen RX (con 5 bytes corrompidos):")
    print(img_rx)

    plot_results(img_tx, img_rx, img_count=1)

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Visualizador de imagen LoRa 16×16 recibida por UART"
    )
    parser.add_argument("--port", "-p", type=str, default=None,
                        help="Puerto serie (ej: COM3, /dev/ttyUSB0)")
    parser.add_argument("--baud", "-b", type=int, default=115200,
                        help="Baudrate (default: 115200)")
    parser.add_argument("--demo", action="store_true",
                        help="Correr en modo demo sin hardware")
    parser.add_argument("--continuous", "-c", action="store_true",
                        help="Modo continuo: mostrar cada imagen recibida")
    parser.add_argument("--timeout", "-t", type=float, default=60.0,
                        help="Timeout en segundos para modo single (default: 60)")
    args = parser.parse_args()

    if args.demo:
        run_demo()
        return

    if args.port is None:
        print("ERROR: especificá --port o usá --demo")
        parser.print_help()
        sys.exit(1)

    img_tx = gen_checkerboard()

    if args.continuous:
        run_continuous(args.port, args.baud)
    else:
        # Modo single: espera una imagen, muestra y sale
        img_rx = read_image_from_serial(args.port, args.baud, args.timeout)
        if img_rx is not None:
            plot_results(img_tx, img_rx, img_count=1)
        else:
            print("No se recibió imagen. Verificá que el TX esté activo.")
            sys.exit(1)

if __name__ == "__main__":
    main()
