import sys

import sys
import re

def print_banner():
    print("==================================")
    print(" Shield Language Compiler v1.0")
    print(" Self-hosted: smc.sm")
    print("==================================")

def shield_to_python(code):
    code = code.replace("import ", "import ")
    code = code.replace("def ", "def ")
    code = code.replace("if ", "if ")
    code = code.replace("else:", "else:")
    code = code.replace("else ", "else ")
    code = code.replace("while ", "while ")
    code = code.replace("print(", "print(")
    code = code.replace("input(", "input(")
    code = code.replace("sys.exit()", "sys.exit()")
    code = code.replace("sys.exit", "sys.exit")
    code = code.replace("return ", "return ")
    code = code.replace(" True", " True")
    code = code.replace(" False", " False")
    return code

def compile_file(filename):
    print("Reading: " + filename)
    f = open(filename, 'r')
    sm_code = f.read()
    f.close()

    py_code = "import sys\n\n" + shield_to_python(sm_code)
    out_filename = filename.replace(".sm", ".py")

    f = open(out_filename, 'w')
    f.write(py_code)
    f.close()

    print("Compiled: " + filename + " -> " + out_filename)
    return out_filename

def run_file(py_filename):
    print("Running: " + py_filename)
    print("----------------------------------")
    f = open(py_filename, 'r')
    py_code = f.read()
    f.close()
    exec(py_code)

def main():
    print_banner()
    if len(sys.argv) < 2:
        print("Usage: smc.sm <file.sm>")
        sys.exit()

    sm_file = sys.argv[1]
    if not sm_file.endswith(".sm"):
        print("Error: file must end with.sm")
        sys.exit()

    py_file = compile_file(sm_file)
    run_file(py_file)
    print("----------------------------------")
    print("Done.")

if __name__ == "__main__":
    main()
