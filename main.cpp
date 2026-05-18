#include "src/lexer.h"
#include "src/parser.h"
#include "src/value.h"
#include "src/environment.h"
#include "src/ast.h"
#include "src/interpreter.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

void install_package(const std::string& git_url) {
    std::string home = home_packages_dir();
    fs::create_directories(home);
    
    std::string pkg_name = git_url;
    size_t last_slash = pkg_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        pkg_name = pkg_name.substr(last_slash + 1);
    }
    if (pkg_name.size() > 4 && pkg_name.substr(pkg_name.size() - 4) == ".git") {
        pkg_name = pkg_name.substr(0, pkg_name.size() - 4);
    }
    
    fs::path target_path = fs::path(home) / pkg_name;
    std::string cmd;
    if (fs::exists(target_path)) {
        std::cout << "Package '" << pkg_name << "' already exists. Updating..." << std::endl;
        cmd = "git -C \"" + target_path.string() + "\" pull";
    } else {
        std::cout << "Installing '" << pkg_name << "' into " << home << "..." << std::endl;
        cmd = "git clone \"" + git_url + "\" \"" + target_path.string() + "\"";
    }
    int res = std::system(cmd.c_str());
    if (res == 0) {
        std::cout << "Successfully installed/updated package '" << pkg_name << "'!" << std::endl;
    } else {
        std::cerr << "Failed to run command: " << cmd << std::endl;
    }
}

void run_repl() {
    std::cout << "───────────────────────────────────────────────────────────────────────────────" << std::endl;
    std::cout << "                         G — GLANG  v0.2 (C++)" << std::endl;
    std::cout << "───────────────────────────────────────────────────────────────────────────────" << std::endl;
    std::cout << "Type ':help' for help, ':exit' or Ctrl+C to exit." << std::endl << std::endl;
    
    Interpreter interp;
    std::string line;
    std::string buffer;
    int open_brackets = 0;
    
    while (true) {
        if (buffer.empty()) {
            std::cout << "g> " << std::flush;
        } else {
            std::cout << "... " << std::flush;
        }
        
        if (!std::getline(std::cin, line)) {
            break;
        }
        
        if (line == ":exit") break;
        if (line == ":help") {
            std::cout << "GLang C++ Interpreter REPL Commands:" << std::endl;
            std::cout << "  :help   Show this help message" << std::endl;
            std::cout << "  :exit   Exit the REPL" << std::endl;
            std::cout << "  :clear  Clear the multi-line input buffer" << std::endl;
            continue;
        }
        if (line == ":clear") {
            buffer.clear();
            open_brackets = 0;
            std::cout << "Buffer cleared." << std::endl;
            continue;
        }
        
        buffer += line + "\n";
        
        for (char c : line) {
            if (c == '{' || c == '[' || c == '(') open_brackets++;
            else if (c == '}' || c == ']' || c == ')') open_brackets--;
        }
        
        if (open_brackets <= 0) {
            open_brackets = 0;
            std::string code = buffer;
            buffer.clear();
            
            if (code.find_first_not_of(" \t\r\n") == std::string::npos) {
                continue;
            }
            
            try {
                Lexer lexer(code);
                auto tokens = lexer.tokenize();
                Parser parser(tokens);
                auto ast = parser.parse();
                
                if (ast->ntype == NodeType::Block) {
                    auto blk = std::static_pointer_cast<BlockNode>(ast);
                    if (blk->stmts.size() == 1 && blk->stmts[0]->ntype == NodeType::ExprStmt) {
                        auto expr_stmt = std::static_pointer_cast<ExprStmtNode>(blk->stmts[0]);
                        GValue result = interp.eval(expr_stmt->expr, interp.globals);
                        if (result.type != ValueType::Null) {
                            std::cout << gi_str(result) << std::endl;
                        }
                        continue;
                    }
                }
                
                interp.run(ast);
            } catch (const LexError& e) {
                std::cerr << "LexError: " << e.what() << std::endl;
            } catch (const ParseError& e) {
                std::cerr << "ParseError: " << e.what() << std::endl;
            } catch (const RuntimeError& e) {
                std::cerr << "RuntimeError: " << e.what() << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
            }
        }
    }
}

void run_file(const std::string& path, const std::vector<std::string>& args) {
    if (!fs::exists(path)) {
        std::cerr << "Error: File '" << path << "' does not exist." << std::endl;
        std::exit(1);
    }
    
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Error: Cannot open file '" << path << "'." << std::endl;
        std::exit(1);
    }
    
    std::string src((std::istreambuf_iterator<char>(f)), {});
    
    std::string dir = fs::path(path).parent_path().string();
    if (dir.empty()) dir = ".";
    
    Interpreter interp(dir);
    
    GList args_list;
    args_list.push_back(GValue::string_val(path));
    for (auto& a : args) {
        args_list.push_back(GValue::string_val(a));
    }
    interp.globals->define("args", GValue::list_val(args_list));
    
    try {
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto ast = parser.parse();
        interp.run(ast);
    } catch (const LexError& e) {
        std::cerr << "LexError: " << e.what() << std::endl;
        std::exit(1);
    } catch (const ParseError& e) {
        std::cerr << "ParseError: " << e.what() << std::endl;
        std::exit(1);
    } catch (const RuntimeError& e) {
        std::cerr << "RuntimeError: " << e.what() << std::endl;
        std::exit(1);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::exit(1);
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    // Seed the standard random number generator
    std::srand(std::time(nullptr));
    
    if (argc > 1) {
        std::string first = argv[1];
        if (first == "--install") {
            if (argc < 3) {
                std::cerr << "Usage: glang --install <git-url>" << std::endl;
                return 1;
            }
            install_package(argv[2]);
            return 0;
        }
        
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i) {
            args.push_back(argv[i]);
        }
        run_file(first, args);
    } else {
        run_repl();
    }
    return 0;
}
