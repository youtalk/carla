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
