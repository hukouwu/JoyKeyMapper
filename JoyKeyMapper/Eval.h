#pragma once
// 1. 强制防止 Windows 宏干扰
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// 2. 包含标准库
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stack>
#include <cctype>
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <algorithm>

// 3. 使用 namespace + constexpr int 替代 enum class
namespace TokenTypes {
    static constexpr int Number = 0;
    static constexpr int Identifier = 1;
    static constexpr int LParen = 2;
    static constexpr int RParen = 3;
    static constexpr int Plus = 4;
    static constexpr int Minus = 5;
    static constexpr int Mul = 6;
    static constexpr int Div = 7;
    static constexpr int Mod = 8;
    static constexpr int Eq = 9;
    static constexpr int Neq = 10;
    static constexpr int Lt = 11;
    static constexpr int Gt = 12;
    static constexpr int Le = 13;
    static constexpr int Ge = 14;
    static constexpr int And = 15;
    static constexpr int Or = 16;
    static constexpr int Not = 17;
    static constexpr int Comma = 18;
    static constexpr int Question = 19;
    static constexpr int Colon = 20;
    static constexpr int UnaryMinus = 21;
}

struct Token {
    int tkType;
    std::string value; // 用于存储数字字符串、变量名
    double numberValue = 0; // 如果是数字，直接存值
    int tkSubType = 0;

    Token() : value(""), numberValue(0.0) {}
    Token(int t, const std::string& v) : tkType(t), value(v) {
        if (t == TokenTypes::Number) {
            try {
                numberValue = std::stod(v);
            }
            catch (...) {
                throw std::runtime_error("Invalid number format: " + v);
            }
        }
    }
};

// 词法分析器
class Lexer {
public:
    Lexer(const std::string& input) : m_input(input), m_pos(0) {}
    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (m_pos < m_input.size()) {
            char c = m_input[m_pos];
            if (std::isspace(c)) { m_pos++; continue; }

            // 处理被 {} 包裹的变量
            if (c == '{') {
                size_t endPos = m_input.find('}', m_pos + 1);
                if (endPos == std::string::npos) {
                    throw std::runtime_error("Missing closing bracket '}'");
                }
                std::string content = m_input.substr(m_pos + 1, endPos - m_pos - 1);
                if (!content.empty()) {
                    tokens.emplace_back(TokenTypes::Identifier, content);
                    usedVariables.insert(content);
                }
                m_pos = endPos + 1;
                continue;
            }

            // 处理数字
            if (std::isdigit(c) || c == '.') {
                size_t start = m_pos;
                bool hasDot = false;

                // 如果以 '.' 开头，后面必须跟数字
                if (c == '.') {
                    if (m_pos + 1 >= m_input.size() || !std::isdigit(m_input[m_pos + 1])) {
                        // 单独的 '.' 或者 '.abc' 不是合法数字，交给后面默认分支报错或处理
                        break;
                    }
                    hasDot = true;
                    m_pos++;
                }

                while (m_pos < m_input.size()) {
                    char curr = m_input[m_pos];
                    if (std::isdigit(curr)) {
                        m_pos++;
                    }
                    else if (curr == '.') {
                        if (hasDot) break; // 第二个小数点，结束
                        hasDot = true;
                        m_pos++;
                    }
                    else {
                        break; // 非数字非小数点，结束
                    }
                }

                std::string numStr = m_input.substr(start, m_pos - start);
                tokens.emplace_back(TokenTypes::Number, numStr);
                continue;
            }

            // 处理运算符
            if (m_pos + 1 < m_input.size()) {
                std::string twoCharOp = m_input.substr(m_pos, 2);
                if (twoCharOp == "&&") { tokens.emplace_back(TokenTypes::And, "&&"); m_pos += 2; continue; }
                if (twoCharOp == "||") { tokens.emplace_back(TokenTypes::Or, "||"); m_pos += 2; continue; }
                if (twoCharOp == "==") { tokens.emplace_back(TokenTypes::Eq, "=="); m_pos += 2; continue; }
                if (twoCharOp == "!=") { tokens.emplace_back(TokenTypes::Neq, "!="); m_pos += 2; continue; }
                if (twoCharOp == "<=") { tokens.emplace_back(TokenTypes::Le, "<="); m_pos += 2; continue; }
                if (twoCharOp == ">=") { tokens.emplace_back(TokenTypes::Ge, ">="); m_pos += 2; continue; }
            }

            switch (c) {
            case '+': tokens.emplace_back(TokenTypes::Plus, "+"); break;
            case '-': {
                bool isUnary = false;
                if (tokens.empty()) isUnary = true;
                else {
                    int lastType = tokens.back().tkType;
                    if (!(lastType == TokenTypes::Number || lastType == TokenTypes::Identifier || lastType == TokenTypes::RParen)) {
                        isUnary = true;
                    }
                }
                if (isUnary) tokens.emplace_back(TokenTypes::UnaryMinus, "-");
                else tokens.emplace_back(TokenTypes::Minus, "-");
                break;
            };
            case '*': tokens.emplace_back(TokenTypes::Mul, "*"); break;
            case '/': tokens.emplace_back(TokenTypes::Div, "/"); break;
            case '%': tokens.emplace_back(TokenTypes::Mod, "%"); break;
            case ',': tokens.emplace_back(TokenTypes::Comma, ","); break;
            case '?': tokens.emplace_back(TokenTypes::Question, "?"); break;
            case ':': tokens.emplace_back(TokenTypes::Colon, ":"); break;
            case '(': tokens.emplace_back(TokenTypes::LParen, "("); break;
            case ')': tokens.emplace_back(TokenTypes::RParen, ")"); break;
            case '<': tokens.emplace_back(TokenTypes::Lt, "<"); break;
            case '>': tokens.emplace_back(TokenTypes::Gt, ">"); break;
            case '!': tokens.emplace_back(TokenTypes::Not, "!"); break;
            default: 
                std::cout << "【" << m_input << "】里存在未知操作符:" << c << std::endl;
                throw std::runtime_error(std::string("Unknown operator: ") + c);
            }
            m_pos++;
        }
        return tokens;
    }
    std::set<std::string> getUsedVariables() const { return usedVariables; }

