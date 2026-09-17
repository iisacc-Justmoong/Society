"""Small official-serializer fixtures and an independent arithmetic oracle."""
import json
from pathlib import Path
import sys

import torch
from safetensors.torch import load_file, save_file

action, directory = sys.argv[1], Path(sys.argv[2])
base = torch.arange(4, dtype=torch.float32).reshape(2, 2)
additional = base + 8
down = torch.tensor([[1.0, 2.0]])
up = torch.tensor([[0.25], [0.5]])
delta = up @ down

if action == "create":
    save_file({"layer.weight": base, "steps": torch.tensor(42)}, directory / "base.safetensors")
    save_file({"layer.weight": additional, "steps": torch.tensor(42)}, directory / "extra.safetensors")
    save_file({"layer.lora_A.weight": down, "layer.lora_B.weight": up}, directory / "style.safetensors")
    save_file({"wrong.weight": base}, directory / "wrong.safetensors")
    for name, value in (("base-pipeline", base), ("extra-pipeline", additional)):
        target = directory / name
        (target / "unet").mkdir(parents=True)
        (target / "model_index.json").write_text(json.dumps({"_class_name": "FixturePipeline"}))
        (target / "unet/config.json").write_text('{"sample_size": 2}')
        save_file({"layer.weight": value}, target / "unet/model.safetensors")
elif action == "verify-unified":
    output = Path(sys.argv[3])
    manifest = json.loads((output / "model_index.json").read_text())
    assert manifest["schema"] == "iild-unified-model-v1"
    assert len(manifest["stages"]) == 2
    first, second = manifest["stages"]
    torch.testing.assert_close(load_file(output / first["model"])["layer.weight"], base)
    torch.testing.assert_close(load_file(output / second["model"])["layer.weight"], additional + delta)
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
else:
    raise SystemExit("Unknown fixture action")
