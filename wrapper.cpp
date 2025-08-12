#include <pybind11/pybind11.h>
#include <string>
#include <stdexcept>
#include <array>
#include <cstdio>
#include <memory>

namespace py = pybind11;

std::string run_command_stdin(const std::string &cmd, const std::string &input) {
    FILE *pipe = popen(cmd.c_str(), "w+");  // open pipe for read/write
    if (!pipe) throw std::runtime_error("popen() failed!");

    // Write input to stdin of the command
    fwrite(input.c_str(), 1, input.size(), pipe);
    fflush(pipe);
    // Close writing side to send EOF to the program
    pclose(pipe);

    // Re-open the command to read output
    pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen() failed on read!");

    std::string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

std::string run_superqalc_onefile(const std::string &input) {
    return run_command_stdin("./advikmathlib/superqalc_onefile", input);
}

std::string run_superqalc_tower(const std::string &input) {
    return run_command_stdin("./advikmathlib/superqalc_tower", input);
}

PYBIND11_MODULE(Advikmathlib, m) {
    m.doc() = "Advik's math library with superqalc";
    m.def("onefile", &run_superqalc_onefile, "Run superqalc_onefile with stdin input");
    m.def("tower", &run_superqalc_tower, "Run superqalc_tower with stdin input");
}
