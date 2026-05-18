#pragma once
#include "lexer.h"
#include "ast.h"

// ═══════════════════════════════════════════════════════════════════════════
//  PARSER — Recursive Descent
// ═══════════════════════════════════════════════════════════════════════════
class Parser {
    std::vector<Token> tokens;
    size_t pos = 0;

    const Token& peek(int offset = 0) const {
        size_t i = pos + offset;
        return (i < tokens.size()) ? tokens[i] : tokens.back();
    }
    Token advance() { return tokens[pos++]; }
    bool check(TokenKind k) const { return peek().kind == k; }
    bool check2(TokenKind a, TokenKind b) const { return peek().kind == a || peek().kind == b; }
    bool match(TokenKind k) { if (check(k)) { advance(); return true; } return false; }
    Token expect(TokenKind k, const std::string& msg = "") {
        if (check(k)) return advance();
        auto& t = peek();
        std::string err = msg.empty() ?
            "Expected token, got '" + t.sval + "' at line " + std::to_string(t.line) : msg;
        throw ParseError(err + " at line " + std::to_string(t.line));
    }

public:
    Parser(std::vector<Token> toks) : tokens(std::move(toks)) {}

    NodePtr parse() {
        std::vector<NodePtr> stmts;
        while (!check(TokenKind::EOF_TOK)) stmts.push_back(parse_stmt());
        return std::make_shared<BlockNode>(std::move(stmts));
    }

private:
    NodePtr parse_block() {
        expect(TokenKind::LBRACE);
        std::vector<NodePtr> stmts;
        while (!check(TokenKind::RBRACE) && !check(TokenKind::EOF_TOK))
            stmts.push_back(parse_stmt());
        expect(TokenKind::RBRACE);
        return std::make_shared<BlockNode>(std::move(stmts));
    }

    NodePtr parse_stmt() {
        match(TokenKind::SEMI);
        auto k = peek().kind;
        if (k == TokenKind::LET)      return parse_let();
        if (k == TokenKind::SET)      return parse_set();
        if (k == TokenKind::IF)       return parse_if();
        if (k == TokenKind::WHILE)    return parse_while();
        if (k == TokenKind::FOR)      return parse_for();
        if (k == TokenKind::FUNC)     return parse_func();
        if (k == TokenKind::RETURN)   return parse_return();
        if (k == TokenKind::PASS)     { advance(); return std::make_shared<PassStmtNode>(); }
        if (k == TokenKind::CONTINUE) { advance(); return std::make_shared<ContinueStmtNode>(); }
        if (k == TokenKind::BREAK)    { advance(); return std::make_shared<BreakStmtNode>(); }
        if (k == TokenKind::WITH)     return parse_with();
        if (k == TokenKind::CLASS)    return parse_class();
        if (k == TokenKind::IMPORT)   return parse_import();
        if (k == TokenKind::STRUCT)   return parse_struct();
        if (k == TokenKind::ENUM)     return parse_enum();
        return parse_assign_or_expr();
    }

    NodePtr parse_let() {
        int ln = peek().line; expect(TokenKind::LET);
        std::string name = expect(TokenKind::IDENT).sval;
        expect(TokenKind::ASSIGN);
        auto val = parse_expr();
        match(TokenKind::SEMI);
        return std::make_shared<LetStmtNode>(name, std::move(val), ln);
    }

    NodePtr parse_set() {
        int ln = peek().line; expect(TokenKind::SET);
        std::string name = expect(TokenKind::IDENT).sval;
        expect(TokenKind::ASSIGN);
        auto val = parse_expr();
        match(TokenKind::SEMI);
        return std::make_shared<SetStmtNode>(name, std::move(val), ln);
    }

