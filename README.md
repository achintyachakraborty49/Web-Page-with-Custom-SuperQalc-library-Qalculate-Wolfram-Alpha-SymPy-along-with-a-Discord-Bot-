
# Advikmathlib

Advikmathlib is a Python wrapper around the SuperQalc C++ math libraries (`superqalc_onefile` and `superqalc_tower`).  
It allows you to run advanced math calculations from Python by invoking the compiled binaries.

---

## Features

- High precision math calculations using SuperQalc
- Supports large integer and floating point operations
- Two engines: Onefile (simple) and Tower (advanced)
- Easy to install and use as a Python package

---

## Building

### Requirements

- C++ compiler (g++)
- GMP and MPFR libraries installed on your system
- pybind11 installed (`pip install pybind11`)
- setuptools

### Build Steps

1. Compile the SuperQalc binaries (`superqalc_onefile` and `superqalc_tower`):

```bash
g++ superqalc_onefile.cpp -o superqalc_onefile -lmpfr -lgmp
g++ superqalc_tower.cpp -o superqalc_tower -lmpfr -lgmp

2. Compile the pybind11 wrapper:



c++ -O3 -Wall -shared -std=c++17 -fPIC \
    `python3 -m pybind11 --includes` wrapper.cpp \
    -o advikmathlib$(python3-config --extension-suffix)

3. Organize files:



Place the binaries in the advikmathlib/ folder.

Make sure binaries are executable (chmod +x advikmathlib/superqalc_onefile advikmathlib/superqalc_tower).



---

Installation

You can install the package locally using:

pip install .


---


