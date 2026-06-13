#include "value.h"

#include <cmath>
#include <iomanip>
#include <sstream>

// Prints whole numbers without a decimal point or scientific notation
// (so 3.0 -> "3", 4999950000 -> "4999950000"), and other numbers with up to
// 15 significant digits (so 3.5 -> "3.5", 3.14 -> "3.14").
static std::string formatNumber(double d) {
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
        return std::to_string(static_cast<long long>(d));
    }
    std::ostringstream ss;
    ss << std::setprecision(15) << d;
    return ss.str();
}

// Like valueToString but quotes strings, used when printing inside a container.
static std::string valueRepr(const Value& v) {
    if (std::holds_alternative<std::string>(v))
        return "\"" + std::get<std::string>(v) + "\"";
    return valueToString(v);
}

std::string valueToString(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return "nil";
    if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
    if (std::holds_alternative<double>(v)) return formatNumber(std::get<double>(v));
    if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
    if (std::holds_alternative<FunctionObj*>(v)) {
        const FunctionObj* fn = std::get<FunctionObj*>(v);
        if (!fn || fn->name.empty()) return "<script>";
        return "<fn " + fn->name + ">";
    }
    if (std::holds_alternative<ClosureObj*>(v)) {
        const ClosureObj* cl = std::get<ClosureObj*>(v);
        if (!cl || !cl->function || cl->function->name.empty()) return "<fn>";
        return "<fn " + cl->function->name + ">";
    }
    if (std::holds_alternative<NativeObj*>(v)) {
        const NativeObj* nf = std::get<NativeObj*>(v);
        return "<native fn " + (nf ? nf->name : "") + ">";
    }
    if (std::holds_alternative<ArrayObj*>(v)) {
        const ArrayObj* arr = std::get<ArrayObj*>(v);
        std::string out = "[";
        for (size_t i = 0; i < arr->elements.size(); i++) {
            out += valueRepr(arr->elements[i]);
            if (i + 1 < arr->elements.size()) out += ", ";
        }
        return out + "]";
    }
    if (std::holds_alternative<MapObj*>(v)) {
        const MapObj* map = std::get<MapObj*>(v);
        std::string out = "{";
        bool first = true;
        for (const auto& [key, val] : map->entries) {
            if (!first) out += ", ";
            first = false;
            out += "\"" + key + "\": " + valueRepr(val);
        }
        return out + "}";
    }
    return "<unknown>";
}

bool valuesEqual(const Value& a, const Value& b) {
    if (a.index() != b.index()) return false;  // different types are never equal
    if (std::holds_alternative<std::monostate>(a)) return true;
    if (std::holds_alternative<bool>(a)) return std::get<bool>(a) == std::get<bool>(b);
    if (std::holds_alternative<double>(a)) return std::get<double>(a) == std::get<double>(b);
    if (std::holds_alternative<std::string>(a)) return std::get<std::string>(a) == std::get<std::string>(b);
    if (std::holds_alternative<FunctionObj*>(a)) return std::get<FunctionObj*>(a) == std::get<FunctionObj*>(b);
    if (std::holds_alternative<ClosureObj*>(a)) return std::get<ClosureObj*>(a) == std::get<ClosureObj*>(b);
    if (std::holds_alternative<NativeObj*>(a)) return std::get<NativeObj*>(a) == std::get<NativeObj*>(b);
    if (std::holds_alternative<ArrayObj*>(a)) return std::get<ArrayObj*>(a) == std::get<ArrayObj*>(b);  // identity
    if (std::holds_alternative<MapObj*>(a)) return std::get<MapObj*>(a) == std::get<MapObj*>(b);  // identity
    return false;
}

bool isFalsey(const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return true;
    if (std::holds_alternative<bool>(v)) return !std::get<bool>(v);
    return false;
}
