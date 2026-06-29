import json
import os
import textwrap
import warnings

import pytest

import cosmos_spec
import cosmos_transfer25
import cosmos_client
import cosmos_restyle


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


def test_restyle_transfer1_injects_overrides_and_delegates(monkeypatch):
    captured = {}

    def fake_worker(url, config_data, **kwargs):
        captured["url"] = url
        captured["config"] = config_data
        return "/tmp/result.mp4"

    monkeypatch.setattr(cosmos_client, "_async_with_upload_example", fake_worker)
    config = {"prompt": "x", "input_video_path": "input_video.mp4",
              "edge": {"input_control": "edges.mp4", "control_weight": 0.5}}

    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        out = cosmos_client.restyle_transfer1(
            "http://localhost:8080", config,
            control_paths={"edge": "artifacts/edges.mp4"},
            input_video="artifacts/rgb.mp4", seed=2048)

    assert out == "/tmp/result.mp4"
    assert captured["url"] == "http://localhost:8080"
    assert captured["config"]["input_video_path"] == "artifacts/rgb.mp4"
    assert captured["config"]["edge"]["input_control"] == "artifacts/edges.mp4"
    assert captured["config"]["seed"] == 2048
    assert any(issubclass(w.category, DeprecationWarning) for w in caught)
    # the caller's dict (and its nested tables) must NOT be mutated
    assert config["input_video_path"] == "input_video.mp4"
    assert config["edge"]["input_control"] == "edges.mp4"
    assert config["edge"]["control_weight"] == 0.5
    # the worker's copy keeps sibling keys when overriding input_control
    assert captured["config"]["edge"]["control_weight"] == 0.5


def test_parse_args_defaults():
    args = cosmos_restyle.parse_args(
        ["p.toml", "--input-video", "rgb.mp4", "--inference-script", "i.py"])
    assert args.backend == "transfer25"
    assert args.resolution == "480p"
    assert args.output == "outputs/"


def test_main_transfer25_toml_dry_run(tmp_path):
    cfg = tmp_path / "p.toml"
    cfg.write_text('prompt = "a street"\n[seg]\ninput_control="s.mp4"\ncontrol_weight=0.9\n',
                   encoding="utf-8")
    out = tmp_path / "out"
    rc = cosmos_restyle.main([
        str(cfg), "--input-video", "rgb.mp4", "--seg-video", "seg.mp4",
        "--inference-script", "i.py", "-o", str(out), "--dry-run"])
    assert rc == 0
    spec = json.loads((out / "controlnet_specs.json").read_text())
    assert spec["video_path"] == "rgb.mp4"
    assert spec["seg"]["control_path"] == "seg.mp4"
    assert spec["resolution"] == "480p"


def test_main_transfer25_requires_inference_script(tmp_path, monkeypatch):
    monkeypatch.delenv("COSMOS_TRANSFER25_INFERENCE_SCRIPT", raising=False)
    cfg = tmp_path / "p.toml"
    cfg.write_text('prompt = "x"\n', encoding="utf-8")
    rc = cosmos_restyle.main([str(cfg), "--input-video", "rgb.mp4"])
    assert rc == 2  # missing --inference-script


def test_main_toml_requires_input_video(tmp_path):
    cfg = tmp_path / "p.toml"
    cfg.write_text('prompt = "x"\n', encoding="utf-8")
    rc = cosmos_restyle.main([str(cfg), "--inference-script", "i.py"])
    assert rc == 2  # .toml config requires --input-video


def test_main_transfer1_dispatch(monkeypatch, tmp_path):
    calls = {}
    monkeypatch.setattr(
        cosmos_restyle, "_load_transfer1",
        lambda: (lambda endpoint, config_data, **kw: calls.update(
            endpoint=endpoint, kw=kw) or "/tmp/out.mp4"))
    cfg = tmp_path / "p.toml"
    cfg.write_text('prompt = "x"\n[seg]\ninput_control="s.mp4"\ncontrol_weight=0.9\n',
                   encoding="utf-8")
    rc = cosmos_restyle.main([
        str(cfg), "--backend", "transfer1", "--endpoint", "http://localhost:8080",
        "--input-video", "rgb.mp4", "--seg-video", "seg.mp4", "-o", "outputs/"])
    assert rc == 0
    assert calls["endpoint"] == "http://localhost:8080"
    assert calls["kw"]["control_paths"]["seg"] == "seg.mp4"


def test_main_transfer1_requires_endpoint(tmp_path):
    cfg = tmp_path / "p.toml"
    cfg.write_text('prompt = "x"\n', encoding="utf-8")
    rc = cosmos_restyle.main([str(cfg), "--backend", "transfer1", "--input-video", "rgb.mp4"])
    assert rc == 2  # missing --endpoint
