.PHONY: clean

libIPUpy_dir=../libIPUpy

all: lib multikernel_codelets.gp

multikernel_codelets.gp : multikernel_codelets.cpp
	$(libIPUpy_dir)/IPUpyc -i $< -o $@ -O2 -Wall -Wextra -I./

lib:
	g++ multikernel_host.cpp -fPIC -shared -std=c++17 -lpoplar -Wall -Wextra -o libmultikernel.so

clean:
	rm multikernel_codelets.gp libmultikernel.so
