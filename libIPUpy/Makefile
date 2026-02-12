.PHONY: make_build_dir gen_hdrs clean

BUILDDIR=build

OBJ = $(BUILDDIR)/libIPUpy.gp


HARDCODEVARS=-DNDEBUG -DFFCONF_H=\"lib/oofatfs/ffconf.h\" -DMICROPY_ROM_TEXT_COMPRESSION=1
WARNFLAGS=-Wall -Wdouble-promotion -Wfloat-conversion -Wno-unused-function -Werror #-Wfatal-errors
INC= -Imicropython -Ibuild -Imicropython/py -I.
LLVMFLAGS=-X -ffunction-sections -X -fdata-sections
POPCFLAGS=--target=ipu21 -Os $(INC) $(WARNFLAGS) $(HARDCODEVARS) $(LLVMFLAGS)

default: make_build_dir gen_hdrs $(BUILDDIR)/runcodelets $(OBJ)
	popc $(POPCFLAGS) -o $(BUILDDIR)/main.gp $(OBJ)

make_build_dir:
	mkdir -p $(BUILDDIR)/py $(BUILDDIR)/shared/runtime

gen_hdrs:
	make -f GENMakefile build/genhdr/compressed.data.h
	make -f GENMakefile build/genhdr/qstrdefs.generated.h
	make -f GENMakefile build/genhdr/moduledefs.h

$(BUILDDIR)/runcodelets: runcodelets.cpp
	g++ runcodelets.cpp -std=c++17 -O2 -lpoplar -o $(BUILDDIR)/runcodelets

$(BUILDDIR)/%.gp: %.c gen_hdrs
	popc $(POPCFLAGS) -o $@ $< 


clean: 
	rm -r $(BUILDDIR)
