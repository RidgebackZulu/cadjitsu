#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>

namespace cad {

// Physical dimension of a value as exponents of length and angle; e.g. an area
// is {2, 0}. Internal base units are millimetres and radians.
struct Dim {
    int length = 0;
    int angle = 0;

    bool operator==(const Dim &) const = default;
    bool isScalar() const { return length == 0 && angle == 0; }
    static Dim scalar() { return {0, 0}; }
    static Dim len() { return {1, 0}; }
    static Dim ang() { return {0, 1}; }
};

struct Quantity {
    double value = 0.0; // in base units (mm, rad)
    Dim dim;
};

// What a value field expects; a bare number is interpreted in the field's
// default unit (mm for lengths, degrees for angles), like Fusion 360.
enum class ValueKind { Length, Angle, Scalar };

struct EvalResult {
    bool ok = false;
    double value = 0.0; // base units
    std::string error;
};

using ParamLookup = std::function<std::optional<Quantity>(const std::string &name)>;

// Parses and evaluates `text`:
//   numbers with optional units (mm cm m um in " ft deg ° rad), + - * / ^,
//   parentheses, parameter names, pi, e, and the functions
//   sin cos tan asin acos atan atan2 sqrt abs min max floor ceil round.
// Trigonometric functions take angles; a bare number is treated as degrees.
EvalResult evaluate(const std::string &text, ValueKind kind, const ParamLookup &lookup = {});

// Evaluates without applying a field kind (dimension is returned as computed).
bool evaluateQuantity(const std::string &text, const ParamLookup &lookup, Quantity &out,
                      std::string &error);

// Collects the identifiers (parameter names) referenced by `text`, excluding
// functions and constants. Returns false if the text does not parse.
bool referencedNames(const std::string &text, std::set<std::string> &names);

// Formats a value for display in its field's unit ("12.5 mm", "30 deg").
std::string formatValue(double baseValue, ValueKind kind, int decimals = 2);

// True if `name` is a valid parameter identifier and not a reserved word.
bool isValidParamName(const std::string &name);

} // namespace cad
