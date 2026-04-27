CXX = g++-9
CXXFLAGS = -O3 -std=gnu++2a -Wall

all: main

main: main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f main
