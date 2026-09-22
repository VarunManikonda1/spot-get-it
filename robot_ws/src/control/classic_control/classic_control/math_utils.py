import math
from typing import Sequence


def clamp(x: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, x))


def smoothstep(t: float) -> float:
    t = clamp(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def finite_list(values: Sequence[float], expected_len: int) -> bool:
    if len(values) != expected_len:
        return False
    return all(math.isfinite(float(v)) for v in values)
