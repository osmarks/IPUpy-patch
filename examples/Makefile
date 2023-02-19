.PHONY: clean

libIPUpy_dir=../libIPUpy

multirepl: multirepl_host.cpp multirepl_codelets.gp
	g++ multirepl_host.cpp -std=c++17 -lpoplar -Wall -Wextra -o multirepl

multirepl_codelets.gp : multirepl_codelets.cpp
	$(libIPUpy_dir)/IPUpyc -i $< -o $@ -O2 -Wall -Wextra -I./

clean:
	rm multirepl multirepl_codelets.gp
