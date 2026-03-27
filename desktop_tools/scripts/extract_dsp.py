#!/usr/bin/env python3
import argparse
from pathlib import Path


START_MARKER = "#define SAMPLE_RATE"
END_MARKER = "// ============================================================================\n// Conversão PCM <-> float"


def extract_section(text: str) -> str:
    start = text.find(START_MARKER)
    if start < 0:
        raise RuntimeError(f"Start marker not found: {START_MARKER}")

    end = text.find(END_MARKER, start)
    if end < 0:
        raise RuntimeError(f"End marker not found: {END_MARKER}")

    return text[start:end].rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract desktop DSP core from Pico source")
    parser.add_argument("--input", required=True, help="Path to univibe_rp2350_dma.cpp")
    parser.add_argument("--output", required=True, help="Generated header output path")
    args = parser.parse_args()

    source_path = Path(args.input)
    output_path = Path(args.output)

    src = source_path.read_text(encoding="utf-8")
    body = extract_section(src)

    generated = (
        "// Auto-generated from univibe_rp2350_dma.cpp. Do not edit manually.\n"
        "#pragma once\n\n"
        "#include <cmath>\n"
        "#include <cstdlib>\n\n"
        f"{body}"
    )

    output_path.write_text(generated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())