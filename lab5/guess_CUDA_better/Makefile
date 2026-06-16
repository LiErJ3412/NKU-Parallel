CXX ?= g++
GNU_CXX ?= $(shell command -v g++-14 2>/dev/null || command -v /opt/homebrew/bin/g++-14 2>/dev/null || command -v g++ 2>/dev/null || echo g++)
MPICXX ?= $(shell command -v mpicxx 2>/dev/null || command -v /opt/local/bin/mpicxx 2>/dev/null || command -v /opt/homebrew/bin/mpicxx 2>/dev/null || echo mpicxx)
NVCC ?= nvcc
NVCC_HOST_CXX ?= $(shell command -v g++-10 2>/dev/null || command -v g++-9 2>/dev/null || command -v g++-8 2>/dev/null || command -v g++ 2>/dev/null || echo g++)
CXXFLAGS ?= -std=c++17 -O2
NVCCFLAGS ?= -std=c++17 -O2
SRC = main.cpp train.cpp guessing.cpp md5.cpp
CUDA_SRC = main.cpp train.cpp guessing.cpp md5.cpp md5_cuda_wrapper.cpp md5_cuda.cu
CUDA_OBJS = main_cuda_main.o main_cuda_train.o main_cuda_guessing.o main_cuda_md5.o main_cuda_md5_cuda_wrapper.o main_cuda_md5_cuda.o

.PHONY: all pthread serial openmp mpi cuda clean

all: main_pthread main_serial main_openmp

pthread: main_pthread
serial: main_serial
openmp: main_openmp
mpi: main_mpi
cuda: main_cuda

main_pthread: $(SRC) PCFG.h md5.h
	$(CXX) $(CXXFLAGS) -pthread $(SRC) -o $@

main_serial: $(SRC) PCFG.h md5.h
	$(CXX) $(CXXFLAGS) -DUSE_SERIAL_HASH -pthread $(SRC) -o $@

main_openmp: $(SRC) PCFG.h md5.h
	$(GNU_CXX) $(CXXFLAGS) -fopenmp $(SRC) -o $@

main_mpi: $(SRC) PCFG.h md5.h
	$(MPICXX) $(CXXFLAGS) -DUSE_MPI -pthread $(SRC) -o $@

main_cuda: $(CUDA_OBJS)
	$(NVCC) $(NVCCFLAGS) -ccbin $(NVCC_HOST_CXX) $(CUDA_OBJS) -Xcompiler -pthread -o $@

main_cuda_main.o: main.cpp PCFG.h md5.h
	$(CXX) $(CXXFLAGS) -DUSE_CUDA_HASH -pthread -c main.cpp -o $@

main_cuda_train.o: train.cpp PCFG.h
	$(CXX) $(CXXFLAGS) -DUSE_CUDA_HASH -pthread -c train.cpp -o $@

main_cuda_guessing.o: guessing.cpp PCFG.h
	$(CXX) $(CXXFLAGS) -DUSE_CUDA_HASH -pthread -c guessing.cpp -o $@

main_cuda_md5.o: md5.cpp md5.h
	$(CXX) $(CXXFLAGS) -DUSE_CUDA_HASH -pthread -c md5.cpp -o $@

main_cuda_md5_cuda_wrapper.o: md5_cuda_wrapper.cpp md5.h
	$(CXX) $(CXXFLAGS) -DUSE_CUDA_HASH -pthread -c md5_cuda_wrapper.cpp -o $@

main_cuda_md5_cuda.o: md5_cuda.cu md5.h
	$(NVCC) $(NVCCFLAGS) -ccbin $(NVCC_HOST_CXX) -DUSE_CUDA_HASH -c md5_cuda.cu -o $@

clean:
	rm -f main_pthread main_serial main_openmp main_mpi main_cuda $(CUDA_OBJS)
