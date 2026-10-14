import math
import torch
import torch.nn as nn


class TransformerX(nn.Module):
    """
    Tiny Transformer operating on sparse wave modes.

    mode_ids: [B, K] identifies a position/mode.
    z:        [B, K, 2] stores only Re(z), Im(z).

    Frequency/identity is implicit in mode_id.
    Amplitude and phase are recoverable from z.
    """

    def __init__(
        self,
        total_modes: int = 256,
        d_model: int = 48,
        nhead: int = 4,
        num_layers: int = 2,
        dim_feedforward: int = 96,
        dropout: float = 0.0,
    ):
        super().__init__()
        self.total_modes = total_modes

        self.input_proj = nn.Sequential(
            nn.Linear(5, d_model),
            nn.GELU(),
            nn.LayerNorm(d_model),
        )

        layer = nn.TransformerEncoderLayer(
            d_model=d_model,
            nhead=nhead,
            dim_feedforward=dim_feedforward,
            dropout=dropout,
            activation="gelu",
            batch_first=True,
            norm_first=True,
        )
        self.core = nn.TransformerEncoder(
            layer,
            num_layers=num_layers,
            norm=nn.LayerNorm(d_model),
        )

        self.delta_head = nn.Linear(d_model, 2)
        nn.init.zeros_(self.delta_head.weight)
        nn.init.zeros_(self.delta_head.bias)

    def mode_features(self, mode_ids: torch.Tensor) -> torch.Tensor:
        u = mode_ids.to(torch.float32) / max(self.total_modes - 1, 1)
        angle = math.pi * u
        return torch.stack(
            [u, torch.sin(angle), torch.cos(angle)],
            dim=-1,
        )

    def forward(self, mode_ids: torch.Tensor, z: torch.Tensor) -> torch.Tensor:
        if z.shape[-1] != 2:
            raise ValueError("z must contain [real, imag]")

        mf = self.mode_features(mode_ids).to(z.dtype)
        x = torch.cat([z, mf], dim=-1)
        x = self.input_proj(x)
        x = self.core(x)

        delta = 0.25 * torch.tanh(self.delta_head(x))
        return z + delta

    @staticmethod
    def amplitude_phase(z: torch.Tensor):
        re, im = z[..., 0], z[..., 1]
        amplitude = torch.sqrt(re.square() + im.square() + 1e-12)
        phase = torch.atan2(im, re)
        return amplitude, phase

    def parameter_report(self):
        n = sum(p.numel() for p in self.parameters())
        return {
            "parameters": n,
            "fp32_weight_bytes": n * 4,
            "fp16_weight_bytes": n * 2,
            "int8_weight_bytes_approx": n,
        }


if __name__ == "__main__":
    model = TransformerX()
    print(model.parameter_report())
