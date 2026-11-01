CXX = "C:/Program Files/CodeBlocks/MinGW/bin/g++.exe"
CXXFLAGS = -O3 -mavx2 -mfma -mbmi2 -mpopcnt -mstackrealign -pthread -static -std=c++20 -Wall -Wextra -Wno-unused-parameter
SRC = $(wildcard src/*.cpp)
TARGET = builds/nexus-chess.exe

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	cmd /c "if exist builds\nexus-chess.exe del /f /q builds\nexus-chess.exe"

.PHONY: all clean
