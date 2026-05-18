#pragma once
#include "value.h"
#include <memory>
#include <vector>
#include <string>

// ═══════════════════════════════════════════════════════════════════════════
//  AST NODE TYPES
// ═══════════════════════════════════════════════════════════════════════════
enum class NodeType : uint8_t {
    Block, LetStmt, SetStmt, AssignStmt, SetAttr, SetIndex,
    IfStmt, WhileStmt, ForStmt, FuncDef, ReturnStmt,
    PassStmt, ContinueStmt, BreakStmt,
    WithStmt, ClassDef, ImportStmt, ExprStmt,
    StructDef, EnumDef,
    // Expressions
    NumberLit, StringLit, BoolLit, NullLit, Ident,
    BinOp, UnaryOp, Call, GetAttr, Index,
    ListLit, DictLit, NewExpr, LambdaExpr,
    SizeofExpr, CastExpr
};

using NodePtr = std::shared_ptr<ASTNode>;

struct ASTNode {
    NodeType ntype;
    int line = 0;

    virtual ~ASTNode() = default;
protected:
    ASTNode(NodeType t, int ln = 0) : ntype(t), line(ln) {}
};

// ── Statements ───────────────────────────────────────────────────────────
struct BlockNode : ASTNode {
    std::vector<NodePtr> stmts;
    BlockNode(std::vector<NodePtr> s) : ASTNode(NodeType::Block), stmts(std::move(s)) {}
};

struct LetStmtNode : ASTNode {
    std::string name;
    NodePtr value;
    LetStmtNode(const std::string& n, NodePtr v, int ln) : ASTNode(NodeType::LetStmt, ln), name(n), value(std::move(v)) {}
};

struct SetStmtNode : ASTNode {
    std::string target;
    NodePtr value;
    SetStmtNode(const std::string& t, NodePtr v, int ln) : ASTNode(NodeType::SetStmt, ln), target(t), value(std::move(v)) {}
};

struct AssignStmtNode : ASTNode {
    std::string target;
    std::string op; // "=", "+=", "-=", "*=", "/="
    NodePtr value;
    AssignStmtNode(const std::string& t, const std::string& o, NodePtr v, int ln)
        : ASTNode(NodeType::AssignStmt, ln), target(t), op(o), value(std::move(v)) {}
};

struct SetAttrNode : ASTNode {
    NodePtr obj;
    std::string attr;
    NodePtr value;
    SetAttrNode(NodePtr o, const std::string& a, NodePtr v) : ASTNode(NodeType::SetAttr), obj(std::move(o)), attr(a), value(std::move(v)) {}
};

struct SetIndexNode : ASTNode {
    NodePtr obj, idx, value;
    SetIndexNode(NodePtr o, NodePtr i, NodePtr v) : ASTNode(NodeType::SetIndex), obj(std::move(o)), idx(std::move(i)), value(std::move(v)) {}
};

struct IfBranch {
    NodePtr cond;
    NodePtr body;
};

struct IfStmtNode : ASTNode {
    std::vector<IfBranch> branches;
    NodePtr else_block; // may be null
    IfStmtNode(std::vector<IfBranch> br, NodePtr eb) : ASTNode(NodeType::IfStmt), branches(std::move(br)), else_block(std::move(eb)) {}
};

struct WhileStmtNode : ASTNode {
    NodePtr cond, body;
    WhileStmtNode(NodePtr c, NodePtr b) : ASTNode(NodeType::WhileStmt), cond(std::move(c)), body(std::move(b)) {}
};

struct ForStmtNode : ASTNode {
    std::string var;
    NodePtr iterable, body;
    ForStmtNode(const std::string& v, NodePtr it, NodePtr b) : ASTNode(NodeType::ForStmt), var(v), iterable(std::move(it)), body(std::move(b)) {}
};

struct FuncDefNode : ASTNode {
    std::string name;
    std::vector<std::string> params;
    NodePtr body;
    FuncDefNode(const std::string& n, std::vector<std::string> p, NodePtr b, int ln)
        : ASTNode(NodeType::FuncDef, ln), name(n), params(std::move(p)), body(std::move(b)) {}
};

struct ReturnStmtNode : ASTNode {
    NodePtr value; // may be null
    ReturnStmtNode(NodePtr v, int ln) : ASTNode(NodeType::ReturnStmt, ln), value(std::move(v)) {}
};

struct PassStmtNode : ASTNode { PassStmtNode() : ASTNode(NodeType::PassStmt) {} };
struct ContinueStmtNode : ASTNode { ContinueStmtNode() : ASTNode(NodeType::ContinueStmt) {} };
struct BreakStmtNode : ASTNode { BreakStmtNode() : ASTNode(NodeType::BreakStmt) {} };

struct WithStmtNode : ASTNode {
    NodePtr call;
    std::string alias;
    NodePtr body;
    WithStmtNode(NodePtr c, const std::string& a, NodePtr b) : ASTNode(NodeType::WithStmt), call(std::move(c)), alias(a), body(std::move(b)) {}
};

