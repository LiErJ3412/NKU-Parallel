CXX ?= g++
GNU_CXX ?= /opt/homebrew/bin/g++-14
MPICXX ?= $(shell command -v mpicxx 2>/dev/null || command -v /opt/local/bin/mpicxx 2>/dev/null || command -v /opt/homebrew/bin/mpicxx 2>/dev/null || echo mpicxx)
CXXFLAGS ?= -std=c++17 -O2
SRC = main.cpp train.cpp guessing.cpp md5.cpp

.PHONY: all pthread serial openmp mpi clean

all: main_pthread main_serial main_openmp

pthread: main_pthread
serial: main_serial
openmp: main_openmp
mpi: main_mpi

main_pthread: $(SRC) PCFG.h md5.h
	$(CXX) $(CXXFLAGS) -pthread $(SRC) -o $@

main_serial: $(SRC) PCFG.h md5.h
	$(CXX) $(CXXFLAGS) -DUSE_SERIAL_HASH -pthread $(SRC) -o $@

main_openmp: $(SRC) PCFG.h md5.h
	$(GNU_CXX) $(CXXFLAGS) -fopenmp $(SRC) -o $@

main_mpi: $(SRC) PCFG.h md5.h
	$(MPICXX) $(CXXFLAGS) -DUSE_MPI -pthread $(SRC) -o $@

clean:
	rm -f main_pthread main_serial main_openmp main_mpi
