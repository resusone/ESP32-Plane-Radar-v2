# PlatformIO pre-build script: gzip the settings page before it is embedded.
# A third of the size means a third of the airtime, which matters when the
# radar sits at the edge of Wi-Fi range.

Import("env")

import gzip
import os

src = os.path.join(env.subst("$PROJECT_DIR"), "data", "portal.html")
dst = src + ".gz"

with open(src, "rb") as f:
    page = f.read()
# mtime=0 keeps the output byte-identical between builds of the same page.
packed = gzip.compress(page, compresslevel=9, mtime=0)

if not os.path.isfile(dst) or open(dst, "rb").read() != packed:
    with open(dst, "wb") as f:
        f.write(packed)
    print(f"portal.html: {len(page)} -> {len(packed)} bytes gzipped")
