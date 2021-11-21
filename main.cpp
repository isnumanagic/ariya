#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <numeric>
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

#define l_1(fn) ([](auto a) { return fn(a); })
#define l_2(fn) ([](auto a, auto b) { return fn(a, b); })
#define v_1(fn) ([](auto &v) { return fn(v[0]); })
#define v_2(fn) ([](auto &v) { return fn(v[0], v[1]); })

static bool debug = false;

namespace parser {
  namespace Operator {
    #define op_id(precedence, arity) (((__COUNTER__) << 8) + ((precedence & 0xf) << 4) + ((arity) & 0xf))
    enum Type {
      Noop = op_id(0, 0),
      Sep  = op_id(1, 2),
      And  = op_id(2, 2),
      Or   = op_id(2, 2),
      Xor  = op_id(2, 2),
      Rsh  = op_id(3, 2),
      Lsh  = op_id(3, 2),
      Add  = op_id(4, 2),
      Sub  = op_id(4, 2),
      Mul  = op_id(5, 2),
      Div  = op_id(5, 2),
      Rem  = op_id(5, 2),
      Exp  = op_id(6, 2),
      Not  = op_id(7, 1),
      Pos  = op_id(7, 1),
      Neg  = op_id(7, 1),
      Lbr  = op_id(8, 0),
      Rbr  = op_id(8, 0),
      Fn   = op_id(8, 0)
    };

    static unsigned int id(Type op) {
      return op >> 8;
    }

    static unsigned int precedence(Type op) {
      return (op >> 4) & 0xf;
    }

    static unsigned int arity(Type op) {
      return op & 0xf;
    }

    static bool sentinel(Type op) {
      return op == Type::Lbr || op == Type::Fn;
    }
  }

  namespace Function {
    #define fn_id(arity) (((__COUNTER__) << 4) + ((arity) & 0xf))
    enum Type {
      Pass  = fn_id(1),
      Abs   = fn_id(1),
      Acos  = fn_id(1),
      Acosh = fn_id(1),
      Asin  = fn_id(1),
      Asinh = fn_id(1),
      Atan  = fn_id(1),
      Atanh = fn_id(1),
      Atan2 = fn_id(2),
      Cbrt  = fn_id(1),
      Ceil  = fn_id(1),
      Cos   = fn_id(1),
      Cosh  = fn_id(1),
      Exp   = fn_id(1),
      Floor = fn_id(1),
      Round = fn_id(1),
      Hypot = fn_id(-1),
      Log   = fn_id(1),
      Log2  = fn_id(1),
      Log10 = fn_id(1),
      Max   = fn_id(-1),
      Min   = fn_id(-1),
      Pow   = fn_id(2),
      Sin   = fn_id(1),
      Sinh  = fn_id(1),
      Sqrt  = fn_id(1),
      Tan   = fn_id(1),
      Tanh  = fn_id(1),
      Trunc = fn_id(1)
    };

    static unsigned int id(Type op) {
      return op >> 4;
    }

    static int arity(Type op) {
      return (op & 0xf) == 0xf ? -1 : (op & 0xf);
    }

    template<typename T>
    using nary = std::function<T(std::vector<T>&)>;

    template<typename T>
    T binary_reduce(std::vector<T> &v, std::function<T(T, T)> fn) {
      if (v.size() == 0) return T();
      if (v.size() == 1) return v[0];
      T r = fn(v[0], v[1]);
      for (int i = 2; i < v.size(); i++)
        r = fn(r, v[i]);
      return r;
    }

    #define r(type, fn) ([](auto &v) { return parser::Function::binary_reduce<type>(v, l_2(fn)); })
  }

  namespace {
    template<typename K, typename V>
    std::map<V, K> invert_map(const std::map<K, V> &map) {
      std::map<V, K> rmap;
      for (auto const& kv : map)
        rmap[kv.second] = kv.first;
      return rmap;
    };

    const double pi = M_PI;
    const double e = M_E;

