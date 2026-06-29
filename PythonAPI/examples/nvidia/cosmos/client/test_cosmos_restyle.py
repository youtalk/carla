import json
import textwrap
import pytest
import cosmos_spec


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
