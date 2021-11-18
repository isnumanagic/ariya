#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <stack>

#include <llvm-c/Core.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>

namespace parser {
  enum Operator {
    Noop = 0x00,
    And  = 0x10,
    Or   = 0x11,
    Xor  = 0x12,
    Rsh  = 0x20,
    Lsh  = 0x21,
    Add  = 0x30,
    Sub  = 0x31,
    Mul  = 0x40,
    Div  = 0x41,
    Mod  = 0x42,
    Exp  = 0x50,
    Lbr  = 0x60,
    Rbr  = 0x61
  };

  namespace {
    int precedence(Operator op) {
      return op >> 4;
    }

    template<typename K, typename V>
    std::map<V, K> inverse_map(const std::map<K, V> &map) {
      std::map<V, K> rmap;
      for (auto const& kv : map)
        rmap[kv.second] = kv.first;
      return rmap;
    };

    const double pi = M_PI;
    const double e = M_E;

    const std::map<std::string, Operator> token_to_operator {
      {"&",  Operator::And},
      {"|",  Operator::Or},
      {"^",  Operator::Xor},
      {">>", Operator::Rsh},
      {"<<", Operator::Lsh},
      {"+",  Operator::Add},
      {"-",  Operator::Sub},
      {"*",  Operator::Mul},
      {"/",  Operator::Div},
      {"%",  Operator::Mod},
      {"**", Operator::Exp},
      {"(",  Operator::Lbr},
      {")",  Operator::Rbr}
    };
    const std::map<Operator, std::string> operator_to_token = inverse_map(token_to_operator);
    const std::map<Operator, std::function<double(double, double)>> operator_to_fn {
      {Operator::And, [](double a, double b) { return (int)a & (int)b; }},
      {Operator::Or,  [](double a, double b) { return (int)a | (int)b; }},
      {Operator::Xor, [](double a, double b) { return (int)a ^ (int)b; }},
      {Operator::Rsh, [](double a, double b) { return (int)a >> (int)b; }},
      {Operator::Lsh, [](double a, double b) { return (int)a << (int)b; }},
      {Operator::Add, [](double a, double b) { return a + b; }},
      {Operator::Sub, [](double a, double b) { return a - b; }},
      {Operator::Mul, [](double a, double b) { return a * b; }},
      {Operator::Div, [](double a, double b) { return a / b; }},
      {Operator::Mod, [](double a, double b) { return fmod(a, b); }},
      {Operator::Exp, [](double a, double b) { return pow(a, b); }}
    };
    const std::map<std::string, double> const_to_value {
      {"pi", M_PI},
      {"e", M_E}
    };
  }

  class Token {
   public:
    typedef std::shared_ptr<Token> ptr;
    enum Type {
      Value = 1,
      Operator = 2,
      Constant = 3,
      Whitespace = 4,
      Invalid = 5
    };

   private:
    Type type;
    parser::Operator operator_;
    double value;

   public:
    Token(double value): type(Token::Value), value(value) {}
    Token(parser::Operator operator_): type(Token::Operator), operator_(operator_) {}

    static Token::ptr shared(double value) {
      return Token::ptr(new Token(value));
    }
    static Token::ptr shared(parser::Operator operator_) {
      return Token::ptr(new Token(operator_));
    }

    bool is_value() {
      return this->type == Token::Value;
    }

    bool is_operator() {
      return this->type == Token::Operator;
    }

    double get_value() {
      return this->is_value() ? this->value : 0.0;
    }

    parser::Operator get_operator() {
      return this->is_operator() ? this->operator_ : Operator::Noop;
    }

    unsigned int get_precedence() {
      return precedence(this->get_operator());
    }

    std::string to_string() {
      if (this->is_value()) {
        char buf[50];
        sprintf(buf, "%.3lf", this->value);
        return std::string(buf);
      } else if (this->is_operator()) {
        auto it = operator_to_token.find(this->get_operator());
        if (it != operator_to_token.end())
          return it->second;
      }
      return "";
    }
  };

  typedef std::list<Token::ptr> TokenizedExpr;

