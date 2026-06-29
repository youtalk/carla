# Working with CARLA x Cosmos Transfer 2.5

CARLA can be connected to NVIDIA Cosmos Transfer 2.5 to create hyper-realistic variations of the synthetic data generated in CARLA. In this integration, CARLA generates control videos — RGB, semantic segmentation, depth, and edges — using the `carla_cosmos_gen.py` script. The `cosmos_restyle.py` unified entry point then calls the local Cosmos Transfer 2.5 inference script to restyle the footage, with a text prompt and TOML parameters controlling the output style.

Unlike the older Transfer1 integration (which required a remote gradio server), Transfer 2.5 runs fully locally on a single GPU — or across multiple GPUs via `torchrun` — once you have cloned the upstream model and exported `COSMOS_TRANSFER25_INFERENCE_SCRIPT`.

## Choosing a configuration for your GPU

| GPU (VRAM)                  | `--resolution` | `num_steps`            | `--num-gpus` | Notes |
|-----------------------------|----------------|------------------------|--------------|-------|
| RTX 5090 / 4090 (24–32 GB)  | `480p`         | 35 (base) / 4 (distilled) | 1         | Blackwell needs CUDA 12.8+ and an sm_120 PyTorch build. |
| A100 / H100 (40–80 GB)      | `720p`         | 35 (base) / 4 (distilled) | 1         | Higher fidelity; ~4 min/clip on H100. |
| Multi-GPU (2–8×)            | `720p`         | 35                     | N            | Runner uses `torchrun --nproc_per_node=N`. |
| Most constrained            | `480p`         | 4 (distilled)          | 1            | Distilled checkpoint, fewest steps. |

# Setting up Cosmos Transfer 2.5

## Install the upstream model and weights

The CARLA client calls the upstream `cosmos-transfer2.5` inference script as a local subprocess. Clone the upstream repository once and point the runner at it via the `COSMOS_TRANSFER25_INFERENCE_SCRIPT` environment variable:

```sh
git clone https://github.com/nvidia-cosmos/cosmos-transfer2.5
export COSMOS_TRANSFER25_INFERENCE_SCRIPT=$PWD/cosmos-transfer2.5/examples/inference.py
```

