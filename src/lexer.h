#pragma once
#include "value.h"

// ═══════════════════════════════════════════════════════════════════════════
//  TOKEN TYPES
// ═══════════════════════════════════════════════════════════════════════════
enum class TokenKind : uint8_t {
    // Literals
    NUMBER, STRING, BOOL_TRUE, BOOL_FALSE, NULL_TOK,
    // Identifier
    IDENT,
    // Keywords
    LET, SET, IF, ELSE, ELIF, WHILE, FOR, IN,
    FUNC, RETURN, PASS, CONTINUE, BREAK,
    WITH, AS, CLASS, NEW, IMPORT,
    STRUCT, ENUM, SIZEOF, CAST,
    // Operators
    PLUS, MINUS, STAR, SLASH, PERCENT, STARSTAR,
    ASSIGN, PLUSEQ, MINUSEQ, STAREQ, SLASHEQ,
    EQ, NEQ, LT, GT, LTE, GTE,
    AND, OR, NOT,
    AMP, PIPE, CARET, TILDE, LSHIFT, RSHIFT,
    ARROW,
    // Delimiters
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACK, RBRACK,
    COMMA, DOT, SEMI, COLON,
    // End
    EOF_TOK
};

struct Token {
    TokenKind kind;
    std::string sval;   // string value (for STRING, IDENT)
    double nval = 0;    // numeric value
    bool is_int = true; // whether nval is integer
    int line = 1;

    Token() : kind(TokenKind::EOF_TOK) {}
    Token(TokenKind k, int ln) : kind(k), line(ln) {}
    Token(TokenKind k, const std::string& s, int ln) : kind(k), sval(s), line(ln) {}
    Token(TokenKind k, double n, bool isint, int ln) : kind(k), nval(n), is_int(isint), line(ln) {}
};

// ═══════════════════════════════════════════════════════════════════════════
//  LEXER
// ═══════════════════════════════════════════════════════════════════════════
class Lexer {
    std::string src;
    size_t pos = 0;
    int line = 1;

    char peek(int offset = 0) const {
        size_t i = pos + offset;
        return (i < src.size()) ? src[i] : '\0';
    }
    char advance() {
        char ch = src[pos++];
        if (ch == '\n') line++;
        return ch;
    }
    bool match(char ch) {
        if (pos < src.size() && src[pos] == ch) { pos++; return true; }
        return false;
    }
    void skip() {
        while (pos < src.size()) {
            char ch = peek();
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                advance();
            } else if (ch == '/' && peek(1) == '/') {
                while (pos < src.size() && peek() != '\n') advance();
            } else if (ch == '/' && peek(1) == '*') {
                advance(); advance();
                while (pos < src.size()) {
                    if (peek() == '*' && peek(1) == '/') { advance(); advance(); break; }
                    advance();
                }
            } else break;
        }
    }
    std::string read_string(char quote) {
        advance(); // consume opening quote
        std::string buf;
        while (pos < src.size()) {
            char ch = peek();
            if (ch == '\\') {
                advance(); char esc = advance();
                switch (esc) {
                    case 'n': buf += '\n'; break;
                    case 't': buf += '\t'; break;
                    case 'r': buf += '\r'; break;
                    case '\\': buf += '\\'; break;
                    case '"': buf += '"'; break;
                    case '\'': buf += '\''; break;
                    case '0': buf += '\0'; break;
                    default: buf += esc; break;
                }
            } else if (ch == quote) { advance(); break; }
            else buf += advance();
        }
        return buf;
    }

