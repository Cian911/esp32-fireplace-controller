# Paste your working ON RAW sequence here:
raw = [
   
-50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -100, 50, -50, 50, -100, 50, -100, 50, -200, 100, -250, 100, -100, 150, -50, 100, -200, 50, -50, 150, -150, 50, -500, 50, -1650, 250, -50, 50, -100, 100, -50, 50, -50, 100, -150, 50, -50, 150, -2550, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -100, 50, -50, 50, -100, 50, -100, 50, -200, 100, -250, 100, -100, 150, -50, 100, -200, 50, -50, 150, -150, 50, -500, 50, -1650, 250, -50, 50, -100, 100, -50, 50, -50, 100, -150, 50, -50, 150, -2550, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -50, 50, -100, 50, -50, 50, -100, 50, -100, 50, -200, 100, -250, 100, -100, 150, -50, 100, -200, 50, -50, 150, -150, 50, -500, 50, -1650, 250, -50, 50, -100, 100, -50, 50, -50, 100, -150, 50, -50, 150, -250,


    # ... full sequence from your Flipper ON file ...
]

SYMBOL_US = 50  # 20 kBaud => 50 microseconds per bit

bits = []
for dur in raw:
    us = abs(dur)
    symbols = int(round(us / SYMBOL_US))
    if symbols <= 0:
        continue
    bit = 1 if dur > 0 else 0
    bits.extend([bit] * symbols)

def bits_to_bytes_msb(bits):
    out = []
    for i in range(0, len(bits), 8):
        chunk = bits[i:i+8]
        if len(chunk) < 8:
            break
        val = 0
        for b in chunk:
            val = (val << 1) | b
        out.append(val)
    return out

def bits_to_bytes_lsb(bits):
    out = []
    for i in range(0, len(bits), 8):
        chunk = bits[i:i+8]
        if len(chunk) < 8:
            break
        val = 0
        for idx, b in enumerate(chunk):
            val |= (b << idx)
        out.append(val)
    return out

bytes_msb = bits_to_bytes_msb(bits)
bytes_lsb = bits_to_bytes_lsb(bits)

print("MSB-first bytes:")
print(", ".join("0x%02X" % b for b in bytes_msb))

print("\nLSB-first bytes:")
print(", ".join("0x%02X" % b for b in bytes_lsb))
