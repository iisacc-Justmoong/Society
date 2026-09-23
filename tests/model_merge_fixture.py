"""Small official-serializer fixtures and an independent arithmetic oracle."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import zipfile

import torch
from safetensors.torch import load_file, save_file

action, directory = sys.argv[1], Path(sys.argv[2])
base = torch.arange(4, dtype=torch.float32).reshape(2, 2)
additional = base + 8
down = torch.tensor([[1.0, 2.0]])
up = torch.tensor([[0.25], [0.5]])
delta = up @ down

if action == "create":
    base_path = directory / "base.safetensors"
    save_file({"layer.weight": base, "steps": torch.tensor(42)}, base_path)
    save_file({"layer.weight": additional, "steps": torch.tensor(42)}, directory / "extra.safetensors")
    save_file({"layer.lora_A.weight": down, "layer.lora_B.weight": up}, directory / "style.safetensors")
    save_file({"wrong.weight": base}, directory / "wrong.safetensors")
    for name, value in (("base-pipeline", base), ("extra-pipeline", additional)):
        target = directory / name
        (target / "unet").mkdir(parents=True)
        (target / "model_index.json").write_text(json.dumps({"_class_name": "FixturePipeline"}))
        (target / "unet/config.json").write_text('{"sample_size": 2}')
        save_file({"layer.weight": value}, target / "unet/model.safetensors")
    member = "members/base.safetensors"
    contents = base_path.read_bytes()
    manifest = {
        "schema": "iild-unified-model-v1",
        "container": "zip-stored-v1",
        "_class_name": "IILDUnifiedCascade",
        "composition": "ordered-image-refinement",
        "stages": [{"model": member, "strength": 1.0, "loras": [],
                    "size_bytes": len(contents), "sha256": hashlib.sha256(contents).hexdigest()}],
    }
    with zipfile.ZipFile(directory / "base.iildmodel", "w", compression=zipfile.ZIP_STORED,
                         allowZip64=True) as archive:
        for name, payload in (("model_index.json", json.dumps(manifest, sort_keys=True).encode()),
                              (member, contents)):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.external_attr = 0o100444 << 16
            archive.writestr(info, payload)
elif action == "verify-unified":
    output = Path(sys.argv[3])
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        with zipfile.ZipFile(output) as archive:
            archive.extractall(root)
        manifest = json.loads((root / "model_index.json").read_text())
        assert manifest["schema"] == "iild-unified-model-v1"
        assert manifest["container"] == "zip-stored-v1"
        assert len(manifest["stages"]) == 2
        first, second = manifest["stages"]
        torch.testing.assert_close(load_file(root / first["model"])["layer.weight"], base)
        torch.testing.assert_close(load_file(root / second["model"])["layer.weight"], additional + delta)
        assert second["loras"] == [{"source_index": 2, "strength": 1.0}]
    print("Verified unified members and checkpoint-specific LoRA fusion.")
elif action == "verify":
    output, mode, weight_mode = Path(sys.argv[3]), sys.argv[4], sys.argv[5]
    checkpoint_weight, lora_weight = {"automatic": (0.5, 1.0), "shared": (0.25, 0.25),
                                    "per-model": (0.25, 1.5)}[weight_mode]
    if output.is_dir():
        state = load_file(output / "unet/model.safetensors")
        expected = (base + additional) / 2
        assert (output / "model_index.json").is_file()
        assert (output / "merge.json").is_file()
    else:
        state = load_file(output)
        expected = ((1 - checkpoint_weight) * base + checkpoint_weight * additional + lora_weight * delta
                    if mode == "weighted-sum" else base - checkpoint_weight * additional - lora_weight * delta)
        assert state["steps"].item() == 42
    torch.testing.assert_close(state["layer.weight"], expected)
    print("Verified merged tensors and preserved buffers.")
elif action == "verify-base":
    state = load_file(Path(sys.argv[3]))
    torch.testing.assert_close(state["layer.weight"], base)
    assert state["steps"].item() == 42
    print("Verified common-layer projection preserves the executable base layout.")
else:
    raise SystemExit("Unknown fixture action")
