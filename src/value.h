#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <cstdint>
#include <sstream>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <cassert>

// ═══════════════════════════════════════════════════════════════════════════
//  FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════════
struct GValue;
struct Env;
struct ASTNode;

using GList = std::vector<GValue>;
using GDict = std::unordered_map<std::string, GValue>;
using NativeFn = std::function<GValue(std::vector<GValue>&)>;



struct RuntimeError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct LexError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct ParseError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ═══════════════════════════════════════════════════════════════════════════
//  VALUE TYPE ENUM
// ═══════════════════════════════════════════════════════════════════════════
enum class ValueType : uint8_t {
    Null, Bool, Int, Float, String,
    List, Dict, Function, NativeFunc,
    Class, Instance, NativeLib, Pointer
};

// ═══════════════════════════════════════════════════════════════════════════
//  COMPLEX TYPE STRUCTS
// ═══════════════════════════════════════════════════════════════════════════
struct GFunction {
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<ASTNode> body;
    std::shared_ptr<Env> closure;
};

struct GClass {
    std::string name;
    std::shared_ptr<GClass> base;
    std::unordered_map<std::string, std::shared_ptr<GFunction>> methods;
    std::shared_ptr<Env> env;
};

struct GInstance {
    std::shared_ptr<GClass> klass;
    std::unordered_map<std::string, GValue> fields;
};

struct GNativeLib {
    std::string name;
    void* handle = nullptr; // HMODULE on Windows, void* on Linux
};

// ═══════════════════════════════════════════════════════════════════════════
//  GVALUE — THE UNIVERSAL RUNTIME VALUE
// ═══════════════════════════════════════════════════════════════════════════
struct GValue {
    ValueType type = ValueType::Null;

    // Primitive storage
    union {
        bool b;
        int64_t i;
        double f;
        void* ptr;
    } data;

    // Heap storage (only one active based on type, via shared_ptr<void>)
    std::shared_ptr<void> heap;

    // Native function (stored separately for quick access)
    NativeFn native_fn;
    std::string native_name; // name for repr

    // ── Constructors ─────────────────────────────────────────────────────
    GValue() { data.i = 0; }

    static GValue null_val() {
        GValue v; v.type = ValueType::Null; return v;
    }
    static GValue bool_val(bool b) {
        GValue v; v.type = ValueType::Bool; v.data.b = b; return v;
    }
    static GValue int_val(int64_t i) {
        GValue v; v.type = ValueType::Int; v.data.i = i; return v;
    }
    static GValue float_val(double f) {
        GValue v; v.type = ValueType::Float; v.data.f = f; return v;
    }
    static GValue string_val(const std::string& s) {
        GValue v; v.type = ValueType::String;
        auto p = std::make_shared<std::string>(s);
        v.heap = std::static_pointer_cast<void>(p);
        return v;
    }
    static GValue string_val(std::string&& s) {
        GValue v; v.type = ValueType::String;
        auto p = std::make_shared<std::string>(std::move(s));
        v.heap = std::static_pointer_cast<void>(p);
        return v;
    }
    static GValue list_val(const GList& l) {
        GValue v; v.type = ValueType::List;
        auto p = std::make_shared<GList>(l);
        v.heap = std::static_pointer_cast<void>(p);
        return v;
    }
    static GValue list_val(GList&& l) {
        GValue v; v.type = ValueType::List;
        auto p = std::make_shared<GList>(std::move(l));
        v.heap = std::static_pointer_cast<void>(p);
        return v;
    }
    static GValue dict_val(const GDict& d) {
        GValue v; v.type = ValueType::Dict;
        auto p = std::make_shared<GDict>(d);
        v.heap = std::static_pointer_cast<void>(p);
        return v;
    }
    static GValue dict_val(GDict&& d) {
        GValue v; v.type = ValueType::Dict;
        auto p = std::make_shared<GDict>(std::move(d));
        v.heap = std::static_pointer_cast<void>(p);
        return v;
    }
    static GValue func_val(std::shared_ptr<GFunction> fn) {
        GValue v; v.type = ValueType::Function;
        v.heap = std::static_pointer_cast<void>(fn);
        return v;
    }
    static GValue native_val(NativeFn fn, const std::string& name = "<native>") {
        GValue v; v.type = ValueType::NativeFunc;
        v.native_fn = std::move(fn);
        v.native_name = name;
        return v;
    }
    static GValue class_val(std::shared_ptr<GClass> cls) {
        GValue v; v.type = ValueType::Class;
        v.heap = std::static_pointer_cast<void>(cls);
        return v;
    }
    static GValue instance_val(std::shared_ptr<GInstance> inst) {
        GValue v; v.type = ValueType::Instance;
        v.heap = std::static_pointer_cast<void>(inst);
        return v;
    }
    static GValue nativelib_val(std::shared_ptr<GNativeLib> lib) {
        GValue v; v.type = ValueType::NativeLib;
        v.heap = std::static_pointer_cast<void>(lib);
        return v;
    }
    static GValue pointer_val(void* p) {
        GValue v; v.type = ValueType::Pointer; v.data.ptr = p; return v;
    }