    const std::map<std::string, Operator::Type> token_to_operator {
      {",",  Operator::Sep},
      {"&",  Operator::And},
      {"|",  Operator::Or},
      {"^",  Operator::Xor},
      {">>", Operator::Rsh},
      {"<<", Operator::Lsh},
      {"+",  Operator::Add},
      {"-",  Operator::Sub},
      {"*",  Operator::Mul},
      {"/",  Operator::Div},
      {"%",  Operator::Rem},
      {"**", Operator::Exp},
      {"~",  Operator::Not},
      {"(",  Operator::Lbr},
      {")",  Operator::Rbr}
    };
    const std::map<Operator::Type, Operator::Type> operator_as_unary {
      {Operator::Add, Operator::Pos},
      {Operator::Sub, Operator::Neg}
    };
    const std::map<Operator::Type, std::string> operator_to_token = ([]() {
      auto map = invert_map(token_to_operator);
      map[Operator::Pos] = "+:";
      map[Operator::Neg] = "-:";
      map[Operator::Fn]  = ":(";
      return map;
    })();

    const std::map<std::string, Function::Type> token_to_function {
      {"abs",   Function::Abs},
      {"acos",  Function::Acos},
      {"acosh", Function::Acosh},
      {"asin",  Function::Asin},
      {"asinh", Function::Asinh},
      {"atan",  Function::Atan},
      {"atanh", Function::Atanh},
      {"atan2", Function::Atan2},
      {"cbrt",  Function::Cbrt},
      {"ceil",  Function::Ceil},
      {"cos",   Function::Cos},
      {"cosh",  Function::Cosh},
      {"exp",   Function::Exp},
      {"floor", Function::Floor},
      {"round", Function::Round},
      {"hypot", Function::Hypot},
      {"log",   Function::Log},
      {"log2",  Function::Log2},
      {"log10", Function::Log10},
      {"max",   Function::Max},
      {"min",   Function::Min},
      {"pow",   Function::Pow},
      {"sin",   Function::Sin},
      {"sinh",  Function::Sinh},
      {"sqrt",  Function::Sqrt},
      {"tan",   Function::Tan},
      {"tanh",  Function::Tanh},
      {"trunc", Function::Trunc}
    };
    const std::map<Function::Type, std::string> function_to_token = invert_map(token_to_function);
    const std::string function_pat = ([]() {
      std::vector<std::string> name_v(token_to_function.size());
      std::transform(token_to_function.begin(), token_to_function.end(), name_v.begin(),
        [&](auto &kv) { return kv.first; });
      std::sort(name_v.begin(), name_v.end(), [](auto &a, auto &b) { return a.length() > b.length(); });
      return std::accumulate(name_v.begin(), name_v.end(), std::string(),
        [](auto a, auto b) { return (a.empty() ? "" : a + "|") + b; });
    })();

    const std::map<std::string, double> const_to_value {
      {"pi", M_PI},
      {"e", M_E}
    };

    const std::map<Operator::Type, Function::nary<double>> operator_exec {
      {Operator::And, [](auto &v) { return (int)v[0] & (int)v[1]; }},
      {Operator::Or,  [](auto &v) { return (int)v[0] | (int)v[1]; }},
      {Operator::Xor, [](auto &v) { return (int)v[0] ^ (int)v[1]; }},
      {Operator::Rsh, [](auto &v) { return (int)v[0] >> (int)v[1]; }},
      {Operator::Lsh, [](auto &v) { return (int)v[0] << (int)v[1]; }},
      {Operator::Add, [](auto &v) { return v[0] + v[1]; }},
      {Operator::Sub, [](auto &v) { return v[0] - v[1]; }},
      {Operator::Mul, [](auto &v) { return v[0] * v[1]; }},
      {Operator::Div, [](auto &v) { return v[0] / v[1]; }},
      {Operator::Rem, [](auto &v) { return fmod(v[0], v[1]); }},
      {Operator::Exp, [](auto &v) { return pow(v[0], v[1]); }},
      {Operator::Not, [](auto &v) { return ~(int)v[0]; }},
      {Operator::Pos, [](auto &v) { return v[0]; }},
      {Operator::Neg, [](auto &v) { return -v[0]; }}
    };
    std::map<Function::Type, Function::nary<double>> function_exec {
      {Function::Abs,   v_1(std::abs)},
      {Function::Acos,  v_1(std::acos)},
      {Function::Acosh, v_1(std::acosh)},
      {Function::Asin,  v_1(std::asin)},
      {Function::Asinh, v_1(std::asinh)},
      {Function::Atan,  v_1(std::atan)},
      {Function::Atan2, v_2(std::atan2)},
      {Function::Atanh, v_1(std::atanh)},
      {Function::Cbrt,  v_1(std::cbrt)},
      {Function::Ceil,  v_1(std::ceil)},
      {Function::Cos,   v_1(std::cos)},
      {Function::Cosh,  v_1(std::cosh)},
      {Function::Exp,   v_1(std::exp)},
      {Function::Floor, v_1(std::floor)},
      {Function::Hypot, r(double, std::hypot)},
      {Function::Log,   v_1(std::log)},
      {Function::Log10, v_1(std::log10)},
      {Function::Log2,  v_1(std::log2)},
      {Function::Max,   r(double, std::max)},
      {Function::Min,   r(double, std::min)},
      {Function::Pow,   v_2(std::pow)},
      {Function::Round, v_1(std::round)},
      {Function::Sin,   v_1(std::sin)},
      {Function::Sinh,  v_1(std::sinh)},
      {Function::Sqrt,  v_1(std::sqrt)},
      {Function::Tan,   v_1(std::tan)},
      {Function::Tanh,  v_1(std::tanh)},
      {Function::Trunc, v_1(std::trunc)}
    };
  }

