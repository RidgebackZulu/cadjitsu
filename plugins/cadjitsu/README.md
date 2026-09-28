# Cadjitsu plugin

Connects AI agents to the [Cadjitsu](https://github.com/RidgebackZulu/cadjitsu) CAD app's MCP server and adds the
`cadjitsu-cad` skill: how to design 3D-printable parts with Cadjitsu's tools.

This folder is both a Claude Code plugin (`.claude-plugin/plugin.json`, `.mcp.json`) and a portable Agent Plugin
for Hermes Agent (`plugin.json` and `skills/`). The skill is `skills/cadjitsu-cad/SKILL.md`; the full tool reference is
`skills/cadjitsu-cad/references/tools.md`.

First, in Cadjitsu: click the **MCP** button (top right), tick **Enable the MCP server**, click **Apply**, and copy
the token. The dialog also shows these steps with your token filled in.

> **Renamed from Cadly.** The MCP server is now `cadjitsu` and its variables are `CADJITSU_MCP_URL` /
> `CADJITSU_MCP_TOKEN` (they were `CADLY_*`). If your agent was set up for Cadly, rename them (the token
> itself is unchanged: Cadjitsu takes over Cadly's settings on first run) and reinstall the plugin as `cadjitsu`.

## Claude Code

1. `/plugin marketplace add RidgebackZulu/cadjitsu`, then `/plugin install cadjitsu@cadjitsu`.
2. Start Claude Code with the token in the environment:
   ```sh
   export CADJITSU_MCP_TOKEN=<token>
   export CADJITSU_MCP_URL=http://127.0.0.1:7823/mcp   # only if you changed the port
   claude
   ```

## Hermes Agent

Install this folder (not the whole repository, which is the app's source), then connect Cadjitsu's MCP server
while Cadjitsu is running with MCP switched on:

```sh
hermes plugins install RidgebackZulu/cadjitsu/plugins/cadjitsu --enable
hermes mcp add cadjitsu --url http://127.0.0.1:7823/mcp --auth header
```

The plugin brings the `cadjitsu-cad` skill. `hermes mcp add` asks for the Bearer token (paste the one from Cadjitsu),
keeps it in Hermes' secrets, lists Cadjitsu's tools and enables them. Use your port if you changed it; the
"Hermes Agent" snippet in Cadjitsu's MCP dialog has both lines with your port and token filled in.

If Hermes stops with "maximum number of tool-calling iterations", that is Hermes' own per-turn cap, not Cadjitsu:
`hermes config set agent.max_turns unlimited` lifts it.
