#!/usr/bin/env python3
"""Checks the Cadly plugin for both clients:

- Claude Code: the marketplace and .claude-plugin/plugin.json, .mcp.json;
- Hermes Agent: plugins/cadly as a portable Agent Plugin (plugin.json, skills/), with the
  rules Hermes' hermes_cli/agent_plugins.py applies, and nothing in the folder that Hermes'
  install-time scanner (tools/skills_guard.py) rates high or critical (a community plugin
  with such findings is blocked)."""
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
    # Agent Skills / Hermes portable rules: metadata maps strings to strings.
    meta = re.search(r"^metadata:\n((?:  .*\n?)*)", front + "\n", re.M)
    if meta:
        for line in meta.group(1).splitlines():
            if not re.match(r'^  [\w.-]+: (?:"[^"]*"|[^\s\[{][^\[{]*)$', line):
                errors.append(f"SKILL.md metadata must map strings to strings: {line.strip()!r}")
if not os.path.exists(os.path.join(root, "plugins/cadly/skills/cadly-cad/references/tools.md")):
    errors.append("references/tools.md is missing")

# --- Hermes Agent: plugins/cadly as a portable Agent Plugin (Agent Plugins v1) ----------------
PLUGIN_SCHEMA = "https://agent-plugins.org/schemas/1.0.0/plugin.schema.json"
PLUGIN_FIELDS = {"$schema", "name", "version", "description", "author", "homepage", "repository", "license",
                 "keywords", "extensions"}
PLUGIN_NAME_RE = re.compile(r"^(?!.*(?:--|\.\.))[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?$")
SKILL_NAME_RE = re.compile(r"^(?!.*--)[a-z0-9]+(?:-[a-z0-9]+)*$")
plugin_dir = os.path.join(root, "plugins/cadly")
portable = load("plugins/cadly/plugin.json")
claude = load("plugins/cadly/.claude-plugin/plugin.json")
if set(portable) - PLUGIN_FIELDS:
    errors.append(f"plugin.json has fields Agent Plugins v1 does not allow: {sorted(set(portable) - PLUGIN_FIELDS)}")
if portable.get("$schema") != PLUGIN_SCHEMA:
    errors.append("plugin.json must declare the Agent Plugins v1 $schema")
name = portable.get("name", "")
if not isinstance(name, str) or not 1 <= len(name) <= 64 or not PLUGIN_NAME_RE.fullmatch(name):
    errors.append(f"plugin.json name {name!r} does not satisfy Agent Plugins v1")
for field in ("version", "description", "homepage", "repository", "license"):
    if field in portable and not isinstance(portable[field], str):
        errors.append(f"plugin.json {field} must be a string")
author = portable.get("author", {})
if not isinstance(author, dict) or set(author) - {"name", "email", "url"} or \
        not all(isinstance(v, str) for v in author.values()):
    errors.append("plugin.json author must be an object with string name / email / url")
if not isinstance(portable.get("keywords", []), list):
    errors.append("plugin.json keywords must be an array")
for field in ("name", "version", "description"):
    if portable.get(field) != claude.get(field):
        errors.append(f"plugin.json and .claude-plugin/plugin.json disagree on {field}")
# Hermes does not expand ${VAR} in a portable plugin's MCP servers, so the token cannot come
# from the environment there: the server is added with `hermes mcp add` instead.
if os.path.exists(os.path.join(plugin_dir, "mcp.json")):
    errors.append("plugins/cadly/mcp.json: Hermes would send its ${...} header literally; "
                  "document `hermes mcp add` instead")
skills_dir = os.path.join(plugin_dir, "skills")
for d in sorted(os.listdir(skills_dir)):
    md = os.path.join(skills_dir, d, "SKILL.md")
    if not os.path.isfile(md):
        continue
    fm = re.match(r"^---\n(.*?)\n---\s*\n", open(md).read(), re.S)
    n = re.search(r"^name: (\S+)$", fm.group(1), re.M) if fm else None
    if not n or n.group(1) != d or not SKILL_NAME_RE.fullmatch(d):
        errors.append(f"skills/{d}/SKILL.md: name must equal its folder and be lowercase-hyphenated")
# What Hermes' install scanner would rate high or critical (blocking a community plugin).
BLOCKING = [
    (r"\bsudo\b", "sudo"), (r"subprocess|os\.system|eval\(|exec\(", "code execution"),
    (r"(?:\$HOME|~)/\.hermes/\.env", "the Hermes secrets file"),
    (r"(?:\$HOME|~)/\.(?:ssh|aws|gnupg|kube|docker)", "a credential directory"),
    (r"printenv|\benv\s*\|", "dumping the environment"), (r"curl |wget ", "a download command"),
    (r"base64[^\n]*env", "encoded environment access"), (r"\\x[0-9a-f]{2}", "escaped bytes"),
]
for dirpath, _, files in os.walk(plugin_dir):
    for fname in files:
        path = os.path.join(dirpath, fname)
        rel = os.path.relpath(path, root)
        if not fname.endswith((".md", ".json")):
            errors.append(f"{rel}: only Markdown and JSON belong in the plugin folder")
            continue
        if os.access(path, os.X_OK):
            errors.append(f"{rel}: must not be executable")
        for lineno, text_line in enumerate(open(path, encoding="utf-8"), 1):
            for pattern, what in BLOCKING:
                if re.search(pattern, text_line):
                    errors.append(f"{rel}:{lineno}: mentions {what} (Hermes' plugin scanner flags it)")

for e in errors:
    print("ERROR:", e)
if errors:
    sys.exit(1)
print("marketplace, Claude and Hermes plugin, and skill look right")
