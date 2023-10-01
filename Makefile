SRCS = sim.cpp pipeline.cpp bpred.cpp
OBJS = $(SRCS:.cpp=.o)

CXX = g++
CXXFLAGS = -g -Wall -Werror -pedantic -std=c++11
TARBALL = ../$(if $(USER),$(USER),gburdell3)-lab2A.tar.gz

.PHONY: all sim clean profile debug validate fvalidate fast submit

all: clean
all: sim

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

sim: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean: 
	-rm -f sim $(OBJS)

profile: clean
profile: CXXFLAGS += -O2 -pg
profile: all

debug: clean
debug: CXXFLAGS += -DDEBUG
debug: all

validate: clean
validate: all
validate: 
	@bash ../scripts/runtests.sh

fvalidate: clean
fvalidate: fast
fvalidate:
	@bash ../scripts/runtests.sh

fast: clean
fast: CXXFLAGS += -O2
fast: all

submit:
	tar -czvf $(TARBALL) -C .. src
	@echo 'Created! Please check the tarball to ensure it was made correctly!'
	@echo 'You are solely responsible for what you submit!'