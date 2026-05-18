#pragma once
#include "value.h"
#include <unordered_map>
#include <string>
#include <memory>

// ═══════════════════════════════════════════════════════════════════════════
//  ENVIRONMENT — Scope chain for variable resolution
// ═══════════════════════════════════════════════════════════════════════════
struct Env : public std::enable_shared_from_this<Env> {
    std::unordered_map<std::string, GValue> vars;
    std::shared_ptr<Env> parent;

    Env(std::shared_ptr<Env> p = nullptr) : parent(std::move(p)) {}

    GValue get(const std::string& name, int line = 0) const {
        auto it = vars.find(name);
        if (it != vars.end()) return it->second;
        if (parent) return parent->get(name, line);
        std::string msg = "Undefined variable '" + name + "'";
        if (line) msg += " at line " + std::to_string(line);
        throw RuntimeError(msg);
    }

    void define(const std::string& name, GValue val) {
        vars[name] = std::move(val);
    }

    // Assign existing variable anywhere in chain, or create locally
    void set(const std::string& name, GValue val) {
        if (vars.count(name)) { vars[name] = std::move(val); return; }
        if (parent && parent->has(name)) { parent->set(name, std::move(val)); return; }
        vars[name] = std::move(val);
    }

    // set-statement: variable MUST already exist somewhere in the chain
    void update(const std::string& name, GValue val, int line = 0) {
        if (vars.count(name)) { vars[name] = std::move(val); return; }
        if (parent && parent->has(name)) { parent->update(name, std::move(val), line); return; }
        std::string msg = "Undefined variable '" + name + "'";
        if (line) msg += " at line " + std::to_string(line);
        throw RuntimeError(msg);
    }

    bool has(const std::string& name) const {
        if (vars.count(name)) return true;
        return parent ? parent->has(name) : false;
    }
};