    // ── Accessors ────────────────────────────────────────────────────────
    std::string& str() { return *static_cast<std::string*>(heap.get()); }
    const std::string& str() const { return *static_cast<std::string*>(heap.get()); }
    GList& list() { return *static_cast<GList*>(heap.get()); }
    const GList& list() const { return *static_cast<GList*>(heap.get()); }
    GDict& dict() { return *static_cast<GDict*>(heap.get()); }
    const GDict& dict() const { return *static_cast<GDict*>(heap.get()); }
    std::shared_ptr<GFunction> func() const {
        return std::static_pointer_cast<GFunction>(heap);
    }
    std::shared_ptr<GClass> cls() const {
        return std::static_pointer_cast<GClass>(heap);
    }
    std::shared_ptr<GInstance> inst() const {
        return std::static_pointer_cast<GInstance>(heap);
    }
    std::shared_ptr<GNativeLib> nlib() const {
        return std::static_pointer_cast<GNativeLib>(heap);
    }

    // ── Truthiness ───────────────────────────────────────────────────────
    bool truthy() const {
        switch (type) {
            case ValueType::Null: return false;
            case ValueType::Bool: return data.b;
            case ValueType::Int: return data.i != 0;
            case ValueType::Float: return data.f != 0.0;
            case ValueType::String: return !str().empty();
            case ValueType::List: return !list().empty();
            case ValueType::Dict: return !dict().empty();
            case ValueType::Pointer: return data.ptr != nullptr;
            default: return true;
        }
    }

    // ── Equality ─────────────────────────────────────────────────────────
    bool equals(const GValue& o) const {
        if (type == ValueType::Null && o.type == ValueType::Null) return true;
        if (type == ValueType::Null || o.type == ValueType::Null) return false;
        if (type == ValueType::Bool && o.type == ValueType::Bool) return data.b == o.data.b;
        // Numeric comparisons (int/float interop)
        if ((type == ValueType::Int || type == ValueType::Float) &&
            (o.type == ValueType::Int || o.type == ValueType::Float)) {
            double a = (type == ValueType::Int) ? (double)data.i : data.f;
            double b = (o.type == ValueType::Int) ? (double)o.data.i : o.data.f;
            return a == b;
        }
        if (type == ValueType::String && o.type == ValueType::String) return str() == o.str();
        return false;
    }

    // ── Numeric conversion ───────────────────────────────────────────────
    double as_number() const {
        if (type == ValueType::Int) return (double)data.i;
        if (type == ValueType::Float) return data.f;
        throw RuntimeError("Expected number");
    }
    int64_t as_int() const {
        if (type == ValueType::Int) return data.i;
        if (type == ValueType::Float) return (int64_t)data.f;
        throw RuntimeError("Expected integer");
    }

    // ── Type name ────────────────────────────────────────────────────────
    std::string type_name() const {
        switch (type) {
            case ValueType::Null: return "null";
            case ValueType::Bool: return "bool";
            case ValueType::Int: return "number";
            case ValueType::Float: return "number";
            case ValueType::String: return "string";
            case ValueType::List: return "list";
            case ValueType::Dict: return "dict";
            case ValueType::Function: return "func";
            case ValueType::NativeFunc: return "func";
            case ValueType::Class: return "class";
            case ValueType::Instance: return inst()->klass->name;
            case ValueType::NativeLib: return "lib";
            case ValueType::Pointer: return "pointer";
        }
        return "unknown";
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  RUNTIME SIGNALS (used as exceptions for control flow)
// ═══════════════════════════════════════════════════════════════════════════
struct ReturnSignal { GValue val; };
struct BreakSignal {};
struct ContinueSignal {};

// ═══════════════════════════════════════════════════════════════════════════
//  gi_str — Convert GValue to display string
// ═══════════════════════════════════════════════════════════════════════════
inline std::string gi_str(const GValue& v) {
    switch (v.type) {
        case ValueType::Null: return "null";
        case ValueType::Bool: return v.data.b ? "true" : "false";
        case ValueType::Int: return std::to_string(v.data.i);
        case ValueType::Float: {
            double d = v.data.f;
            if (d == (int64_t)d && std::isfinite(d)) return std::to_string((int64_t)d);
            std::ostringstream os; os << d; return os.str();
        }
        case ValueType::String: return v.str();
        case ValueType::List: {
            std::string r = "[";
            auto& l = v.list();
            for (size_t i = 0; i < l.size(); i++) {
                if (i) r += ", ";
                r += gi_str(l[i]);
            }
            return r + "]";
        }
        case ValueType::Dict: {
            std::string r = "{";
            bool first = true;
            for (auto& [k, val] : v.dict()) {
                if (!first) r += ", ";
                first = false;
                r += gi_str(GValue::string_val(k)) + ": " + gi_str(val);
            }
            return r + "}";
        }
        case ValueType::Function: return "<func " + v.func()->name + ">";
        case ValueType::NativeFunc: return "<native " + v.native_name + ">";
        case ValueType::Class: return "<class " + v.cls()->name + ">";
        case ValueType::Instance: return "<" + v.inst()->klass->name + " instance>";
        case ValueType::NativeLib: return "<lib '" + v.nlib()->name + "'>";
        case ValueType::Pointer: {
            std::ostringstream os; os << "<ptr 0x" << std::hex << (uintptr_t)v.data.ptr << ">";
            return os.str();
        }
    }
    return "<unknown>";
}

// Key stringification for dict storage
inline std::string dict_key(const GValue& v) {
    return gi_str(v);
}
