# Run `make debug` to include -g for GDB
# Run `make release` for optimized build (default)
CXX ?= g++
INCLUDES := -I/usr/local/include -I/opt/homebrew/include -Ilibs
LIBPATHS := -L/usr/local/lib -L/opt/homebrew/lib
LDFLAGS := $(LIBPATHS) -lwebp -lwebpdemux -lssl -lcrypto
CFLAGS=-W -Wall -Wextra -Wno-unused-parameter -O3 -fPIC -march=native
TARGET := tronberry
SRCS := main.cc startup.cc

# Build modes
all: release

debug: CXXFLAGS = -O0 -g -Wall -Wextra -Wno-unused-parameter -fno-exceptions -std=c++23
debug: $(TARGET)

release: CXXFLAGS = -O3 -Wall -Wextra -Wno-unused-parameter -fno-exceptions -std=c++23
release: $(TARGET)
	strip $(TARGET)

RGB_LIB_DISTRIBUTION=libs/rpi-rgb-led-matrix
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
RGB_LDFLAGS+=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread
IXWEBSOCKET_LIBRARY=libs/IXWebSocket/libixwebsocket.a
IXWEBSOCKET_LDFLAGS=-Llibs/IXWebSocket -lixwebsocket -lz
IXWEBSOCKET_INCDIR=libs/IXWebSocket
CPPFLAGS=-D_FILE_OFFSET_BITS=64 -DCPPHTTPLIB_OPENSSL_SUPPORT -DCPPHTTPLIB_NO_EXCEPTIONS -DCPPHTTPLIB_NO_DEFAULT_USER_AGENT -DCPPHTTPLIB_ZLIB_SUPPORT -DIXWEBSOCKET_USE_TLS -DIXWEBSOCKET_USE_OPEN_SSL -DIXWEBSOCKET_USE_ZLIB -DJSON_NOEXCEPTION -DJSON_NO_IO $(INCLUDES) -I$(RGB_INCDIR) -I$(IXWEBSOCKET_INCDIR)

.PHONY: all clean $(RGB_LIBRARY) check-and-reinit-submodules

all: $(TARGET)

IXWEBSOCKET_OBJS := $(patsubst %.cpp,%.o,$(wildcard libs/IXWebSocket/ixwebsocket/*.cpp))

$(IXWEBSOCKET_LIBRARY): check-and-reinit-submodules $(IXWEBSOCKET_OBJS)
	@echo "Creating IXWebSocket static library...";
	$(AR) rcs $@ $(filter-out check-and-reinit-submodules,$^)

$(RGB_LIBRARY): check-and-reinit-submodules
	$(MAKE) -C $(RGB_LIBDIR) CFLAGS="$(CFLAGS) -DDEFAULT_HARDWARE='\"regular\"'"

OBJS := $(SRCS:.cc=.o)

$(TARGET): $(OBJS) $(RGB_LIBRARY) $(IXWEBSOCKET_LIBRARY)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS) $(RGB_LDFLAGS) $(IXWEBSOCKET_LDFLAGS) -latomic

clean: check-and-reinit-submodules
	rm -f $(TARGET)
	$(MAKE) -C $(RGB_LIBDIR) clean
	find . -name '*.o' -delete
	find . -name '*.a' -delete

check-and-reinit-submodules:
	@if git submodule status | egrep -q '^[-+]' ; then \
		echo "INFO: Need to reinitialize git submodules"; \
		git submodule update --init; \
	fi
