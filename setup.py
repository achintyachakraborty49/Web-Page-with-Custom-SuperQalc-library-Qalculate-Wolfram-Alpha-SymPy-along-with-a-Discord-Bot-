from setuptools import setup, Extension
import pybind11

ext_modules = [
    Extension(
        "Advikmathlib",
        ["wrapper.cpp"],  # Only compile the wrapper here
        include_dirs=[
            pybind11.get_include(),
            pybind11.get_include(user=True)
        ],
        language="c++",
        extra_compile_args=["-std=c++17"]
    )
]

setup(
    name="Advikmathlib",
    version="0.1.0",
    author="Advik",
    description="Advik's math library with superqalc",
    ext_modules=ext_modules,
    zip_safe=False,
)
