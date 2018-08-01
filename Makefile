CXX = g++
PLUGINDIR=$(shell $(CXX) -print-file-name=plugin)
 
all: handler_plug.so
 
handler_plug.so: handler_plug.o
	$(CXX) $(LDFLAGS) -shared -o $@ $<
 
handler_plug.o : handler_plug.cc handler_plug.hh
	$(CXX) $(CXXFLAGS) -std=c++11 -Wall -fno-rtti -Wno-literal-suffix -I$(PLUGINDIR)/include -fPIC -c -o $@ $<
	
test: handler_plug.so
	cd ./tests && cmake . && make test
 
clean:
	rm -f handler_plug.o handler_plug.so 
