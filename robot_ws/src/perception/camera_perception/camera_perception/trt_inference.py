"""
trt_inference.py
추론 모듈

- 로컬 PC : ONNX Runtime 사용
- Orin Nano : TensorRT FP16 사용

engine_path 확장자로 자동 분기:
  .onnx  → ONNX Runtime
  .engine → TensorRT
"""

import numpy as np
import cv2


class TRTInference:

    def __init__(self, engine_path: str, infer_size: int = 480,
                 conf_threshold: float = 0.25, iou_threshold: float = 0.45):
        self.infer_size     = infer_size
        self.conf_threshold = conf_threshold
        self.iou_threshold  = iou_threshold
        self.backend        = None

        if engine_path.endswith('.onnx'):
            self._init_onnx(engine_path)
        elif engine_path.endswith('.engine'):
            self._init_trt(engine_path)
        else:
            raise ValueError(f'지원하지 않는 파일 형식: {engine_path}')

    def _init_onnx(self, engine_path: str):
        import onnxruntime as ort
        self.backend = 'onnx'
        self.sess = ort.InferenceSession(
            engine_path,
            providers=['CUDAExecutionProvider', 'CPUExecutionProvider']
        )
        print(f'[TRTInference] ONNX Runtime 사용: {engine_path}')

    def _init_trt(self, engine_path: str):
        import tensorrt as trt
        import pycuda.driver as cuda
        import pycuda.autoinit  # noqa: F401

        self.backend = 'trt'
        self.cuda    = cuda

        logger = trt.Logger(trt.Logger.WARNING)
        with open(engine_path, 'rb') as f, trt.Runtime(logger) as runtime:
            self.engine = runtime.deserialize_cuda_engine(f.read())
        self.context = self.engine.create_execution_context()

        self.input_name  = 'images'
        self.output_name = 'output0'

        input_shape  = (1, 3, self.infer_size, self.infer_size)
        output_shape = self.engine.get_tensor_shape(self.output_name)

        self.h_input  = cuda.pagelocked_empty(int(np.prod(input_shape)),  dtype=np.float32)
        self.h_output = cuda.pagelocked_empty(int(np.prod(output_shape)), dtype=np.float32)
        self.d_input  = cuda.mem_alloc(self.h_input.nbytes)
        self.d_output = cuda.mem_alloc(self.h_output.nbytes)
        self.stream   = cuda.Stream()
        self.output_shape = output_shape
        print(f'[TRTInference] TensorRT FP16 사용: {engine_path}')

    def preprocess(self, frame: np.ndarray) -> np.ndarray:
        """letterbox 리사이즈 (비율 유지 + 회색 패딩)"""
        h, w = frame.shape[:2]
        scale = min(self.infer_size / w, self.infer_size / h)
        new_w = int(w * scale)
        new_h = int(h * scale)
        resized = cv2.resize(frame, (new_w, new_h))

        pad_w = (self.infer_size - new_w) // 2
        pad_h = (self.infer_size - new_h) // 2

        img = np.full((self.infer_size, self.infer_size, 3), 114, dtype=np.uint8)
        img[pad_h:pad_h + new_h, pad_w:pad_w + new_w] = resized

        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
        return np.transpose(img, (2, 0, 1))[np.newaxis]

    def _postprocess(self, output: np.ndarray) -> list:
        pred = output[0].T  # (N, 5): cx, cy, w, h, conf
        mask = pred[:, 4] > self.conf_threshold
        filtered = pred[mask]
        if len(filtered) == 0:
            return []

        cx, cy, w, h, conf = (filtered[:, i] for i in range(5))

        x1 = (cx - w / 2) / self.infer_size
        y1 = (cy - h / 2) / self.infer_size
        bw = w / self.infer_size
        bh = h / self.infer_size

        boxes_px = np.stack([
            (cx - w / 2).astype(int),
            (cy - h / 2).astype(int),
            w.astype(int),
            h.astype(int)
        ], axis=1)
        indices = cv2.dnn.NMSBoxes(
            boxes_px.tolist(), conf.tolist(),
            self.conf_threshold, self.iou_threshold
        )
        if len(indices) == 0:
            return []
        indices = indices.flatten()

        results = []
        for i in indices:
            results.append({
                'bbox': [
                    float(np.clip(x1[i], 0, 1)),
                    float(np.clip(y1[i], 0, 1)),
                    float(np.clip(bw[i], 0, 1)),
                    float(np.clip(bh[i], 0, 1)),
                ],
                'conf': float(conf[i]),
            })
        return results

    def infer(self, frame: np.ndarray) -> list:
        input_tensor = self.preprocess(frame)

        if self.backend == 'onnx':
            output = self.sess.run(None, {'images': input_tensor})[0]
        else:
            np.copyto(self.h_input, input_tensor.ravel())
            self.cuda.memcpy_htod_async(self.d_input, self.h_input, self.stream)
            self.context.set_tensor_address(self.input_name,  int(self.d_input))
            self.context.set_tensor_address(self.output_name, int(self.d_output))
            self.context.execute_async_v3(stream_handle=self.stream.handle)
            self.cuda.memcpy_dtoh_async(self.h_output, self.d_output, self.stream)
            self.stream.synchronize()
            output = self.h_output.reshape(self.output_shape)

        return self._postprocess(output)
