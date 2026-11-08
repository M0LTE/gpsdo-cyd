# PlatformIO pre-build script: gzip web/index.html into src/device/web_index_gen.h.
import gzip, os
Import("env")  # noqa: F821 (provided by PlatformIO)
root = env["PROJECT_DIR"]  # noqa: F821
src = os.path.join(root, "web", "index.html")
dst = os.path.join(root, "src", "device", "web_index_gen.h")
data = gzip.compress(open(src, "rb").read(), compresslevel=9, mtime=0)
lines = ["// Generated from web/index.html by tools/embed_web.py. Do not edit.", "#pragma once", "#include <cstddef>",
         "#include <cstdint>", f"static const size_t kWebIndexGzLen = {len(data)};", "static const uint8_t kWebIndexGz[] = {"]
for i in range(0, len(data), 20):
    lines.append("  " + ",".join(str(b) for b in data[i:i + 20]) + ",")
lines.append("};")
text = "\n".join(lines) + "\n"
if not os.path.exists(dst) or open(dst).read() != text:
    open(dst, "w").write(text)
