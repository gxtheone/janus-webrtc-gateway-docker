TARGET = libsrsrtmp.so


INCLUDES = -I.

CC = gcc
CXX = g++
CC_FLAGS += -g -Wall -fPIC

SOURCE  := $(wildcard *.c) $(wildcard *.cpp)
OBJS    := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE)))

.PHONY: all clean

all:$(TARGET)
	

$(TARGET):$(OBJS)
	$(CXX) -shared -o $@ $^ $(INCLUDES)

%.o:%.cpp
	$(CXX) -c $(CC_FLAGS) $(INCLUDES) $< -o $@

clean:
	rm -f $(SRCS:.c=.o) $(TARGET)

install:
	cp srs_librtmp.h /usr/local/include
	cp libsrsrtmp.so /usr/local/lib
 
