# Makefile for L1simulate

CXX = g++
CXXFLAGS = -Wall -O2 -std=c++11
LDFLAGS = 

# Directories for headers and sources
INCDIR = include
SRCDIR = src

# List source files (adjust if file locations change)
SOURCES = main.cpp $(SRCDIR)/Cache.cpp $(SRCDIR)/Core.cpp $(SRCDIR)/Bus.cpp $(SRCDIR)/Simulator.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = L1simulate

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
