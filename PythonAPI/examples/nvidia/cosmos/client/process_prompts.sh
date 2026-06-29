#!/usr/bin/env bash
# Batch-restyle every prompt in example_data/prompts/ with Cosmos Transfer 2.5.
# Usage: ./process_prompts.sh /path/to/cosmos-transfer2.5/examples/inference.py [resolution]
set -euo pipefail

INFERENCE_SCRIPT="${1:-${COSMOS_TRANSFER25_INFERENCE_SCRIPT:-}}"
RESOLUTION="${2:-480p}"
INPUT_VIDEO="example_data/artifacts/rgb.mp4"
EDGE_VIDEO="example_data/artifacts/edges.mp4"
SEG_VIDEO="example_data/artifacts/semantic_segmentation.mp4"
SEEDS=(512 1024 2048 31858)

if [[ -z "${INFERENCE_SCRIPT}" ]]; then
  echo "usage: $0 <path-to-cosmos-transfer2.5/examples/inference.py> [resolution]" >&2
  exit 2
fi

for toml_file in example_data/prompts/*.toml; do
  base_name="$(basename "${toml_file}" .toml)"
  for seed in "${SEEDS[@]}"; do
    python cosmos_restyle.py "${toml_file}" \
      --backend transfer25 \
      --input-video "${INPUT_VIDEO}" --edge-video "${EDGE_VIDEO}" --seg-video "${SEG_VIDEO}" \
      --seed "${seed}" --resolution "${RESOLUTION}" \
      --inference-script "${INFERENCE_SCRIPT}" \
      --output "outputs/${base_name}_seed_${seed}"
  done
done