private:
    std::set<std::string> usedVariables;
    std::string m_input;
    size_t m_pos;
};

// --- 优化层：中缀转后缀 (Shunting-yard Algorithm with Ternary Support) ---
class RPNConverter {
public:
    // 将中缀 Token 列表转换为后缀 (RPN) Token 列表
    static std::vector<Token> convertToRPN(std::vector<Token>& infixTokens) {
        std::vector<Token> outputQueue;
        std::stack<Token> operatorStack;

        std::stack<std::pair<int, int>> ternaryStack;
        int outCnt = 0;
        for (Token& token : infixTokens) {
            if (token.tkType == TokenTypes::Number || token.tkType == TokenTypes::Identifier) {
                outputQueue.push_back(token);
                outCnt++;
            }
            // 2. 如果是逗号，弹出栈直到左括号
            else if (token.tkType == TokenTypes::Comma) {
                while (!operatorStack.empty() && operatorStack.top().tkType != TokenTypes::LParen) {
                    if (operatorStack.top().tkType == TokenTypes::Question
                        || operatorStack.top().tkType == TokenTypes::Colon) {
                        int qIndex = outputQueue.size() - outCnt;
                        int preOutCnt = outputQueue[qIndex].numberValue;
                        outputQueue[qIndex].numberValue = outCnt;
                        outCnt += preOutCnt;
                        operatorStack.pop();
                        continue;
                    }
                    outputQueue.push_back(operatorStack.top());
                    operatorStack.pop();
                    outCnt++;
                }

                outputQueue.push_back(token);
                outCnt++;
            }
            else if (isOperator(token.tkType)) {
                int o1 = precedence(token.tkType);
                while (!operatorStack.empty() && isOperator(operatorStack.top().tkType)) {
                    int o2 = precedence(operatorStack.top().tkType);
                    if (o1 < o2 || (o1 == o2 && isLeftAssociative(token.tkType))) {
                        outputQueue.push_back(operatorStack.top());
                        operatorStack.pop();
                        outCnt++;
                    }
                    else {
                        break;
                    }
                }
                
                // 三元表达式
                if (token.tkType == TokenTypes::Question) {
                    token.tkSubType = 1;
                    token.numberValue = outCnt;
                    outputQueue.push_back(token);
                    outCnt = 1;
                }
                else if (token.tkType == TokenTypes::Colon) {
                    int qIndex = outputQueue.size() - outCnt;
                    token.tkSubType = 1;
                    if (outputQueue[qIndex].tkType == TokenTypes::Question) {
                        token.tkSubType = 0;
                        Token& qToken = outputQueue[qIndex];
                        int preOutCnt = qToken.numberValue;
                        qToken.tkSubType = 0;
                        qToken.numberValue = outCnt + 1;
                        outCnt += preOutCnt;
                        operatorStack.pop();
                    }
                    token.numberValue = outCnt;
                    outputQueue.push_back(token);
                    outCnt = 1;
                }

                operatorStack.push(token);
            }
            // 4. 左括号入栈
            else if (token.tkType == TokenTypes::LParen) {
                operatorStack.push(token);
            }
            // 5. 右括号：弹出直到左括号
            else if (token.tkType == TokenTypes::RParen) {
                while (!operatorStack.empty() && operatorStack.top().tkType != TokenTypes::LParen) {
                    if (operatorStack.top().tkType == TokenTypes::Question
                        || operatorStack.top().tkType == TokenTypes::Colon) {
                        int qIndex = outputQueue.size() - outCnt;
                        int preOutCnt = outputQueue[qIndex].numberValue;
                        outputQueue[qIndex].numberValue = outCnt;
                        outCnt += preOutCnt;
                        operatorStack.pop();
                        continue;
                    }
                    outputQueue.push_back(operatorStack.top());
                    operatorStack.pop();
                    outCnt++;
                }
                if (operatorStack.empty()) throw std::runtime_error("Mismatched parentheses");
                operatorStack.pop(); // 弹出左括号
            }
        }

        // 将栈中剩余运算符弹出
        while (!operatorStack.empty()) {
            if (operatorStack.top().tkType == TokenTypes::LParen || operatorStack.top().tkType == TokenTypes::RParen) {
                throw std::runtime_error("Mismatched parentheses");
            }
            if (operatorStack.top().tkType == TokenTypes::Question
                || operatorStack.top().tkType == TokenTypes::Colon) {
                int qIndex = outputQueue.size() - outCnt;
                int preOutCnt = outputQueue[qIndex].numberValue;
                outputQueue[qIndex].numberValue = outCnt;
                outCnt += preOutCnt;
                operatorStack.pop();
                continue;
            }
            outputQueue.push_back(operatorStack.top());
            operatorStack.pop();
            outCnt++;
            
        }

        return outputQueue;
    }

private:
    static bool isOperator(int type) {
        return type >= TokenTypes::Plus && type <= TokenTypes::UnaryMinus;
    }