  class Token {
   public:
    typedef std::shared_ptr<Token> ptr;
    enum Type {
      Value = 1,
      Operator = 2,
      Function = 3,
      Constant = 4,
      Whitespace = 5,
      Invalid = 6
    };

   private:
    Type _type;
    Operator::Type _operator;
    Function::Type _function;
    double _value;
    int _argc;

   public:
    Token(double _value): _type(Token::Value), _value(_value) {}
    Token(Operator::Type _operator): _type(Token::Operator), _operator(_operator) {}
    Token(Function::Type _function): _type(Token::Function), _function(_function), _argc(0) {}

    bool is_value() {
      return this->_type == Token::Value;
    }

    bool is_operator() {
      return this->_type == Token::Operator;
    }

    bool is_function() {
      return this->_type == Token::Function;
    }

    bool is_sentinel() {
      return this->is_operator() && Operator::sentinel(this->_operator);
    }

    double value() {
      return this->is_value() ? this->_value : 0.0;
    }

    Operator::Type operator_() {
      return this->is_operator() ? this->_operator : Operator::Noop;
    }

    Function::Type function() {
      return this->is_function() ? this->_function : Function::Pass;
    }

    unsigned int operator_arity() {
      return Operator::arity(this->_operator);
    }

    unsigned int operator_precedence() {
      return Operator::precedence(this->_operator);
    }

    int function_arity() {
      return Function::arity(this->_function);
    }

    void function_init_argc() {
      if (this->is_function() && !this->_argc)
        this->_argc++;
    }

    void function_increase_argc() {
      if (this->is_function())
        this->_argc++;
    }

    int function_argc() {
      return this->is_function() ? this->_argc : 0;
    }

    std::string to_string() {
      if (this->is_value()) {
        char buf[50];
        sprintf(buf, "%.3lf", this->_value);
        return std::string(buf);
      } else if (this->is_operator()) {
        auto it = operator_to_token.find(this->_operator);
        if (it != operator_to_token.end())
          return it->second;
      } else if (this->is_function()) {
        auto it = function_to_token.find(this->_function);
        if (it != function_to_token.end())
          return it->second;
      }
      return "";
    }
  };

  typedef std::list<Token::ptr> TokenizedExpr;

  bool parse_infix(const std::string &expr, TokenizedExpr &infix) {
    static const std::regex token_rx(
        "((?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:e[+-]?\\d+)?)|"
        "([()]|\\*{2}|[-+~,\\/*%|&^]|<<|>>)|"
        "(" + function_pat + "(?=\\s*\\())|"
        "(e|pi)|"
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
              token = std::make_shared<Token>(value);
              break;
            }
            case Token::Operator: {
              Operator::Type operator_ = token_to_operator.at(key);
              if ((infix.empty() || (!infix.back()->is_value() && infix.back()->operator_() != Operator::Rbr)) &&
                  operator_as_unary.find(operator_) != operator_as_unary.end())
                operator_ = operator_as_unary.at(operator_);
              if (!infix.empty() && infix.back()->is_function())
                operator_ = Operator::Fn;
              token = std::make_shared<Token>(operator_);
              break;
            }
            case Token::Function: {
              Function::Type function = token_to_function.at(key);
              token = std::make_shared<Token>(function);
              break;
            }
            case Token::Constant: {
              std::transform(key.begin(), key.end(), key.begin(), l_1(std::tolower));
              auto value = const_to_value.at(key);
              token = std::make_shared<Token>(value);
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
          if (token) infix.push_back(token);
        }
    if (debug) {
      for (auto t : infix)
        printf("%s ", t->to_string().c_str());
      printf("\n");
    }
    return true;
  }

