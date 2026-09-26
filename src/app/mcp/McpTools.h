#pragma once

#include "mcp/McpServer.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <string>

namespace cadly {

class MainWindow;

// The MCP tools: everything an agent can do with the open design. They go
// through the same document and view as the user (each edit is one undo
// step and shows in the timeline) and wait for the model before answering.
class McpTools : public McpToolProvider {
public:
    explicit McpTools(MainWindow &window);

    nlohmann::json toolList() const override;
    nlohmann::json callTool(const std::string &name, const nlohmann::json &args) override;

private:
    using json = nlohmann::json;
    struct Tool {
        std::string description;
        json schema;
        std::function<json(const json &)> run;
    };
    void define();
    void add(const std::string &name, const std::string &description, json properties, std::vector<std::string> required,
             std::function<json(const json &)> run);

    MainWindow &m_w;
    std::vector<std::string> m_order;
    std::map<std::string, Tool> m_tools;
};

} // namespace cadly
