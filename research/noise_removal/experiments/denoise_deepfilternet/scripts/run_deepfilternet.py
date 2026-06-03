#!/usr/bin/env python3

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path


def resolve_inputs(path):
    if path.is_dir():
        return sorted(path.glob("*.wav"))
    return [path]


def find_tool():
    for name in ("deep-filter", "deepFilter"):
        path = shutil.which(name)
        if path:
            return name
    local_bin = Path("research/noise_removal/.venv/bin")
    for name in ("deep-filter", "deepFilter"):
        path = local_bin / name
        if path.exists():
            return str(path)
    return None


def run_file(tool, input_path, output_dir, compensate_delay, post_filter):
    cmd = [tool]

    tool_name = Path(tool).name

    if tool_name == "deep-filter":
        if compensate_delay:
            cmd.append("--compensate-delay")
        if post_filter:
            cmd.append("--pf")
        cmd.extend(["--out-dir", str(output_dir), str(input_path)])
    else:
        if not compensate_delay:
            cmd.append("--no-delay-compensation")
        if post_filter:
            cmd.append("--pf")
        cmd.extend(["--output-dir", str(output_dir), str(input_path)])

    started = time.perf_counter()
    subprocess.run(cmd, check=True)
    elapsed = time.perf_counter() - started
    return elapsed


def parse_args():
    parser = argparse.ArgumentParser(description="Run DeepFilterNet denoise evaluation.")
    parser.add_argument("--input", required=True, type=Path, help="Input WAV or directory of WAV files.")
    parser.add_argument("--output", required=True, type=Path, help="Output directory.")
    parser.add_argument("--no-compensate-delay", action="store_true", help="Do not ask DeepFilterNet to compensate delay.")
    parser.add_argument("--post-filter", action="store_true", help="Enable DeepFilterNet post-filter.")
    return parser.parse_args()


def main():
    args = parse_args()
    inputs = resolve_inputs(args.input)
    if not inputs:
        print(f"No WAV files found in {args.input}", file=sys.stderr)
        return 2

    tool = find_tool()
    if tool is None:
        print("DeepFilterNet tool not found.", file=sys.stderr)
        print("Install `deep-filter` or run `pip install deepfilternet` for `deepFilter`.", file=sys.stderr)
        return 2

    args.output.mkdir(parents=True, exist_ok=True)

    print(f"Tool: {tool}")
    print(f"Files: {len(inputs)}")
    print(f"Output: {args.output}")

    for input_path in inputs:
        elapsed = run_file(
            tool=tool,
            input_path=input_path,
            output_dir=args.output,
            compensate_delay=not args.no_compensate_delay,
            post_filter=args.post_filter,
        )
        print(f"{input_path.name}: {elapsed:.2f}s")


if __name__ == "__main__":
    raise SystemExit(main())