  bool shunting_yard(TokenizedExpr &infix, TokenizedExpr &postfix) {
    std::stack<Token::ptr> operator_cache, function_cache;
    while (!infix.empty()) {
      auto token = infix.front();
      infix.pop_front();
      if (token->is_value()) {
        if (!function_cache.empty())
          function_cache.top()->function_init_argc();
        postfix.push_back(token);
      } else if (token->is_function()) {
        if (!function_cache.empty())
          function_cache.top()->function_init_argc();
        function_cache.push(token);
      } else if (token->is_operator()) {
        if (token->is_sentinel())
          operator_cache.push(token);
        else if (token->operator_() == Operator::Rbr) {
          while (true) {
            if (operator_cache.empty()) {
              printf("Parentheses are mismatched\n");
              return false;
            }
            auto op = operator_cache.top(); operator_cache.pop();
            if (op->operator_() == Operator::Fn) {
              auto fn = function_cache.top();
              function_cache.pop();
              postfix.push_back(fn);
              postfix.push_back(std::make_shared<Token>(fn->function_argc()));
              break;
            } else if (op->operator_() == Operator::Lbr)
              break;
            else postfix.push_back(op);
          }
        } else {
          if (token->operator_arity() != 1)
            while (!operator_cache.empty() && !operator_cache.top()->is_sentinel()) {
              auto op = operator_cache.top();
              if (token->operator_precedence() > op->operator_precedence())
                break;
              operator_cache.pop();
              postfix.push_back(op);
            }
          if (token->operator_() != Operator::Sep)
            operator_cache.push(token);
          else if (function_cache.empty()) {
            printf("Separator outside function\n");
            return false;
          } else function_cache.top()->function_increase_argc();
        }
      }
    }
    while (!operator_cache.empty()) {
      auto op = operator_cache.top();
      if (op->is_sentinel()) {
        printf("Parentheses are mismatched\n");
        return false;
      }
      postfix.push_back(op);
      operator_cache.pop();
    }
    if (!function_cache.empty()) {
      printf("Syntax error\n");
      return false;
    }
    if (debug) {
      for (auto t : postfix)
        printf("%s ", t->to_string().c_str());
      printf("\n");
    }
    return true;
  }

  template<typename T>
  bool eval(
      TokenizedExpr &postfix, T &out,
      std::function<T(double)> mapper,
      std::function<T(Operator::Type, std::vector<T>&)> operator_exec,
      std::function<T(Function::Type, std::vector<T>&)> function_exec) {
    std::stack<T> result;
    while (!postfix.empty()) {
      auto token = postfix.front(); postfix.pop_front();
      if (token->is_value())
        result.push(mapper(token->value()));
      else if (token->is_operator()) {
        int n = token->operator_arity();
        if (n > result.size()) {
          printf("Syntax error\n");
          return false;
        }
        std::vector<T> values(n);
        while (n--) { values[n] = result.top(); result.pop(); }
        auto r = operator_exec(token->operator_(), values);
        result.push(r);
      } else if (token->is_function()) {
        auto n = (int)postfix.front()->value(); postfix.pop_front();
        if (token->function_arity() > (int)result.size() || n > result.size()) {
          printf("Syntax error\n");
          return false;
        }
        std::vector<T> values(n);
        while (n--) { values[n] = result.top(); result.pop(); }
        auto r = function_exec(token->function(), values);
        result.push(r);
      }
    }
    if (result.size() != 1) {
      printf("Syntax error\n");
      return false;
    }
    out = result.top();
    return true;
  }

