"""Patch TinyGSM's numeric stream parser for large Quectel QIRD counters.

TinyGSM 0.12.0 returns int16_t from streamGetIntBefore().  EC200U can report
more than 32767 queued bytes during a firmware download, so valid counters
wrap negative and are later converted to enormous size_t values.  PlatformIO
recreates .pio/libdeps, therefore apply the compatibility fix on every build.
"""

from pathlib import Path

Import("env")

libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR"))
header = libdeps_dir / env.subst("$PIOENV") / "TinyGSM" / "src" / "TinyGsmModem.tpp"
tcp_header = libdeps_dir / env.subst("$PIOENV") / "TinyGSM" / "src" / "TinyGsmTCP.tpp"

if header.exists():
    source = header.read_text(encoding="utf-8")
    original = source
    old = """  inline int16_t streamGetIntBefore(char lastChar) {
    char   buf[7];
    size_t bytesRead = thisModem().stream.readBytesUntil(
        lastChar, buf, static_cast<size_t>(7));
    // if we read 7 or more bytes, it's an overflow
    if (bytesRead && bytesRead < 7) {"""
    new = """  inline int32_t streamGetIntBefore(char lastChar) {
    char   buf[12];
    size_t bytesRead = thisModem().stream.readBytesUntil(
        lastChar, buf, static_cast<size_t>(12));
    // Signed 32-bit decimal plus sign fits; a full buffer is an overflow.
    if (bytesRead && bytesRead < 12) {"""
    if old in source:
        source = source.replace(old, new, 1)
    elif "inline int32_t streamGetIntBefore(char lastChar)" not in source:
        raise RuntimeError("Unsupported TinyGSM streamGetIntBefore implementation")

    # The upstream function also stores atoi() in an int16_t local.  Merely
    # widening the return type still wraps values above 32767.
    source = source.replace("      int16_t res    = atoi(buf);\n      return res;",
                            "      int32_t res    = atol(buf);\n      return res;", 1)
    if "int32_t res    = atol(buf);" not in source:
        raise RuntimeError("Failed to widen TinyGSM numeric parser local")

    if source != original:
        header.write_text(source, encoding="utf-8")
        print("Patched TinyGSM streamGetIntBefore for 32-bit QIRD counters")

if tcp_header.exists():
    source = tcp_header.read_text(encoding="utf-8")
    original = source
    # Quectel modems can buffer more than 65535 bytes.  At exactly 65536,
    # TinyGSM 0.12.0 wraps this field to zero and available() never reads.
    source = source.replace("    uint16_t   sock_available;",
                            "    uint32_t   sock_available;", 1)
    source = source.replace("TinyGsmMin((uint16_t)rx.free(), sock_available)",
                            "TinyGsmMin((uint32_t)rx.free(), sock_available)")
    if "uint32_t   sock_available;" not in source:
        raise RuntimeError("Failed to widen TinyGSM socket availability counter")
    if source != original:
        tcp_header.write_text(source, encoding="utf-8")
        print("Patched TinyGSM sock_available for Quectel buffers over 64 KiB")
