.PHONY: clean

libIPUpy_dir=../libIPUpy

POPCFLAGS = -Wall -Wextra -I./ -I$(libIPUpy_dir) --target=ipu21 -O2

multirepl: multirepl_host.cpp multirepl_codelets.gp
	g++ multirepl_host.cpp -std=c++17 -lpoplar -Wall -Wextra -o multirepl

multirepl_codelets.gp : multirepl_codelets.cpp
	popc  multirepl_codelets.cpp -o multirepl_stub_codelets.gp $(POPCFLAGS)
	popc multirepl_stub_codelets.gp $(libIPUpy_dir)/build/libIPUpy.gp -o multirepl_codelets.gp $(POPCFLAGS)

clean:
	rm multirepl *_codelets.gp
