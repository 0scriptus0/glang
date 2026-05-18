#pragma once
#include "value.h"
#include "environment.h"
#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <thread>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>
#include <string>
#include <unordered_map>
#include <limits>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════════════
//  WIN32 / UNIX MAXIMUM-ARITY REGULAR CALL INTEROP (Dynamic Register Routing)
// ═══════════════════════════════════════════════════════════════════════════
typedef uintptr_t (*GenericFn8)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

inline GValue call_native_ffi(void* sym, const std::vector<GValue>& args) {
    uintptr_t c_args[8] = {0};
    for (size_t i = 0; i < args.size() && i < 8; ++i) {
        auto& arg = args[i];
        if (arg.type == ValueType::Int) {
            c_args[i] = (uintptr_t)arg.data.i;
        } else if (arg.type == ValueType::Float) {
            double d = arg.data.f;
            c_args[i] = (uintptr_t)d;
        } else if (arg.type == ValueType::String) {
            c_args[i] = (uintptr_t)arg.str().c_str();
        } else if (arg.type == ValueType::Pointer) {
            c_args[i] = (uintptr_t)arg.data.ptr;
        } else if (arg.type == ValueType::Bool) {
            c_args[i] = arg.data.b ? 1 : 0;
        } else if (arg.type == ValueType::Null) {
            c_args[i] = 0;
        } else {
            c_args[i] = (uintptr_t)arg.heap.get();
        }
    }
    auto fn = (GenericFn8)sym;
    uintptr_t result = fn(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4], c_args[5], c_args[6], c_args[7]);
    return GValue::int_val(result);
}

// Helper path resolution utilities
inline std::string home_packages_dir() {
    #ifdef _WIN32
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) {
        return (fs::path(userprofile) / ".glang" / "packages").string();
    }
    #else
    const char* home = std::getenv("HOME");
    if (home) {
        return (fs::path(home) / ".glang" / "packages").string();
    }
    #endif
    return "./packages";
}

inline std::string resolve_import_path(const std::string& path, bool is_pkg, const std::string& current_dir) {
    std::string norm = path;
    if (norm.size() > 2 && norm.substr(norm.size() - 2) == ".g") {
        norm = norm.substr(0, norm.size() - 2);
    }
    
    fs::path rel_path;
    std::string seg;
    for (char c : norm) {
        if (c == '.' || c == '/' || c == '\\') {
            if (!seg.empty()) {
                rel_path /= seg;
                seg.clear();
            }
        } else {
            seg += c;
        }
    }
    if (!seg.empty()) rel_path /= seg;
    
    fs::path rel_path_g = rel_path;
    rel_path_g.replace_extension(".g");
    
    std::vector<fs::path> candidates;
    if (!is_pkg) {
        candidates.push_back(fs::path(current_dir) / rel_path_g);
        candidates.push_back(fs::current_path() / rel_path_g);
    }
    
    std::vector<fs::path> roots = {
        fs::current_path() / "packages",
        fs::path(home_packages_dir())
    };
    
    for (auto& r : roots) {
        candidates.push_back(r / rel_path_g);
        candidates.push_back(r / rel_path / "main.g");
        candidates.push_back(r / rel_path / "__init__.g");
    }
    
    for (auto& c : candidates) {
        if (fs::exists(c) && fs::is_regular_file(c)) {
            return fs::absolute(c).string();
        }
    }
    return "";
}

inline std::string get_parent_dir(const std::string& file_path) {
    return fs::path(file_path).parent_path().string();
}

inline std::string get_basename_without_ext(const std::string& file_path) {
    return fs::path(file_path).stem().string();
}

// ═══════════════════════════════════════════════════════════════════════════
//  INTERPRETER CLASS
// ═══════════════════════════════════════════════════════════════════════════
class Interpreter {
public:
    std::shared_ptr<Env> globals;
    std::string script_dir;
    std::unordered_map<std::string, GDict> mod_cache;
    
    Interpreter(std::string dir = ".") 
        : globals(std::make_shared<Env>()), script_dir(dir) {
        setup_builtins();
    }
    
    void run(NodePtr node, std::shared_ptr<Env> env = nullptr) {
        if (!env) env = globals;
        exec(node, env);
    }
    