Then follow the [upstream installation instructions](https://github.com/nvidia-cosmos/cosmos-transfer2.5) to download model weights. All weights and compute requirements are governed by NVIDIA's model licence.

Install the CARLA Cosmos client dependencies:

```sh
cd PythonAPI/examples/nvidia/cosmos
conda env create --file client/carla-cosmos-client.yaml
conda activate carla-cosmos-client
pip install -r requirements.txt
pip install -r client/requirements_client.txt
```

## RTX 50-series (Blackwell / sm_120) notes

!!! note
    This subsection applies only to RTX 5090 and other Blackwell (sm_120) GPUs. Most users can skip it.

Blackwell GPUs require CUDA 12.8 or later and a PyTorch build compiled for the `sm_120` compute capability. PyTorch builds that target earlier CUDA versions do not load on Blackwell. Install a compatible PyTorch nightly or CUDA 12.8 stable release before running inference on an RTX 5090.

The `--disable-guardrails` flag disables Cosmos's built-in VRAM guardrails, which can reduce peak VRAM usage and speed up inference. This flag is intended for experimentation only and should not be used in production workflows.

```sh
# Blackwell: 480p restyle with guardrails disabled (experiments only)
python cosmos_restyle.py example_data/transfer25_defaults.toml \
  --input-video example_data/artifacts/rgb.mp4 \
  --edge-video example_data/artifacts/edges.mp4 \
  --seg-video example_data/artifacts/semantic_segmentation.mp4 \
  --resolution 480p --disable-guardrails --output outputs/
```

# Generating control inputs

**1. Start CARLA**

Navigate to the root folder of your CARLA installation and execute the launch script:

```sh
./CarlaUnreal.sh
```

**2. Generate control videos from a CARLA log**

If you want to generate new control inputs using an example, you can run the `PythonAPI/examples/nvidia/cosmos/client/carla_cosmos_gen.py` script. The example log `town10hd_opt_actorPOV298.log` in the `example_data/logs/` directory can be used to generate a ready set of artifacts. You will find several other example log files in the same directory to experiment with.

A typical invocation will look like this:

```sh
cd PythonAPI/examples/nvidia/cosmos/client
# Replace /full_path_to_log/your_log.log with the absolute path to your log file
# and output_path with your path to store the results (can be a relative path)
python carla_cosmos_gen.py -f full_path_to_log/your_log.log \
  --sensors cosmos_aov.yaml \
  --class-filter-config filter_semantic_classes.yaml \
  -c ego_sim_id -s 0.0 -d 5.0 -o output_path
```

The `ego_sim_id` value is the actor ID of the ego vehicle, which the generation script uses to identify it. If you are recording your own scenarios, note the ego vehicle's actor ID from the `id` attribute.

This step produces a set of video files — including `rgb.mp4`, `edges.mp4`, and `semantic_segmentation.mp4` — that are used to control Cosmos Transfer 2.5.

The videos generated may include:

- RGB
- Depth
- Edges
- Semantic segmentation
- Instance segmentation
- Sky mask

# Restyling with cosmos_restyle.py

## Quickstart

1. Install the upstream model (one-time), then point the runner at it:
   ```sh
   git clone https://github.com/nvidia-cosmos/cosmos-transfer2.5
   export COSMOS_TRANSFER25_INFERENCE_SCRIPT=$PWD/cosmos-transfer2.5/examples/inference.py
   ```
2. Generate control inputs (unchanged — produces example_data/artifacts/*.mp4):
   ```sh
   python carla_cosmos_gen.py -f example_data/logs/town10hd_opt_actorPOV298.log \
     --sensors cosmos_aov.yaml -c 298 -o example_data/artifacts
   ```
3. Restyle (pick `--resolution` for your GPU from the matrix above):
   ```sh
   python cosmos_restyle.py example_data/transfer25_defaults.toml \
     --input-video example_data/artifacts/rgb.mp4 \
     --edge-video example_data/artifacts/edges.mp4 \
     --seg-video example_data/artifacts/semantic_segmentation.mp4 \
     --resolution 480p --output outputs/
   ```

## Configuration reference

`cosmos_restyle.py` accepts either a TOML prompt file or a pre-built JSON `controlnet_specs` dict.

### TOML schema

The TOML format used by `transfer25_defaults.toml` (and all files under `example_data/prompts/`) follows the flat schema below. The client validates required fields and then translates the TOML into a `controlnet_specs` dict before invoking the inference script.

#### Required scalar fields

| Field             | Type   | Description |
|-------------------|--------|-------------|
| `prompt`          | string | Text describing the desired scene |
| `num_steps`       | int    | Number of diffusion steps (35 for the base checkpoint, 4 for the distilled checkpoint) |
| `guidance`        | float  | CFG guidance scale (Transfer 2.5 prefers ~3.0, lower than Transfer1's ~7) |

#### Optional scalar fields

| Field             | Type   | Default | Description |
|-------------------|--------|---------|-------------|
| `negative_prompt` | string | —       | Text describing what to avoid in the output |
| `seed`            | int    | —       | Random seed for reproducibility |

#### Control modalities (all optional)

Each modality is a TOML table with the keys below. Modalities absent from the config and not overridden on the command line are omitted; the model computes them on the fly when needed.

| Modality | Key               | Type          | Description |
|----------|-------------------|---------------|-------------|
| `edge`   | `input_control`   | string        | Path to the edges video |
|          | `control_weight`  | float         | Strength of this control signal |
| `depth`  | `input_control`   | string        | Path to the depth video |
|          | `control_weight`  | float         | Strength of this control signal |
| `seg`    | `input_control`   | string        | Path to the semantic segmentation video |
|          | `control_weight`  | float         | Strength of this control signal |
| `vis`    | `input_control`   | string (opt.) | Path to the visualisation control video |
|          | `control_weight`  | float         | Strength of this control signal |

#### Transfer1-only fields (ignored by Transfer 2.5)

The fields `sigma_max`, `blur_strength`, and `canny_threshold` are recognised by the Transfer1 backend but are **ignored** (with a warning) when using Transfer 2.5. You may keep them in shared TOML files for backwards compatibility; they will not affect Transfer 2.5 output.

### JSON controlnet_specs passthrough

If `config` ends with `.json`, the file is loaded as a ready `controlnet_specs` dict and passed directly to the inference script without validation or translation. This is useful when you want to assemble the controlnet_specs yourself or replay a previously generated spec from `outputs/controlnet_specs.json`.

### Example TOML

```toml
prompt = "Captured from a roof-mounted vehicle camera, a photorealistic city street; cinematic, ultra high quality."
negative_prompt = "low resolution graphics, cartoon, CG, flat scene, unrealistic colors."

num_steps = 35
guidance = 3.0

[edge]
input_control = "edges.mp4"
control_weight = 0.5

[seg]
input_control = "semantic_segmentation.mp4"
control_weight = 0.9
```

## Command-line arguments

### Shared arguments (both backends)

| Argument         | Default      | Description |
|------------------|--------------|-------------|
| `config`         | *(required)* | Path to a `.toml` prompt config or a `.json` controlnet_specs passthrough |
| `--backend`      | `transfer25` | Restyle backend; choices: `transfer25`, `transfer1` |
| `-o` / `--output`| `outputs/`   | Output directory or `.mp4` path |
| `--input-video`  | —            | Input RGB video (required when `config` is a `.toml`) |
| `--edge-video`   | —            | Override `edge.input_control` from the TOML |
| `--depth-video`  | —            | Override `depth.input_control` from the TOML |
| `--seg-video`    | —            | Override `seg.input_control` from the TOML |
| `--vis-video`    | —            | Override `vis.input_control` from the TOML |
| `--seed`         | —            | Override `seed` from the TOML |

### Transfer 2.5-only arguments

| Argument                | Default                                  | Description |
|-------------------------|------------------------------------------|-------------|
| `--resolution`          | `480p`                                   | Output resolution (e.g. `480p`, `720p`); pick from the GPU matrix above |
| `--num-gpus`            | `1`                                      | Number of GPUs; values >1 launch via `torchrun --nproc_per_node=N` |
| `--inference-script`    | `$COSMOS_TRANSFER25_INFERENCE_SCRIPT`    | Path to `cosmos-transfer2.5/examples/inference.py`; falls back to the env var |
| `--disable-guardrails`  | *(off)*                                  | Disable VRAM guardrails; reduces peak VRAM usage (experiments only) |
| `--dry-run`             | *(off)*                                  | Write the `controlnet_specs.json` and print the command without running inference |

### Transfer1-only arguments (deprecated)

| Argument      | Default | Description |
|---------------|---------|-------------|
| `--endpoint`  | —       | Gradio server URL (required when `--backend transfer1`); e.g. `http://host:8080` |

## High-resolution on a data-center GPU

On an A100 or H100 with 40–80 GB VRAM, pass `--resolution 720p` and keep `num_steps = 35` in your TOML for the highest-fidelity output. On multi-GPU nodes, increase `--num-gpus` to match the number of available GPUs:

```sh
# 720p, 2-GPU node
python cosmos_restyle.py example_data/transfer25_defaults.toml \
  --input-video example_data/artifacts/rgb.mp4 \
  --edge-video example_data/artifacts/edges.mp4 \
  --seg-video example_data/artifacts/semantic_segmentation.mp4 \
  --resolution 720p --num-gpus 2 --output outputs/
```

The runner internally calls `torchrun --nproc_per_node=2` when `--num-gpus` is greater than 1.

## Using the Transfer1 backend (deprecated)

!!! warning
    The Transfer1 backend is deprecated and will be removed in a future release. Use the default Transfer 2.5 backend instead.

To restyle with the deprecated Transfer1 gradio server, pass `--backend transfer1` and provide the server URL via `--endpoint`:

```sh
python cosmos_restyle.py example_data/prompts/rain.toml \
  --backend transfer1 \
  --endpoint http://host:8080 \
  --input-video example_data/artifacts/rgb.mp4 \
  --edge-video example_data/artifacts/edges.mp4 \
  --seg-video example_data/artifacts/semantic_segmentation.mp4
```

# Migrating from Cosmos Transfer1

If you previously used the Transfer1 integration via `cosmos_client.py`, migration to `cosmos_restyle.py` (Transfer 2.5) requires the following changes:

- **No remote server needed.** Transfer 2.5 runs locally; remove your Brev or Docker server setup.
- **Export `COSMOS_TRANSFER25_INFERENCE_SCRIPT`** (or pass `--inference-script`) pointing at the cloned `cosmos-transfer2.5/examples/inference.py`.
- **Replace `cosmos_client.py <url> <toml>` with `cosmos_restyle.py <toml> --input-video ...`**. All existing TOML prompt files work unchanged; Transfer1-only fields (`sigma_max`, `blur_strength`, `canny_threshold`) are silently ignored.
- **Choose `--resolution`** from the GPU matrix at the top of this page. The default `480p` fits on a 24 GB GPU.
- **Control video paths** are now passed via `--edge-video`, `--seg-video`, etc. on the command line (or set via `edge.input_control` in the TOML).

The control-input generation step (`carla_cosmos_gen.py`) is **identical** between Transfer1 and Transfer 2.5. If you already have `rgb.mp4`, `edges.mp4`, and `semantic_segmentation.mp4` from a previous Transfer1 run, you can restyle them immediately without re-running the generation step.

For the legacy Transfer1 workflow and server-deployment instructions, see [nvidia_cosmos_transfer.md](nvidia_cosmos_transfer.md).
