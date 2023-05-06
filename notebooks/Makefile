.PHONY: clean

libIPUpy_dir=../libIPUpy

all: lib multikernel_codelets.gp

multikernel_codelets.gp:multikernel_codelets.cpp multikernel_params.hpp
	$(libIPUpy_dir)/IPUpyc -i multikernel_codelets.cpp -o $@ -O2 -Wall -Wextra -I./

lib: multikernel_host.cpp multikernel_params.hpp
	g++ multikernel_host.cpp -fPIC -shared -std=c++17 -lpoplar -Wall -Wextra -o libmultikernel.so

clean:
	rm multikernel_codelets.gp libmultikernel.so
