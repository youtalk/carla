"""Cosmos Transfer 2.5 backend: build a controlnet_specs JSON and invoke the
upstream ``examples/inference.py`` as a local subprocess."""

import glob
import json
import os
import sys

from loguru import logger


def write_spec(spec: dict, spec_path: str) -> None:
    """Serialize a controlnet_specs dict to ``spec_path`` as JSON."""
    with open(spec_path, "w", encoding="utf-8") as handle:
        json.dump(spec, handle, indent=2)


def build_inference_command(
    spec_path: str,
    output_dir: str,
    *,
    inference_script: str,
    num_gpus: int = 1,
    disable_guardrails: bool = False,
    python_exe: str = sys.executable,
) -> list:
    """Build the argv for the upstream Transfer 2.5 inference entrypoint."""
    if num_gpus > 1:
        cmd = ["torchrun", "--nproc_per_node=%d" % num_gpus,
               "--master_port=12341", inference_script]
    else:
        cmd = [python_exe, inference_script]
    cmd += ["-i", spec_path, "-o", output_dir]
    if disable_guardrails:
        cmd.append("--disable-guardrails")
    return cmd


def run_inference(
    spec: dict,
    output_dir: str,
    *,
    inference_script: str,
    num_gpus: int = 1,
    disable_guardrails: bool = False,
    dry_run: bool = False,
) -> str:
    """Write the spec, run inference, and return the produced ``.mp4`` path.

    On ``dry_run`` the command is logged but not executed and the spec path is
    returned instead.
    """
    import subprocess

    os.makedirs(output_dir, exist_ok=True)
    spec_path = os.path.join(output_dir, "controlnet_specs.json")
    write_spec(spec, spec_path)
    cmd = build_inference_command(
        spec_path, output_dir, inference_script=inference_script,
        num_gpus=num_gpus, disable_guardrails=disable_guardrails)
    logger.info("running: {}", " ".join(cmd))
    if dry_run:
        return spec_path
    subprocess.run(cmd, check=True)
    outputs = sorted(glob.glob(os.path.join(output_dir, "*.mp4")), key=os.path.getmtime)
    if not outputs:
        raise RuntimeError("no output .mp4 produced in %s" % output_dir)
    return outputs[-1]