    static bool isLeftAssociative(int type) {
        return type != TokenTypes::Question &&
            type != TokenTypes::Colon &&
            type != TokenTypes::Not &&
            type != TokenTypes::UnaryMinus;
    }

    static int precedence(int type) {
        switch (type) {
        case TokenTypes::Or: return 1;
        case TokenTypes::And: return 2;
        case TokenTypes::Eq: case TokenTypes::Neq:
        case TokenTypes::Lt: case TokenTypes::Gt:
        case TokenTypes::Le: case TokenTypes::Ge: return 3;
        case TokenTypes::Plus: case TokenTypes::Minus: return 4;
        case TokenTypes::Mul: case TokenTypes::Div: case TokenTypes::Mod: return 5;
        case TokenTypes::Not: case TokenTypes::UnaryMinus: return 6;
        case TokenTypes::Question: case TokenTypes::Colon: return 0;
        default: return -1;
        }
    }
};

// --- 优化层：RPN 求值器 (基于栈循环) ---
class RPNCalculator {
public:
    static std::vector<double> evaluate(const std::vector<Token>& rpnTokens, const std::vector<std::map<std::string, int>>& params) {
        std::stack<double> evalStack;
        std::vector<double> results;

        for (size_t i = 0; i < rpnTokens.size(); ++i) {
            const auto& token = rpnTokens[i];

            if (token.tkType == TokenTypes::Number) {
                evalStack.push(token.numberValue);
            }
            else if (token.tkType == TokenTypes::Identifier) {
                double val = 0;
                for (const auto& paramMap : params) {
                    auto it = paramMap.find(token.value);
                    if (it != paramMap.end()) {
                        val = static_cast<double>(it->second);
                        break;
                    }
                }
                evalStack.push(val);
            }
            else if (token.tkType == TokenTypes::Comma) {
                if (!evalStack.empty()) {
                    results.push_back(evalStack.top());
                    evalStack.pop();
                }
            }
            else if (token.tkType == TokenTypes::Question) {// 三元表达式
                if (evalStack.empty()) throw std::runtime_error("Stack underflow on ?");
                double cond = evalStack.top(); evalStack.pop();
                cond = (cond > 0) - (cond < 0);
                if (token.tkSubType == 1) {
                    if (token.numberValue <= 1) {
                        evalStack.push(cond);
                    }
                    else if (cond <= 0) {
                        // 条件为假，跳过 True 分支
                        int skipCount = token.numberValue - 1;
                        i += skipCount;
                        evalStack.push(cond);
                    }
                }
                else if (cond <= 0) {
                    // 条件为假，跳过 True 分支
                    int skipCount = token.numberValue - 1;
                    i += skipCount;
                }
            }
            else if (token.tkType == TokenTypes::Colon) {
                if (token.tkSubType == 1) {
                    double cond = evalStack.top(); evalStack.pop();
                    if (cond > 0) {
                        evalStack.push(cond);
                        int skipCount = token.numberValue - 1;
                        i += skipCount;
                    }
                }
                else {
                    // 已执行 True 分支，跳过 False 分支
                    int skipCount = token.numberValue - 1;
                    i += skipCount;
                }
            }
            else if (isBinaryOp(token.tkType)) {
                if (evalStack.size() < 2) throw std::runtime_error("Stack underflow on binary op");
                double right = evalStack.top(); evalStack.pop();
                double left = evalStack.top(); evalStack.pop();
                evalStack.push(applyBinaryOp(token.tkType, left, right));
            }
            else if (token.tkType == TokenTypes::Not) {
                if (evalStack.empty()) throw std::runtime_error("Stack underflow on unary op");
                double val = evalStack.top(); evalStack.pop();
                evalStack.push((val != 0) ? 0.0 : 1.0);
            }
            else if (token.tkType == TokenTypes::UnaryMinus) {
                if (evalStack.empty()) throw std::runtime_error("Stack underflow on unary minus");
                double val = evalStack.top(); evalStack.pop();
                evalStack.push(-val);
            }
        }

        while (!evalStack.empty()) {
            results.push_back(evalStack.top());
            evalStack.pop();
        }

        return results.empty() ? std::vector<double>{0.0} : results;
    }

private:
    static bool isBinaryOp(int type) {
        return type == TokenTypes::Plus || type == TokenTypes::Minus ||
            type == TokenTypes::Mul || type == TokenTypes::Div ||
            type == TokenTypes::Mod || type == TokenTypes::Eq ||
            type == TokenTypes::Neq || type == TokenTypes::Lt ||
            type == TokenTypes::Gt || type == TokenTypes::Le ||
            type == TokenTypes::Ge || type == TokenTypes::And ||
            type == TokenTypes::Or;
    }

