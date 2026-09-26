#!/usr/bin/env python3
"""Checks the Claude plugin marketplace and the Cadly skill: JSON files parse and
name what they should, and SKILL.md has the frontmatter Claude and Hermes need."""
import json
import os
import re
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
errors = []


def load(rel):
    try:
        with open(os.path.join(root, rel)) as f:
            return json.load(f)
    except Exception as e:  # noqa: BLE001
        errors.append(f"{rel}: {e}")
        return {}


market = load(".claude-plugin/marketplace.json")
for p in market.get("plugins", []):
    src = os.path.join(root, p.get("source", ""))
    if not os.path.isdir(src):
        errors.append(f"marketplace plugin {p.get('name')}: {p.get('source')} is not a folder")
    plugin = load(os.path.join(p.get("source", ""), ".claude-plugin/plugin.json"))
    if plugin.get("name") != p.get("name"):
        errors.append(f"plugin.json name {plugin.get('name')!r} != marketplace entry {p.get('name')!r}")
    mcp = load(os.path.join(p.get("source", ""), ".mcp.json"))
    if "cadly" not in mcp.get("mcpServers", {}):
        errors.append(".mcp.json does not declare the cadly server")

skill = os.path.join(root, "plugins/cadly/skills/cadly-cad/SKILL.md")
text = open(skill).read()
m = re.match(r"^---\n(.*?)\n---\n", text, re.S)
if not m:
    errors.append("SKILL.md has no YAML frontmatter")
else:
    front = m.group(1)
    for key in ("name", "description", "version", "license"):
        if not re.search(rf"^{key}: \S", front, re.M):
            errors.append(f"SKILL.md frontmatter lacks {key}")
    name = re.search(r"^name: (\S+)", front, re.M)
    if name and name.group(1) != "cadly-cad":
        errors.append("SKILL.md name must be cadly-cad (its folder name)")
    desc = re.search(r"^description: (.+)$", front, re.M)
    if desc and len(desc.group(1)) > 1024:
        errors.append("SKILL.md description is longer than 1024 characters")
    if "hermes:" not in front:
        errors.append("SKILL.md frontmatter lacks metadata.hermes")
if not os.path.exists(os.path.join(root, "plugins/cadly/skills/cadly-cad/references/tools.md")):
    errors.append("references/tools.md is missing")

for e in errors:
    print("ERROR:", e)
if errors:
    sys.exit(1)
print("marketplace, plugin and skill look right")
