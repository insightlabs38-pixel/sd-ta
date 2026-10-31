"""Endpoints of every length 1..125 and keys that share long prefixes must stay distinct."""
import random
import sys

import synthetic

alphabet = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/-_.{}:'
rng = random.Random(7)
names = {'/'}
for length in range(2, 126):
    names.add('/' + ''.join(rng.choice(alphabet) for _ in range(length - 1)))
stem = '/' + 'x' * 62
for tail in ('', 'a', 'b', 'ab', 'a' * 20, 'a' * 61, 'b' + 'a' * 60):
    names.add(stem + tail)  # same first 63..124 bytes, different ends
for k in (31, 32, 33, 63, 64, 65):
    names.add('/' + 'y' * (k - 1))
    names.add('/' + 'y' * (k - 2) + 'z')
synthetic.ENDPOINTS[:] = sorted(names)
synthetic.COLLISION_ENDPOINTS = (synthetic.ENDPOINTS[13], synthetic.ENDPOINTS[14])
for count in (5, 20000, 100000):
    synthetic.check(sys.argv[1], 11, count)