  bool parse_infix(const std::string &expr, TokenizedExpr &infix) {
    static const std::regex token_rx(
        "((?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:e[+-]?\\d+)?)|"
        "([()]|\\*{2}|[-+\\/*|&^]|<<|>>)|"
        "(-?(?:e|pi))|"
        "(\\s+)|"
        "(.)",
        std::regex::icase);
    std::sregex_iterator iter(expr.begin(), expr.end(), token_rx), end;
    for (; iter != end; iter++)
      for (int i = 1; i < iter->size(); i++)
        if ((*iter)[i].matched) {
          auto key = iter->str();
          Token::ptr token;
          switch (i) {
            case Token::Value: {
              auto value = stod(key);
              token = Token::shared(value);
              break;
            }
            case Token::Operator: {
              auto operator_ = token_to_operator.at(key);
              token = Token::shared(operator_);
              break;
            }
            case Token::Constant: {
              transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c) { return tolower(c); });
              auto value = const_to_value.at(key);
              token = Token::shared(value);
              break;
            }
            case Token::Whitespace:
              break;
            case Token::Invalid:
            default: {
              printf("Invalid character '%c' at position %ld\n", key[0], iter->position());
              return false;
            }
          }
          if (token) {
            auto token_p = infix.empty() ? Token::ptr() : infix.back();
            if (token_p && token_p->is_operator() &&
                token_p->get_precedence() == precedence(Operator::Add) &&
                token->get_precedence() == precedence(Operator::Add)) {
              infix.pop_back();
              token = token_p->get_operator() == token->get_operator() ?
                Token::shared(Operator::Add) : Token::shared(Operator::Sub);
            }
            infix.push_back(token);
          }
        }
    return true;
  }

  bool shunting_yard(TokenizedExpr &infix, TokenizedExpr &postfix) {
    std::stack<Token::ptr> cache;
    while (!infix.empty()) {
      auto token = infix.front();
      infix.pop_front();
      if (token->is_value())
        postfix.push_back(token);
      else if (token->is_operator()) {
        if (token->get_operator() == Operator::Lbr)
          cache.push(token);
        else if (token->get_operator() == Operator::Rbr) {
          while (true) {
            if (cache.empty()) {
              printf("Parentheses are mismatched\n");
              return false;
            }
            auto op = cache.top();
            cache.pop();
            if (op->get_operator() != Operator::Lbr)
              postfix.push_back(op);
            else break;
          }
        } else {
          while (!cache.empty() && cache.top()->get_operator() != Operator::Lbr) {
            auto op = cache.top();
            if (token->get_precedence() > op->get_precedence())
              break;
            cache.pop();
            postfix.push_back(op);
          }
          cache.push(token);
        }
      }
    }
    while (!cache.empty()) {
      auto op = cache.top();
      if (op->get_operator() == Operator::Lbr) {
        printf("Parentheses are mismatched\n");
        return false;
      }
      postfix.push_back(op);
      cache.pop();
    }
    return true;
  }

  bool eval(TokenizedExpr &postfix, double &out) {
    std::stack<double> result;
    while (!postfix.empty()) {
      auto token = postfix.front();
      postfix.pop_front();
      if (token->is_value())
        result.push(token->get_value());
      else if (token->is_operator()) {
        if (result.size() == 1) {
          if (token->get_operator() == Operator::Sub) {
            auto a = -result.top();
            result.pop();
            result.push(a);
            continue;
          }
          if (token->get_operator() == Operator::Add)
            continue;
        }
        if (result.size() < 2) {
          printf("Operators are mismatched\n");
          return false;
        }
        auto a = result.top(); result.pop();
        auto b = result.top(); result.pop();
        auto r = operator_to_fn.at(token->get_operator())(b, a);
        result.push(r);
      }
    }
    if (result.size() != 1) {
      printf("Operators are mismatched\n");
      return false;
    }
    out = result.top();
    return true;
  }
}

namespace llir {
  namespace {
    static llvm::LLVMContext context;
    static llvm::IRBuilder<llvm::NoFolder> builder(context);
    static llvm::Module module("main.ll", context);
    
    static const auto t_char_ptr = []() { return llvm::Type::getInt8PtrTy(context); };
    static const auto t_int32 = []() { return llvm::Type::getInt32Ty(context); };
    static const auto t_int64 = []() { return llvm::Type::getInt64Ty(context); };
    static const auto t_double = []() { return llvm::Type::getDoubleTy(context); };

    llvm::Value* int_operator(std::function<llvm::Value*(llvm::Value*, llvm::Value*)> fn, llvm::Value *a, llvm::Value *b) {
      auto a_i = builder.CreateBitCast(a, t_int32());
      auto b_i = builder.CreateBitCast(b, t_int32());
      auto r_i = fn(a_i, b_i);
      return builder.CreateBitCast(r_i, t_double());
    }

    llvm::Function *pow_proto() {
      auto pow = llvm::Function::Create(
          llvm::FunctionType::get(t_double(), { t_double(), t_double() }, false),
          llvm::GlobalValue::ExternalLinkage,
          "pow",
          module
        );
      pow->setCallingConv(llvm::CallingConv::C);
      return pow;
    }

    llvm::Function *printf_proto() {
      auto printf = llvm::Function::Create(
          llvm::FunctionType::get(t_int32(), { t_char_ptr() }, true),
          llvm::GlobalValue::ExternalLinkage,
          "printf",
          module
        );
      printf->setCallingConv(llvm::CallingConv::C);
      return printf;
    }

