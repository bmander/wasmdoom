"""Versioned recurrent policy layout and exact warm-start from the 149-weight MLP."""
import math
import random

INPUTS, MEMORY, HIDDEN, OUTPUTS = 24, 8, 32, 10
STRIDE = INPUTS + MEMORY + 1
OUTPUT_START = STRIDE * HIDDEN
WEIGHTS = OUTPUT_START + (HIDDEN + 1) * (OUTPUTS + MEMORY)


def upgrade(policy, seed):
    rng = random.Random(seed)
    old = policy['network']
    if old is not None and len(old) == WEIGHTS:
        return {'params': policy['params'][:], 'network': old[:]}
    weights = [0.] * WEIGHTS
    # Extra hidden units and memory have random connections. Actions initially
    # ignore them, exactly preserving the parent controller's tactical outputs.
    for h in range(8 if old else 0, HIDDEN):
        for i in range(1, STRIDE): weights[h * STRIDE + i] = rng.gauss(0, 1 / math.sqrt(STRIDE))
    for o in range(OUTPUTS, OUTPUTS + MEMORY):
        for h in range(1, HIDDEN + 1):
            weights[OUTPUT_START + o * (HIDDEN + 1) + h] = rng.gauss(0, .15)
    if old:
        if len(old) != 149: raise ValueError('Unknown parent architecture')
        for h in range(8):
            weights[h * STRIDE:h * STRIDE + 13] = old[h * 13:h * 13 + 13]
        for o in range(5):
            weights[OUTPUT_START + o * 33:OUTPUT_START + o * 33 + 9] = old[104 + o * 9:104 + o * 9 + 9]
    return {'params': policy['params'][:], 'network': weights}


def shifted(policy, direction, scale):
    return {'params': policy['params'][:],
            'network': [max(-4., min(4., w + scale * d)) for w, d in zip(policy['network'], direction)]}


def direction(rng):
    # Layer-normalized perturbations avoid saturating all actions at once.
    return [rng.gauss(0, .5 if i < OUTPUT_START else 1.) for i in range(WEIGHTS)]