  bool eval(TokenizedExpr &postfix, double &out) {
    return eval<double>(
      postfix,
      out,
      [](auto a) { return a; },
      [&](auto op, auto &v) { return operator_exec.at(op)(v); },
      [&](auto fn, auto &v) { return function_exec.at(fn)(v); });
  }
}

namespace llir {
  namespace {
    static auto context = std::make_shared<llvm::LLVMContext>();
    static auto builder = std::make_shared<llvm::IRBuilder<llvm::NoFolder>>(*context);
    static auto module  = std::make_shared<llvm::Module>("main.ll", *context);
    
    static const auto t_char_ptr = []() { return llvm::Type::getInt8PtrTy(*context); };
    static const auto t_int32 = []() { return llvm::Type::getInt32Ty(*context); };
    static const auto t_int64 = []() { return llvm::Type::getInt64Ty(*context); };
    static const auto t_double = []() { return llvm::Type::getDoubleTy(*context); };

    llvm::Value* as_int(std::vector<llvm::Value*> &v, std::function<llvm::Value*(std::vector<llvm::Value*>&)> fn) {
      std::vector<llvm::Value*> mapped(v.size());
      for (int i = 0; i < v.size(); i++)
        mapped[i] = builder->CreateFPToSI(v[i], t_int32());
      auto i = fn(mapped);
      return builder->CreateSIToFP(i, t_double());
    }

    #define i_1(fn) ([](auto &v) { return as_int(v, v_1(fn)); })
    #define i_2(fn) ([](auto &v) { return as_int(v, v_2(fn)); })

    std::function<llvm::Function*()> declare_fn(std::function<llvm::Function*()> init) {
      llvm::Function *fn_ptr = nullptr;
      return [=]() mutable {
        if (!fn_ptr) fn_ptr = init();
        return fn_ptr;
      };
    }

    auto declare_math_fn(std::string name, int argc = 1) {
      return declare_fn([&, name, argc]() {
        std::vector<llvm::Type*> argt(argc);
        for (int i = 0; i < argc; i++) argt[i] = t_double();
        return llvm::Function::Create(
          llvm::FunctionType::get(t_double(), argt, false),
          llvm::GlobalValue::ExternalLinkage,
          name,
          *module
        );
      });
    }

    const std::map<parser::Function::Type, std::function<llvm::Function*()>> ir_math {
      {parser::Function::Abs,   declare_math_fn("fabs",  1)},
      {parser::Function::Acos,  declare_math_fn("acos",  1)},
      {parser::Function::Acosh, declare_math_fn("acosh", 1)},
      {parser::Function::Asin,  declare_math_fn("asin",  1)},
      {parser::Function::Asinh, declare_math_fn("asinh", 1)},
      {parser::Function::Atan,  declare_math_fn("atan",  1)},
      {parser::Function::Atanh, declare_math_fn("atanh", 1)},
      {parser::Function::Atan2, declare_math_fn("atan2", 2)},
      {parser::Function::Cbrt,  declare_math_fn("cbrt",  1)},
      {parser::Function::Ceil,  declare_math_fn("fabs",  1)},
      {parser::Function::Cos,   declare_math_fn("cos",   1)},
      {parser::Function::Cosh,  declare_math_fn("cosh",  1)},
      {parser::Function::Exp,   declare_math_fn("exp",   1)},
      {parser::Function::Floor, declare_math_fn("floor", 1)},
      {parser::Function::Hypot, declare_math_fn("hypot", 2)},
      {parser::Function::Log,   declare_math_fn("log",   1)},
      {parser::Function::Log2,  declare_math_fn("log2",  1)},
      {parser::Function::Log10, declare_math_fn("log10", 1)},
      {parser::Function::Max,   declare_math_fn("fmax",  2)},
      {parser::Function::Min,   declare_math_fn("fmin",  2)},
      {parser::Function::Pow,   declare_math_fn("pow",   2)},
      {parser::Function::Round, declare_math_fn("round", 1)},
      {parser::Function::Sin,   declare_math_fn("sin",   1)},
      {parser::Function::Sinh,  declare_math_fn("sinh",  1)},
      {parser::Function::Sqrt,  declare_math_fn("sqrt",  1)},
      {parser::Function::Tan,   declare_math_fn("tan",   1)},
      {parser::Function::Tanh,  declare_math_fn("tanh",  1)},
      {parser::Function::Trunc, declare_math_fn("trunc", 1)}
    };

