import argparse
import sys
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from bridge_x import BridgeX
from transformer_x import TransformerX


def windows(data: bytes, window_size: int):
    usable = len(data) // window_size
    for i in range(usable):
        start = i * window_size
        yield data[start:start + window_size]


def build_examples(data: bytes, window_size=512, top_k=32):
    bridge = BridgeX()
    chunks = list(windows(data, window_size))

    for current, nxt in zip(chunks, chunks[1:]):
        mode_ids, z_now = bridge.encode(current, top_k=top_k)
        z_next = bridge.encode_for_modes(nxt, mode_ids)
        yield mode_ids, z_now, z_next


def loss_fn(pred, target):
    return (pred - target).square().mean()


@torch.no_grad()
def evaluate(model, examples, device):
    model.eval()
    model_loss = 0.0
    baseline_loss = 0.0

    for mode_ids, z_now, z_next in examples:
        mode_ids = mode_ids.unsqueeze(0).to(device)
        z_now = z_now.unsqueeze(0).to(device)
        z_next = z_next.unsqueeze(0).to(device)

        pred = model(mode_ids, z_now)

        model_loss += loss_fn(pred, z_next).item()
        baseline_loss += loss_fn(z_now, z_next).item()

    n = max(len(examples), 1)
    return model_loss / n, baseline_loss / n


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("file", help="Any local file to read as raw bytes")
    parser.add_argument("--window", type=int, default=512)
    parser.add_argument("--top-k", type=int, default=32)
    parser.add_argument("--epochs", type=int, default=20)
    args = parser.parse_args()

    raw = Path(args.file).read_bytes()

    examples = list(
        build_examples(
            raw,
            window_size=args.window,
            top_k=args.top_k,
        )
    )

    if len(examples) < 10:
        raise SystemExit("File is too small. Use a larger file or smaller --window.")

    split = max(int(len(examples) * 0.8), 1)
    train_examples = examples[:split]
    val_examples = examples[split:]

    device = (
        "cuda"
        if torch.cuda.is_available()
        else "mps"
        if torch.backends.mps.is_available()
        else "cpu"
    )

    model = TransformerX().to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=5e-4)

    print("device:", device)
    print("model:", model.parameter_report())
    print("train examples:", len(train_examples))
    print("validation examples:", len(val_examples))

    for epoch in range(1, args.epochs + 1):
        model.train()
        running = 0.0

        for mode_ids, z_now, z_next in train_examples:
            mode_ids = mode_ids.unsqueeze(0).to(device)
            z_now = z_now.unsqueeze(0).to(device)
            z_next = z_next.unsqueeze(0).to(device)

            pred = model(mode_ids, z_now)
            loss = loss_fn(pred, z_next)

            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()

            running += loss.item()

        model_loss, baseline = evaluate(
            model,
            val_examples,
            device,
        )

        print(
            f"epoch={epoch:03d} "
            f"train={running / len(train_examples):.8f} "
            f"val={model_loss:.8f} "
            f"identity={baseline:.8f}"
        )

    torch.save(
        {
            "state_dict": model.state_dict(),
            "model_report": model.parameter_report(),
            "window": args.window,
            "top_k": args.top_k,
        },
        "transformer_x_v0_1.pt",
    )
    print("saved: transformer_x_v0_1.pt")


if __name__ == "__main__":
    main()
