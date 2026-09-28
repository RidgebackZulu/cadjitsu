#!/usr/bin/env python3
"""Writes the Cadjitsu skill's tool reference from the app's own tool list.

    Cadjitsu --mcp-tools > tools.json
    scripts/gen_mcp_tool_docs.py tools.json plugins/cadjitsu/skills/cadjitsu-cad/references/tools.md

With --check it fails (exit 1) if the file is out of date instead of writing it.
"""
import json
import sys


def type_of(schema):
    if "enum" in schema:
        return " / ".join(f"`{v}`" for v in schema["enum"])
    if "anyOf" in schema:
        return " or ".join(t.get("type", "?") for t in schema["anyOf"])
    t = schema.get("type", "any")
    if t == "array":
        item = schema.get("items", {})
        if item.get("type") == "number" and "minItems" in schema:
            return f"[{', '.join(['x', 'y', 'z'][:schema['minItems']])}]"
        return f"array of {type_of(item) if item.get('type') != 'object' else 'objects'}"
    return t


def render(tools):
    out = [
        "# Cadjitsu MCP tools",
        "",
        "Generated from the app (`Cadjitsu --mcp-tools`); do not edit by hand.",
        "Lengths are millimetres, angles degrees. Numeric arguments also take expressions such as `\"d1 * 2\"` or",
        "`\"0.5 in\"`. Every tool that changes the design is one undo step and shows in Cadjitsu's timeline.",
        "",
    ]
    for t in tools:
        out.append(f"## `{t['name']}`")
        out.append("")
        out.append(t["description"])
        out.append("")
        props = t["inputSchema"].get("properties", {})
        required = set(t["inputSchema"].get("required", []))
        if props:
            out.append("| Argument | Type | Description |")
            out.append("|---|---|---|")
            for name, schema in props.items():
                desc = schema.get("description", "").replace("|", "\\|").replace("\n", " ")
                req = " **(required)**" if name in required else ""
                out.append(f"| `{name}`{req} | {type_of(schema)} | {desc} |")
        else:
            out.append("No arguments.")
        out.append("")
    return "\n".join(out)


def main():
    args = [a for a in sys.argv[1:] if a != "--check"]
    if len(args) != 2:
        sys.exit(__doc__)
    with open(args[0]) as f:
        text = render(json.load(f))
    if "--check" in sys.argv:
        with open(args[1]) as f:
            if f.read() != text:
                sys.exit(f"{args[1]} is out of date: run scripts/gen_mcp_tool_docs.py")
        print(f"{args[1]} is up to date")
        return
    with open(args[1], "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