    auto ir_printf = declare_fn([]() {
      return llvm::Function::Create(
        llvm::FunctionType::get(t_int32(), { t_char_ptr() }, true),
        llvm::GlobalValue::ExternalLinkage,
        "printf",
        *module
      );
    });

    auto ir_main = declare_fn([]() {
      return llvm::Function::Create(
        llvm::FunctionType::get(t_int32(), {}, false),
        llvm::GlobalValue::ExternalLinkage,
        "main",
        *module
      );
    });

    void init() {
      auto main_bl = llvm::BasicBlock::Create(*context, "entry", ir_main());
      builder->SetInsertPoint(main_bl);
    }

    void print(const char *format, std::initializer_list<llvm::Value*> values) {
      auto ll_format = builder->CreateGlobalStringPtr(format);
      std::vector<llvm::Value*> args {ll_format};
      args.insert(args.end(), values);
      builder->CreateCall(ir_printf(), args);
    }

    void wrap() {
      builder->CreateRet(llvm::Constant::getNullValue(t_int32()));
      llvm::verifyModule(*module);
      std::error_code error;
      auto stream = new llvm::raw_fd_ostream("main.ll", error);
      module->print(*stream, nullptr);
    }

    const std::map<parser::Operator::Type, parser::Function::nary<llvm::Value*>> operator_exec {
      {parser::Operator::And, i_2(builder->CreateAnd)},
      {parser::Operator::Or,  i_2(builder->CreateOr)},
      {parser::Operator::Xor, i_2(builder->CreateXor)},
      {parser::Operator::Rsh, i_2(builder->CreateAShr)},
      {parser::Operator::Lsh, i_2(builder->CreateShl)},
      {parser::Operator::Add, v_2(builder->CreateFAdd)},
      {parser::Operator::Sub, v_2(builder->CreateFSub)},
      {parser::Operator::Mul, v_2(builder->CreateFMul)},
      {parser::Operator::Div, v_2(builder->CreateFDiv)},
      {parser::Operator::Rem, v_2(builder->CreateFRem)},
      {parser::Operator::Exp, [](auto &v) { return builder->CreateCall(ir_math.at(parser::Function::Pow)(), v); }},
      {parser::Operator::Not, i_1(builder->CreateNot)},
      {parser::Operator::Pos, [](auto &v) { return v[0]; }},
      {parser::Operator::Neg, v_1(builder->CreateFNeg)}
    };
    const std::map<parser::Function::Type, parser::Function::nary<llvm::Value*>> function_exec = ([]() {
      std::map<parser::Function::Type, parser::Function::nary<llvm::Value*>> map;
      for (auto &kv : ir_math) {
        auto fn = kv.first;
        auto exec = kv.second;
        map[fn] = [&, exec](std::vector<llvm::Value*> &v) -> llvm::Value* {
          if (v.empty())
            return llvm::Constant::getNullValue(t_double());
          if (parser::Function::arity(fn) == -1)
            return parser::Function::binary_reduce<llvm::Value*>(v, [&](auto a, auto b)
              { return builder->CreateCall(exec(), { a, b }); });
          return builder->CreateCall(exec(), v);
        };
      }
      return map;
    })();
  }

  bool compile(parser::TokenizedExpr &postfix) {
    init();
    llvm::Value* out;
    auto result = parser::eval<llvm::Value*>(
      postfix,
      out,
      [&](auto a) { return llvm::ConstantFP::get(t_double(), a); },
      [&](auto op, auto v) { return operator_exec.at(op)(v); },
      [&](auto fn, auto v) { return function_exec.at(fn)(v); });
    if (!result)
      return false;
    print("Result: %.3lf\n", { out });
    wrap();
    return true;
  }
}

void parse_args(int argc, char *argv[]) {
  if (argc > 1)
    if (!strcmp(argv[1], "-d") || !strcmp(argv[1], "--debug"))
      debug = true;
}

int main(int argc, char *argv[]) {
  parse_args(argc, argv);

  printf("Enter math expression to be parsed:\n");
  // i: -1 + 5 * (6 + 2) - 12 / 4 + 2**4 + pi - e * 1.01e-1 - (1 << 5) + -hypot(1, -2, 3) * max(1, 2, min(4, 5))
  // o: 7.900
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
