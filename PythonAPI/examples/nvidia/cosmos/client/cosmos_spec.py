"""Shared Cosmos restyle config handling + Transfer 2.5 controlnet_specs build.

GPU-free and backend-agnostic: the prompt TOML is a superset consumed by both
the Transfer 2.5 and the (deprecated) Transfer1 backends.
"""

import json
import typing

import toml
from loguru import logger

DEFAULT_RESOLUTION = "480p"
CONTROL_MODALITIES = ("edge", "depth", "seg", "vis")
# Fields only Cosmos Transfer1 understood; dropped (with a warning) for 2.5.
TRANSFER1_ONLY_FIELDS = ("sigma_max", "blur_strength", "canny_threshold")


def load_config(config_path: str) -> dict:
    """Load a restyle config.

    A ``.json`` file is treated as a ready Transfer 2.5 controlnet_specs dict
    (passthrough); a ``.toml`` file is the CARLA prompt format.
    """
    if config_path.endswith(".json"):
        with open(config_path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    return toml.load(config_path)


def validate_specs(config_data: dict) -> None:
    """Raise ``ValueError`` if the config cannot produce a restyle spec."""
    prompt = config_data.get("prompt")
    if not isinstance(prompt, str) or not prompt:
        raise ValueError("config must define a non-empty string 'prompt'")
    for name in CONTROL_MODALITIES:
        if name not in config_data:
            continue
        control = config_data[name]
        if not isinstance(control, dict):
            raise ValueError("control '%s' must be a table/dict" % name)
        weight = control.get("control_weight")
        if not isinstance(weight, (int, float)) or isinstance(weight, bool):
            raise ValueError("control '%s' needs a numeric 'control_weight'" % name)


def build_controlnet_specs(
    config_data: dict,
    *,
    video_path: str,
    resolution: str = DEFAULT_RESOLUTION,
    control_paths: typing.Optional[dict] = None,
    seed: typing.Optional[int] = None,
) -> dict:
    """Translate a CARLA prompt config into a Cosmos Transfer 2.5
    ``controlnet_specs`` dict.

    ``control_paths`` overrides each modality's input video (mirrors the
    ``--edge-video`` / ``--seg-video`` CLI flags). Modalities absent from both
    the config and ``control_paths`` are omitted, so the model computes them on
    the fly when needed.
    """
    control_paths = control_paths or {}
    for field in TRANSFER1_ONLY_FIELDS:
        if field in config_data:
            logger.warning(
                "ignoring Transfer1-only field '{}' (unused by Transfer 2.5)",
                field)

    spec = {
        "prompt": config_data["prompt"],
        "video_path": video_path,
        "guidance": config_data.get("guidance", 3.0),
        "num_steps": config_data.get("num_steps", 35),
        "resolution": resolution,
    }
    if "negative_prompt" in config_data:
        spec["negative_prompt"] = config_data["negative_prompt"]
    resolved_seed = seed if seed is not None else config_data.get("seed")
    if resolved_seed is not None:
        spec["seed"] = int(resolved_seed)

    for name in CONTROL_MODALITIES:
        override = control_paths.get(name)
        table = config_data.get(name)
        if table is None and override is None:
            continue
        table = dict(table) if isinstance(table, dict) else {}
        entry = {}
        control_path = (override if override is not None
                        else table.get("input_control"))
        if control_path:
            entry["control_path"] = control_path
        entry["control_weight"] = table.get("control_weight", 1.0)
        spec[name] = entry
    return spec
