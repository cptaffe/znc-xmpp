CXX := clang++
CXXFLAGS := -I/usr/include/libxml2 -fPIC --std=c++11
LIBS := -lxml2

SRCS := Stanza.cpp Socket.cpp Client.cpp Listener.cpp JID.cpp xmpp.cpp
SRCS := $(addprefix src/,$(SRCS))
OBJS := $(patsubst %cpp,%o,$(SRCS))

.PHONY: all clean

all: xmpp.so
	@echo "Module complete (xmpp.so)"

xmpp.so: $(OBJS)
	@echo Linking $@
	@$(CXX) $(LIBS) -g -o $@ $(OBJS) -shared

src/%.o: src/%.cpp Makefile
	@mkdir -p .depend
	@echo Building $@
	@$(CXX) $(CXXFLAGS) -c $< -g -o $@ -MD -MF .depend/$*.dep -MT $@

clean:
	rm src/*.o *.so
	rm -r .depend

-include $(wildcard .depend/*.dep)
