CC = clang
CXX = clang++

CFLAGS := $(CFLAGS) -g -O3 -Wall -Wextra -pedantic -Werror -std=c18 -pthread
CXXFLAGS := $(CXXFLAGS) -g -O3 -Wall -Wextra -pedantic -Werror -std=c++20 -pthread
DEBUGFLAGS := \
	-Winvalid-pch \
	-Wall \
	-Wextra \
	-pedantic \
	-Wshadow \
	-Wformat=2 \
	-Wfloat-equal \
	-Wconversion \
	-Wshift-overflow \
	-Wcast-qual \
	-Wcast-align \
	-D_GLIBCXX_DEBUG \
	-D_GLIBCXX_DEBUG_PEDANTIC \
	-D_FORTIFY_SOURCE=2 \
	-fsanitize=undefined \
	-fsanitize-undefined-trap-on-error \
	-fno-sanitize-recover \
	-fstack-protector-strong

BUILDDIR = build

SRCS = main.cpp engine.cpp io.cpp

all: engine client

engine: $(SRCS:%=$(BUILDDIR)/%.o)
	$(LINK.cc) $^ $(LOADLIBES) $(LDLIBS) -o $@

client: $(BUILDDIR)/client.cpp.o
	$(LINK.cc) $^ $(LOADLIBES) $(LDLIBS) -o $@

.PHONY: clean
clean:
	rm -rf $(BUILDDIR)
	rm -f client engine

DEPFLAGS = -MT $@ -MMD -MP -MF $(BUILDDIR)/$<.d
COMPILE.cpp = $(CXX) $(DEPFLAGS) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) $(DEBUGFLAGS) -c

$(BUILDDIR)/%.cpp.o: %.cpp | $(BUILDDIR)
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<

$(BUILDDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:%=$(BUILDDIR)/%.d) $(BUILDDIR)/client.cpp.d

-include $(DEPFILES)