    void exec(NodePtr node, std::shared_ptr<Env> env) {
        if (!node) return;
        switch (node->ntype) {
            case NodeType::Block: {
                auto n = std::static_pointer_cast<BlockNode>(node);
                for (auto& stmt : n->stmts) {
                    exec(stmt, env);
                }
                break;
            }
            case NodeType::LetStmt: {
                auto n = std::static_pointer_cast<LetStmtNode>(node);
                env->define(n->name, eval(n->value, env));
                break;
            }
            case NodeType::SetStmt: {
                auto n = std::static_pointer_cast<SetStmtNode>(node);
                env->update(n->target, eval(n->value, env), n->line);
                break;
            }
            case NodeType::AssignStmt: {
                auto n = std::static_pointer_cast<AssignStmtNode>(node);
                GValue val = eval(n->value, env);
                if (n->op != "=") {
                    GValue cur = env->get(n->target, n->line);
                    std::string op = n->op.substr(0, n->op.size() - 1);
                    if (op == "+" && (cur.type == ValueType::String || val.type == ValueType::String)) {
                        val = GValue::string_val(gi_str(cur) + gi_str(val));
                    } else if (cur.type == ValueType::Float || val.type == ValueType::Float) {
                        double a = cur.as_number();
                        double b = val.as_number();
                        if (op == "+") val = GValue::float_val(a + b);
                        else if (op == "-") val = GValue::float_val(a - b);
                        else if (op == "*") val = GValue::float_val(a * b);
                        else if (op == "/") val = GValue::float_val(a / b);
                    } else {
                        int64_t a = cur.as_int();
                        int64_t b = val.as_int();
                        if (op == "+") val = GValue::int_val(a + b);
                        else if (op == "-") val = GValue::int_val(a - b);
                        else if (op == "*") val = GValue::int_val(a * b);
                        else if (op == "/") {
                            if (b == 0) throw RuntimeError("Division by zero");
                            val = GValue::int_val(a / b);
                        }
                    }
                }
                env->set(n->target, val);
                break;
            }
            case NodeType::SetAttr: {
                auto n = std::static_pointer_cast<SetAttrNode>(node);
                GValue obj = eval(n->obj, env);
                GValue val = eval(n->value, env);
                if (obj.type == ValueType::Instance) {
                    obj.inst()->fields[n->attr] = val;
                } else if (obj.type == ValueType::Dict) {
                    obj.dict()[n->attr] = val;
                } else {
                    throw RuntimeError("Cannot set attribute '" + n->attr + "' on " + obj.type_name());
                }
                break;
            }
            case NodeType::SetIndex: {
                auto n = std::static_pointer_cast<SetIndexNode>(node);
                GValue obj = eval(n->obj, env);
                GValue idx = eval(n->idx, env);
                GValue val = eval(n->value, env);
                if (obj.type == ValueType::List) {
                    int64_t i = idx.as_int();
                    auto& l = obj.list();
                    if (i < 0) i += l.size();
                    if (i < 0 || i >= (int64_t)l.size()) throw RuntimeError("Index out of bounds");
                    l[i] = val;
                } else if (obj.type == ValueType::Dict) {
                    obj.dict()[gi_str(idx)] = val;
                } else {
                    throw RuntimeError("Cannot set index on " + obj.type_name());
                }
                break;
            }
            case NodeType::IfStmt: {
                auto n = std::static_pointer_cast<IfStmtNode>(node);
                bool matched = false;
                for (auto& branch : n->branches) {
                    if (eval(branch.cond, env).truthy()) {
                        exec(branch.body, std::make_shared<Env>(env));
                        matched = true;
                        break;
                    }
                }
                if (!matched && n->else_block) {
                    exec(n->else_block, std::make_shared<Env>(env));
                }
                break;
            }
            case NodeType::WhileStmt: {
                auto n = std::static_pointer_cast<WhileStmtNode>(node);
                while (eval(n->cond, env).truthy()) {
                    try {
                        exec(n->body, std::make_shared<Env>(env));
                    } catch (ContinueSignal&) {
                        continue;
                    } catch (BreakSignal&) {
                        break;
                    }
                }
                break;
            }
            case NodeType::ForStmt: {
                auto n = std::static_pointer_cast<ForStmtNode>(node);
                GValue iterable = eval(n->iterable, env);
                std::vector<GValue> items;
                if (iterable.type == ValueType::List) {
                    items = iterable.list();
                } else if (iterable.type == ValueType::Dict) {
                    for (auto& [k, v] : iterable.dict()) {
                        items.push_back(GValue::string_val(k));
                    }
                } else if (iterable.type == ValueType::String) {
                    for (char c : iterable.str()) {
                        items.push_back(GValue::string_val(std::string(1, c)));
                    }
                } else {
                    throw RuntimeError("Type " + iterable.type_name() + " is not iterable");
                }
                for (auto& item : items) {
                    auto inner = std::make_shared<Env>(env);
                    inner->define(n->var, item);
                    try {
                        exec(n->body, inner);
                    } catch (ContinueSignal&) {
                        continue;
                    } catch (BreakSignal&) {
                        break;
                    }
                }
                break;
            }
            case NodeType::FuncDef: {
                auto n = std::static_pointer_cast<FuncDefNode>(node);
                auto fn = std::make_shared<GFunction>();
                fn->name = n->name;
                fn->params = n->params;
                fn->body = n->body;
                fn->closure = env;
                env->define(n->name, GValue::func_val(fn));
                break;
            }
            case NodeType::ReturnStmt: {
                auto n = std::static_pointer_cast<ReturnStmtNode>(node);
                throw ReturnSignal{n->value ? eval(n->value, env) : GValue::null_val()};
            }
            case NodeType::PassStmt: {
                break;
            }
            case NodeType::ContinueStmt: {
                throw ContinueSignal{};
            }
            case NodeType::BreakStmt: {
                throw BreakSignal{};
            }
            case NodeType::WithStmt: {
                auto n = std::static_pointer_cast<WithStmtNode>(node);
                GValue ctx = eval(n->call, env);
                auto inner = std::make_shared<Env>(env);
                inner->define(n->alias, ctx);
                try {
                    exec(n->body, inner);
                } catch (...) {
                    GValue close_fn;
                    try { close_fn = get_attr(ctx, "close"); } catch (...) {}
                    if (close_fn.type == ValueType::NativeFunc || close_fn.type == ValueType::Function) {
                        std::vector<GValue> args;
                        call_value(close_fn, args);
                    }
                    throw;
                }
                GValue close_fn;
                try { close_fn = get_attr(ctx, "close"); } catch (...) {}
                if (close_fn.type == ValueType::NativeFunc || close_fn.type == ValueType::Function) {
                    std::vector<GValue> args;
                    call_value(close_fn, args);
                }
                break;
            }
            case NodeType::ClassDef: {
                auto n = std::static_pointer_cast<ClassDefNode>(node);
                auto klass = std::make_shared<GClass>();
                klass->name = n->name;
                klass->env = env;
                if (!n->base.empty()) {
                    auto b = env->get(n->base);
                    if (b.type == ValueType::Class) klass->base = b.cls();
                }
                auto body_block = std::static_pointer_cast<BlockNode>(n->body);
                for (auto& stmt : body_block->stmts) {
                    if (stmt->ntype == NodeType::FuncDef) {
                        auto fd = std::static_pointer_cast<FuncDefNode>(stmt);
                        auto fn = std::make_shared<GFunction>();
                        fn->name = fd->name;
                        fn->params = fd->params;
                        fn->body = fd->body;
                        fn->closure = env;
                        klass->methods[fd->name] = fn;
                    }
                }
                env->define(n->name, GValue::class_val(klass));
                break;
            }
            case NodeType::ImportStmt: {
                do_import(std::static_pointer_cast<ImportStmtNode>(node), env);
                break;
            }
            case NodeType::StructDef: {
                auto n = std::static_pointer_cast<StructDefNode>(node);
                auto klass = std::make_shared<GClass>();
                klass->name = n->name;
                klass->env = env;
                auto init_fn = std::make_shared<GFunction>();
                init_fn->name = "init";
                init_fn->params.push_back("self");
                for (auto& f : n->fields) init_fn->params.push_back(f);
                std::vector<NodePtr> init_stmts;
                for (auto& f : n->fields) {
                    auto self_id = std::make_shared<IdentNode>("self", 0);
                    auto val_id = std::make_shared<IdentNode>(f, 0);
                    init_stmts.push_back(std::make_shared<SetAttrNode>(self_id, f, val_id));
                }
                init_fn->body = std::make_shared<BlockNode>(init_stmts);
                init_fn->closure = env;
                klass->methods["init"] = init_fn;
                env->define(n->name, GValue::class_val(klass));
                break;
            }
            case NodeType::EnumDef: {
                auto n = std::static_pointer_cast<EnumDefNode>(node);
                GDict d;
                for (size_t i = 0; i < n->values.size(); ++i) {
                    d[n->values[i]] = GValue::int_val(i);
                }
                env->define(n->name, GValue::dict_val(d));
                break;
            }
            case NodeType::ExprStmt: {
                eval(std::static_pointer_cast<ExprStmtNode>(node)->expr, env);
                break;
            }
            default:
                throw RuntimeError("Unknown statement node type");
        }
    }

