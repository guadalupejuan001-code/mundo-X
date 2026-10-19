#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "mundo_x/basis_vm.hpp"

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      const auto report = mundo_x::basis_vm::self_test_json();
      std::cout << report << '\n';
      return report.find("\"status\":\"PASS\"") != std::string::npos ? 0 : 1;
    }
    if (argc == 3 && std::string(argv[1]) == "--example") {
      const std::string index = argv[2];
      if (index != "0" && index != "1" && index != "2")
        throw std::invalid_argument("El ejemplo debe ser 0, 1 o 2.");
      std::cout << mundo_x::basis_vm::example_source(index[0] - '0');
      return 0;
    }
    if (argc < 2 || argc > 3) {
      std::cerr << "Uso: mundo_x_vm programa.mxvm [max_pasos]\n"
                   "     mundo_x_vm --self-test\n"
                   "     mundo_x_vm --example 0|1|2\n";
      return 2;
    }
    int max_steps = 1000;
    if (argc == 3) {
      const std::string value = argv[2];
      std::size_t used = 0;
      max_steps = std::stoi(value, &used);
      if (used != value.size() || max_steps < 1 || max_steps > 10000)
        throw std::invalid_argument("max_pasos debe estar entre 1 y 10000.");
    }
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("No se puede abrir el programa.");
    std::string source;
    char character;
    while (input.get(character)) {
      source.push_back(character);
      if (source.size() > 16384)
        throw std::runtime_error("El programa supera 16 KiB.");
    }
    const auto report = mundo_x::basis_vm::run_json(source, max_steps);
    std::cout << report << '\n';
    return report.find("\"status\":\"PASS\"") != std::string::npos ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 2;
  }
}
