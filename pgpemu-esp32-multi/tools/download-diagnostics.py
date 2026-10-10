"""Read only the persistent diagnostic partition, then decode its CRC-checked records."""

import argparse
import importlib.util
import os
from datetime import datetime
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

FLASH_OFFSET = 0x110000
FLASH_SIZE = 0x80000
RECORD_SIZE = 256
MAGIC = 0x50475044


def esptool_command():
    # ESP-IDF 4.4 bundles esptool as a script instead of installing a module.
    if os.environ.get("IDF_PATH"):
        bundled = Path(os.environ["IDF_PATH"]) / "components/esptool_py/esptool/esptool.py"
        if bundled.is_file():
            return [sys.executable, str(bundled)]
    if importlib.util.find_spec("esptool") is not None:
        return [sys.executable, "-m", "esptool"]
    raise SystemExit("ESP-IDF tools are not active. Run export.ps1 in this PowerShell session first.")


def decode_records(data):
    if len(data) != FLASH_SIZE:
        raise ValueError(f"Expected {FLASH_SIZE} diagnostic bytes, got {len(data)}")
    records = []
    invalid = 0
    for offset in range(0, len(data), RECORD_SIZE):
        raw = data[offset:offset + RECORD_SIZE]
        if raw == b"\xff" * RECORD_SIZE:
            continue
        magic, sequence, uptime = struct.unpack_from("<IIQ", raw)
        crc, = struct.unpack_from("<I", raw, 252)
        if magic != MAGIC or zlib.crc32(raw[:252]) != crc:
            invalid += 1
            continue
        message = raw[16:252].split(b"\0", 1)[0].decode("utf-8", errors="replace")
        records.append((sequence, uptime, message))
    records.sort(key=lambda record: record[0])
    return records, invalid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="ESP32 serial port, for example COM3")
    source.add_argument("--input", type=Path, help="Decode an existing diagnostics-only flash dump")
    parser.add_argument("--output", type=Path,
                        default=Path(datetime.now().strftime("diagnostics-%Y%m%d-%H%M%S.txt")))
    args = parser.parse_args()
    if args.output.exists():
        parser.error(f"Output already exists: {args.output}")
    if args.input:
        data = args.input.read_bytes()
    else:
        print("Close idf.py monitor first. This reads diagnostic flash and restarts the ESP32.")
        with tempfile.TemporaryDirectory(prefix="pgp-diagnostics-") as directory:
            dump = Path(directory) / "diagnostics.bin"
            subprocess.run(esptool_command() + ["--chip", "esp32",
                            "--port", args.port, "--baud", "115200", "read_flash",
                            hex(FLASH_OFFSET), hex(FLASH_SIZE), str(dump)], check=True)
            data = dump.read_bytes()
    records, invalid = decode_records(data)
    if not records:
        raise SystemExit("No valid diagnostic records. Check that the diagnostic firmware and partition table were flashed.")
    with args.output.open("x", encoding="utf-8") as output:
        output.write("PGP-Badge-Multi persistent diagnostics v1\n")
        output.write(f"Records={len(records)} invalid_or_interrupted={invalid}\n")
        output.write("Uptime restarts at each BOOT; sequence preserves order across reboots.\n\n")
        for sequence, uptime, message in records:
            output.write(f"{sequence:010d} +{uptime / 1000:012.3f}s {message}\n")
    print(f"Saved {len(records)} records: {args.output.resolve()}")


if __name__ == "__main__":
    main()
