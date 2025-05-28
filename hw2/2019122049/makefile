CXX = g++
CXXFLAGS = -std=c++11 -O2 -pthread
LDFLAGS =

all: attention attention_mp multiHeadAttention

attention: attention.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

attention_mp: attention_mp.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

multiHeadAttention: multiHeadAttention.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f attention attention_mp multiHeadAttention *.o input_*.txt output_*.txt
