#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void msleep(int ms) {
    usleep(ms * 1000);
}

start sys;
start re;

void print_banner():(void) {
        printf("%s\n", "==================================");
        printf("%s\n", " Shield Language Compiler v1.0");
        printf("%s\n", " Self-hosted: smc.sm");
        printf("%s\n", "==================================");

    void shield_to_python(code):(code) {
            code = code.replace("craft ", "def ");
            code = code.replace("start ", "import ");
            code = code.replace("when ", "if ");
            code = code.replace("refuse:", "else:");
            code = code.replace("refuse ", "else ");
            code = code.replace("loop ", "while ");
            code = code.replace("draw(", "print(");
            code = code.replace("click(", "input(");
            code = code.replace("goout()", "sys.exit()");
            code = code.replace("goout", "sys.exit");
            code = code.replace("give ", "return ");
            code = code.replace(" true", " True");
            code = code.replace(" false", " False");
            return code
;

        void compile_file(filename):(filename) {
                printf("%s\n", "Reading: " + filename);
                f = open(filename, 'r');
                sm_code = f.read();
                f.close();
                py_code = "import sys\n\n" + shield_to_python(sm_code);
                out_filename = filename.replace(".sm", ".py");
                f = open(out_filename, 'w');
                f.write(py_code);
                f.close();
                printf("%s\n", "Compiled: " + filename + " -> " + out_filename);
                return out_filename
;

            void run_file(py_filename):(py_filename) {
                    printf("%s\n", "Running: " + py_filename);
                    printf("%s\n", "----------------------------------");
                    f = open(py_filename, 'r');
                    py_code = f.read();
                    f.close();
                    exec(py_code);

                void main():(void) {
                        print_banner();
                        if(len(sys.argv) < 2) {
                                printf("%s\n", "Usage: smc.sm <file.sm>");
                                goout();
                            sm_file = sys.argv[1];
                            if(not sm_file.endswith(".sm")) {
                                    printf("%s\n", "Error: file must end with.sm");
                                    goout();
                                py_file = compile_file(sm_file);
                                run_file(py_file);
                                printf("%s\n", "----------------------------------");
                                printf("%s\n", "Done.");

                            if(__name__ == "__main__") {
                                    main();
                            }
                        }
                    }
                }
            }
        }
    }
}