    NodePtr parse_assign_or_expr() {
        auto expr = parse_expr();
        auto k = peek().kind;
        std::string op;
        if (k == TokenKind::ASSIGN)   op = "=";
        else if (k == TokenKind::PLUSEQ)  op = "+=";
        else if (k == TokenKind::MINUSEQ) op = "-=";
        else if (k == TokenKind::STAREQ)  op = "*=";
        else if (k == TokenKind::SLASHEQ) op = "/=";
        if (!op.empty()) {
            advance();
            auto val = parse_expr();
            match(TokenKind::SEMI);
            if (expr->ntype == NodeType::Ident) {
                auto id = std::static_pointer_cast<IdentNode>(expr);
                return std::make_shared<AssignStmtNode>(id->name, op, std::move(val), id->line);
            }
            if (expr->ntype == NodeType::GetAttr) {
                auto ga = std::static_pointer_cast<GetAttrNode>(expr);
                return std::make_shared<SetAttrNode>(std::move(ga->obj), ga->attr, std::move(val));
            }
            if (expr->ntype == NodeType::Index) {
                auto idx = std::static_pointer_cast<IndexNode>(expr);
                return std::make_shared<SetIndexNode>(std::move(idx->obj), std::move(idx->idx), std::move(val));
            }
            throw ParseError("Invalid assignment target");
        }
        match(TokenKind::SEMI);
        return std::make_shared<ExprStmtNode>(std::move(expr));
    }

    NodePtr parse_if() {
        expect(TokenKind::IF); expect(TokenKind::LPAREN);
        auto cond = parse_expr(); expect(TokenKind::RPAREN);
        auto body = parse_block();
        std::vector<IfBranch> branches;
        branches.push_back({std::move(cond), std::move(body)});
        while (check(TokenKind::ELIF)) {
            advance(); expect(TokenKind::LPAREN);
            auto c2 = parse_expr(); expect(TokenKind::RPAREN);
            auto b2 = parse_block();
            branches.push_back({std::move(c2), std::move(b2)});
        }
        NodePtr else_block;
        if (match(TokenKind::ELSE)) else_block = parse_block();
        return std::make_shared<IfStmtNode>(std::move(branches), std::move(else_block));
    }

    NodePtr parse_while() {
        expect(TokenKind::WHILE); expect(TokenKind::LPAREN);
        auto cond = parse_expr(); expect(TokenKind::RPAREN);
        return std::make_shared<WhileStmtNode>(std::move(cond), parse_block());
    }

    NodePtr parse_for() {
        expect(TokenKind::FOR); expect(TokenKind::LPAREN);
        std::string var = expect(TokenKind::IDENT).sval;
        expect(TokenKind::IN);
        auto iterable = parse_expr(); expect(TokenKind::RPAREN);
        return std::make_shared<ForStmtNode>(var, std::move(iterable), parse_block());
    }

    NodePtr parse_func() {
        int ln = peek().line; expect(TokenKind::FUNC);
        std::string name = expect(TokenKind::IDENT).sval;
        expect(TokenKind::LPAREN);
        std::vector<std::string> params;
        while (!check(TokenKind::RPAREN) && !check(TokenKind::EOF_TOK)) {
            params.push_back(expect(TokenKind::IDENT).sval);
            if (!match(TokenKind::COMMA)) break;
        }
        expect(TokenKind::RPAREN);
        return std::make_shared<FuncDefNode>(name, std::move(params), parse_block(), ln);
    }

    NodePtr parse_return() {
        int ln = peek().line; expect(TokenKind::RETURN);
        NodePtr val;
        if (!check(TokenKind::SEMI) && !check(TokenKind::RBRACE) && !check(TokenKind::EOF_TOK))
            val = parse_expr();
        match(TokenKind::SEMI);
        return std::make_shared<ReturnStmtNode>(std::move(val), ln);
    }

    NodePtr parse_with() {
        expect(TokenKind::WITH); expect(TokenKind::LPAREN);
        auto call = parse_expr(); expect(TokenKind::RPAREN);
        expect(TokenKind::AS);
        std::string alias = expect(TokenKind::IDENT).sval;
        return std::make_shared<WithStmtNode>(std::move(call), alias, parse_block());
    }

