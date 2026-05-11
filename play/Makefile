.PHONY: all clean help build-6 build-7 build-8 build-9 run run-6 run-7 run-8 run-9

# Build play/play_greedy_{6,7,8,9}
TUPLE_TYPE ?= 6

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  CXX := $(shell xcrun --find clang++)
  SDKROOT := $(shell xcrun --sdk macosx --show-sdk-path)
  SYSROOT_FLAGS := -isysroot $(SDKROOT)
	ARCH ?= arm64
	ARCH_FLAGS := -arch $(ARCH)
else
  CXX := g++
  SYSROOT_FLAGS :=
	ARCH_FLAGS :=
endif

CXXFLAGS ?=
CXXFLAGS += $(ARCH_FLAGS) -std=c++20 -O2 -I../head -pthread -W -Wall $(SYSROOT_FLAGS)

LDFLAGS ?=
LDFLAGS += $(ARCH_FLAGS) -std=c++20 -O2 -pthread $(SYSROOT_FLAGS)

ifeq ($(UNAME_S),Linux)
  CXXFLAGS += -mcmodel=large -march=native
  LDFLAGS += -mcmodel=large -march=native
endif

# Optional: static link libc++ using Homebrew LLVM (helps when /usr/lib/libc++.1.dylib is missing)
HAVE_USR_LIBCXX := $(wildcard /usr/lib/libc++.1.dylib)
ifeq ($(strip $(HAVE_USR_LIBCXX)),)
	WANT_STATIC_LIBCXX ?= 1
else
	WANT_STATIC_LIBCXX ?= 0
endif
ifeq ($(UNAME_S),Darwin)
  ifeq ($(WANT_STATIC_LIBCXX),1)
    LLVM_PREFIX ?= /opt/homebrew/opt/llvm
    LIBCXX_A := $(LLVM_PREFIX)/lib/c++/libc++.a
    LIBCXXABI_A := $(LLVM_PREFIX)/lib/c++/libc++abi.a
    LIBUNWIND_A := $(LLVM_PREFIX)/lib/unwind/libunwind.a
    ifeq ($(wildcard $(LIBCXX_A)),)
      $(error libc++.a not found at $(LIBCXX_A). Try: brew install llvm)
    endif
    ifeq ($(wildcard $(LIBCXXABI_A)),)
      $(error libc++abi.a not found at $(LIBCXXABI_A). Try: brew install llvm)
    endif
    ifeq ($(wildcard $(LIBUNWIND_A)),)
      $(error libunwind.a not found at $(LIBUNWIND_A). Try: brew install llvm)
    endif
    LDFLAGS += -nostdlib++ $(LIBCXX_A) $(LIBCXXABI_A) $(LIBUNWIND_A)
  endif
endif

TARGET := play_greedy

TD_SRC := ../learn/tdplayer_VSE_symmetric2.cpp
GAME_SRC := ../learn/game2048_VSE.cpp
SYM_SRC := ../learn/symmetric.cpp

OBJS := \
	play_greedy_2048.o \
	init_tuple_wrapper.o \
	alpha_stub.o \
	tdplayer_VSE_symmetric2.o \
	game2048_VSE.o \
	symmetric.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

tdplayer_VSE_symmetric2.o: $(TD_SRC)
	$(CXX) $(CXXFLAGS) -c $(TD_SRC) -DTUPLE_FILE_TYPE=$(TUPLE_TYPE) -o $@

game2048_VSE.o: $(GAME_SRC)
	$(CXX) $(CXXFLAGS) -c $(GAME_SRC) -o $@

symmetric.o: $(SYM_SRC)
	$(CXX) $(CXXFLAGS) -c $(SYM_SRC) -o $@

build-6:
	$(MAKE) clean
	$(MAKE) TUPLE_TYPE=6 all
	mv $(TARGET) $(TARGET)_6

build-7:
	$(MAKE) clean
	$(MAKE) TUPLE_TYPE=7 all
	mv $(TARGET) $(TARGET)_7

build-8:
	$(MAKE) clean
	$(MAKE) TUPLE_TYPE=8 all
	mv $(TARGET) $(TARGET)_8

build-9:
	$(MAKE) clean
	$(MAKE) TUPLE_TYPE=9 all
	mv $(TARGET) $(TARGET)_9

run: $(TARGET)
	@echo "Usage: ./$(TARGET) <seed> <game_counts> <evfile>"

# Run helpers: write logs to repo-root ./log/
LOG_DIR ?= ../log
SEED ?= 0
GAMES ?= 100
EVFILE ?= ../tuples$(TUPLE_TYPE)-seed0-VSE-count200.dat.xz

ifeq ($(UNAME_S),Darwin)
  ifeq ($(ARCH),x86_64)
    RUN_PREFIX := arch -x86_64
  else
    RUN_PREFIX :=
  endif
else
  RUN_PREFIX :=
endif

define RUN_AND_LOG
	@mkdir -p $(LOG_DIR)
	@echo "Logging to $(LOG_DIR)/$@.log"
	@$(RUN_PREFIX) ./$(TARGET)_$(1) $(SEED) $(GAMES) $(EVFILE) > $(LOG_DIR)/$@.log 2>&1
endef

run-6: build-6
	$(call RUN_AND_LOG,6)

run-7: build-7
	$(call RUN_AND_LOG,7)

run-8: build-8
	$(call RUN_AND_LOG,8)

run-9: build-9
	$(call RUN_AND_LOG,9)

clean:
	rm -f $(TARGET) $(TARGET)_6 $(TARGET)_7 $(TARGET)_8 $(TARGET)_9 *.o

help:
	@echo "Makefile for play/play_greedy_2048.cpp"
	@echo ""
	@echo "Targets:"
	@echo "  build-6/7/8/9  - Build with tuple type"
	@echo "  clean          - Remove binaries and objects"
	@echo ""
	@echo "Options:"
	@echo "  WANT_STATIC_LIBCXX=1  - Static link libc++ via Homebrew llvm"
	@echo "  LLVM_PREFIX=...       - Override Homebrew llvm prefix"
