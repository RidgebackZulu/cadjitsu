#pragma once

#include <string>

namespace cad {

enum class Severity { Ok = 0, Warning = 1, Error = 2 };

// Outcome of computing one feature; shown on the timeline (yellow / red icons).
struct Status {
    Severity severity = Severity::Ok;
    std::string message;

    static Status ok() { return {}; }
    static Status warning(std::string msg) { return {Severity::Warning, std::move(msg)}; }
    static Status error(std::string msg) { return {Severity::Error, std::move(msg)}; }

    bool isOk() const { return severity == Severity::Ok; }
    bool isError() const { return severity == Severity::Error; }

    // Keeps the most severe status; messages of equal severity are joined.
    void merge(const Status &other) {
        if(other.severity > severity) {
            *this = other;
        } else if(other.severity == severity && other.severity != Severity::Ok &&
                  !other.message.empty() && message.find(other.message) == std::string::npos) {
            message += message.empty() ? other.message : "; " + other.message;
        }
    }
};

} // namespace cad
