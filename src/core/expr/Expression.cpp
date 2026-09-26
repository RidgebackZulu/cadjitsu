#include "expr/Expression.h"

#include "base/Vec2.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace cad {

namespace {

constexpr double kDeg = kPi / 180.0;

struct UnitDef {
    const char *name;
    double factor;
    Dim dim;
};

const UnitDef kUnits[] = {
    {"mm", 1.0, Dim::len()},    {"cm", 10.0, Dim::len()},    {"m", 1000.0, Dim::len()},
    {"um", 0.001, Dim::len()},  {"in", 25.4, Dim::len()},    {"inch", 25.4, Dim::len()},
    {"ft", 304.8, Dim::len()},  {"\"", 25.4, Dim::len()},    {"deg", kDeg, Dim::ang()},
    {"\xC2\xB0", kDeg, Dim::ang()}, {"rad", 1.0, Dim::ang()},
};

const UnitDef *findUnit(const std::string &name) {
    for(const auto &u : kUnits)
        if(name == u.name) return &u;
    return nullptr;
}

const char *const kFunctions[] = {"sin",  "cos", "tan", "asin",  "acos", "atan", "atan2", "sqrt",
                                  "abs",  "min", "max", "floor", "ceil", "round"};

bool isFunction(const std::string &name) {
    for(const char *f : kFunctions)
        if(name == f) return true;
    return false;
}

bool isConstant(const std::string &name) { return name == "pi" || name == "e"; }

// ---------------------------------------------------------------------------
// Tokenizer

enum class Tok { Number, Ident, Unit, Op, LParen, RParen, Comma, End };

struct Token {
    Tok type;
    std::string text;
    double number = 0.0;
    size_t pos = 0;
};

bool tokenize(const std::string &s, std::vector<Token> &out, std::string &error) {
    size_t i = 0;
    while(i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if(std::isspace(c)) {
            ++i;
            continue;
        }
        if(std::isdigit(c) || (c == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))) {
            const char *begin = s.c_str() + i;
            char *end = nullptr;
            const double v = std::strtod(begin, &end);
            if(end == begin) {
                error = "invalid number";
                return false;
            }
            out.push_back({Tok::Number, std::string(begin, static_cast<const char *>(end)), v, i});
            i += size_t(end - begin);
            continue;
        }
        if(std::isalpha(c) || c == '_') {
            size_t j = i + 1;
            while(j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) ++j;
            out.push_back({Tok::Ident, s.substr(i, j - i), 0.0, i});
            i = j;
            continue;
        }
        if(c == '"') {
            out.push_back({Tok::Unit, "\"", 0.0, i});
            ++i;
            continue;
        }
        if(c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xB0) {
            out.push_back({Tok::Unit, "\xC2\xB0", 0.0, i});
            i += 2;
            continue;
        }
        switch(c) {
        case '+':
        case '-':
        case '*':
        case '/':
        case '^':
            out.push_back({Tok::Op, std::string(1, char(c)), 0.0, i});
            break;
        case '(':
            out.push_back({Tok::LParen, "(", 0.0, i});
            break;
        case ')':
            out.push_back({Tok::RParen, ")", 0.0, i});
            break;
        case ',':
            out.push_back({Tok::Comma, ",", 0.0, i});
            break;
        default:
            error = std::string("unexpected character '") + char(c) + "'";
            return false;
        }
        ++i;
    }
    out.push_back({Tok::End, "", 0.0, s.size()});
    return true;
}

// ---------------------------------------------------------------------------
// AST

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    enum Kind { Number, Ident, Neg, Add, Sub, Mul, Div, Pow, Call, Unit } kind;
    double number = 0.0;
    std::string name; // identifier / function name
    const UnitDef *unit = nullptr;
    std::vector<NodePtr> kids;
};

NodePtr makeNode(Node::Kind k) {
    auto n = std::make_unique<Node>();
    n->kind = k;
    return n;
}

class Parser {
public:
    explicit Parser(const std::vector<Token> &t) : toks(t) {}

    NodePtr parse(std::string &error) {
        NodePtr n = expr();
        if(!err.empty()) {
            error = err;
            return nullptr;
        }
        if(peek().type != Tok::End) {
            error = "unexpected '" + peek().text + "'";
            return nullptr;
        }
        return n;
    }

private:
    const std::vector<Token> &toks;
    size_t pos = 0;
    std::string err;

    const Token &peek() const { return toks[pos]; }
    const Token &next() { return toks[pos++]; }
    bool failed() const { return !err.empty(); }
    NodePtr fail(const std::string &msg) {
        if(err.empty()) err = msg;
        return nullptr;
    }

