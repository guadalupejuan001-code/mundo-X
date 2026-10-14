import math
from collections import Counter

import torch


class BridgeX:
    """
    Compatibility layer between today's byte-oriented computers and
    Transformer X's sparse wave-state representation.

    Byte identity -> mode position (0..255)
    Byte activity -> complex state z = A * exp(i*phi)
    """

    def __init__(self, total_modes: int = 256):
        if total_modes != 256:
            raise ValueError("BridgeX v0.1 uses exactly 256 byte modes")
        self.total_modes = total_modes

    @staticmethod
    def _state_for_mode(data: bytes, mode: int):
        n = len(data)
        if n == 0:
            return 0.0, 0.0

        positions = [i for i, value in enumerate(data) if value == mode]
        if not positions:
            return 0.0, 0.0

        amplitude = len(positions) / n

        # Circular mean of positions. Position is encoded as phase.
        re = 0.0
        im = 0.0
        for pos in positions:
            angle = 2.0 * math.pi * (pos / n)
            re += math.cos(angle)
            im += math.sin(angle)

        phase = math.atan2(im, re)

        return (
            amplitude * math.cos(phase),
            amplitude * math.sin(phase),
        )

    def active_modes(self, data: bytes, top_k: int = 32):
        counts = Counter(data)
        return [
            value
            for value, _ in counts.most_common(top_k)
        ]

    def encode(self, data: bytes, top_k: int = 32):
        modes = self.active_modes(data, top_k=top_k)
        if not modes:
            modes = [0]

        z = [self._state_for_mode(data, m) for m in modes]

        return (
            torch.tensor(modes, dtype=torch.long),
            torch.tensor(z, dtype=torch.float32),
        )

    def encode_for_modes(self, data: bytes, mode_ids: torch.Tensor):
        modes = [int(x) for x in mode_ids.tolist()]
        z = [self._state_for_mode(data, m) for m in modes]
        return torch.tensor(z, dtype=torch.float32)
