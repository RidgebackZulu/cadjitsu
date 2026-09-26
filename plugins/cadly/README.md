# Cadly plugin

Connects AI agents to the [Cadly](https://github.com/RidgebackZulu/cadly) CAD app's MCP server and adds the
`cadly-cad` skill: how to design 3D-printable parts with Cadly's tools.

This folder is both a Claude Code plugin (`.claude-plugin/plugin.json`, `.mcp.json`) and a portable Agent Plugin
for Hermes Agent (`plugin.json` and `skills/`). The skill is `skills/cadly-cad/SKILL.md`; the full tool reference is
`skills/cadly-cad/references/tools.md`.

First, in Cadly: click the **MCP** button (top right), tick **Enable the MCP server**, click **Apply**, and copy
the token. The dialog also shows these steps with your token filled in.

## Claude Code

1. `/plugin marketplace add RidgebackZulu/cadly`, then `/plugin install cadly@cadly`.
2. Start Claude Code with the token in the environment:
   ```sh
   export CADLY_MCP_TOKEN=<token>
   export CADLY_MCP_URL=http://127.0.0.1:7823/mcp   # only if you changed the port
   claude
   ```

## Hermes Agent

Install this folder (not the whole repository, which is the app's source), then connect Cadly's MCP server
while Cadly is running with MCP switched on:

```sh
hermes plugins install RidgebackZulu/cadly/plugins/cadly --enable
hermes mcp add cadly --url http://127.0.0.1:7823/mcp --auth header
```

The plugin brings the `cadly-cad` skill. `hermes mcp add` asks for the Bearer token (paste the one from Cadly),
keeps it in Hermes' secrets, lists Cadly's tools and enables them. Use your port if you changed it; the
"Hermes Agent" snippet in Cadly's MCP dialog has both lines with your port and token filled in.

If Hermes stops with "maximum number of tool-calling iterations", that is Hermes' own per-turn cap, not Cadly:
`hermes config set agent.max_turns unlimited` lifts it.