public:
    Lexer(const std::string& source) : src(source) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (true) {
            skip();
            if (pos >= src.size()) { tokens.emplace_back(TokenKind::EOF_TOK, line); break; }
            int ln = line;
            char ch = peek();

            // Strings
            if (ch == '"' || ch == '\'') {
                tokens.emplace_back(TokenKind::STRING, read_string(ch), ln);
                continue;
            }
            // Numbers (including 0x hex, 0b binary)
            if (ch >= '0' && ch <= '9' || (ch == '.' && peek(1) >= '0' && peek(1) <= '9')) {
                std::string buf;
                bool is_float = false;
                if (ch == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                    advance(); advance(); // consume 0x
                    while (pos < src.size() && isxdigit(peek())) buf += advance();
                    tokens.emplace_back(TokenKind::NUMBER, (double)std::stoll(buf, nullptr, 16), true, ln);
                } else if (ch == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
                    advance(); advance(); // consume 0b
                    while (pos < src.size() && (peek() == '0' || peek() == '1')) buf += advance();
                    tokens.emplace_back(TokenKind::NUMBER, (double)std::stoll(buf, nullptr, 2), true, ln);
                } else {
                    while (pos < src.size() && (peek() >= '0' && peek() <= '9' || peek() == '.')) {
                        if (peek() == '.') is_float = true;
                        buf += advance();
                    }
                    double val = std::stod(buf);
                    tokens.emplace_back(TokenKind::NUMBER, val, !is_float, ln);
                }
                continue;
            }
            // Identifiers / keywords
            if (isalpha(ch) || ch == '_') {
                std::string word;
                while (pos < src.size() && (isalnum(peek()) || peek() == '_')) word += advance();
                if      (word == "true")     tokens.emplace_back(TokenKind::BOOL_TRUE, ln);
                else if (word == "false")    tokens.emplace_back(TokenKind::BOOL_FALSE, ln);
                else if (word == "null")     tokens.emplace_back(TokenKind::NULL_TOK, ln);
                else if (word == "and")      tokens.emplace_back(TokenKind::AND, ln);
                else if (word == "or")       tokens.emplace_back(TokenKind::OR, ln);
                else if (word == "not")      tokens.emplace_back(TokenKind::NOT, ln);
                else if (word == "let")      tokens.emplace_back(TokenKind::LET, ln);
                else if (word == "set")      tokens.emplace_back(TokenKind::SET, ln);
                else if (word == "if")       tokens.emplace_back(TokenKind::IF, ln);
                else if (word == "else")     tokens.emplace_back(TokenKind::ELSE, ln);
                else if (word == "elif")     tokens.emplace_back(TokenKind::ELIF, ln);
                else if (word == "while")    tokens.emplace_back(TokenKind::WHILE, ln);
                else if (word == "for")      tokens.emplace_back(TokenKind::FOR, ln);
                else if (word == "in")       tokens.emplace_back(TokenKind::IN, ln);
                else if (word == "func")     tokens.emplace_back(TokenKind::FUNC, ln);
                else if (word == "return")   tokens.emplace_back(TokenKind::RETURN, ln);
                else if (word == "pass")     tokens.emplace_back(TokenKind::PASS, ln);
                else if (word == "continue") tokens.emplace_back(TokenKind::CONTINUE, ln);
                else if (word == "break")    tokens.emplace_back(TokenKind::BREAK, ln);
                else if (word == "with")     tokens.emplace_back(TokenKind::WITH, ln);
                else if (word == "as")       tokens.emplace_back(TokenKind::AS, ln);
                else if (word == "class")    tokens.emplace_back(TokenKind::CLASS, ln);
                else if (word == "new")      tokens.emplace_back(TokenKind::NEW, ln);
                else if (word == "import")   tokens.emplace_back(TokenKind::IMPORT, ln);
                else if (word == "struct")   tokens.emplace_back(TokenKind::STRUCT, ln);
                else if (word == "enum")     tokens.emplace_back(TokenKind::ENUM, ln);
                else if (word == "sizeof")   tokens.emplace_back(TokenKind::SIZEOF, ln);
                else if (word == "cast")     tokens.emplace_back(TokenKind::CAST, ln);
                else tokens.emplace_back(TokenKind::IDENT, word, ln);
                continue;
            }
            // Two-char operators
            char next = peek(1);
            if (ch == '*' && next == '*') { advance(); advance(); tokens.emplace_back(TokenKind::STARSTAR, ln); continue; }
            if (ch == '=' && next == '=') { advance(); advance(); tokens.emplace_back(TokenKind::EQ, ln); continue; }
            if (ch == '!' && next == '=') { advance(); advance(); tokens.emplace_back(TokenKind::NEQ, ln); continue; }
            if (ch == '<' && next == '=') { advance(); advance(); tokens.emplace_back(TokenKind::LTE, ln); continue; }
            if (ch == '>' && next == '=') { advance(); advance(); tokens.emplace_back(TokenKind::GTE, ln); continue; }
            if (ch == '<' && next == '<') { advance(); advance(); tokens.emplace_back(TokenKind::LSHIFT, ln); continue; }
            if (ch == '>' && next == '>') { advance(); advance(); tokens.emplace_back(TokenKind::RSHIFT, ln); continue; }
            if (ch == '&' && next == '&') { advance(); advance(); tokens.emplace_back(TokenKind::AND, ln); continue; }
            if (ch == '|' && next == '|') { advance(); advance(); tokens.emplace_back(TokenKind::OR, ln); continue; }
            if (ch == '+' && next == '=') { advance(); advance(); tokens.emplace_back(TokenKind::PLUSEQ, ln); continue; }
            if (ch == '-' && next == '=') { advance(); advance(); tokens.emplace_back(TokenKind::MINUSEQ, ln); continue; }
            if (ch == '*' && next == '=') { advance(); advance(); tokens.emplace_back(TokenKind::STAREQ, ln); continue; }
            if (ch == '/' && next == '=') { advance(); advance(); tokens.emplace_back(TokenKind::SLASHEQ, ln); continue; }
            if (ch == '-' && next == '>') { advance(); advance(); tokens.emplace_back(TokenKind::ARROW, ln); continue; }
            // Single-char operators
            advance();
            switch (ch) {
                case '+': tokens.emplace_back(TokenKind::PLUS, ln); break;
                case '-': tokens.emplace_back(TokenKind::MINUS, ln); break;
                case '*': tokens.emplace_back(TokenKind::STAR, ln); break;
                case '/': tokens.emplace_back(TokenKind::SLASH, ln); break;
                case '%': tokens.emplace_back(TokenKind::PERCENT, ln); break;
                case '=': tokens.emplace_back(TokenKind::ASSIGN, ln); break;
                case '<': tokens.emplace_back(TokenKind::LT, ln); break;
                case '>': tokens.emplace_back(TokenKind::GT, ln); break;
                case '!': tokens.emplace_back(TokenKind::NOT, ln); break;
                case '&': tokens.emplace_back(TokenKind::AMP, ln); break;
                case '|': tokens.emplace_back(TokenKind::PIPE, ln); break;
                case '^': tokens.emplace_back(TokenKind::CARET, ln); break;
                case '~': tokens.emplace_back(TokenKind::TILDE, ln); break;
                case '(': tokens.emplace_back(TokenKind::LPAREN, ln); break;
                case ')': tokens.emplace_back(TokenKind::RPAREN, ln); break;
                case '{': tokens.emplace_back(TokenKind::LBRACE, ln); break;
                case '}': tokens.emplace_back(TokenKind::RBRACE, ln); break;
                case '[': tokens.emplace_back(TokenKind::LBRACK, ln); break;
                case ']': tokens.emplace_back(TokenKind::RBRACK, ln); break;
                case ',': tokens.emplace_back(TokenKind::COMMA, ln); break;
                case '.': tokens.emplace_back(TokenKind::DOT, ln); break;
                case ';': tokens.emplace_back(TokenKind::SEMI, ln); break;
                case ':': tokens.emplace_back(TokenKind::COLON, ln); break;
                default:
                    throw LexError("Unexpected character '" + std::string(1, ch) + "' at line " + std::to_string(ln));
            }
        }
        return tokens;
    }
};
