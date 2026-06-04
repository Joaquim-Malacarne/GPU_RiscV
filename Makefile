CXX      = g++
CXXFLAGS = -std=c++17 -O2 -march=native -Wall -Wextra
LDFLAGS  = -lSDL2 -lpthread

TARGET = gpu_v
SRCS   = main.cpp RV32ICore.cpp gpu.cpp geom_shader.cpp frag_shader.cpp janela.cpp
OBJS   = $(SRCS:.cpp=.o)

# benchmark — headless, sem SDL2
BENCH_TARGET = benchmark
BENCH_SRCS   = benchmark.cpp RV32ICore.cpp geom_shader.cpp frag_shader.cpp
BENCH_OBJS   = $(BENCH_SRCS:.cpp=.bench.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

benchmark: $(BENCH_OBJS)
	$(CXX) $(CXXFLAGS) -o $(BENCH_TARGET) $^ -lpthread

# Regra genérica para objetos do benchmark (sem SDL2, evita conflito com OBJS)
%.bench.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run-benchmark: benchmark
	./$(BENCH_TARGET) 2>&1 | tee TESTES_BENCHMARK/benchmark_stdout.txt

clean:
	rm -f $(OBJS) $(BENCH_OBJS) $(TARGET) $(BENCH_TARGET) \
	      TESTES_BENCHMARK/benchmark_report.txt \
	      TESTES_BENCHMARK/benchmark_results.csv \
	      TESTES_BENCHMARK/benchmark_stdout.txt

.PHONY: all benchmark run-benchmark clean