    GValue eval(NodePtr node, std::shared_ptr<Env> env) {
        if (!node) return GValue::null_val();
        switch (node->ntype) {
            case NodeType::NumberLit: {
                auto n = std::static_pointer_cast<NumberLitNode>(node);
                return n->is_int ? GValue::int_val((int64_t)n->value) : GValue::float_val(n->value);
            }
            case NodeType::StringLit: {
                auto n = std::static_pointer_cast<StringLitNode>(node);
                return GValue::string_val(n->value);
            }
            case NodeType::BoolLit: {
                auto n = std::static_pointer_cast<BoolLitNode>(node);
                return GValue::bool_val(n->value);
            }
            case NodeType::NullLit: {
                return GValue::null_val();
            }
            case NodeType::Ident: {
                auto n = std::static_pointer_cast<IdentNode>(node);
                return env->get(n->name, n->line);
            }
            case NodeType::BinOp: {
                return eval_binop(std::static_pointer_cast<BinOpNode>(node), env);
            }
            case NodeType::UnaryOp: {
                return eval_unaryop(std::static_pointer_cast<UnaryOpNode>(node), env);
            }
            case NodeType::Index: {
                return eval_index(std::static_pointer_cast<IndexNode>(node), env);
            }
            case NodeType::GetAttr: {
                auto n = std::static_pointer_cast<GetAttrNode>(node);
                GValue obj = eval(n->obj, env);
                return get_attr(obj, n->attr);
            }
            case NodeType::ListLit: {
                auto n = std::static_pointer_cast<ListLitNode>(node);
                GList l;
                for (auto& item : n->items) l.push_back(eval(item, env));
                return GValue::list_val(l);
            }
            case NodeType::DictLit: {
                auto n = std::static_pointer_cast<DictLitNode>(node);
                GDict d;
                for (auto& p : n->pairs) {
                    d[gi_str(eval(p.first, env))] = eval(p.second, env);
                }
                return GValue::dict_val(d);
            }
            case NodeType::LambdaExpr: {
                auto n = std::static_pointer_cast<LambdaExprNode>(node);
                auto fn = std::make_shared<GFunction>();
                fn->name = "<lambda>";
                fn->params = n->params;
                fn->body = n->body;
                fn->closure = env;
                return GValue::func_val(fn);
            }
            case NodeType::NewExpr: {
                auto n = std::static_pointer_cast<NewExprNode>(node);
                GValue klass_val = env->get(n->cls, n->line);
                std::vector<GValue> args;
                for (auto& a : n->args) args.push_back(eval(a, env));
                return instantiate(klass_val, args);
            }
            case NodeType::Call: {
                auto n = std::static_pointer_cast<CallNode>(node);
                GValue callee = eval(n->callee, env);
                std::vector<GValue> args;
                for (auto& a : n->args) args.push_back(eval(a, env));
                return call_value(callee, args);
            }
            case NodeType::SizeofExpr: {
                auto n = std::static_pointer_cast<SizeofExprNode>(node);
                std::string t = n->type_name;
                if (t == "int" || t == "int32") return GValue::int_val(sizeof(int));
                if (t == "long" || t == "int64") return GValue::int_val(sizeof(long long));
                if (t == "float") return GValue::int_val(sizeof(float));
                if (t == "double") return GValue::int_val(sizeof(double));
                if (t == "char" || t == "byte") return GValue::int_val(sizeof(char));
                if (t == "ptr" || t == "pointer") return GValue::int_val(sizeof(void*));
                throw RuntimeError("Unknown sizeof type: " + t);
            }
            case NodeType::CastExpr: {
                auto n = std::static_pointer_cast<CastExprNode>(node);
                GValue val = eval(n->expr, env);
                std::string t = n->target_type;
                if (t == "int") {
                    return GValue::int_val(val.as_int());
                }
                if (t == "float") {
                    return GValue::float_val(val.as_number());
                }
                if (t == "str") {
                    return GValue::string_val(gi_str(val));
                }
                if (t == "ptr" || t == "pointer") {
                    if (val.type == ValueType::Pointer) return val;
                    if (val.type == ValueType::Int) return GValue::pointer_val((void*)(uintptr_t)val.data.i);
                    if (val.type == ValueType::Null) return GValue::pointer_val(nullptr);
                    throw RuntimeError("Cannot cast " + val.type_name() + " to pointer");
                }
                throw RuntimeError("Unknown cast target type: " + t);
            }
            default:
                throw RuntimeError("Unknown expression node type");
        }
    }

    GValue get_attr(GValue obj, const std::string& attr) {
        if (obj.type == ValueType::Instance) {
            auto inst = obj.inst();
            auto it = inst->fields.find(attr);
            if (it != inst->fields.end()) return it->second;
            
            auto* k = inst->klass.get();
            while (k) {
                auto mit = k->methods.find(attr);
                if (mit != k->methods.end()) {
                    auto fn = mit->second;
                    auto bound_env = std::make_shared<Env>(fn->closure);
                    bound_env->define("self", obj);
                    
                    auto bound_fn = std::make_shared<GFunction>();
                    bound_fn->name = fn->name;
                    bound_fn->body = fn->body;
                    bound_fn->closure = bound_env;
                    bound_fn->params = fn->params;
                    if (!bound_fn->params.empty() && bound_fn->params[0] == "self") {
                        bound_fn->params.erase(bound_fn->params.begin());
                    }
                    return GValue::func_val(bound_fn);
                }
                k = k->base.get();
            }
            throw RuntimeError("No attribute '" + attr + "' on class " + inst->klass->name);
        }
        
        if (obj.type == ValueType::Dict) {
            auto& d = obj.dict();
            auto it = d.find(attr);
            if (it != d.end()) return it->second;
            throw RuntimeError("No key '" + attr + "' in dict");
        }
        
        if (obj.type == ValueType::NativeLib) {
            void* handle = obj.nlib()->handle;
            std::string lib_name = obj.nlib()->name;
            void* sym = nullptr;
            #ifdef _WIN32
            sym = (void*)GetProcAddress((HMODULE)handle, attr.c_str());
            #else
            sym = dlsym(handle, attr.c_str());
            #endif
            if (!sym) {
                throw RuntimeError("Library '" + lib_name + "' has no symbol '" + attr + "'");
            }
            return GValue::native_val([sym](std::vector<GValue>& args) -> GValue {
                return call_native_ffi(sym, args);
            }, attr);
        }
        
        if (obj.type == ValueType::List) {
            if (attr == "push") {
                return GValue::native_val([obj](std::vector<GValue>& args) mutable -> GValue {
                    if (args.empty()) throw RuntimeError("list.push expects 1 argument");
                    obj.list().push_back(args[0]);
                    return GValue::null_val();
                }, "list.push");
            }
            if (attr == "pop") {
                return GValue::native_val([obj](std::vector<GValue>&) mutable -> GValue {
                    auto& l = obj.list();
                    if (l.empty()) throw RuntimeError("list.pop from empty list");
                    GValue back = l.back();
                    l.pop_back();
                    return back;
                }, "list.pop");
            }
            if (attr == "len") {
                return GValue::native_val([obj](std::vector<GValue>&) -> GValue {
                    return GValue::int_val(obj.list().size());
                }, "list.len");
            }
            if (attr == "sort") {
                return GValue::native_val([obj](std::vector<GValue>&) mutable -> GValue {
                    auto& l = obj.list();
                    std::sort(l.begin(), l.end(), [](const GValue& a, const GValue& b) {
                        if (a.type == ValueType::Int && b.type == ValueType::Int) return a.data.i < b.data.i;
                        if ((a.type == ValueType::Int || a.type == ValueType::Float) &&
                            (b.type == ValueType::Int || b.type == ValueType::Float)) {
                            return a.as_number() < b.as_number();
                        }
                        if (a.type == ValueType::String && b.type == ValueType::String) return a.str() < b.str();
                        return false;
                    });
                    return GValue::null_val();
                }, "list.sort");
            }
            if (attr == "reverse") {
                return GValue::native_val([obj](std::vector<GValue>&) mutable -> GValue {
                    auto& l = obj.list();
                    std::reverse(l.begin(), l.end());
                    return GValue::null_val();
                }, "list.reverse");
            }
            if (attr == "insert") {
                return GValue::native_val([obj](std::vector<GValue>& args) mutable -> GValue {
                    if (args.size() < 2) throw RuntimeError("list.insert expects index and value");
                    int64_t idx = args[0].as_int();
                    auto& l = obj.list();
                    if (idx < 0) idx += l.size();
                    if (idx < 0 || idx > (int64_t)l.size()) throw RuntimeError("list.insert out of bounds");
                    l.insert(l.begin() + idx, args[1]);
                    return GValue::null_val();
                }, "list.insert");
            }
            if (attr == "remove") {
                return GValue::native_val([obj](std::vector<GValue>& args) mutable -> GValue {
                    if (args.empty()) throw RuntimeError("list.remove expects 1 argument");
                    auto& l = obj.list();
                    auto it = std::find_if(l.begin(), l.end(), [&](const GValue& v) { return v.equals(args[0]); });
                    if (it != l.end()) l.erase(it);
                    return GValue::null_val();
                }, "list.remove");
            }
            if (attr == "contains") {
                return GValue::native_val([obj](std::vector<GValue>& args) -> GValue {
                    if (args.empty()) throw RuntimeError("list.contains expects 1 argument");
                    auto& l = obj.list();
                    auto it = std::find_if(l.begin(), l.end(), [&](const GValue& v) { return v.equals(args[0]); });
                    return GValue::bool_val(it != l.end());
                }, "list.contains");
            }
        }
        
        if (obj.type == ValueType::String) {
            if (attr == "len") {
                return GValue::native_val([obj](std::vector<GValue>&) -> GValue {
                    return GValue::int_val(obj.str().size());
                }, "string.len");
            }
            if (attr == "upper") {
                return GValue::native_val([obj](std::vector<GValue>&) -> GValue {
                    std::string s = obj.str();
                    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
                    return GValue::string_val(s);
                }, "string.upper");
            }
            if (attr == "lower") {
                return GValue::native_val([obj](std::vector<GValue>&) -> GValue {
                    std::string s = obj.str();
                    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                    return GValue::string_val(s);
                }, "string.lower");
            }
            if (attr == "trim" || attr == "strip") {
                return GValue::native_val([obj](std::vector<GValue>&) -> GValue {
                    std::string s = obj.str();
                    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
                    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
                    return GValue::string_val(s);
                }, "string.trim");
            }
            if (attr == "split") {
                return GValue::native_val([obj](std::vector<GValue>& args) -> GValue {
                    std::string delim = " ";
                    if (!args.empty()) delim = args[0].str();
                    
                    GList res;
                    std::string s = obj.str();
                    if (delim.empty()) {
                        for (char c : s) res.push_back(GValue::string_val(std::string(1, c)));
                        return GValue::list_val(res);
                    }
                    size_t pos = 0;
                    while ((pos = s.find(delim)) != std::string::npos) {
                        res.push_back(GValue::string_val(s.substr(0, pos)));
                        s.erase(0, pos + delim.length());
                    }
                    res.push_back(GValue::string_val(s));
                    return GValue::list_val(res);
                }, "string.split");
            }
            if (attr == "contains") {
                return GValue::native_val([obj](std::vector<GValue>& args) -> GValue {
                    if (args.empty()) throw RuntimeError("string.contains expects 1 argument");
                    return GValue::bool_val(obj.str().find(args[0].str()) != std::string::npos);
                }, "string.contains");
            }
            if (attr == "startswith") {
                return GValue::native_val([obj](std::vector<GValue>& args) -> GValue {
                    if (args.empty()) throw RuntimeError("string.startswith expects 1 argument");
                    std::string prefix = args[0].str();
                    return GValue::bool_val(obj.str().rfind(prefix, 0) == 0);
                }, "string.startswith");
            }
            if (attr == "endswith") {
                return GValue::native_val([obj](std::vector<GValue>& args) -> GValue {
                    if (args.empty()) throw RuntimeError("string.endswith expects 1 argument");
                    std::string suffix = args[0].str();
                    std::string s = obj.str();
                    if (s.length() >= suffix.length()) {
                        return GValue::bool_val(s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0);
                    }
                    return GValue::bool_val(false);
                }, "string.endswith");
            }
        }
        
        throw RuntimeError("No attribute '" + attr + "' on " + obj.type_name());
    }

