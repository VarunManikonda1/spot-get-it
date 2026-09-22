import os
from typing import List, Optional, Sequence

import numpy as np


class PolicyRunner:
    """
    Final RL policy inference wrapper.

    Supported backend:
      - onnx

    Dummy backend is intentionally removed.
    ONNX Runtime provider priority:
      1. TensorrtExecutionProvider
      2. CUDAExecutionProvider
      3. CPUExecutionProvider
    """

    def __init__(
        self,
        backend: str,
        model_path: str,
        obs_dim: int,
        action_dim: int,
        obs_clip: float = 100.0,
        require_model: bool = True,
        preferred_providers: Optional[Sequence[str]] = None,
    ):
        self.backend = backend.lower().strip()
        self.model_path = model_path
        self.obs_dim = int(obs_dim)
        self.action_dim = int(action_dim)
        self.obs_clip = float(obs_clip)
        self.require_model = bool(require_model)

        self.session = None
        self.input_name: Optional[str] = None
        self.output_name: Optional[str] = None
        self.providers: List[str] = []

        if self.backend != "onnx":
            raise ValueError(
                f"Final PolicyRunner supports only backend='onnx', got '{backend}'"
            )

        if not self.require_model:
            raise ValueError("Final PolicyRunner requires require_model=True")

        self._load_onnx(preferred_providers)

    def _load_onnx(self, preferred_providers: Optional[Sequence[str]]):
        if not self.model_path:
            raise FileNotFoundError("model_path is empty")

        if not os.path.exists(self.model_path):
            raise FileNotFoundError(f"Policy model not found: {self.model_path}")

        try:
            import onnxruntime as ort
        except ImportError as exc:
            raise ImportError(
                "onnxruntime is not installed. "
                "Install onnxruntime-gpu on Jetson if TensorRT/CUDA acceleration is needed."
            ) from exc

        available = set(ort.get_available_providers())

        if preferred_providers is None:
            preferred_providers = [
                "TensorrtExecutionProvider",
                "CUDAExecutionProvider",
                "CPUExecutionProvider",
            ]

        providers = [p for p in preferred_providers if p in available]

        if not providers:
            raise RuntimeError(
                f"No usable ONNX Runtime provider. available={sorted(available)}"
            )

        self.session = ort.InferenceSession(
            self.model_path,
            providers=providers,
        )

        self.providers = list(self.session.get_providers())

        inputs = self.session.get_inputs()
        outputs = self.session.get_outputs()

        if len(inputs) < 1:
            raise RuntimeError("ONNX model has no inputs")
        if len(outputs) < 1:
            raise RuntimeError("ONNX model has no outputs")

        self.input_name = inputs[0].name
        self.output_name = outputs[0].name

    def infer(self, obs: List[float]) -> List[float]:
        if len(obs) != self.obs_dim:
            raise ValueError(f"obs length mismatch: {len(obs)} != {self.obs_dim}")

        obs_np = np.asarray(obs, dtype=np.float32)

        if not np.all(np.isfinite(obs_np)):
            raise ValueError("obs contains NaN or Inf")

        obs_np = np.clip(obs_np, -self.obs_clip, self.obs_clip)

        outputs = self.session.run(
            [self.output_name],
            {self.input_name: obs_np.reshape(1, self.obs_dim)},
        )

        action = np.asarray(outputs[0], dtype=np.float32).reshape(-1)

        if action.shape[0] != self.action_dim:
            raise RuntimeError(
                f"action dim mismatch: {action.shape[0]} != {self.action_dim}"
            )

        if not np.all(np.isfinite(action)):
            raise ValueError("policy output contains NaN or Inf")

        return action.tolist()

    def backend_name(self) -> str:
        return self.backend

    def provider_names(self) -> List[str]:
        return list(self.providers)