    static double applyBinaryOp(int type, double left, double right) {
        switch (type) {
        case TokenTypes::Plus: return left + right;
        case TokenTypes::Minus: return left - right;
        case TokenTypes::Mul: return left * right;
        case TokenTypes::Div:
            if (right == 0) throw std::runtime_error("Division by zero");
            return left / right;
        case TokenTypes::Mod:
            if (right == 0) throw std::runtime_error("Modulo by zero");
            return std::fmod(left, right);
        case TokenTypes::Eq: return (left == right) ? 1.0 : 0.0;
        case TokenTypes::Neq: return (left != right) ? 1.0 : 0.0;
        case TokenTypes::Lt: return (left < right) ? 1.0 : 0.0;
        case TokenTypes::Gt: return (left > right) ? 1.0 : 0.0;
        case TokenTypes::Le: return (left <= right) ? 1.0 : 0.0;
        case TokenTypes::Ge: return (left >= right) ? 1.0 : 0.0;
        case TokenTypes::And: return ((left != 0) && (right != 0)) ? 1.0 : 0.0;
        case TokenTypes::Or: return ((left != 0) || (right != 0)) ? 1.0 : 0.0;
        default: throw std::runtime_error("Unknown binary operator");
        }
    }
};

// 表达式求值器类
class ExpressionEvaluator {
public:
    explicit ExpressionEvaluator() {}
    explicit ExpressionEvaluator(const std::string& expr) { setExpression(expr); }


    void setExpression(const std::string& expr) {
        m_usedVars.clear();
        m_rpnTokens.clear();

        if (expr.empty()) return;

        Lexer lexer(expr);
        std::vector<Token> infixTokens = lexer.tokenize();
        m_usedVars = lexer.getUsedVariables();

        m_rpnTokens = RPNConverter::convertToRPN(infixTokens);
        isEmpty = m_rpnTokens.empty();
    }

    const std::set<std::string>& getUsedVariables() const { return m_usedVars; }

    // 求值：传入当前的变量状态
    std::vector<double> eval(const std::vector<std::map<std::string, int>>& pms) const {
        if (m_rpnTokens.empty()) return {};

        try {
            return RPNCalculator::evaluate(m_rpnTokens, pms);
        }
        catch (...) {
            return { };
        }
    }

    

public:
    std::vector<Token> m_rpnTokens;
    std::set<std::string> m_usedVars;
    bool isEmpty = true;
};