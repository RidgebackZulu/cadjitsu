// Expressions with units and the parameter table.
#include <doctest.h>

#include "base/Vec2.h"
#include "expr/Expression.h"
#include "expr/ParamTable.h"

using namespace cad;

namespace {
double len(const std::string &s) {
    auto r = evaluate(s, ValueKind::Length);
    REQUIRE_MESSAGE(r.ok, s << ": " << r.error);
    return r.value;
}
double ang(const std::string &s) {
    auto r = evaluate(s, ValueKind::Angle);
    REQUIRE_MESSAGE(r.ok, s << ": " << r.error);
    return r.value;
}
bool fails(const std::string &s, ValueKind k = ValueKind::Length) { return !evaluate(s, k).ok; }
} // namespace

TEST_CASE("numbers and units") {
    CHECK(len("10") == doctest::Approx(10));
    CHECK(len("10 mm") == doctest::Approx(10));
    CHECK(len("2 cm") == doctest::Approx(20));
    CHECK(len("1 in") == doctest::Approx(25.4));
    CHECK(len("1\"") == doctest::Approx(25.4));
    CHECK(len("0.5 m") == doctest::Approx(500));
    CHECK(len(".5") == doctest::Approx(0.5));
    CHECK(len("1e2") == doctest::Approx(100));
    CHECK(ang("90") == doctest::Approx(kPi / 2));
    CHECK(ang("90 deg") == doctest::Approx(kPi / 2));
    CHECK(ang("90\xC2\xB0") == doctest::Approx(kPi / 2));
    CHECK(ang("1 rad") == doctest::Approx(1.0));
}

TEST_CASE("precedence and operators") {
    CHECK(len("1 + 2 * 3") == doctest::Approx(7));
    CHECK(len("(1 + 2) * 3") == doctest::Approx(9));
    CHECK(len("2 ^ 3 ^ 2") == doctest::Approx(512));
    CHECK(len("-2 ^ 2") == doctest::Approx(-4));
    CHECK(len("10 / 4") == doctest::Approx(2.5));
    CHECK(len("10 mm + 1 cm") == doctest::Approx(20));
    CHECK(len("(5 + 5) mm") == doctest::Approx(10));
    CHECK(len("10 + 5 mm") == doctest::Approx(15)); // bare number joins the length
    CHECK(len("2 * 3 mm") == doctest::Approx(6));
    CHECK(len("sqrt(9 mm * 16 mm)") == doctest::Approx(12));
}

TEST_CASE("functions") {
    CHECK(len("sin(30) * 10") == doctest::Approx(5)); // bare trig args are degrees
    CHECK(len("cos(60 deg) * 10") == doctest::Approx(5));
    CHECK(ang("atan(1)") == doctest::Approx(kPi / 4));
    CHECK(len("max(3, 7 mm, 5)") == doctest::Approx(7));
    CHECK(len("min(3 mm, 7)") == doctest::Approx(3));
    CHECK(len("abs(-4)") == doctest::Approx(4));
    CHECK(len("round(2.6)") == doctest::Approx(3));
    CHECK(len("floor(2.6 mm)") == doctest::Approx(2));
    CHECK(len("pi") == doctest::Approx(kPi));
}

TEST_CASE("errors") {
    CHECK(fails(""));
    CHECK(fails("1 +"));
    CHECK(fails("(1 + 2"));
    CHECK(fails("1 / 0"));
    CHECK(fails("foo"));
    CHECK(fails("10 mm", ValueKind::Angle));
    CHECK(fails("10 deg", ValueKind::Length));
    CHECK(fails("10 mm * 10 mm"));  // area is not a length
    CHECK(fails("10 mm + 5 deg"));
    CHECK(fails("sqrt(-1)"));
    CHECK(fails("nosuch(1)"));
    CHECK(fails("mm"));
    CHECK(fails("1 $ 2"));
}

TEST_CASE("formatting") {
    CHECK(formatValue(12.5, ValueKind::Length) == "12.5 mm");
    CHECK(formatValue(10.0, ValueKind::Length) == "10 mm");
    CHECK(formatValue(kPi / 4, ValueKind::Angle) == "45 deg");
    CHECK(formatValue(-0.0001, ValueKind::Length) == "0 mm");
}

TEST_CASE("parameter table: references, kinds, cycles") {
    ParamTable t;
    t.add({"d1", "20 mm", ValueKind::Length, 1, "width"});
    t.add({"d2", "d1 * 2", ValueKind::Length, 2, "length"});
    t.add({"d3", "d2 / 4 + 1", ValueKind::Length, 3, ""});
    t.add({"d4", "30", ValueKind::Angle, 3, ""});
    t.add({"d5", "d4 * 2", ValueKind::Angle, 3, ""});
    t.add({"d6", "d7 + 1", ValueKind::Length, 4, ""});
    t.add({"d7", "d6 + 1", ValueKind::Length, 4, ""});
    t.add({"d8", "d99", ValueKind::Length, 4, ""});
    t.add({"d9", "d8 + 1", ValueKind::Length, 4, ""});
    t.add({"d10", "d10", ValueKind::Length, 4, ""});
    t.evaluateAll();
    CHECK(t.find("d2")->value == doctest::Approx(40));
    CHECK(t.find("d3")->value == doctest::Approx(11));
    CHECK(t.find("d5")->value == doctest::Approx(kPi / 3));
    CHECK_FALSE(t.find("d6")->ok);
    CHECK_FALSE(t.find("d7")->ok);
    CHECK_FALSE(t.find("d8")->ok);
    CHECK_FALSE(t.find("d9")->ok);
    CHECK_FALSE(t.find("d10")->ok);
    CHECK(t.find("d8")->error.find("unknown") != std::string::npos);
    CHECK(t.evaluateExpression("d1 + d3", ValueKind::Length).value == doctest::Approx(31));
    CHECK(t.nextFreeName() == "d11");
}

TEST_CASE("parameter names") {
    CHECK(isValidParamName("width"));
    CHECK(isValidParamName("d12"));
    CHECK(isValidParamName("_x"));
    CHECK_FALSE(isValidParamName("12d"));
    CHECK_FALSE(isValidParamName("sin"));
    CHECK_FALSE(isValidParamName("mm"));
    CHECK_FALSE(isValidParamName("pi"));
    CHECK_FALSE(isValidParamName("a-b"));
}