struct ClassDefNode : ASTNode {
    std::string name;
    std::string base; // empty if no base
    NodePtr body;
    ClassDefNode(const std::string& n, const std::string& b, NodePtr bd) : ASTNode(NodeType::ClassDef), name(n), base(b), body(std::move(bd)) {}
};

struct ImportStmtNode : ASTNode {
    std::string path;
    std::string alias; // empty if no alias
    bool is_pkg;
    ImportStmtNode(const std::string& p, const std::string& a, bool pkg) : ASTNode(NodeType::ImportStmt), path(p), alias(a), is_pkg(pkg) {}
};

struct ExprStmtNode : ASTNode {
    NodePtr expr;
    ExprStmtNode(NodePtr e) : ASTNode(NodeType::ExprStmt), expr(std::move(e)) {}
};

struct StructDefNode : ASTNode {
    std::string name;
    std::vector<std::string> fields;
    StructDefNode(const std::string& n, std::vector<std::string> f) : ASTNode(NodeType::StructDef), name(n), fields(std::move(f)) {}
};

struct EnumDefNode : ASTNode {
    std::string name;
    std::vector<std::string> values;
    EnumDefNode(const std::string& n, std::vector<std::string> v) : ASTNode(NodeType::EnumDef), name(n), values(std::move(v)) {}
};

// ── Expressions ──────────────────────────────────────────────────────────
struct NumberLitNode : ASTNode {
    double value;
    bool is_int;
    NumberLitNode(double v, bool isint) : ASTNode(NodeType::NumberLit), value(v), is_int(isint) {}
};

struct StringLitNode : ASTNode {
    std::string value;
    StringLitNode(const std::string& v) : ASTNode(NodeType::StringLit), value(v) {}
};

struct BoolLitNode : ASTNode {
    bool value;
    BoolLitNode(bool v) : ASTNode(NodeType::BoolLit), value(v) {}
};

struct NullLitNode : ASTNode {
    NullLitNode() : ASTNode(NodeType::NullLit) {}
};

struct IdentNode : ASTNode {
    std::string name;
    IdentNode(const std::string& n, int ln) : ASTNode(NodeType::Ident, ln), name(n) {}
};

struct BinOpNode : ASTNode {
    std::string op;
    NodePtr left, right;
    BinOpNode(const std::string& o, NodePtr l, NodePtr r) : ASTNode(NodeType::BinOp), op(o), left(std::move(l)), right(std::move(r)) {}
};

struct UnaryOpNode : ASTNode {
    std::string op;
    NodePtr expr;
    UnaryOpNode(const std::string& o, NodePtr e) : ASTNode(NodeType::UnaryOp), op(o), expr(std::move(e)) {}
};

struct CallNode : ASTNode {
    NodePtr callee;
    std::vector<NodePtr> args;
    CallNode(NodePtr c, std::vector<NodePtr> a, int ln) : ASTNode(NodeType::Call, ln), callee(std::move(c)), args(std::move(a)) {}
};

struct GetAttrNode : ASTNode {
    NodePtr obj;
    std::string attr;
    GetAttrNode(NodePtr o, const std::string& a, int ln) : ASTNode(NodeType::GetAttr, ln), obj(std::move(o)), attr(a) {}
};

struct IndexNode : ASTNode {
    NodePtr obj, idx;
    IndexNode(NodePtr o, NodePtr i, int ln) : ASTNode(NodeType::Index, ln), obj(std::move(o)), idx(std::move(i)) {}
};

struct ListLitNode : ASTNode {
    std::vector<NodePtr> items;
    ListLitNode(std::vector<NodePtr> i) : ASTNode(NodeType::ListLit), items(std::move(i)) {}
};

struct DictLitNode : ASTNode {
    std::vector<std::pair<NodePtr, NodePtr>> pairs;
    DictLitNode(std::vector<std::pair<NodePtr, NodePtr>> p) : ASTNode(NodeType::DictLit), pairs(std::move(p)) {}
};

struct NewExprNode : ASTNode {
    std::string cls;
    std::vector<NodePtr> args;
    NewExprNode(const std::string& c, std::vector<NodePtr> a, int ln) : ASTNode(NodeType::NewExpr, ln), cls(c), args(std::move(a)) {}
};

struct LambdaExprNode : ASTNode {
    std::vector<std::string> params;
    NodePtr body;
    LambdaExprNode(std::vector<std::string> p, NodePtr b) : ASTNode(NodeType::LambdaExpr), params(std::move(p)), body(std::move(b)) {}
};

struct SizeofExprNode : ASTNode {
    std::string type_name;
    SizeofExprNode(const std::string& t) : ASTNode(NodeType::SizeofExpr), type_name(t) {}
};

struct CastExprNode : ASTNode {
    std::string target_type;
    NodePtr expr;
    CastExprNode(const std::string& t, NodePtr e) : ASTNode(NodeType::CastExpr), target_type(t), expr(std::move(e)) {}
};
