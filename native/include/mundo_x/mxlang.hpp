#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <istream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mundo_x {

struct MxSourceLocation {
  std::size_t line = 1;
  std::size_t column = 1;
};

struct MxCompiledProgram {
  std::string xasm;
  std::vector<MxSourceLocation> source_map;
  std::size_t qubits = 0;
  std::size_t instructions = 0;
  std::size_t t_gates = 0;
  std::string backend = "compact";
};

inline constexpr std::size_t mx_max_source_bytes = 1024 * 1024;
inline constexpr std::size_t mx_max_tokens = 100000;
inline constexpr std::size_t mx_max_syntax_depth = 64;
inline constexpr std::size_t mx_max_call_depth = 32;
inline constexpr std::size_t mx_max_expansion_work = 100000;
inline constexpr std::size_t mx_max_xasm_lines = 10000;
inline constexpr std::size_t mx_max_output_bytes = 1024 * 1024;
inline constexpr std::size_t mx_max_compact_qubits = 10000;

namespace mx_detail {

[[noreturn]] inline void fail(MxSourceLocation at, const std::string& message) {
  throw std::runtime_error("line " + std::to_string(at.line) + ":" +
                           std::to_string(at.column) + ": " + message);
}

inline bool letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool digit(char c) { return c >= '0' && c <= '9'; }
inline bool gate(const std::string& name) {
  return name == "h" || name == "x" || name == "z" || name == "s" ||
         name == "t" || name == "cx" || name == "cz";
}
inline bool physics_command(const std::string& name) { return name == "scatter"; }
inline bool reserved(const std::string& name) {
  return gate(name) || physics_command(name) || name == "q" || name == "backend" || name == "let" ||
         name == "qubits" || name == "repeat" || name == "as" ||
         name == "circuit" || name == "show" || name == "bytes" || name == "assert";
}

enum class TokenKind { Identifier, Number, Symbol, End };
struct Token {
  TokenKind kind;
  std::string text;
  MxSourceLocation at;
};

inline std::vector<Token> tokenize(const std::string& source) {
  std::vector<Token> result;
  std::size_t i = 0;
  MxSourceLocation at;
  const auto advance = [&]() {
    if (source[i++] == '\n') { ++at.line; at.column = 1; }
    else ++at.column;
  };
  while (i < source.size()) {
    const char c = source[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
    if (c == '#' || (c == '/' && i + 1 < source.size() && source[i + 1] == '/')) {
      while (i < source.size() && source[i] != '\n') advance();
      continue;
    }
    const auto start = i;
    const auto location = at;
    TokenKind kind = TokenKind::Symbol;
    if (letter(c)) {
      kind = TokenKind::Identifier;
      do { advance(); } while (i < source.size() && (letter(source[i]) || digit(source[i])));
    } else if (digit(c)) {
      kind = TokenKind::Number;
      do { advance(); } while (i < source.size() && digit(source[i]));
    } else if (std::string("(){}[],;:=+-*").find(c) != std::string::npos) {
      advance();
    } else {
      fail(at, "unexpected character '" + std::string(1, c) + "'");
    }
    if (result.size() >= mx_max_tokens) fail(location, "token limit exceeded (100000)");
    result.push_back({kind, source.substr(start, i - start), location});
  }
  result.push_back({TokenKind::End, "", at});
  return result;
}

enum class Type { Count, Qubit };
struct Value {
  Type type = Type::Count;
  std::uint64_t number = 0;
  bool known = true;
};
struct Expr {
  enum class Kind { Literal, Name, Register, Add, Subtract, Multiply };
  Kind kind = Kind::Literal;
  MxSourceLocation at;
  std::size_t height = 1;
  std::uint64_t number = 0;
  std::string name;
  std::unique_ptr<Expr> left;
  std::unique_ptr<Expr> right;
};
using ExprPtr = std::unique_ptr<Expr>;
struct Parameter {
  std::string name;
  Type type;
  MxSourceLocation at;
};
struct Statement {
  enum class Kind { Backend, Let, Qubits, Repeat, Circuit, Invoke, Show, Bytes, Assert };
  Kind kind = Kind::Invoke;
  MxSourceLocation at;
  std::string name;
  ExprPtr expression;
  std::vector<ExprPtr> arguments;
  std::vector<Parameter> parameters;
  std::vector<Statement> body;
};

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
  std::vector<Statement> parse() {
    std::vector<Statement> statements;
    while (peek().kind != TokenKind::End) statements.push_back(statement(0));
    return statements;
  }
 private:
  const Token& peek() const { return tokens_[position_]; }
  bool take(const std::string& token) {
    if (peek().text != token) return false;
    ++position_;
    return true;
  }
  Token expect(const std::string& token) {
    if (peek().text != token) fail(peek().at, "expected '" + token + "'");
    return tokens_[position_++];
  }
  Token identifier(bool declaration = false) {
    if (peek().kind != TokenKind::Identifier) fail(peek().at, "expected identifier");
    auto token = tokens_[position_++];
    if (declaration && reserved(token.text)) fail(token.at, "reserved name '" + token.text + "'");
    return token;
  }
  void depth(std::size_t n, MxSourceLocation at) const {
    if (n > mx_max_syntax_depth) fail(at, "syntax depth limit exceeded (64)");
  }
  ExprPtr binary(ExprPtr left, ExprPtr right, Expr::Kind kind,
                 MxSourceLocation at, std::size_t nesting) {
    auto result = std::make_unique<Expr>();
    result->kind = kind;
    result->at = at;
    result->height = 1 + std::max(left->height, right->height);
    depth(nesting + result->height, at);
    result->left = std::move(left);
    result->right = std::move(right);
    return result;
  }
  ExprPtr expression(std::size_t nesting) {
    depth(nesting + 1, peek().at);
    auto result = product(nesting);
    while (peek().text == "+" || peek().text == "-") {
      const auto token = tokens_[position_++];
      auto right = product(nesting);
      result = binary(std::move(result), std::move(right),
                      token.text == "+" ? Expr::Kind::Add : Expr::Kind::Subtract,
                      token.at, nesting);
    }
    return result;
  }
  ExprPtr product(std::size_t nesting) {
    auto result = primary(nesting);
    while (peek().text == "*") {
      const auto token = tokens_[position_++];
      auto right = primary(nesting);
      result = binary(std::move(result), std::move(right), Expr::Kind::Multiply,
                      token.at, nesting);
    }
    return result;
  }
  ExprPtr primary(std::size_t nesting) {
    depth(nesting + 1, peek().at);
    const auto token = peek();
    if (take("(")) {
      auto result = expression(nesting + 1);
      expect(")");
      // Retain parenthesis depth so flat expressions cannot hide deep subtrees.
      ++result->height;
      depth(nesting + result->height, token.at);
      return result;
    }
    auto result = std::make_unique<Expr>();
    result->at = token.at;
    if (token.kind == TokenKind::Number) {
      ++position_;
      for (char c : token.text) {
        const auto part = static_cast<std::uint64_t>(c - '0');
        if (result->number > (std::numeric_limits<std::uint64_t>::max() - part) / 10)
          fail(token.at, "count literal exceeds uint64 range");
        result->number = result->number * 10 + part;
      }
    } else if (token.kind == TokenKind::Identifier) {
      ++position_;
      if (token.text == "q" && take("[")) {
        result->kind = Expr::Kind::Register;
        result->left = expression(nesting + 1);
        expect("]");
        result->height = 1 + result->left->height;
        depth(nesting + result->height, token.at);
      } else {
        result->kind = Expr::Kind::Name;
        result->name = token.text;
      }
    } else {
      fail(token.at, "expected count expression or qubit reference");
    }
    return result;
  }
  std::vector<Statement> block(std::size_t nesting) {
    expect("{");
    depth(nesting + 1, peek().at);
    std::vector<Statement> body;
    while (!take("}")) {
      if (peek().kind == TokenKind::End) fail(peek().at, "expected '}'");
      body.push_back(statement(nesting + 1));
    }
    return body;
  }
  Statement statement(std::size_t nesting) {
    depth(nesting + 1, peek().at);
    const auto token = identifier();
    Statement result;
    result.at = token.at;
    if (token.text == "backend") {
      result.kind = Statement::Kind::Backend;
      result.name = identifier().text;
      expect(";");
    } else if (token.text == "let") {
      result.kind = Statement::Kind::Let;
      result.name = identifier(true).text;
      expect("=");
      result.expression = expression(nesting);
      expect(";");
    } else if (token.text == "qubits") {
      result.kind = Statement::Kind::Qubits;
      result.expression = expression(nesting);
      expect(";");
    } else if (token.text == "repeat") {
      result.kind = Statement::Kind::Repeat;
      result.expression = expression(nesting);
      if (take("as")) result.name = identifier(true).text;
      result.body = block(nesting);
    } else if (token.text == "circuit") {
      result.kind = Statement::Kind::Circuit;
      result.name = identifier(true).text;
      expect("(");
      if (!take(")")) {
        do {
          const auto parameter = identifier(true);
          expect(":");
          const auto type = identifier();
          if (type.text != "count" && type.text != "qubit")
            fail(type.at, "expected parameter type count or qubit");
          result.parameters.push_back({parameter.text,
                                      type.text == "count" ? Type::Count : Type::Qubit,
                                      parameter.at});
        } while (take(","));
        expect(")");
      }
      result.body = block(nesting);
    } else if (token.text == "show" || token.text == "bytes") {
      result.kind = token.text == "show" ? Statement::Kind::Show : Statement::Kind::Bytes;
      expect(";");
    } else if (token.text == "assert") {
      result.kind = Statement::Kind::Assert;
      result.name = identifier().text;
      expect(";");
    } else {
      if (reserved(token.text) && !gate(token.text) && !physics_command(token.text)) fail(token.at, "unexpected keyword '" + token.text + "'");
      result.kind = Statement::Kind::Invoke;
      result.name = token.text;
      expect("(");
      if (!take(")")) {
        do { result.arguments.push_back(expression(nesting)); } while (take(","));
        expect(")");
      }
      expect(";");
    }
    return result;
  }
  std::vector<Token> tokens_;
  std::size_t position_ = 0;
};

struct Global {
  Value value;
  std::size_t sequence;
};
struct Frame {
  std::size_t globals = 0;
  bool register_visible = false;
  bool circuit_body = false;
  std::vector<std::unordered_map<std::string, Value>> scopes{1};
};
struct Circuit {
  const Statement* statement = nullptr;
  std::size_t globals = 0;
  bool register_visible = false;
  std::vector<std::pair<std::string, MxSourceLocation>> calls;
  unsigned color = 0;
  std::size_t height = 0;
};

class Compiler {
 public:
  explicit Compiler(std::vector<Statement> statements) : statements_(std::move(statements)) {}
  MxCompiledProgram compile() {
    // Collect signatures first, making call composition independent of definition order.
    for (const auto& statement : statements_) {
      if (statement.kind != Statement::Kind::Circuit) continue;
      if (!circuits_.emplace(statement.name, Circuit{&statement, 0, false, {}, 0, 0}).second)
        fail(statement.at, "duplicate circuit '" + statement.name + "'");
    }
    Frame root;
    for (const auto& statement : statements_) validate_root(statement, root);
    if (!qubits_declared_ && !scattering_seen_)
      fail({1, 1}, "program must declare qubits or execute scatter");
    for (const auto& statement : statements_) {
      if (statement.kind != Statement::Kind::Circuit) continue;
      auto& circuit = circuits_.at(statement.name);
      Frame frame;
      frame.globals = circuit.globals;
      frame.register_visible = circuit.register_visible;
      frame.circuit_body = true;
      for (const auto& parameter : statement.parameters)
        declare(frame, parameter.name, {parameter.type, 0, false}, parameter.at);
      validate_block(statement.body, frame, &circuit);
    }
    for (const auto& statement : statements_) {
      if (statement.kind == Statement::Kind::Circuit) check_calls(statement.name, 1, statement.at);
    }
    expanding_ = true;
    emit("backend " + result_.backend, backend_at_);
    if (qubits_declared_) emit("qubits " + std::to_string(result_.qubits), qubits_at_);
    Frame expansion;
    for (const auto& statement : statements_) expand(statement, expansion, 0, true);
    return std::move(result_);
  }
 private:
  void work(MxSourceLocation at) {
    if (++work_ > mx_max_expansion_work) fail(at, "expansion work limit exceeded (100000)");
  }
  void emit(const std::string& line, MxSourceLocation at) {
    if (result_.source_map.size() >= mx_max_xasm_lines) fail(at, "XASM line limit exceeded (10000)");
    if (line.size() + 1 > mx_max_output_bytes - result_.xasm.size())
      fail(at, "XASM output byte limit exceeded (1 MiB)");
    result_.xasm += line;
    result_.xasm += '\n';
    result_.source_map.push_back(at);
  }
  static void require_type(Value value, Type type, MxSourceLocation at) {
    if (value.type != type) fail(at, type == Type::Count ? "expected count, got qubit" : "expected qubit, got count");
  }
  void declare(Frame& frame, const std::string& name, Value value, MxSourceLocation at) {
    if (!frame.scopes.back().emplace(name, value).second) fail(at, "duplicate name '" + name + "' in this scope");
  }
  Value lookup(const Frame& frame, const std::string& name, MxSourceLocation at) const {
    for (auto scope = frame.scopes.rbegin(); scope != frame.scopes.rend(); ++scope) {
      const auto found = scope->find(name);
      if (found != scope->end()) return found->second;
    }
    const auto global = globals_.find(name);
    if (global != globals_.end() && global->second.sequence <= frame.globals) return global->second.value;
    fail(at, "unknown symbol '" + name + "'");
  }
  Value evaluate(const Expr& expression, const Frame& frame) {
    if (expanding_) work(expression.at);
    switch (expression.kind) {
      case Expr::Kind::Literal: return {Type::Count, expression.number, true};
      case Expr::Kind::Name: return lookup(frame, expression.name, expression.at);
      case Expr::Kind::Register: {
        if (!frame.register_visible) fail(expression.at, "declare qubits before using q[index]");
        const auto index = evaluate(*expression.left, frame);
        require_type(index, Type::Count, expression.left->at);
        if (index.known && index.number >= result_.qubits) fail(expression.at, "qubit index out of range");
        return {Type::Qubit, index.number, index.known};
      }
      default: break;
    }
    const auto a = evaluate(*expression.left, frame);
    const auto b = evaluate(*expression.right, frame);
    require_type(a, Type::Count, expression.left->at);
    require_type(b, Type::Count, expression.right->at);
    if (!a.known || !b.known) return {Type::Count, 0, false};
    std::uint64_t number = 0;
    if (expression.kind == Expr::Kind::Add) {
      if (a.number > std::numeric_limits<std::uint64_t>::max() - b.number) fail(expression.at, "count addition overflow");
      number = a.number + b.number;
    } else if (expression.kind == Expr::Kind::Subtract) {
      if (a.number < b.number) fail(expression.at, "count subtraction underflow");
      number = a.number - b.number;
    } else {
      if (b.number != 0 && a.number > std::numeric_limits<std::uint64_t>::max() / b.number) fail(expression.at, "count multiplication overflow");
      number = a.number * b.number;
    }
    return {Type::Count, number, true};
  }
  void assertion(const Statement& statement) const {
    const auto& name = statement.name;
    if (name == "one" || name == "ghz") {
      if (result_.backend != "compact") fail(statement.at, "assert " + name + " requires backend compact");
    } else if (name == "fidelity" || name == "norm") {
      if (result_.backend != "reduced") fail(statement.at, "assert " + name + " requires backend reduced");
    } else fail(statement.at, "unknown assertion '" + name + "'");
  }
  std::vector<Value> arguments(const Statement& statement, const Frame& frame) {
    const bool quantum_gate = gate(statement.name);
    const bool physics = physics_command(statement.name);
    const bool builtin = quantum_gate || physics;
    const Statement* definition = nullptr;
    if (!builtin) {
      const auto found = circuits_.find(statement.name);
      if (found == circuits_.end()) fail(statement.at, "unknown circuit '" + statement.name + "'");
      definition = found->second.statement;
    }
    const auto expected = physics ? 4U :
        (quantum_gate ? ((statement.name == "cx" || statement.name == "cz") ? 2U : 1U)
                      : definition->parameters.size());
    if (statement.arguments.size() != expected) fail(statement.at, "wrong argument count for '" + statement.name + "'");
    if (statement.name == "t" && result_.backend != "reduced") fail(statement.at, "t requires backend reduced");
    std::vector<Value> values;
    values.reserve(expected);
    for (std::size_t i = 0; i < expected; ++i) {
      const auto value = evaluate(*statement.arguments[i], frame);
      require_type(value, physics ? Type::Count :
          (quantum_gate ? Type::Qubit : definition->parameters[i].type),
          statement.arguments[i]->at);
      values.push_back(value);
    }
    if ((statement.name == "cx" || statement.name == "cz") && values[0].known && values[1].known && values[0].number == values[1].number)
      fail(statement.at, "'" + statement.name + "' requires distinct qubits");
    return values;
  }
  void validate_root(const Statement& statement, Frame& frame) {
    switch (statement.kind) {
      case Statement::Kind::Backend:
        if (backend_declared_) fail(statement.at, "backend already declared");
        if (qubits_declared_) fail(statement.at, "backend must be declared before qubits");
        if (statement.name != "compact" && statement.name != "reduced") fail(statement.at, "expected backend compact or reduced");
        result_.backend = statement.name;
        backend_at_ = statement.at;
        backend_declared_ = true;
        return;
      case Statement::Kind::Qubits: {
        if (qubits_declared_) fail(statement.at, "qubits already declared");
        const auto count = evaluate(*statement.expression, frame);
        require_type(count, Type::Count, statement.expression->at);
        const std::uint64_t limit = result_.backend == "reduced" ? 12 : mx_max_compact_qubits;
        if (!count.known || count.number == 0 || count.number > limit)
          fail(statement.at, "backend " + result_.backend + " requires 1.." + std::to_string(limit) + " qubits");
        result_.qubits = static_cast<std::size_t>(count.number);
        qubits_at_ = statement.at;
        qubits_declared_ = true;
        frame.register_visible = true;
        return;
      }
      case Statement::Kind::Let: {
        if (globals_.count(statement.name) || circuits_.count(statement.name)) fail(statement.at, "duplicate global name '" + statement.name + "'");
        const auto value = evaluate(*statement.expression, frame);
        globals_.emplace(statement.name, Global{value, ++frame.globals});
        return;
      }
      case Statement::Kind::Circuit: {
        auto& circuit = circuits_.at(statement.name);
        circuit.globals = frame.globals;
        circuit.register_visible = frame.register_visible;
        return;
      }
      default: validate(statement, frame, nullptr); return;
    }
  }
  void validate_block(const std::vector<Statement>& statements, Frame& frame, Circuit* circuit) {
    for (const auto& statement : statements) validate(statement, frame, circuit);
  }
  void validate(const Statement& statement, Frame& frame, Circuit* circuit) {
    switch (statement.kind) {
      case Statement::Kind::Backend:
      case Statement::Kind::Qubits:
      case Statement::Kind::Circuit:
        fail(statement.at, "declaration is only allowed at the program root");
      case Statement::Kind::Let:
        declare(frame, statement.name, evaluate(*statement.expression, frame), statement.at);
        return;
      case Statement::Kind::Repeat: {
        const auto count = evaluate(*statement.expression, frame);
        require_type(count, Type::Count, statement.expression->at);
        frame.scopes.emplace_back();
        if (!statement.name.empty()) declare(frame, statement.name, {Type::Count, 0, false}, statement.at);
        validate_block(statement.body, frame, circuit);
        frame.scopes.pop_back();
        return;
      }
      default: break;
    }
    if (!frame.register_visible && !frame.circuit_body &&
        !(statement.kind == Statement::Kind::Invoke && physics_command(statement.name)))
      fail(statement.at, "declare qubits before operations");
    if (statement.kind == Statement::Kind::Invoke) {
      arguments(statement, frame);
      if (physics_command(statement.name)) scattering_seen_ = true;
      if (!gate(statement.name) && !physics_command(statement.name) && circuit)
        circuit->calls.emplace_back(statement.name, statement.at);
    } else if (statement.kind == Statement::Kind::Assert) assertion(statement);
  }
  std::size_t check_calls(const std::string& name, std::size_t depth, MxSourceLocation at) {
    if (depth > mx_max_call_depth) fail(at, "circuit call depth limit exceeded (32)");
    auto& circuit = circuits_.at(name);
    if (circuit.color == 1) fail(at, "recursive circuit call involving '" + name + "'");
    if (circuit.color == 2) {
      if (depth - 1 + circuit.height > mx_max_call_depth) fail(at, "circuit call depth limit exceeded (32)");
      return circuit.height;
    }
    circuit.color = 1;
    std::size_t height = 1;
    for (const auto& call : circuit.calls)
      height = std::max(height, 1 + check_calls(call.first, depth + 1, call.second));
    circuit.color = 2;
    circuit.height = height;
    return height;
  }
  void expand_block(const std::vector<Statement>& statements, Frame& frame, std::size_t call_depth) {
    for (const auto& statement : statements) expand(statement, frame, call_depth, false);
  }
  void expand(const Statement& statement, Frame& frame, std::size_t call_depth, bool root) {
    work(statement.at);
    switch (statement.kind) {
      case Statement::Kind::Backend:
      case Statement::Kind::Circuit: return;
      case Statement::Kind::Qubits: frame.register_visible = true; return;
      case Statement::Kind::Let:
        if (root) ++frame.globals;
        else declare(frame, statement.name, evaluate(*statement.expression, frame), statement.at);
        return;
      case Statement::Kind::Repeat: {
        const auto count = evaluate(*statement.expression, frame);
        for (std::uint64_t i = 0; i < count.number; ++i) {
          work(statement.at);  // Empty iterations are work too.
          frame.scopes.emplace_back();
          if (!statement.name.empty()) declare(frame, statement.name, {Type::Count, i, true}, statement.at);
          expand_block(statement.body, frame, call_depth);
          frame.scopes.pop_back();
        }
        return;
      }
      case Statement::Kind::Invoke: {
        const auto values = arguments(statement, frame);
        if (gate(statement.name) || physics_command(statement.name)) {
          if (statement.name == "t" && result_.t_gates >= 128) fail(statement.at, "reduced program exceeds 128 T gates");
          std::string line = statement.name;
          for (const auto& value : values) line += " " + std::to_string(value.number);
          emit(line, statement.at);
          ++result_.instructions;
          if (statement.name == "t") ++result_.t_gates;
        } else {
          if (call_depth >= mx_max_call_depth) fail(statement.at, "circuit call depth limit exceeded (32)");
          const auto& circuit = circuits_.at(statement.name);
          Frame called;
          called.globals = circuit.globals;
          // A circuit may be defined before the register, and receive qubit parameters.
          called.register_visible = circuit.register_visible;
          called.circuit_body = true;
          for (std::size_t i = 0; i < values.size(); ++i)
            declare(called, circuit.statement->parameters[i].name, values[i], circuit.statement->parameters[i].at);
          expand_block(circuit.statement->body, called, call_depth + 1);
        }
        return;
      }
      case Statement::Kind::Show: emit("show", statement.at); return;
      case Statement::Kind::Bytes: emit("bytes", statement.at); return;
      case Statement::Kind::Assert: emit("assert " + statement.name, statement.at); return;
    }
  }
  std::vector<Statement> statements_;
  std::unordered_map<std::string, Global> globals_;
  std::unordered_map<std::string, Circuit> circuits_;
  MxCompiledProgram result_;
  MxSourceLocation backend_at_;
  MxSourceLocation qubits_at_;
  bool backend_declared_ = false;
  bool qubits_declared_ = false;
  bool scattering_seen_ = false;
  bool expanding_ = false;
  std::size_t work_ = 0;
};

}  // namespace mx_detail

inline MxCompiledProgram compile_mx(std::istream& input) {
  std::string source;
  MxSourceLocation at;
  char c;
  while (input.get(c)) {
    if (source.size() >= mx_max_source_bytes) mx_detail::fail(at, "source byte limit exceeded (1 MiB)");
    source += c;
    if (c == '\n') { ++at.line; at.column = 1; }
    else ++at.column;
  }
  if (input.bad()) mx_detail::fail(at, "cannot read .mx source");
  mx_detail::Parser parser(mx_detail::tokenize(source));
  mx_detail::Compiler compiler(parser.parse());
  return compiler.compile();
}

inline MxCompiledProgram compile_mx_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) mx_detail::fail({1, 1}, "cannot open .mx source: " + path);
  return compile_mx(file);
}

}  // namespace mundo_x