    NodePtr parse_class() {
        expect(TokenKind::CLASS);
        std::string name = expect(TokenKind::IDENT).sval;
        std::string base;
        if (match(TokenKind::LPAREN)) {
            base = expect(TokenKind::IDENT).sval;
            expect(TokenKind::RPAREN);
        }
        return std::make_shared<ClassDefNode>(name, base, parse_block());
    }

    NodePtr parse_import() {
        expect(TokenKind::IMPORT);
        std::string path; bool is_pkg = false; std::string alias;
        if (match(TokenKind::LT)) {
            path = expect(TokenKind::STRING).sval;
            expect(TokenKind::GT);
            is_pkg = true;
        } else {
            path = expect(TokenKind::IDENT).sval;
            while (match(TokenKind::DOT)) {
                path += "." + expect(TokenKind::IDENT).sval;
            }
        }
        if (match(TokenKind::AS)) alias = expect(TokenKind::IDENT).sval;
        match(TokenKind::SEMI);
        return std::make_shared<ImportStmtNode>(path, alias, is_pkg);
    }

    NodePtr parse_struct() {
        expect(TokenKind::STRUCT);
        std::string name = expect(TokenKind::IDENT).sval;
        expect(TokenKind::LBRACE);
        std::vector<std::string> fields;
        while (!check(TokenKind::RBRACE) && !check(TokenKind::EOF_TOK)) {
            fields.push_back(expect(TokenKind::IDENT).sval);
            if (!match(TokenKind::COMMA)) break;
        }
        expect(TokenKind::RBRACE);
        return std::make_shared<StructDefNode>(name, std::move(fields));
    }

    NodePtr parse_enum() {
        expect(TokenKind::ENUM);
        std::string name = expect(TokenKind::IDENT).sval;
        expect(TokenKind::LBRACE);
        std::vector<std::string> values;
        while (!check(TokenKind::RBRACE) && !check(TokenKind::EOF_TOK)) {
            values.push_back(expect(TokenKind::IDENT).sval);
            if (!match(TokenKind::COMMA)) break;
        }
        expect(TokenKind::RBRACE);
        return std::make_shared<EnumDefNode>(name, std::move(values));
    }

    // ── Expression Parsing (Pratt-style precedence climbing) ─────────────
    NodePtr parse_expr()      { return parse_or(); }

