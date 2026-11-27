#!/usr/bin/env python3
"""Compatibility entrypoint; complete importer: gen-nes-full-catalog.py."""
import argparse
import io
import runpy
import os

module = runpy.run_path(os.path.join(os.path.dirname(__file__), "gen-nes-full-catalog.py"))

def generate(data):
    return module["banked"](module["load"](data))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    with io.open(args.input, "rb") as src:
        content = generate(src.read())
    with io.open(args.output, "w", encoding="utf-8", newline="\n") as dst:
        dst.write(content)