    llvm::Function *main_proto() {
      auto main = llvm::Function::Create(
          llvm::FunctionType::get(t_int32(), {}, false),
          llvm::GlobalValue::ExternalLinkage,
          "main",
          module
        );
      main->setCallingConv(llvm::CallingConv::C);
      return main;
    }

    void init() {
      auto main_fn = main_proto();
      auto main_bl = llvm::BasicBlock::Create(context, "entry", main_fn);
      builder.SetInsertPoint(main_bl);
    }

    void print(const char *format, std::initializer_list<llvm::Value*> values) {
      static auto printf_fn = printf_proto();
      auto ll_format = builder.CreateGlobalStringPtr(format);
      std::vector<llvm::Value*> args {ll_format};
      args.insert(args.end(), values);
      builder.CreateCall(printf_fn, args);
    }

    void wrap() {
      builder.CreateRet(llvm::Constant::getNullValue(t_int32()));
      llvm::verifyModule(module);
      std::error_code error;
      auto stream = new llvm::raw_fd_ostream("main.ll", error);
      module.print(*stream, nullptr);
    }

    const std::map<parser::Operator, std::function<llvm::Value*(llvm::Value*, llvm::Value*)>> operator_to_fn {
      {parser::Operator::And, [](auto a, auto b)
        { return int_operator([](auto a_i, auto b_i) { return builder.CreateAnd(a_i, b_i); }, a, b); }},
      {parser::Operator::Or, [](auto a, auto b)
        { return int_operator([](auto a_i, auto b_i) { return builder.CreateOr(a_i, b_i); }, a, b); }},
      {parser::Operator::Xor, [](auto a, auto b)
        { return int_operator([](auto a_i, auto b_i) { return builder.CreateXor(a_i, b_i); }, a, b); }},
      {parser::Operator::Rsh, [](auto a, auto b)
        { return int_operator([](auto a_i, auto b_i) { return builder.CreateAShr(a_i, b_i); }, a, b); }},
      {parser::Operator::Lsh, [](auto a, auto b)
        { return int_operator([](auto a_i, auto b_i) { return builder.CreateLShr(a_i, b_i); }, a, b); }},
      {parser::Operator::Add, [](auto a, auto b) { return builder.CreateFAdd(a, b); }},
      {parser::Operator::Sub, [](auto a, auto b) { return builder.CreateFSub(a, b); }},
      {parser::Operator::Mul, [](auto a, auto b) { return builder.CreateFMul(a, b); }},
      {parser::Operator::Div, [](auto a, auto b) { return builder.CreateFDiv(a, b); }},
      {parser::Operator::Mod, [](auto a, auto b) { return builder.CreateFRem(a, b); }},
      {parser::Operator::Exp, [](auto a, auto b) {
        static auto pow_fn = pow_proto();
        return builder.CreateCall(pow_fn, {a, b});
      }}
    };
  }

  bool compile(parser::TokenizedExpr &postfix) {
    init();

    std::stack<llvm::Value*> result;
    while (!postfix.empty()) {
      auto token = postfix.front();
      postfix.pop_front();
      if (token->is_value()) {
        auto value = llvm::ConstantFP::get(t_double(), token->get_value());
        result.push(value);
      } else if (token->is_operator()) {
        if (result.size() == 1) {
          if (token->get_operator() == parser::Operator::Sub) {
            auto a = result.top();
            auto b = builder.CreateNeg(a);
            result.pop();
            result.push(b);
            continue;
          }
          if (token->get_operator() == parser::Operator::Add)
            continue;
        }
        if (result.size() < 2) {
          printf("Operators are mismatched\n");
          return false;
        }
        auto a = result.top(); result.pop();
        auto b = result.top(); result.pop();
        auto r = operator_to_fn.at(token->get_operator())(b, a);
        result.push(r);
      }
    }
    if (result.size() != 1) {
      printf("Operators are mismatched\n");
      return false;
    }
    auto out = result.top();
    print("Result: %.3lf\n", { out });
    wrap();
    return true;
  }
}

int main() {
  printf("Enter math expression to be parsed:\n");
  // ex: 5 * (6 + 2) - 12 / 4 + 2**4 + pi - e * 1.01e-1
  std::string expr;
  std::getline(std::cin, expr);

  parser::TokenizedExpr infix;
  if (!parser::parse_infix(expr, infix))
    return 1;
  parser::TokenizedExpr postfix;
  if (!parser::shunting_yard(infix, postfix))
    return 1;
  double out;
  parser::TokenizedExpr postfix_ll = postfix;
  if (!parser::eval(postfix, out))
    return 1;

  printf("Result: %.3lf\n", out);
  if (!llir::compile(postfix_ll))
    return 1;

  return 0;
}
