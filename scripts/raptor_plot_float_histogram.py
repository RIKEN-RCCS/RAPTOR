#!/usr/bin/env python3
import argparse
import numpy as np
import matplotlib.pyplot as plt

def plot_exponent_distribution(filename, dtype='float32', output_file='exponent_hist.png'):
    """
    Plot histograms of exponent field usage in a raw binary file of floating-point numbers.
    Generates two subplots:
      1. Histogram of used exponent range (min to max of actual data)
      2. Histogram over the full possible exponent range

    Parameters
    ----------
    filename : str
        Path to the binary file.
    dtype : str
        Data type of the floats in the file. One of: 'float16', 'float32', 'float64'.
    output_file : str
        Output filename to save the plot (e.g., 'plot.png' or 'plot.pdf').
    """
    dtype_info = {
        'float16': {'bits': 16, 'exp_bits': 5, 'bias': 15},
        'float32': {'bits': 32, 'exp_bits': 8, 'bias': 127},
        'float64': {'bits': 64, 'exp_bits': 11, 'bias': 1023},
    }

    if dtype not in dtype_info:
        raise ValueError(f"Unsupported dtype '{dtype}'. Must be one of {list(dtype_info.keys())}")

    info = dtype_info[dtype]
    bits = info['bits']
    exp_bits = info['exp_bits']
    bias = info['bias']

    # Load binary data
    data = np.fromfile(filename, dtype=dtype)
    if data.size == 0:
        raise ValueError("No data found in file or file empty.")

    # View data as unsigned integer
    int_view = data.view({16: np.uint16, 32: np.uint32, 64: np.uint64}[bits])

    # Extract exponent bits
    mantissa_bits = bits - exp_bits - 1
    exponent_mask = ((1 << exp_bits) - 1) << mantissa_bits
    exponents = ((int_view & exponent_mask) >> mantissa_bits).astype(int)

    # Mask out special exponents (0 or all ones)
    normal_mask = (exponents != 0) & (exponents != (1 << exp_bits) - 1)
    exponents_normal = exponents[normal_mask]

    # Convert to unbiased exponent values
    unbiased_exponents = exponents_normal - bias

    # Prepare plots
    fig, axes = plt.subplots(2, 1, figsize=(10, 8), constrained_layout=True)

    # --- Subplot 1: Only used exponent range ---
    bins_used = np.arange(unbiased_exponents.min() - 1, unbiased_exponents.max() + 2)
    axes[0].hist(unbiased_exponents, bins=bins_used, edgecolor='black', alpha=0.7)
    axes[0].set_title(f"Exponent Distribution (Used Range)\n{dtype}, File: {filename}")
    axes[0].set_xlabel("Unbiased Exponent Value")
    axes[0].set_ylabel("Frequency")
    axes[0].grid(True, linestyle='--', alpha=0.5)

    # --- Subplot 2: Full possible exponent range ---
    exp_min_possible = 1 - bias                    # Smallest normal exponent
    exp_max_possible = (1 << exp_bits) - 2 - bias  # Largest normal exponent
    bins_full = np.arange(exp_min_possible - 0.5, exp_max_possible + 1.5)

    axes[1].hist(unbiased_exponents, bins=bins_full, edgecolor='black', alpha=0.7)
    axes[1].set_xlim(exp_min_possible - 1, exp_max_possible + 1)
    axes[1].set_title(f"Exponent Distribution (Full Range)\n{dtype}")
    axes[1].set_xlabel("Unbiased Exponent Value (All Possible)")
    axes[1].set_ylabel("Frequency")
    axes[1].grid(True, linestyle='--', alpha=0.5)

    # Save to file
    plt.savefig(output_file)
    plt.close()
    print(f"✅ Histogram saved to '{output_file}'")


def main():
    parser = argparse.ArgumentParser(
        description="Plot histogram of exponent field usage in a raw binary float file."
    )
    parser.add_argument("filename", help="Path to the binary input file")
    parser.add_argument(
        "--dtype",
        choices=["float16", "float32", "float64"],
        default="float32",
        help="Data type of floats in the file (default: float32)",
    )
    parser.add_argument(
        "--output",
        default="exponent_hist.png",
        help="Output filename for the plot (default: exponent_hist.png)",
    )

    args = parser.parse_args()
    plot_exponent_distribution(args.filename, args.dtype, args.output)


if __name__ == "__main__":
    main()
