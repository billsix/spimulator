import random, struct
random.seed(1)
special = [0.0, -0.0, 0.1+0.2, 1/3, 1.0, 1e-5, 1e-4, 0.0001234, 123456789012345678.0, 1e17, 1e18, 1e19,
           5e-324, 2.2250738585072014e-308, 1.7976931348623157e308, float('inf'), float('-inf'), float('nan'),
           -float('nan'), 0.5, 2.5, 0.125, 0.000000005, 0.000000015, 0.000000025, 999999999999999999.0,
           0.99999999999999999, 9.5, 1e-310, 3.0e10, -7.25]
vals = [struct.unpack('<Q', struct.pack('<d', v))[0] for v in special]
vals += [random.getrandbits(64) for _ in range(100000)]
for e in range(-30, 30):                     # values near decimal boundaries
    for _ in range(300):
        vals.append(struct.unpack('<Q', struct.pack('<d', random.uniform(0.5, 10) * 10.0**e))[0])
for i in range(1, 20000):                    # small integers and halves, single-precision values
    vals.append(struct.unpack('<Q', struct.pack('<d', i / 8))[0])
    vals.append(struct.unpack('<Q', struct.pack('<d', struct.unpack('<f', struct.pack('<f', random.uniform(-1e6, 1e6)))[0]))[0])
print('\n'.join('%x' % v for v in vals))
