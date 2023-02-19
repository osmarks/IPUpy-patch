.PHONY: clean

EXAMPLES = example1 example2 example4 example5 example6 example7 multirepl

CODELETS = $(addsuffix _codelets.gp, $(EXAMPLES))

PREPROCESSOR = #--external_preprocessor ./example3

CFLAGS = -std=c++17 -lpoplar -Wall -Wextra

all: $(EXAMPLES) example3 

example1: example1_host.cpp example1_codelets.gp
	g++ example1_host.cpp $(CFLAGS) -o $@

example2: example2_host.cpp example2_codelets.gp
	g++ example2_host.cpp $(CFLAGS) -o $@

example3: example3_host.cpp example3_codelets.gp
	g++ example3_host.cpp $(CFLAGS) -o $@

example4: example4_host.cpp example4_codelets.gp
	g++ example4_host.cpp $(CFLAGS) -o $@

example5: example5_host.cpp example5_codelets.gp
	g++ example5_host.cpp $(CFLAGS) -o $@

example6: example6_host.cpp example6_codelets.gp
	g++ example6_host.cpp $(CFLAGS) -o $@

example7: example7_host.cpp example7_codelets.gp example7.hpp
	g++ example7_host.cpp $(CFLAGS) -o $@ 

example7_codelets.gp : example7_codelets.cpp example7.hpp
	../IPUpyc -i $< -o example7_codelets.gp -O2 -Wall -Wextra -I./

multirepl: multirepl_host.cpp multirepl_codelets.gp
	g++ multirepl_host.cpp $(CFLAGS) -o $@

%.gp : %.cpp
	../IPUpyc -i $< -o $@ $(PREPROCESSOR) -O2 -Wall -Wextra -I./

clean:
	rm $(EXAMPLES) $(CODELETS)