    NodePtr expr() {
        NodePtr lhs = term();
        while(!failed() && peek().type == Tok::Op && (peek().text == "+" || peek().text == "-")) {
            const bool add = next().text == "+";
            NodePtr rhs = term();
            if(failed()) return nullptr;
            NodePtr n = makeNode(add ? Node::Add : Node::Sub);
            n->kids.push_back(std::move(lhs));
            n->kids.push_back(std::move(rhs));
            lhs = std::move(n);
        }
        return lhs;
    }

    NodePtr term() {
        NodePtr lhs = unary();
        while(!failed() && peek().type == Tok::Op && (peek().text == "*" || peek().text == "/")) {
            const bool mul = next().text == "*";
            NodePtr rhs = unary();
            if(failed()) return nullptr;
            NodePtr n = makeNode(mul ? Node::Mul : Node::Div);
            n->kids.push_back(std::move(lhs));
            n->kids.push_back(std::move(rhs));
            lhs = std::move(n);
        }
        return lhs;
    }

    NodePtr unary() {
        if(peek().type == Tok::Op && (peek().text == "-" || peek().text == "+")) {
            const bool neg = next().text == "-";
            NodePtr operand = unary();
            if(failed()) return nullptr;
            if(!neg) return operand;
            NodePtr n = makeNode(Node::Neg);
            n->kids.push_back(std::move(operand));
            return n;
        }
        return power();
    }

    NodePtr power() {
        NodePtr base = postfix();
        if(failed()) return nullptr;
        if(peek().type == Tok::Op && peek().text == "^") {
            next();
            NodePtr exponent = unary(); // right associative
            if(failed()) return nullptr;
            NodePtr n = makeNode(Node::Pow);
            n->kids.push_back(std::move(base));
            n->kids.push_back(std::move(exponent));
            return n;
        }
        return base;
    }

    NodePtr postfix() {
        NodePtr n = primary();
        if(failed()) return nullptr;
        // A unit may follow a number or a parenthesized expression.
        const Token &t = peek();
        if(t.type == Tok::Unit || (t.type == Tok::Ident && findUnit(t.text))) {
            const UnitDef *u = findUnit(next().text);
            NodePtr un = makeNode(Node::Unit);
            un->unit = u;
            un->kids.push_back(std::move(n));
            return un;
        }
        return n;
    }

    NodePtr primary() {
        const Token &t = next();
        switch(t.type) {
        case Tok::Number: {
            NodePtr n = makeNode(Node::Number);
            n->number = t.number;
            return n;
        }
        case Tok::Ident: {
            if(peek().type == Tok::LParen) {
                if(!isFunction(t.text)) return fail("unknown function '" + t.text + "'");
                next();
                NodePtr call = makeNode(Node::Call);
                call->name = t.text;
                if(peek().type != Tok::RParen) {
                    for(;;) {
                        NodePtr arg = expr();
                        if(failed()) return nullptr;
                        call->kids.push_back(std::move(arg));
                        if(peek().type == Tok::Comma) {
                            next();
                            continue;
                        }
                        break;
                    }
                }
                if(next().type != Tok::RParen) return fail("missing ')'");
                return call;
            }
            if(isFunction(t.text)) return fail("function '" + t.text + "' needs arguments");
            if(findUnit(t.text)) return fail("unit '" + t.text + "' needs a number");
            NodePtr n = makeNode(Node::Ident);
            n->name = t.text;
            return n;
        }
        case Tok::LParen: {
            NodePtr n = expr();
            if(failed()) return nullptr;
            if(next().type != Tok::RParen) return fail("missing ')'");
            return n;
        }
        case Tok::End:
            return fail("unexpected end of expression");
        default:
            return fail("unexpected '" + t.text + "'");
        }
    }
};

NodePtr parseText(const std::string &text, std::string &error) {
    std::vector<Token> toks;
    if(!tokenize(text, toks, error)) return nullptr;
    if(toks.size() == 1) {
        error = "empty expression";
        return nullptr;
    }
    Parser p(toks);
    return p.parse(error);
}

// ---------------------------------------------------------------------------
// Evaluation

std::string dimName(Dim d) {
    if(d.isScalar()) return "a plain number";
    if(d == Dim::len()) return "a length";
    if(d == Dim::ang()) return "an angle";
    if(d == Dim{2, 0}) return "an area";
    if(d == Dim{3, 0}) return "a volume";
    return "a value with mixed units";
}

// When a plain number meets a dimensioned value in + - min max, the number is
// read in that dimension's default unit (mm or degrees).
bool coerceScalar(Quantity &a, const Quantity &b) {
    if(!a.dim.isScalar() || b.dim.isScalar()) return false;
    if(b.dim == Dim::len()) {
        a.dim = Dim::len();
        return true;
    }
    if(b.dim == Dim::ang()) {
        a.value *= kDeg;
        a.dim = Dim::ang();
        return true;
    }
    return false;
}

