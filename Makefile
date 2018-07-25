GCCDIR = /home/dkozovsk/gcc-plugins/gcc-install/bin
CXX = g++
CC = gcc
CXXFLAGS = -std=c++11 -Wall -fno-rtti -Wno-literal-suffix
PLUGINDIR=$(shell $(CXX) -print-file-name=plugin)
CXXFLAGS += -I$(PLUGINDIR)/include
 
all: handler_plug.so
 
handler_plug.so: handler_plug.o
	$(CXX) $(LDFLAGS) -shared -o $@ $<
 
handler_plug.o : handler_plug.cc handler_plug.hh
	$(CXX) $(CXXFLAGS) -fPIC -c -o $@ $<
 
clean:
	rm -f handler_plug.o handler_plug.so 

print: handler_plug.so
	$(CC) -fplugin=./handler_plug.so --version

check: handler_plug.so
	$(CC) -fplugin=./handler_plug.so -c test.c -o /dev/null

check2: handler_plug.so
	$(CC) -fplugin=./handler_plug.so -c test2.c -o /dev/null
