
CXX = g++
SRC = procsim.cpp procsim_driver.cpp
INCLUDE = procsim.h
CXXFLAGS := -Wall -Wextra -Wconversion -std=c++11 # -Werror
DEBUG := -g -O0
#RELEASE := -O3

build:
	$(CXX) $(RELEASE) $(CXXFLAGS) $(SRC) $(INCLUDE) -o procsim

debug:
	$(CXX) $(DEBUG) $(CXXFLAGS) $(SRC) $(INCLUDE) -o procsim

clean:
	rm -f procsim *.o

# This looks for a report pdf in the top level directory of the project
.PHONY: submit

submit:
	tar -cvzf project3-submit.tar.gz procsim.cpp procsim_driver.cpp procsim.hpp \
				Makefile report.pdf