    GValue call_value(GValue callee, const std::vector<GValue>& args) {
        if (callee.type == ValueType::NativeFunc) {
            std::vector<GValue> mutable_args = args;
            return callee.native_fn(mutable_args);
        }
        
        if (callee.type == ValueType::Function) {
            auto fn = callee.func();
            auto fn_env = std::make_shared<Env>(fn->closure);
            for (size_t i = 0; i < fn->params.size(); ++i) {
                fn_env->define(fn->params[i], i < args.size() ? args[i] : GValue::null_val());
            }
            if (fn->body->ntype >= NodeType::NumberLit) {
                return eval(fn->body, fn_env);
            }
            try {
                exec(fn->body, fn_env);
            } catch (ReturnSignal& r) {
                return r.val;
            }
            return GValue::null_val();
        }
        
        if (callee.type == ValueType::Class) {
            return instantiate(callee, args);
        }
        
        throw RuntimeError(callee.type_name() + " is not callable");
    }
    
    GValue instantiate(GValue kv, const std::vector<GValue>& args) {
        if (kv.type != ValueType::Class) {
            throw RuntimeError("Cannot instantiate non-class");
        }
        auto inst = std::make_shared<GInstance>();
        inst->klass = kv.cls();
        GValue iv = GValue::instance_val(inst);
        
        auto* k = kv.cls().get();
        while (k) {
            auto it = k->methods.find("init");
            if (it != k->methods.end()) {
                auto fn = it->second;
                auto fn_env = std::make_shared<Env>(fn->closure);
                fn_env->define("self", iv);
                
                size_t skip = (fn->params.empty() || fn->params[0] != "self") ? 0 : 1;
                for (size_t i = 0; i < fn->params.size() - skip; ++i) {
                    fn_env->define(fn->params[i + skip], i < args.size() ? args[i] : GValue::null_val());
                }
                try {
                    exec(fn->body, fn_env);
                } catch (ReturnSignal&) {
                }
                break;
            }
            k = k->base.get();
        }
        return iv;
    }

private:
    void define_native(const std::string& name, NativeFn fn) {
        globals->define(name, GValue::native_val(fn, name));
    }

