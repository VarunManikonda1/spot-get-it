#!/usr/bin/env python3

import argparse
import sys

import numpy as np


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True, help="ONNX model path")
    parser.add_argument("--obs", required=True, help="test_obs.npy")
    parser.add_argument("--expected", required=True, help="test_action.npy from PyTorch")
    parser.add_argument("--atol", type=float, default=1e-4)
    args = parser.parse_args()

    try:
        import onnxruntime as ort
    except ImportError:
        print("[ERROR] onnxruntime is not installed.", file=sys.stderr)
        print("Install onnxruntime first, then retry.", file=sys.stderr)
        raise

    obs = np.load(args.obs).astype(np.float32)
    expected = np.load(args.expected).astype(np.float32)

    if obs.ndim != 2 or obs.shape[1] != 47:
        raise RuntimeError(f"obs shape must be (N, 47), got {obs.shape}")
    if expected.ndim != 2 or expected.shape[1] != 12:
        raise RuntimeError(f"expected shape must be (N, 12), got {expected.shape}")
    if obs.shape[0] != expected.shape[0]:
        raise RuntimeError(f"N mismatch: obs {obs.shape[0]} vs expected {expected.shape[0]}")

    sess = ort.InferenceSession(args.model, providers=["CPUExecutionProvider"])
    input_name = sess.get_inputs()[0].name
    output_name = sess.get_outputs()[0].name

    actual = sess.run([output_name], {input_name: obs})[0].astype(np.float32)

    if actual.shape != expected.shape:
        raise RuntimeError(f"actual shape {actual.shape} != expected shape {expected.shape}")

    diff = np.abs(actual - expected)
    max_abs_error = float(np.max(diff))
    mean_abs_error = float(np.mean(diff))
    p99_abs_error = float(np.percentile(diff, 99))

    print("========== ONNX Policy Verification ==========")
    print(f"model          : {args.model}")
    print(f"obs            : {args.obs} {obs.shape}")
    print(f"expected       : {args.expected} {expected.shape}")
    print(f"input_name     : {input_name}")
    print(f"output_name    : {output_name}")
    print(f"max_abs_error  : {max_abs_error:.10f}")
    print(f"mean_abs_error : {mean_abs_error:.10f}")
    print(f"p99_abs_error  : {p99_abs_error:.10f}")
    print(f"atol           : {args.atol:.1e}")

    if max_abs_error <= args.atol:
        print("RESULT         : PASS")
        return 0

    print("RESULT         : FAIL")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