bool unify(Quantity &a, Quantity &b, const char *op, std::string &error) {
    if(a.dim == b.dim) return true;
    if(coerceScalar(a, b) || coerceScalar(b, a)) return true;
    error = std::string("cannot ") + op + " " + dimName(a.dim) + " and " + dimName(b.dim);
    return false;
}

class Evaluator {
public:
    Evaluator(const ParamLookup &l) : lookup(l) {}
    std::string error;

    bool eval(const Node &n, Quantity &out) {
        switch(n.kind) {
        case Node::Number:
            out = {n.number, Dim::scalar()};
            return true;
        case Node::Ident: {
            if(n.name == "pi") {
                out = {kPi, Dim::scalar()};
                return true;
            }
            if(n.name == "e") {
                out = {std::exp(1.0), Dim::scalar()};
                return true;
            }
            std::optional<Quantity> q;
            if(lookup) q = lookup(n.name);
            if(!q) {
                error = "unknown parameter '" + n.name + "'";
                return false;
            }
            out = *q;
            return true;
        }
        case Node::Unit: {
            Quantity v;
            if(!eval(*n.kids[0], v)) return false;
            if(!v.dim.isScalar()) {
                error = std::string("unit '") + n.unit->name + "' applied to " + dimName(v.dim);
                return false;
            }
            out = {v.value * n.unit->factor, n.unit->dim};
            return true;
        }
        case Node::Neg:
            if(!eval(*n.kids[0], out)) return false;
            out.value = -out.value;
            return true;
        case Node::Add:
        case Node::Sub: {
            Quantity a, b;
            if(!eval(*n.kids[0], a) || !eval(*n.kids[1], b)) return false;
            if(!unify(a, b, n.kind == Node::Add ? "add" : "subtract", error)) return false;
            out = {n.kind == Node::Add ? a.value + b.value : a.value - b.value, a.dim};
            return true;
        }
        case Node::Mul:
        case Node::Div: {
            Quantity a, b;
            if(!eval(*n.kids[0], a) || !eval(*n.kids[1], b)) return false;
            if(n.kind == Node::Mul) {
                out = {a.value * b.value, {a.dim.length + b.dim.length, a.dim.angle + b.dim.angle}};
            } else {
                if(b.value == 0.0) {
                    error = "division by zero";
                    return false;
                }
                out = {a.value / b.value, {a.dim.length - b.dim.length, a.dim.angle - b.dim.angle}};
            }
            return true;
        }
        case Node::Pow: {
            Quantity a, b;
            if(!eval(*n.kids[0], a) || !eval(*n.kids[1], b)) return false;
            if(!b.dim.isScalar()) {
                error = "exponent must be a plain number";
                return false;
            }
            if(!a.dim.isScalar()) {
                const double r = std::round(b.value);
                if(std::fabs(r - b.value) > 1e-12) {
                    error = "units can only be raised to whole powers";
                    return false;
                }
                out = {std::pow(a.value, b.value),
                       {a.dim.length * int(r), a.dim.angle * int(r)}};
                return true;
            }
            out = {std::pow(a.value, b.value), Dim::scalar()};
            return true;
        }
        case Node::Call:
            return call(n, out);
        }
        return false;
    }

private:
    const ParamLookup &lookup;

    bool argCount(const Node &n, size_t count) {
        if(n.kids.size() != count) {
            error = n.name + "() takes " + std::to_string(count) + " argument" +
                    (count == 1 ? "" : "s");
            return false;
        }
        return true;
    }

    bool angleArg(const Node &n, double &radians) {
        Quantity a;
        if(!eval(*n.kids[0], a)) return false;
        if(a.dim == Dim::ang()) radians = a.value;
        else if(a.dim.isScalar()) radians = a.value * kDeg;
        else {
            error = n.name + "() needs an angle";
            return false;
        }
        return true;
    }

