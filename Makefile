CXXFLAGS := -Wall -g -O3 --std=c++0x
LDFLAGS := -lnuma -lpthread -ljemalloc
HEADERS := timer.hh

all: bench

%.o: %.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

bench: bench.o
	$(CXX) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f bench *.o
