"""
core/map_builder.py

Pure coordinate-transform and map-data logic.
No ROS2, no tkinter, no OpenCV dependencies.
"""

import math
from typing import Dict, List, Tuple, Optional


Waypoint = Tuple[float, float]


# ── Scale calculation ──────────────────────────────────────────────────────────

def compute_scale(altitude_m: float, fov_h_deg: float, image_width_px: int) -> float:
    """
    Compute meters-per-pixel from drone altitude and horizontal FOV.
    Scale is based on the ORIGINAL image width and never changes with rotation.
    """
    return (2.0 * altitude_m * math.tan(math.radians(fov_h_deg) / 2.0)) / image_width_px


# ── Coordinate system ──────────────────────────────────────────────────────────

def compute_axes(
    origin_px: Tuple[float, float],
    xdir_px:   Tuple[float, float],
) -> Tuple[Optional[Tuple[float, float]], Optional[Tuple[float, float]]]:
    """
    Compute unit vectors for project +X and +Y axes from two image-pixel clicks.

    x_hat : image-space unit vector for project +X direction
    y_hat : image-space unit vector for project +Y direction
            (90 deg CCW in real world = 90 deg CW in image, because image y-axis points down)
    """
    dx = xdir_px[0] - origin_px[0]
    dy = xdir_px[1] - origin_px[1]
    length = math.hypot(dx, dy)
    if length < 1e-6:
        return None, None
    x_hat = (dx / length, dy / length)
    y_hat = (x_hat[1], -x_hat[0])   # 90 deg CW in image
    return x_hat, y_hat


def pixel_to_project(
    px: float, py: float,
    origin: Tuple[float, float],
    x_hat:  Tuple[float, float],
    y_hat:  Tuple[float, float],
    scale:  float,
) -> Tuple[float, float]:
    """Image pixel (px, py) → project coordinate (x_m, y_m)."""
    du = px - origin[0]
    dv = py - origin[1]
    x_m = (du * x_hat[0] + dv * x_hat[1]) * scale
    y_m = (du * y_hat[0] + dv * y_hat[1]) * scale
    return round(x_m, 2), round(y_m, 2)


def project_to_pixel(
    x_m:   float, y_m: float,
    origin: Tuple[float, float],
    x_hat:  Tuple[float, float],
    y_hat:  Tuple[float, float],
    scale:  float,
) -> Tuple[float, float]:
    """Project coordinate (x_m, y_m) → image pixel (px, py)."""
    x_px = x_m / scale
    y_px = y_m / scale
    # x_hat, y_hat are orthonormal → inverse = transpose
    du = x_px * x_hat[0] + y_px * y_hat[0]
    dv = x_px * x_hat[1] + y_px * y_hat[1]
    return (origin[0] + du, origin[1] + dv)


# ── Field boundary ─────────────────────────────────────────────────────────────

def field_from_image_corners(
    img_w: int, img_h: int,
    origin: Tuple[float, float],
    x_hat:  Tuple[float, float],
    y_hat:  Tuple[float, float],
    scale:  float,
) -> Tuple[float, float, float, float]:
    """
    Convert image corners to project coordinates and return field bounds.
    Returns: (x_min, x_max, y_min, y_max) in meters
    """
    corners_m = [
        pixel_to_project(u, v, origin, x_hat, y_hat, scale)
        for u, v in [(0, 0), (img_w, 0), (img_w, img_h), (0, img_h)]
    ]
    xs = [m[0] for m in corners_m]
    ys = [m[1] for m in corners_m]
    return round(min(xs), 2), round(max(xs), 2), round(min(ys), 2), round(max(ys), 2)


# ── Wall obstacles ─────────────────────────────────────────────────────────────

def make_wall_obstacles(
    x_min: float, x_max: float,
    y_min: float, y_max: float,
    thickness: float = 0.01,
    clearance: float = 0.3,
) -> List[dict]:
    """Generate 4 wall obstacle dicts from field boundary."""
    t  = thickness
    cl = clearance
    return [
        {"id": "wall_bottom", "x_min": x_min,   "x_max": x_max,
         "y_min": y_min - t, "y_max": y_min,     "clearance": cl},
        {"id": "wall_top",    "x_min": x_min,   "x_max": x_max,
         "y_min": y_max,     "y_max": y_max + t, "clearance": cl},
        {"id": "wall_left",   "x_min": x_min-t, "x_max": x_min,
         "y_min": y_min,     "y_max": y_max,     "clearance": cl},
        {"id": "wall_right",  "x_min": x_max,   "x_max": x_max+t,
         "y_min": y_min,     "y_max": y_max,     "clearance": cl},
    ]


# ── Map data assembly ──────────────────────────────────────────────────────────

def assemble_map_data(
    starts:     Dict[str, Waypoint],
    end:        Waypoint,
    field:      Tuple[float, float, float, float],
    sampling:   Tuple[Waypoint, Waypoint],
    obstacles:  List[dict],
    grid_step:  float = 1.0,
) -> dict:
    """
    Assemble the full map.yaml data structure.

    Args:
        starts:    {robot_id: (x_m, y_m)}
        end:       (x_m, y_m)
        field:     (x_min, x_max, y_min, y_max)
        sampling:  two corner points defining the waypoint sampling rectangle
        obstacles: list of internal obstacle dicts (walls will be auto-added)
        grid_step: waypoint sampling step (m)
    """
    fx1, fx2, fy1, fy2 = field
    (sx1, sy1), (sx2, sy2) = sampling
    x_min = round(min(sx1, sx2), 2)
    x_max = round(max(sx1, sx2), 2)
    y_min = round(min(sy1, sy2), 2)
    y_max = round(max(sy1, sy2), 2)

    starts_out = {
        robot_id: [float(m[0]), float(m[1])]
        for robot_id, m in starts.items()
    }

    wall_obs  = make_wall_obstacles(fx1, fx2, fy1, fy2)
    all_obs   = wall_obs + obstacles

    return {
        "map": {
            "starts": starts_out,
            "end":    [float(end[0]), float(end[1])],
            "field": {
                "x_min": fx1, "x_max": fx2,
                "y_min": fy1, "y_max": fy2,
            },
            "waypoint_sampling": {
                "x_min":  x_min, "x_max":  x_max, "x_step": grid_step,
                "y_min":  y_min, "y_max":  y_max, "y_step": grid_step,
            },
            "obstacles": all_obs,
        }
    }
