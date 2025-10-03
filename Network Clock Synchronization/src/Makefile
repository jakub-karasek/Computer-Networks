CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17

TARGETS = peer-time-sync
SRC = peer-time-sync.cpp socket_utility.cpp messages.cpp

all: $(TARGETS)

peer-time-sync: $(SRC) socket_utility.h messages.h
	$(CXX) $(CXXFLAGS) -o peer-time-sync $(SRC)

clean:
	rm -f $(TARGETS)