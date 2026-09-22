from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path


_TARGET = Path(__file__).with_name("final_local_path_pipeline.launch.py")
_SPEC = spec_from_file_location("_final_local_path_pipeline_launch", _TARGET)

if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError(f"Failed to load launch file: {_TARGET}")

_MODULE = module_from_spec(_SPEC)
_SPEC.loader.exec_module(_MODULE)

generate_launch_description = _MODULE.generate_launch_description
