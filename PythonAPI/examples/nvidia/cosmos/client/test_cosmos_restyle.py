import json
import os
import textwrap
import pytest
import cosmos_spec
import cosmos_transfer25


def _write(tmp_path, name, text):
    path = tmp_path / name
    path.write_text(textwrap.dedent(text), encoding="utf-8")
    return str(path)


def test_load_config_toml(tmp_path):
    path = _write(tmp_path, "p.toml", """
        prompt = "a street"
        num_steps = 35
        [seg]
        input_control = "semantic_segmentation.mp4"
        control_weight = 0.9
    """)
    data = cosmos_spec.load_config(path)
    assert data["prompt"] == "a street"
    assert data["seg"]["control_weight"] == 0.9


def test_load_config_json_passthrough(tmp_path):
    path = tmp_path / "spec.json"
    path.write_text(json.dumps({"prompt": "x", "video_path": "v.mp4"}), encoding="utf-8")
    assert cosmos_spec.load_config(str(path)) == {"prompt": "x", "video_path": "v.mp4"}


def test_validate_specs_rejects_missing_prompt():
    with pytest.raises(ValueError):
        cosmos_spec.validate_specs({"num_steps": 4})


def test_validate_specs_rejects_nonnumeric_control_weight():
    with pytest.raises(ValueError):
        cosmos_spec.validate_specs({"prompt": "x", "edge": {"control_weight": "high"}})


def _base_toml():
    return {
        "prompt": "a winter street",
        "negative_prompt": "cartoon, low-res",
        "input_video_path": "input_video.mp4",
        "num_steps": 35,
        "guidance": 7.0,
        "sigma_max": 78,            # Transfer1-only -> dropped
        "blur_strength": "medium",  # Transfer1-only -> dropped
        "seed": 1024,
        "edge": {"input_control": "edges.mp4", "control_weight": 0.5},
        "seg": {"input_control": "semantic_segmentation.mp4", "control_weight": 0.9},
    }


def test_build_specs_maps_core_fields():
    spec = cosmos_spec.build_controlnet_specs(_base_toml(), video_path="rgb.mp4")
    assert spec["prompt"] == "a winter street"
    assert spec["negative_prompt"] == "cartoon, low-res"
    assert spec["video_path"] == "rgb.mp4"
    assert spec["num_steps"] == 35
    assert spec["resolution"] == "480p"
    assert spec["seed"] == 1024


def test_build_specs_drops_transfer1_only_fields():
    spec = cosmos_spec.build_controlnet_specs(_base_toml(), video_path="rgb.mp4")
    for field in cosmos_spec.TRANSFER1_ONLY_FIELDS:
        assert field not in spec


def test_build_specs_maps_control_tables():
    spec = cosmos_spec.build_controlnet_specs(_base_toml(), video_path="rgb.mp4")
    assert spec["edge"] == {"control_path": "edges.mp4", "control_weight": 0.5}
    assert spec["seg"] == {"control_path": "semantic_segmentation.mp4", "control_weight": 0.9}
    assert "depth" not in spec  # commented out in the config -> absent
    assert "vis" not in spec


def test_build_specs_applies_overrides_and_resolution():
    spec = cosmos_spec.build_controlnet_specs(
        _base_toml(), video_path="artifacts/rgb.mp4", resolution="720p",
        control_paths={"edge": "artifacts/edges.mp4", "seg": "artifacts/seg.mp4"}, seed=2048)
    assert spec["resolution"] == "720p"
    assert spec["seed"] == 2048
    assert spec["edge"]["control_path"] == "artifacts/edges.mp4"
    assert spec["seg"]["control_path"] == "artifacts/seg.mp4"


def test_build_specs_vis_without_input_is_on_the_fly():
    spec = cosmos_spec.build_controlnet_specs(
        {"prompt": "x", "vis": {"control_weight": 0.3}}, video_path="rgb.mp4")
    assert spec["vis"] == {"control_weight": 0.3}  # no control_path -> computed on the fly


def test_build_command_single_gpu():
    cmd = cosmos_transfer25.build_inference_command(
        "out/controlnet_specs.json", "out",
        inference_script="/repo/examples/inference.py", python_exe="python3")
    assert cmd == ["python3", "/repo/examples/inference.py",
                   "-i", "out/controlnet_specs.json", "-o", "out"]


def test_build_command_multi_gpu_uses_torchrun():
    cmd = cosmos_transfer25.build_inference_command(
        "out/spec.json", "out", inference_script="/repo/examples/inference.py", num_gpus=8)
    assert cmd[0] == "torchrun"
    assert "--nproc_per_node=8" in cmd
    assert cmd[-4:] == ["-i", "out/spec.json", "-o", "out"]


def test_build_command_disable_guardrails():
    cmd = cosmos_transfer25.build_inference_command(
        "s.json", "out", inference_script="i.py", disable_guardrails=True)
    assert "--disable-guardrails" in cmd


def test_run_inference_dry_run_writes_spec_and_skips_exec(tmp_path):
    out = str(tmp_path / "out")
    spec = {"prompt": "x", "video_path": "rgb.mp4", "resolution": "480p"}
    result = cosmos_transfer25.run_inference(spec, out, inference_script="i.py", dry_run=True)
    assert result == os.path.join(out, "controlnet_specs.json")
    assert os.path.exists(result)
