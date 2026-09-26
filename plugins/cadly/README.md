# Cadly plugin

Connects Claude Code (and other agents) to the [Cadly](https://github.com/RidgebackZulu/cadly) CAD app's MCP
server and adds the `cadly-cad` skill: how to design 3D-printable parts with Cadly's tools.

## Claude Code

1. In Cadly: click the **MCP** button (top right), tick **Enable the MCP server**, click **Apply**, and copy the
   token.
2. `/plugin marketplace add RidgebackZulu/cadly`, then `/plugin install cadly@cadly`.
3. Start Claude Code with the token in the environment:
   ```sh
   export CADLY_MCP_TOKEN=<token>
   export CADLY_MCP_URL=http://127.0.0.1:7823/mcp   # only if you changed the port
   claude
   ```

## Hermes Agent

1. Copy the skill: `cp -r skills/cadly-cad ~/.hermes/skills/` (or clone this repository and link it).
2. Add the server to `~/.hermes/config.yaml`:
   ```yaml
   mcp_servers:
     cadly:
       url: "http://127.0.0.1:7823/mcp"
       headers:
         Authorization: "Bearer <token>"
   ```

The skill is `skills/cadly-cad/SKILL.md`; the full tool reference is `skills/cadly-cad/references/tools.md`.
