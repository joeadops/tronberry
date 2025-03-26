CXX ?= g++
INCLUDES := -I/usr/local/include -I/opt/homebrew/include
LIBPATHS := -L/usr/local/lib -L/opt/homebrew/lib
LDFLAGS := -lwebp -lwebpdemux -lssl -lcrypto
CXXFLAGS := -O3 -W -Wall -Wextra -Wno-unused-parameter -D_FILE_OFFSET_BITS=64 -std=c++17 $(INCLUDES) $(LIBPATHS)
TARGET := tronberry
SRCS := main.cc startup.cc
RGB_LIB_DISTRIBUTION=rpi-rgb-led-matrix
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
RGB_LDFLAGS+=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

all: $(TARGET)

$(RGB_LIBRARY): check-and-reinit-submodules
	$(MAKE) -C $(RGB_LIBDIR)

$(TARGET): $(SRCS) $(RGB_LIBRARY)
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -o $(TARGET) $^ $(LDFLAGS) $(RGB_LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: check-and-reinit-submodules
check-and-reinit-submodules:
	@if git submodule status | egrep -q '^[-+]' ; then \
		echo "INFO: Need to reinitialize git submodules"; \
		git submodule update --init; \
	fi

.PHONY: all clean $(RGB_LIBRARY)
