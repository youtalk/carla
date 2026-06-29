"""Unified CARLA x Cosmos restyle entry point.

Default backend is Cosmos Transfer 2.5 (local CLI). The deprecated Transfer1
gradio backend is selectable with ``--backend transfer1`` and lazy-imported so
the Transfer 2.5 path carries no gradio dependency.
"""

import argparse
import os
import sys

from loguru import logger

import cosmos_spec
import cosmos_transfer25

_CONTROL_FLAGS = {"edge": "edge_video", "depth": "depth_video",
                  "seg": "seg_video", "vis": "vis_video"}


def _load_transfer1():
    """Lazy-import the deprecated Transfer1 backend (pulls gradio_client)."""
    import cosmos_client
    return cosmos_client.restyle_transfer1


def parse_args(argv) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Restyle CARLA control inputs with Cosmos Transfer.")
    parser.add_argument("config", help="Path to a .toml prompt config or a .json controlnet_specs")
    parser.add_argument("--backend", choices=("transfer25", "transfer1"), default="transfer25",
                        help="Restyle backend (default: transfer25)")
    parser.add_argument("-o", "--output", default="outputs/",
                        help="Output dir or .mp4 path (default: outputs/)")
    parser.add_argument("--input-video", default=None, help="Input RGB video (required for .toml)")
    parser.add_argument("--edge-video", default=None)
    parser.add_argument("--depth-video", default=None)
    parser.add_argument("--seg-video", default=None)
    parser.add_argument("--vis-video", default=None)
    parser.add_argument("--seed", type=int, default=None)
    # transfer25-only
    parser.add_argument("--resolution", default=cosmos_spec.DEFAULT_RESOLUTION,
                        help="Output resolution, e.g. 480p (local) or 720p (data-center GPU)")
    parser.add_argument("--num-gpus", type=int, default=1)
    parser.add_argument("--inference-script",
                        default=os.environ.get("COSMOS_TRANSFER25_INFERENCE_SCRIPT"),
                        help="Path to cosmos-transfer2.5 examples/inference.py")
    parser.add_argument("--disable-guardrails", action="store_true",
                        help="Lower VRAM / speed up (experiments only)")
    parser.add_argument("--dry-run", action="store_true",
                        help="transfer25: write spec + print command without running")
    # transfer1-only
    parser.add_argument("--endpoint", default=None, help="Transfer1 gradio server URL")
    return parser.parse_args(argv)


def _control_paths(args) -> dict:
    return {name: getattr(args, attr) for name, attr in _CONTROL_FLAGS.items()
            if getattr(args, attr)}


def main(argv=None) -> int:
    args = parse_args(argv)
    config_data = cosmos_spec.load_config(args.config)
    is_toml = not args.config.endswith(".json")
    if is_toml and not args.input_video:
        logger.error("--input-video is required when config is a .toml")
        return 2

    if args.backend == "transfer1":
        if not args.endpoint:
            logger.error("--endpoint is required for --backend transfer1")
            return 2
        restyle_transfer1 = _load_transfer1()
        result = restyle_transfer1(
            args.endpoint, config_data, control_paths=_control_paths(args),
            input_video=args.input_video, seed=args.seed)
        logger.info("result: {}", result)
        return 0

    # transfer25 (default)
    if not args.inference_script:
        logger.error("--inference-script (or COSMOS_TRANSFER25_INFERENCE_SCRIPT) is required")
        return 2
    if is_toml:
        cosmos_spec.validate_specs(config_data)
        spec = cosmos_spec.build_controlnet_specs(
            config_data, video_path=args.input_video, resolution=args.resolution,
            control_paths=_control_paths(args), seed=args.seed)
    else:
        spec = config_data  # already a controlnet_specs dict
    # Determine output directory: treat as file path only if
    # it ends with a known video extension.
    if args.output.endswith((".mp4", ".avi", ".mov", ".mkv")):
        out_dir = os.path.dirname(args.output) or "."
    else:
        out_dir = args.output
    result = cosmos_transfer25.run_inference(
        spec, out_dir, inference_script=args.inference_script, num_gpus=args.num_gpus,
        disable_guardrails=args.disable_guardrails, dry_run=args.dry_run)
    logger.info("result: {}", result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
