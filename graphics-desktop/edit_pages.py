"""Derived GPU index for the authoritative sparse edits, never a second world.

One directory word per 256 IDs points to 32 words of eight 4-bit states.
An absent page means state zero. Only pages with nonzero edits are stored.
Offsets are word indices, so texture row boundaries do not affect decoding.
"""
import math
from itertools import islice
import numpy as np


def pack_edits(count, edits, max_texture):
    if not 1 <= count <= 1_048_576 or max_texture < 1:
        raise ValueError('Invalid edit index extent')
    if any(not 0 <= i < count or not 0 <= state <= 7 for i, state in edits.items()):
        raise ValueError('Invalid scene edits')
    pages = sorted({i >> 8 for i, state in edits.items() if state})
    directory = (count + 255) // 256 if edits else 1
    words = directory + len(pages) * 32
    # Fixed power-of-two rows eliminate divisions from every GPU lookup.
    # GLES 3 supports this width; the fully populated index is only 528 rows.
    width = 256 if edits else 1
    height = math.ceil(words / width)
    if width > max_texture or height > max_texture:
        raise ValueError('Edit texture exceeds this device capability')
    data = np.zeros((height, width), dtype=np.uint32)
    flat = data.ravel()
    for index, page in enumerate(pages):
        flat[page] = directory + index * 32
    if len(edits) <= 256:
        for i, state in edits.items():
            if state:
                word = int(flat[i >> 8]) + ((i & 255) >> 3)
                flat[word] |= np.uint32(state << ((i & 7) * 4))
    else:
        # Vectorize bounded batches, not an array proportional to all IDs.
        # Several states share a word: indexed |= would lose duplicate writes.
        # bitwise_or.at applies every update exactly, including adjacent IDs.
        items = iter(edits.items())
        while True:
            part = list(islice(items, 4096))
            if not part:
                break
            values = np.asarray(part, dtype=np.uint32)
            values = values[values[:, 1] != 0]
            ids, states = values[:, 0], values[:, 1]
            words = flat[ids >> 8] + ((ids & 255) >> 3)
            np.bitwise_or.at(flat, words, states << ((ids & 7) * 4))
    return data