    bool call(const Node &n, Quantity &out) {
        const std::string &f = n.name;
        if(f == "sin" || f == "cos" || f == "tan") {
            double r;
            if(!argCount(n, 1) || !angleArg(n, r)) return false;
            out = {f == "sin" ? std::sin(r) : f == "cos" ? std::cos(r) : std::tan(r), Dim::scalar()};
            return true;
        }
        if(f == "asin" || f == "acos" || f == "atan") {
            if(!argCount(n, 1)) return false;
            Quantity a;
            if(!eval(*n.kids[0], a)) return false;
            if(!a.dim.isScalar()) {
                error = f + "() needs a plain number";
                return false;
            }
            if((f == "asin" || f == "acos") && std::fabs(a.value) > 1.0) {
                error = f + "() argument out of range";
                return false;
            }
            out = {f == "asin" ? std::asin(a.value) : f == "acos" ? std::acos(a.value) : std::atan(a.value),
                   Dim::ang()};
            return true;
        }
        if(f == "atan2") {
            if(!argCount(n, 2)) return false;
            Quantity y, x;
            if(!eval(*n.kids[0], y) || !eval(*n.kids[1], x)) return false;
            if(!unify(y, x, "compare", error)) return false;
            out = {std::atan2(y.value, x.value), Dim::ang()};
            return true;
        }
        if(f == "sqrt") {
            if(!argCount(n, 1)) return false;
            Quantity a;
            if(!eval(*n.kids[0], a)) return false;
            if(a.value < 0) {
                error = "sqrt() of a negative value";
                return false;
            }
            if(a.dim.length % 2 != 0 || a.dim.angle % 2 != 0) {
                error = "sqrt() of " + dimName(a.dim);
                return false;
            }
            out = {std::sqrt(a.value), {a.dim.length / 2, a.dim.angle / 2}};
            return true;
        }
        if(f == "abs" || f == "floor" || f == "ceil" || f == "round") {
            if(!argCount(n, 1)) return false;
            Quantity a;
            if(!eval(*n.kids[0], a)) return false;
            // Rounding works in display units (mm, degrees).
            const double scale = a.dim == Dim::ang() ? kDeg : 1.0;
            double v = a.value / scale;
            if(f == "abs") v = std::fabs(v);
            else if(f == "floor") v = std::floor(v);
            else if(f == "ceil") v = std::ceil(v);
            else v = std::round(v);
            out = {v * scale, a.dim};
            return true;
        }
        if(f == "min" || f == "max") {
            if(n.kids.empty()) {
                error = f + "() needs arguments";
                return false;
            }
            Quantity acc;
            if(!eval(*n.kids[0], acc)) return false;
            for(size_t i = 1; i < n.kids.size(); ++i) {
                Quantity b;
                if(!eval(*n.kids[i], b)) return false;
                if(!unify(acc, b, "compare", error)) return false;
                acc.value = f == "min" ? std::min(acc.value, b.value) : std::max(acc.value, b.value);
            }
            out = acc;
            return true;
        }
        error = "unknown function '" + f + "'";
        return false;
    }
};

void collectNames(const Node &n, std::set<std::string> &names) {
    if(n.kind == Node::Ident && !isConstant(n.name)) names.insert(n.name);
    for(const auto &k : n.kids) collectNames(*k, names);
}

} // namespace

bool evaluateQuantity(const std::string &text, const ParamLookup &lookup, Quantity &out,
                      std::string &error) {
    NodePtr ast = parseText(text, error);
    if(!ast) return false;
    Evaluator ev(lookup);
    if(!ev.eval(*ast, out)) {
        error = ev.error;
        return false;
    }
    if(!std::isfinite(out.value)) {
        error = "result is not a finite number";
        return false;
    }
    return true;
}

EvalResult evaluate(const std::string &text, ValueKind kind, const ParamLookup &lookup) {
    EvalResult r;
    Quantity q;
    if(!evaluateQuantity(text, lookup, q, r.error)) return r;
    switch(kind) {
    case ValueKind::Length:
        if(q.dim.isScalar() || q.dim == Dim::len()) {
            r.value = q.value;
            r.ok = true;
        } else {
            r.error = "expected a length, got " + dimName(q.dim);
        }
        break;
    case ValueKind::Angle:
        if(q.dim == Dim::ang()) {
            r.value = q.value;
            r.ok = true;
        } else if(q.dim.isScalar()) {
            r.value = q.value * kDeg;
            r.ok = true;
        } else {
            r.error = "expected an angle, got " + dimName(q.dim);
        }
        break;
    case ValueKind::Scalar:
        if(q.dim.isScalar()) {
            r.value = q.value;
            r.ok = true;
        } else {
            r.error = "expected a plain number, got " + dimName(q.dim);
        }
        break;
    }
    return r;
}

bool referencedNames(const std::string &text, std::set<std::string> &names) {
    std::string error;
    NodePtr ast = parseText(text, error);
    if(!ast) return false;
    collectNames(*ast, names);
    return true;
}

std::string formatValue(double baseValue, ValueKind kind, int decimals) {
    double v = baseValue;
    const char *unit = "";
    if(kind == ValueKind::Length) unit = " mm";
    else if(kind == ValueKind::Angle) {
        v = baseValue / kDeg;
        unit = " deg";
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s(buf);
    if(s.find('.') != std::string::npos) {
        while(!s.empty() && s.back() == '0') s.pop_back();
        if(!s.empty() && s.back() == '.') s.pop_back();
    }
    if(s == "-0") s = "0";
    return s + unit;
}

bool isValidParamName(const std::string &name) {
    if(name.empty()) return false;
    if(!(std::isalpha((unsigned char)name[0]) || name[0] == '_')) return false;
    for(char c : name)
        if(!(std::isalnum((unsigned char)c) || c == '_')) return false;
    return !isFunction(name) && !isConstant(name) && !findUnit(name);
}

} // namespace cad