    void setup_builtins() {
        globals->define("null", GValue::null_val());
        
        define_native("CNLSTDOUT", [](std::vector<GValue>& args) -> GValue {
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) std::cout << " ";
                std::cout << gi_str(args[i]);
            }
            std::cout << std::endl;
            return GValue::null_val();
        });
        define_native("STDOUT", [](std::vector<GValue>& args) -> GValue {
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) std::cout << " ";
                std::cout << gi_str(args[i]);
            }
            std::cout << std::flush;
            return GValue::null_val();
        });
        define_native("STDIN", [](std::vector<GValue>& args) -> GValue {
            if (!args.empty()) {
                std::cout << gi_str(args[0]) << std::flush;
            }
            std::string line;
            if (!std::getline(std::cin, line)) {
                std::exit(0);
            }
            return GValue::string_val(line);
        });
        define_native("STDIN_BUFFER", [](std::vector<GValue>&) -> GValue {
            std::string content, line;
            while (std::getline(std::cin, line)) {
                content += line + "\n";
            }
            return GValue::string_val(content);
        });
        define_native("system", [](std::vector<GValue>& args) -> GValue {
            std::string cmd;
            for (auto& a : args) cmd += gi_str(a);
            int res = std::system(cmd.c_str());
            return GValue::int_val(res);
        });
        
        // Output and Input aliases
        globals->define("printl", globals->get("CNLSTDOUT"));
        globals->define("print", globals->get("STDOUT"));
        globals->define("readl", globals->get("STDIN"));
        
        define_native("int", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::int_val(0);
            return GValue::int_val(args[0].as_int());
        });
        define_native("float", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::float_val(0.0);
            return GValue::float_val(args[0].as_number());
        });
        define_native("str", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::string_val("");
            return GValue::string_val(gi_str(args[0]));
        });
        define_native("bool", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::bool_val(false);
            return GValue::bool_val(args[0].truthy());
        });
        define_native("typeof", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::string_val("null");
            return GValue::string_val(args[0].type_name());
        });
        
        define_native("assert", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("assert expects condition");
            if (!args[0].truthy()) {
                std::string msg = args.size() > 1 ? args[1].str() : "Assertion failed";
                throw RuntimeError(msg);
            }
            return GValue::null_val();
        });
        define_native("error", [](std::vector<GValue>& args) -> GValue {
            std::string msg = args.empty() ? "Runtime error" : args[0].str();
            throw RuntimeError(msg);
        });
        define_native("panic", [](std::vector<GValue>& args) -> GValue {
            std::string msg = args.empty() ? "Panic" : args[0].str();
            throw RuntimeError(msg);
        });
        
        define_native("range", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("range expects at least end");
            int64_t start = 0, end = 0, step = 1;
            if (args.size() == 1) {
                end = args[0].as_int();
            } else {
                start = args[0].as_int();
                end = args[1].as_int();
                if (args.size() > 2) step = args[2].as_int();
            }
            GList l;
            if (step > 0) {
                for (int64_t i = start; i < end; i += step) l.push_back(GValue::int_val(i));
            } else if (step < 0) {
                for (int64_t i = start; i > end; i += step) l.push_back(GValue::int_val(i));
            }
            return GValue::list_val(l);
        });
        define_native("len", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::int_val(0);
            auto& a = args[0];
            if (a.type == ValueType::List) return GValue::int_val(a.list().size());
            if (a.type == ValueType::Dict) return GValue::int_val(a.dict().size());
            if (a.type == ValueType::String) return GValue::int_val(a.str().size());
            return GValue::int_val(0);
        });
        define_native("keys", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].type != ValueType::Dict) throw RuntimeError("keys expects 1 dictionary");
            GList l;
            for (auto& [k, v] : args[0].dict()) l.push_back(GValue::string_val(k));
            return GValue::list_val(l);
        });
        define_native("values", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].type != ValueType::Dict) throw RuntimeError("values expects 1 dictionary");
            GList l;
            for (auto& [k, v] : args[0].dict()) l.push_back(v);
            return GValue::list_val(l);
        });
        define_native("haskey", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2 || args[0].type != ValueType::Dict) throw RuntimeError("haskey expects dictionary and key");
            return GValue::bool_val(args[0].dict().count(args[1].str()) > 0);
        });
        define_native("delkey", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2 || args[0].type != ValueType::Dict) throw RuntimeError("delkey expects dictionary and key");
            args[0].dict().erase(args[1].str());
            return GValue::null_val();
        });
        
        // Dynamic map, filter, reduce
        define_native("map", [this](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2 || args[1].type != ValueType::List) throw RuntimeError("map expects function and list");
            GValue f = args[0];
            auto& l = args[1].list();
            GList res;
            for (auto& x : l) {
                std::vector<GValue> fargs = {x};
                res.push_back(call_value(f, fargs));
            }
            return GValue::list_val(res);
        });
        define_native("filter", [this](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2 || args[1].type != ValueType::List) throw RuntimeError("filter expects function and list");
            GValue f = args[0];
            auto& l = args[1].list();
            GList res;
            for (auto& x : l) {
                std::vector<GValue> fargs = {x};
                if (call_value(f, fargs).truthy()) {
                    res.push_back(x);
                }
            }
            return GValue::list_val(res);
        });
        define_native("reduce", [this](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2 || args[1].type != ValueType::List) throw RuntimeError("reduce expects function and list, [init]");
            GValue f = args[0];
            auto& l = args[1].list();
            if (l.empty()) {
                return args.size() > 2 ? args[2] : GValue::null_val();
            }
            GValue acc = args.size() > 2 ? args[2] : l[0];
            size_t start = args.size() > 2 ? 0 : 1;
            for (size_t i = start; i < l.size(); ++i) {
                std::vector<GValue> fargs = {acc, l[i]};
                acc = call_value(f, fargs);
            }
            return acc;
        });
        
        // String functions
        define_native("strlen", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::int_val(0);
            return GValue::int_val(args[0].str().size());
        });
        define_native("substr", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 3) throw RuntimeError("substr expects string, start, end");
            std::string s = args[0].str();
            int64_t start = args[1].as_int();
            int64_t end = args[2].as_int();
            if (start < 0) start += s.size();
            if (end < 0) end += s.size();
            if (start < 0) start = 0;
            if (end > (int64_t)s.size()) end = s.size();
            if (start >= end) return GValue::string_val("");
            return GValue::string_val(s.substr(start, end - start));
        });
        define_native("split", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("split expects at least 1 string");
            std::string s = args[0].str();
            std::string delim = args.size() > 1 ? args[1].str() : " ";
            GList res;
            if (delim.empty()) {
                for (char c : s) res.push_back(GValue::string_val(std::string(1, c)));
                return GValue::list_val(res);
            }
            size_t pos = 0;
            while ((pos = s.find(delim)) != std::string::npos) {
                res.push_back(GValue::string_val(s.substr(0, pos)));
                s.erase(0, pos + delim.length());
            }
            res.push_back(GValue::string_val(s));
            return GValue::list_val(res);
        });
        define_native("join", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2 || args[1].type != ValueType::List) throw RuntimeError("join expects delim and list");
            std::string delim = args[0].str();
            auto& l = args[1].list();
            std::string res;
            for (size_t i = 0; i < l.size(); ++i) {
                if (i > 0) res += delim;
                res += gi_str(l[i]);
            }
            return GValue::string_val(res);
        });
        define_native("trim", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::string_val("");
            std::string s = args[0].str();
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
            return GValue::string_val(s);
        });
        define_native("upper", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::string_val("");
            std::string s = args[0].str();
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            return GValue::string_val(s);
        });
        define_native("lower", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::string_val("");
            std::string s = args[0].str();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return GValue::string_val(s);
        });
        define_native("contains", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("contains expects string and substring");
            return GValue::bool_val(args[0].str().find(args[1].str()) != std::string::npos);
        });
        define_native("replace", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 3) throw RuntimeError("replace expects string, old, and new");
            std::string s = args[0].str();
            std::string old_str = args[1].str();
            std::string new_str = args[2].str();
            size_t pos = 0;
            while ((pos = s.find(old_str, pos)) != std::string::npos) {
                s.replace(pos, old_str.length(), new_str);
                pos += new_str.length();
            }
            return GValue::string_val(s);
        });
        define_native("startswith", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("startswith expects string and prefix");
            return GValue::bool_val(args[0].str().rfind(args[1].str(), 0) == 0);
        });
        define_native("endswith", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("endswith expects string and suffix");
            std::string s = args[0].str();
            std::string suffix = args[1].str();
            if (s.length() >= suffix.length()) {
                return GValue::bool_val(s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0);
            }
            return GValue::bool_val(false);
        });
        define_native("chars", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::list_val({});
            std::string s = args[0].str();
            GList l;
            for (char c : s) l.push_back(GValue::string_val(std::string(1, c)));
            return GValue::list_val(l);
        });
        define_native("charcode", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].str().empty()) return GValue::int_val(0);
            return GValue::int_val((unsigned char)args[0].str()[0]);
        });
        define_native("fromcode", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) return GValue::string_val("");
            char c = (char)args[0].as_int();
            return GValue::string_val(std::string(1, c));
        });
        
        // List mutation functions
        define_native("push", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2 || args[0].type != ValueType::List) throw RuntimeError("push expects list and value");
            args[0].list().push_back(args[1]);
            return GValue::null_val();
        });
        define_native("pop", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].type != ValueType::List) throw RuntimeError("pop expects list");
            auto& l = args[0].list();
            if (l.empty()) throw RuntimeError("pop from empty list");
            GValue v = l.back();
            l.pop_back();
            return v;
        });
        define_native("insert", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 3 || args[0].type != ValueType::List) throw RuntimeError("insert expects list, index, and value");
            auto& l = args[0].list();
            int64_t idx = args[1].as_int();
            if (idx < 0) idx += l.size();
            if (idx < 0 || idx > (int64_t)l.size()) throw RuntimeError("insert index out of bounds");
            l.insert(l.begin() + idx, args[2]);
            return GValue::null_val();
        });
        define_native("remove", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2 || args[0].type != ValueType::List) throw RuntimeError("remove expects list and value");
            auto& l = args[0].list();
            auto it = std::find_if(l.begin(), l.end(), [&](const GValue& v) { return v.equals(args[1]); });
            if (it != l.end()) l.erase(it);
            return GValue::null_val();
        });
        define_native("reverse", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].type != ValueType::List) throw RuntimeError("reverse expects list");
            auto& l = args[0].list();
            std::reverse(l.begin(), l.end());
            return GValue::null_val();
        });
        define_native("sort", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].type != ValueType::List) throw RuntimeError("sort expects list");
            auto& l = args[0].list();
            std::sort(l.begin(), l.end(), [](const GValue& a, const GValue& b) {
                if (a.type == ValueType::Int && b.type == ValueType::Int) return a.data.i < b.data.i;
                if ((a.type == ValueType::Int || a.type == ValueType::Float) &&
                    (b.type == ValueType::Int || b.type == ValueType::Float)) {
                    return a.as_number() < b.as_number();
                }
                if (a.type == ValueType::String && b.type == ValueType::String) return a.str() < b.str();
                return false;
            });
            return GValue::null_val();
        });
        define_native("slice", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 3 || args[0].type != ValueType::List) throw RuntimeError("slice expects list, start, end");
            auto& l = args[0].list();
            int64_t start = args[1].as_int();
            int64_t end = args[2].as_int();
            if (start < 0) start += l.size();
            if (end < 0) end += l.size();
            if (start < 0) start = 0;
            if (end > (int64_t)l.size()) end = l.size();
            if (start >= end) return GValue::list_val({});
            GList res(l.begin() + start, l.begin() + end);
            return GValue::list_val(res);
        });
        define_native("sorted", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].type != ValueType::List) throw RuntimeError("sorted expects list");
            GList l = args[0].list();
            std::sort(l.begin(), l.end(), [](const GValue& a, const GValue& b) {
                if (a.type == ValueType::Int && b.type == ValueType::Int) return a.data.i < b.data.i;
                if ((a.type == ValueType::Int || a.type == ValueType::Float) &&
                    (b.type == ValueType::Int || b.type == ValueType::Float)) {
                    return a.as_number() < b.as_number();
                }
                if (a.type == ValueType::String && b.type == ValueType::String) return a.str() < b.str();
                return false;
            });
            return GValue::list_val(l);
        });
        define_native("any", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].type != ValueType::List) throw RuntimeError("any expects list");
            for (auto& v : args[0].list()) {
                if (v.truthy()) return GValue::bool_val(true);
            }
            return GValue::bool_val(false);
        });
        define_native("all", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].type != ValueType::List) throw RuntimeError("all expects list");
            for (auto& v : args[0].list()) {
                if (!v.truthy()) return GValue::bool_val(false);
            }
            return GValue::bool_val(true);
        });
        define_native("sum", [](std::vector<GValue>& args) -> GValue {
            if (args.empty() || args[0].type != ValueType::List) throw RuntimeError("sum expects list");
            double s = 0;
            bool has_float = false;
            for (auto& v : args[0].list()) {
                if (v.type == ValueType::Float) has_float = true;
                s += v.as_number();
            }
            return has_float ? GValue::float_val(s) : GValue::int_val((int64_t)s);
        });
        define_native("getenv", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("getenv expects variable name");
            const char* val = std::getenv(args[0].str().c_str());
            if (!val) return args.size() > 1 ? args[1] : GValue::null_val();
            return GValue::string_val(val);
        });
        define_native("setenv", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("setenv expects variable name and value");
            std::string name = args[0].str();
            std::string val = gi_str(args[1]);
            #ifdef _WIN32
            _putenv_s(name.c_str(), val.c_str());
            #else
            setenv(name.c_str(), val.c_str(), 1);
            #endif
            return GValue::null_val();
        });
        
        // Path helpers
        define_native("abspath", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("abspath expects path");
            return GValue::string_val(fs::absolute(args[0].str()).string());
        });
        define_native("basename", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("basename expects path");
            return GValue::string_val(fs::path(args[0].str()).filename().string());
        });
        define_native("dirname", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("dirname expects path");
            return GValue::string_val(fs::path(args[0].str()).parent_path().string());
        });
        define_native("joinpath", [](std::vector<GValue>& args) -> GValue {
            fs::path p;
            for (auto& a : args) {
                p /= gi_str(a);
            }
            return GValue::string_val(p.string());
        });
        
        setup_math_builtins();
        setup_ffi_builtins();
        setup_memory_builtins();
        setup_fs_builtins();
    }

    void setup_math_builtins() {
        GDict m;
        m["abs"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.abs expects 1 argument");
            if (args[0].type == ValueType::Int) return GValue::int_val(std::abs(args[0].data.i));
            return GValue::float_val(std::abs(args[0].as_number()));
        }, "math.abs");
        m["sqrt"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.sqrt expects 1 argument");
            return GValue::float_val(std::sqrt(args[0].as_number()));
        }, "math.sqrt");
        m["floor"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.floor expects 1 argument");
            return GValue::float_val(std::floor(args[0].as_number()));
        }, "math.floor");
        m["ceil"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.ceil expects 1 argument");
            return GValue::float_val(std::ceil(args[0].as_number()));
        }, "math.ceil");
        m["round"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.round expects 1 argument");
            return GValue::float_val(std::round(args[0].as_number()));
        }, "math.round");
        m["sin"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.sin expects 1 argument");
            return GValue::float_val(std::sin(args[0].as_number()));
        }, "math.sin");
        m["cos"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.cos expects 1 argument");
            return GValue::float_val(std::cos(args[0].as_number()));
        }, "math.cos");
        m["tan"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.tan expects 1 argument");
            return GValue::float_val(std::tan(args[0].as_number()));
        }, "math.tan");
        m["asin"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.asin expects 1 argument");
            return GValue::float_val(std::asin(args[0].as_number()));
        }, "math.asin");
        m["acos"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.acos expects 1 argument");
            return GValue::float_val(std::acos(args[0].as_number()));
        }, "math.acos");
        m["atan"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.atan expects 1 argument");
            return GValue::float_val(std::atan(args[0].as_number()));
        }, "math.atan");
        m["atan2"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("math.atan2 expects 2 arguments");
            return GValue::float_val(std::atan2(args[0].as_number(), args[1].as_number()));
        }, "math.atan2");
        m["log"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.log expects 1 argument");
            return GValue::float_val(std::log(args[0].as_number()));
        }, "math.log");
        m["log2"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.log2 expects 1 argument");
            return GValue::float_val(std::log2(args[0].as_number()));
        }, "math.log2");
        m["log10"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.log10 expects 1 argument");
            return GValue::float_val(std::log10(args[0].as_number()));
        }, "math.log10");
        m["exp"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.exp expects 1 argument");
            return GValue::float_val(std::exp(args[0].as_number()));
        }, "math.exp");
        m["pow"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("math.pow expects 2 arguments");
            return GValue::float_val(std::pow(args[0].as_number(), args[1].as_number()));
        }, "math.pow");
        m["min"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.min expects at least 1 argument");
            GValue res = args[0];
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i].as_number() < res.as_number()) res = args[i];
            }
            return res;
        }, "math.min");
        m["max"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("math.max expects at least 1 argument");
            GValue res = args[0];
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i].as_number() > res.as_number()) res = args[i];
            }
            return res;
        }, "math.max");
        
        m["pi"] = GValue::float_val(3.14159265358979323846);
        m["e"] = GValue::float_val(2.71828182845904523536);
        m["tau"] = GValue::float_val(6.28318530717958647692);
        m["inf"] = GValue::float_val(std::numeric_limits<double>::infinity());
        m["nan"] = GValue::float_val(std::numeric_limits<double>::quiet_NaN());
        
        m["random"] = GValue::native_val([](std::vector<GValue>&) -> GValue {
            return GValue::float_val((double)std::rand() / RAND_MAX);
        }, "math.random");
        m["randint"] = GValue::native_val([](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("math.randint expects min and max arguments");
            int64_t min = args[0].as_int();
            int64_t max = args[1].as_int();
            if (min >= max) return GValue::int_val(min);
            return GValue::int_val(min + (std::rand() % (max - min + 1)));
        }, "math.randint");
        
        globals->define("math", GValue::dict_val(m));
    }

    void setup_ffi_builtins() {
        define_native("loadlib", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("loadlib expects 1 argument");
            std::string name = args[0].str();
            void* handle = nullptr;
            #ifdef _WIN32
            handle = (void*)LoadLibraryA(name.c_str());
            if (!handle) {
                handle = (void*)LoadLibraryA((name + ".dll").c_str());
            }
            #else
            handle = dlopen(name.c_str(), RTLD_LAZY | RTLD_GLOBAL);
            if (!handle) {
                handle = dlopen(("lib" + name + ".so").c_str(), RTLD_LAZY | RTLD_GLOBAL);
            }
            #endif
            if (!handle) {
                throw RuntimeError("loadlib('" + name + "') failed");
            }
            
            auto lib = std::make_shared<GNativeLib>();
            lib->name = name;
            lib->handle = handle;
            return GValue::nativelib_val(lib);
        });
        
        GDict cbox;
        cbox["loadlib"] = globals->get("loadlib");
        globals->define("cbox", GValue::dict_val(cbox));
    }

    void setup_memory_builtins() {
        define_native("malloc", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("malloc expects 1 argument");
            size_t size = (size_t)args[0].as_int();
            void* p = std::malloc(size);
            return GValue::pointer_val(p);
        });
        define_native("calloc", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("calloc expects 2 arguments");
            size_t num = (size_t)args[0].as_int();
            size_t size = (size_t)args[1].as_int();
            void* p = std::calloc(num, size);
            return GValue::pointer_val(p);
        });
        define_native("realloc", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("realloc expects 2 arguments");
            void* ptr = args[0].data.ptr;
            size_t size = (size_t)args[1].as_int();
            void* p = std::realloc(ptr, size);
            return GValue::pointer_val(p);
        });
        define_native("free", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("free expects 1 argument");
            void* p = args[0].data.ptr;
            if (p) std::free(p);
            return GValue::null_val();
        });
        define_native("memset", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 3) throw RuntimeError("memset expects 3 arguments");
            void* ptr = args[0].data.ptr;
            int val = (int)args[1].as_int();
            size_t size = (size_t)args[2].as_int();
            if (ptr) std::memset(ptr, val, size);
            return args[0];
        });
        define_native("memcpy", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 3) throw RuntimeError("memcpy expects 3 arguments");
            void* dest = args[0].data.ptr;
            void* src = args[1].data.ptr;
            size_t size = (size_t)args[2].as_int();
            if (dest && src) std::memcpy(dest, src, size);
            return args[0];
        });
        
        define_native("ptr_read_byte", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("ptr_read_byte expects at least 1 argument");
            char* ptr = (char*)args[0].data.ptr;
            int64_t offset = args.size() > 1 ? args[1].as_int() : 0;
            if (!ptr) throw RuntimeError("Null pointer dereference");
            return GValue::int_val((unsigned char)*(ptr + offset));
        });
        define_native("ptr_write_byte", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("ptr_write_byte expects pointer, [offset], value");
            char* ptr = (char*)args[0].data.ptr;
            int64_t offset = args.size() > 2 ? args[1].as_int() : 0;
            int64_t val = args.size() > 2 ? args[2].as_int() : args[1].as_int();
            if (!ptr) throw RuntimeError("Null pointer dereference");
            *(ptr + offset) = (char)val;
            return GValue::null_val();
        });
        define_native("ptr_read_int", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("ptr_read_int expects pointer, [offset]");
            char* ptr = (char*)args[0].data.ptr;
            int64_t offset = args.size() > 1 ? args[1].as_int() : 0;
            if (!ptr) throw RuntimeError("Null pointer dereference");
            int* target = (int*)(ptr + offset);
            return GValue::int_val(*target);
        });
        define_native("ptr_write_int", [](std::vector<GValue>& args) -> GValue {
            if (args.size() < 2) throw RuntimeError("ptr_write_int expects pointer, [offset], value");
            char* ptr = (char*)args[0].data.ptr;
            int64_t offset = args.size() > 2 ? args[1].as_int() : 0;
            int64_t val = args.size() > 2 ? args[2].as_int() : args[1].as_int();
            if (!ptr) throw RuntimeError("Null pointer dereference");
            int* target = (int*)(ptr + offset);
            *target = (int)val;
            return GValue::null_val();
        });
        
        define_native("ptr_to_int", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("ptr_to_int expects 1 argument");
            return GValue::int_val((uintptr_t)args[0].data.ptr);
        });
        define_native("int_to_ptr", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("int_to_ptr expects 1 argument");
            return GValue::pointer_val((void*)(uintptr_t)args[0].as_int());
        });
        
        globals->define("nullptr", GValue::pointer_val(nullptr));
        globals->define("null_ptr", GValue::pointer_val(nullptr));
    }

    void setup_fs_builtins() {
        define_native("exit", [](std::vector<GValue>& args) -> GValue {
            int code = args.empty() ? 0 : (int)args[0].as_int();
            std::exit(code);
            return GValue::null_val();
        });
        define_native("getcwd", [](std::vector<GValue>&) -> GValue {
            return GValue::string_val(fs::current_path().string());
        });
        define_native("chdir", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("chdir expects 1 argument");
            fs::current_path(args[0].str());
            return GValue::null_val();
        });
        define_native("exists", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("exists expects 1 argument");
            return GValue::bool_val(fs::exists(args[0].str()));
        });
        define_native("isfile", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("isfile expects 1 argument");
            return GValue::bool_val(fs::is_regular_file(args[0].str()));
        });
        define_native("isdir", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("isdir expects 1 argument");
            return GValue::bool_val(fs::is_directory(args[0].str()));
        });
        define_native("mkdir", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("mkdir expects 1 argument");
            fs::create_directories(args[0].str());
            return GValue::null_val();
        });
        define_native("remove_file", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("remove_file expects 1 argument");
            return GValue::bool_val(fs::remove(args[0].str()));
        });
        define_native("sleep", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("sleep expects 1 argument");
            double seconds = args[0].as_number();
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
            return GValue::null_val();
        });
        define_native("time", [](std::vector<GValue>&) -> GValue {
            auto now = std::chrono::system_clock::now();
            auto duration = now.time_since_epoch();
            double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(duration).count();
            return GValue::float_val(seconds);
        });
        
        define_native("file", [](std::vector<GValue>& args) -> GValue {
            if (args.empty()) throw RuntimeError("file expects path and optional mode");
            std::string path = args[0].str();
            std::string mode = args.size() > 1 ? args[1].str() : "r";
            
            auto fs_ptr = std::make_shared<std::fstream>();
            std::ios_base::openmode open_mode = std::ios_base::in;
            if (mode == "w") open_mode = std::ios_base::out | std::ios_base::trunc;
            else if (mode == "a") open_mode = std::ios_base::out | std::ios_base::app;
            else if (mode == "r+") open_mode = std::ios_base::in | std::ios_base::out;
            else if (mode == "w+") open_mode = std::ios_base::in | std::ios_base::out | std::ios_base::trunc;
            
            if (mode.find('b') != std::string::npos) {
                open_mode |= std::ios_base::binary;
            }
            
            fs_ptr->open(path, open_mode);
            if (!fs_ptr->is_open()) {
                throw RuntimeError("Cannot open file: " + path);
            }
            
            GDict f;
            f["close"] = GValue::native_val([fs_ptr](std::vector<GValue>&) -> GValue {
                if (fs_ptr->is_open()) fs_ptr->close();
                return GValue::null_val();
            }, "file.close");
            f["read"] = GValue::native_val([fs_ptr](std::vector<GValue>&) -> GValue {
                if (!fs_ptr->is_open()) throw RuntimeError("file is closed");
                std::string content((std::istreambuf_iterator<char>(*fs_ptr)), std::istreambuf_iterator<char>());
                return GValue::string_val(content);
            }, "file.read");
            f["readline"] = GValue::native_val([fs_ptr](std::vector<GValue>&) -> GValue {
                if (!fs_ptr->is_open()) throw RuntimeError("file is closed");
                std::string line;
                std::getline(*fs_ptr, line);
                return GValue::string_val(line);
            }, "file.readline");
            f["write"] = GValue::native_val([fs_ptr](std::vector<GValue>& args) -> GValue {
                if (args.empty()) throw RuntimeError("file.write expects 1 argument");
                if (!fs_ptr->is_open()) throw RuntimeError("file is closed");
                *fs_ptr << gi_str(args[0]);
                return GValue::null_val();
            }, "file.write");
            
            return GValue::dict_val(f);
        });
    }

    void do_import(std::shared_ptr<ImportStmtNode> n, std::shared_ptr<Env> env) {
        std::string path = n->path;
        std::string resolved = resolve_import_path(path, n->is_pkg, script_dir);
        if (resolved.empty()) {
            throw RuntimeError("Cannot find module '" + path + "'");
        }
        
        if (mod_cache.count(resolved)) {
            std::string alias = n->alias.empty() ? get_basename_without_ext(resolved) : n->alias;
            env->define(alias, GValue::dict_val(mod_cache[resolved]));
            return;
        }
        
        std::ifstream f(resolved);
        if (!f) {
            throw RuntimeError("Cannot open module file: " + resolved);
        }
        std::string src((std::istreambuf_iterator<char>(f)), {});
        std::string mod_dir = get_parent_dir(resolved);
        
        Interpreter mod_interp(mod_dir);
        mod_interp.mod_cache = mod_cache;
        
        auto mod_env = std::make_shared<Env>(mod_interp.globals);
        try {
            Lexer lexer(src);
            Parser parser(lexer.tokenize());
            auto ast = parser.parse();
            mod_interp.run(ast, mod_env);
        } catch (ReturnSignal&) {
        }
        
        GDict exported;
        for (auto& [k, v] : mod_env->vars) {
            exported[k] = v;
        }
        
        mod_cache[resolved] = exported;
        std::string alias = n->alias.empty() ? get_basename_without_ext(resolved) : n->alias;
        env->define(alias, GValue::dict_val(exported));
    }

    GValue eval_binop(std::shared_ptr<BinOpNode> n, std::shared_ptr<Env> env) {
        std::string op = n->op;
        
        if (op == "&&" || op == "and") {
            GValue l = eval(n->left, env);
            if (!l.truthy()) return l;
            return eval(n->right, env);
        }
        if (op == "||" || op == "or") {
            GValue l = eval(n->left, env);
            if (l.truthy()) return l;
            return eval(n->right, env);
        }
        
        GValue l = eval(n->left, env);
        GValue r = eval(n->right, env);
        
        if (op == "==") return GValue::bool_val(l.equals(r));
        if (op == "!=") return GValue::bool_val(!l.equals(r));
        
        if (l.type == ValueType::String && r.type == ValueType::String) {
            if (op == "<")  return GValue::bool_val(l.str() < r.str());
            if (op == ">")  return GValue::bool_val(l.str() > r.str());
            if (op == "<=") return GValue::bool_val(l.str() <= r.str());
            if (op == ">=") return GValue::bool_val(l.str() >= r.str());
        }
        
        if (op == "+" && (l.type == ValueType::String || r.type == ValueType::String)) {
            return GValue::string_val(gi_str(l) + gi_str(r));
        }
        
        if (op == "+" && l.type == ValueType::List && r.type == ValueType::List) {
            GList merged = l.list();
            merged.insert(merged.end(), r.list().begin(), r.list().end());
            return GValue::list_val(merged);
        }
        if (op == "+" && l.type == ValueType::List) {
            GList merged = l.list();
            merged.push_back(r);
            return GValue::list_val(merged);
        }
        
        if (l.type == ValueType::Float || r.type == ValueType::Float) {
            double a = l.as_number();
            double b = r.as_number();
            if (op == "+") return GValue::float_val(a + b);
            if (op == "-") return GValue::float_val(a - b);
            if (op == "*") return GValue::float_val(a * b);
            if (op == "/") {
                if (b == 0) throw RuntimeError("Division by zero");
                return GValue::float_val(a / b);
            }
            if (op == "%") return GValue::float_val(std::fmod(a, b));
            if (op == "**") return GValue::float_val(std::pow(a, b));
            if (op == "==") return GValue::bool_val(a == b);
            if (op == "!=") return GValue::bool_val(a != b);
            if (op == "<")  return GValue::bool_val(a < b);
            if (op == ">")  return GValue::bool_val(a > b);
            if (op == "<=") return GValue::bool_val(a <= b);
            if (op == ">=") return GValue::bool_val(a >= b);
        } else {
            int64_t a = l.as_int();
            int64_t b = r.as_int();
            if (op == "+") return GValue::int_val(a + b);
            if (op == "-") return GValue::int_val(a - b);
            if (op == "*") return GValue::int_val(a * b);
            if (op == "/") {
                if (b == 0) throw RuntimeError("Division by zero");
                return GValue::int_val(a / b);
            }
            if (op == "%") {
                if (b == 0) throw RuntimeError("Division by zero");
                return GValue::int_val(a % b);
            }
            if (op == "**") return GValue::int_val((int64_t)std::pow(a, b));
            if (op == "==") return GValue::bool_val(l.equals(r));
            if (op == "!=") return GValue::bool_val(!l.equals(r));
            if (op == "<")  return GValue::bool_val(a < b);
            if (op == ">")  return GValue::bool_val(a > b);
            if (op == "<=") return GValue::bool_val(a <= b);
            if (op == ">=") return GValue::bool_val(a >= b);
            
            if (op == "&") return GValue::int_val(a & b);
            if (op == "|") return GValue::int_val(a | b);
            if (op == "^") return GValue::int_val(a ^ b);
            if (op == "<<") return GValue::int_val(a << b);
            if (op == ">>") return GValue::int_val(a >> b);
        }
        
        throw RuntimeError("Unsupported binary operator '" + op + "' on types " + l.type_name() + " and " + r.type_name());
    }

    GValue eval_unaryop(std::shared_ptr<UnaryOpNode> n, std::shared_ptr<Env> env) {
        std::string op = n->op;
        GValue val = eval(n->expr, env);
        if (op == "-" || op == "neg") {
            if (val.type == ValueType::Int) return GValue::int_val(-val.data.i);
            return GValue::float_val(-val.as_number());
        }
        if (op == "!" || op == "not") {
            return GValue::bool_val(!val.truthy());
        }
        if (op == "~") {
            return GValue::int_val(~val.as_int());
        }
        throw RuntimeError("Unsupported unary operator '" + op + "'");
    }

    GValue eval_index(std::shared_ptr<IndexNode> n, std::shared_ptr<Env> env) {
        GValue obj = eval(n->obj, env);
        GValue idx = eval(n->idx, env);
        
        if (obj.type == ValueType::List) {
            int64_t i = idx.as_int();
            auto& l = obj.list();
            if (i < 0) i += l.size();
            if (i < 0 || i >= (int64_t)l.size()) {
                throw RuntimeError("Index out of bounds");
            }
            return l[i];
        }
        
        if (obj.type == ValueType::Dict) {
            std::string key = gi_str(idx);
            auto& d = obj.dict();
            auto it = d.find(key);
            if (it == d.end()) {
                throw RuntimeError("Key error: '" + key + "'");
            }
            return it->second;
        }
        
        if (obj.type == ValueType::String) {
            int64_t i = idx.as_int();
            auto& s = obj.str();
            if (i < 0) i += s.size();
            if (i < 0 || i >= (int64_t)s.size()) {
                throw RuntimeError("Index out of bounds");
            }
            return GValue::string_val(std::string(1, s[i]));
        }
        
        throw RuntimeError("Type " + obj.type_name() + " is not indexable");
    }
};