    NodePtr parse_or() {
        auto l = parse_and_expr();
        while (check(TokenKind::OR)) { advance(); l = std::make_shared<BinOpNode>("||", std::move(l), parse_and_expr()); }
        return l;
    }
    NodePtr parse_and_expr() {
        auto l = parse_bitwise_or();
        while (check(TokenKind::AND)) { advance(); l = std::make_shared<BinOpNode>("&&", std::move(l), parse_bitwise_or()); }
        return l;
    }
    NodePtr parse_bitwise_or() {
        auto l = parse_bitwise_xor();
        while (check(TokenKind::PIPE)) { advance(); l = std::make_shared<BinOpNode>("|", std::move(l), parse_bitwise_xor()); }
        return l;
    }
    NodePtr parse_bitwise_xor() {
        auto l = parse_bitwise_and();
        while (check(TokenKind::CARET)) { advance(); l = std::make_shared<BinOpNode>("^", std::move(l), parse_bitwise_and()); }
        return l;
    }
    NodePtr parse_bitwise_and() {
        auto l = parse_equality();
        while (check(TokenKind::AMP)) { advance(); l = std::make_shared<BinOpNode>("&", std::move(l), parse_equality()); }
        return l;
    }
    NodePtr parse_equality() {
        auto l = parse_comparison();
        while (check(TokenKind::EQ) || check(TokenKind::NEQ)) {
            std::string op = (advance().kind == TokenKind::EQ) ? "==" : "!=";
            l = std::make_shared<BinOpNode>(op, std::move(l), parse_comparison());
        }
        return l;
    }
    NodePtr parse_comparison() {
        auto l = parse_shift();
        while (check(TokenKind::LT) || check(TokenKind::GT) || check(TokenKind::LTE) || check(TokenKind::GTE)) {
            auto t = advance();
            std::string op;
            if (t.kind == TokenKind::LT) op = "<";
            else if (t.kind == TokenKind::GT) op = ">";
            else if (t.kind == TokenKind::LTE) op = "<=";
            else op = ">=";
            l = std::make_shared<BinOpNode>(op, std::move(l), parse_shift());
        }
        return l;
    }
    NodePtr parse_shift() {
        auto l = parse_add();
        while (check(TokenKind::LSHIFT) || check(TokenKind::RSHIFT)) {
            std::string op = (advance().kind == TokenKind::LSHIFT) ? "<<" : ">>";
            l = std::make_shared<BinOpNode>(op, std::move(l), parse_add());
        }
        return l;
    }
    NodePtr parse_add() {
        auto l = parse_mul();
        while (check(TokenKind::PLUS) || check(TokenKind::MINUS)) {
            std::string op = (advance().kind == TokenKind::PLUS) ? "+" : "-";
            l = std::make_shared<BinOpNode>(op, std::move(l), parse_mul());
        }
        return l;
    }
    NodePtr parse_mul() {
        auto l = parse_power();
        while (check(TokenKind::STAR) || check(TokenKind::SLASH) || check(TokenKind::PERCENT)) {
            auto t = advance();
            std::string op;
            if (t.kind == TokenKind::STAR) op = "*";
            else if (t.kind == TokenKind::SLASH) op = "/";
            else op = "%";
            l = std::make_shared<BinOpNode>(op, std::move(l), parse_power());
        }
        return l;
    }
    NodePtr parse_power() {
        auto l = parse_unary();
        if (check(TokenKind::STARSTAR)) {
            advance();
            return std::make_shared<BinOpNode>("**", std::move(l), parse_power());
        }
        return l;
    }
    NodePtr parse_unary() {
        if (check(TokenKind::NOT)) { advance(); return std::make_shared<UnaryOpNode>("!", parse_unary()); }
        if (check(TokenKind::MINUS)) { advance(); return std::make_shared<UnaryOpNode>("-", parse_unary()); }
        if (check(TokenKind::TILDE)) { advance(); return std::make_shared<UnaryOpNode>("~", parse_unary()); }
        return parse_postfix();
    }
    NodePtr parse_postfix() {
        auto expr = parse_primary();
        while (true) {
            if (check(TokenKind::DOT)) {
                int ln = peek().line; advance();
                std::string attr = expect(TokenKind::IDENT).sval;
                if (check(TokenKind::LPAREN)) {
                    auto args = parse_arglist();
                    auto ga = std::make_shared<GetAttrNode>(std::move(expr), attr, ln);
                    expr = std::make_shared<CallNode>(std::move(ga), std::move(args), ln);
                } else {
                    expr = std::make_shared<GetAttrNode>(std::move(expr), attr, ln);
                }
            } else if (check(TokenKind::LPAREN)) {
                int ln = peek().line;
                auto args = parse_arglist();
                expr = std::make_shared<CallNode>(std::move(expr), std::move(args), ln);
            } else if (check(TokenKind::LBRACK)) {
                int ln = peek().line; advance();
                auto idx = parse_expr(); expect(TokenKind::RBRACK);
                expr = std::make_shared<IndexNode>(std::move(expr), std::move(idx), ln);
            } else break;
        }
        return expr;
    }
    std::vector<NodePtr> parse_arglist() {
        expect(TokenKind::LPAREN);
        std::vector<NodePtr> args;
        while (!check(TokenKind::RPAREN) && !check(TokenKind::EOF_TOK)) {
            args.push_back(parse_expr());
            if (!match(TokenKind::COMMA)) break;
        }
        expect(TokenKind::RPAREN);
        return args;
    }
    NodePtr parse_primary() {
        auto& t = peek();
        if (t.kind == TokenKind::NUMBER) { auto tok = advance(); return std::make_shared<NumberLitNode>(tok.nval, tok.is_int); }
        if (t.kind == TokenKind::STRING) { auto tok = advance(); return std::make_shared<StringLitNode>(tok.sval); }
        if (t.kind == TokenKind::BOOL_TRUE)  { advance(); return std::make_shared<BoolLitNode>(true); }
        if (t.kind == TokenKind::BOOL_FALSE) { advance(); return std::make_shared<BoolLitNode>(false); }
        if (t.kind == TokenKind::NULL_TOK)   { advance(); return std::make_shared<NullLitNode>(); }
        if (t.kind == TokenKind::IDENT) { auto tok = advance(); return std::make_shared<IdentNode>(tok.sval, tok.line); }
        if (t.kind == TokenKind::NEW) {
            int ln = t.line; advance();
            std::string cls = expect(TokenKind::IDENT).sval;
            auto args = parse_arglist();
            return std::make_shared<NewExprNode>(cls, std::move(args), ln);
        }
        if (t.kind == TokenKind::SIZEOF) {
            advance(); expect(TokenKind::LPAREN);
            std::string tname = expect(TokenKind::IDENT).sval;
            expect(TokenKind::RPAREN);
            return std::make_shared<SizeofExprNode>(tname);
        }
        if (t.kind == TokenKind::CAST) {
            advance(); expect(TokenKind::LPAREN);
            std::string tname = expect(TokenKind::IDENT).sval;
            expect(TokenKind::COMMA);
            auto expr = parse_expr();
            expect(TokenKind::RPAREN);
            return std::make_shared<CastExprNode>(tname, std::move(expr));
        }
        if (t.kind == TokenKind::LPAREN) {
            advance(); auto e = parse_expr(); expect(TokenKind::RPAREN); return e;
        }
        if (t.kind == TokenKind::LBRACK) {
            advance();
            std::vector<NodePtr> items;
            while (!check(TokenKind::RBRACK) && !check(TokenKind::EOF_TOK)) {
                items.push_back(parse_expr());
                if (!match(TokenKind::COMMA)) break;
            }
            expect(TokenKind::RBRACK);
            return std::make_shared<ListLitNode>(std::move(items));
        }
        if (t.kind == TokenKind::LBRACE) {
            advance();
            std::vector<std::pair<NodePtr, NodePtr>> pairs;
            while (!check(TokenKind::RBRACE) && !check(TokenKind::EOF_TOK)) {
                auto k = parse_expr(); expect(TokenKind::COLON); auto v = parse_expr();
                pairs.push_back({std::move(k), std::move(v)});
                if (!match(TokenKind::COMMA)) break;
            }
            expect(TokenKind::RBRACE);
            return std::make_shared<DictLitNode>(std::move(pairs));
        }
        if (t.kind == TokenKind::FUNC) {
            advance(); expect(TokenKind::LPAREN);
            std::vector<std::string> params;
            while (!check(TokenKind::RPAREN) && !check(TokenKind::EOF_TOK)) {
                params.push_back(expect(TokenKind::IDENT).sval);
                if (!match(TokenKind::COMMA)) break;
            }
            expect(TokenKind::RPAREN);
            NodePtr body;
            if (check(TokenKind::ARROW)) { advance(); body = parse_expr(); }
            else body = parse_block();
            return std::make_shared<LambdaExprNode>(std::move(params), std::move(body));
        }
        throw ParseError("Unexpected token '" + t.sval + "' at line " + std::to_string(t.line));
    }
};
