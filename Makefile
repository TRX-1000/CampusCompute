# CampusCompute - Makefile
#   make            build ./campuscompute
#   make test       run every feature with the sample inputs
#   make demo       integrated demonstration
#   make clean      remove build output (keeps logs)
#   make distclean  also remove logs and zip files
#   make zip SRN=PES1UGxxxxxxx   create the submission archive and check it

CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -O2 -g -D_GNU_SOURCE -Iinclude -pthread
LDLIBS  := -pthread -lrt

TARGET  := campuscompute
SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC))
HDR     := $(wildcard include/*.h)

PROJ    := $(notdir $(CURDIR))
ZIPNAME := $(if $(SRN),$(SRN)_CampusCompute.zip,CampusCompute.zip)

.PHONY: all clean distclean test demo zip

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
	@echo "Build OK -> ./$(TARGET)"

build/%.o: src/%.c $(HDR) | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

test: $(TARGET)
	./scripts/run_tests.sh

demo: $(TARGET)
	./$(TARGET) demo

clean:
	rm -rf build $(TARGET)

distclean: clean
	rm -rf logs *.zip

zip:
	rm -f $(ZIPNAME)
	cd .. && zip -rq $(CURDIR)/$(ZIPNAME) $(PROJ)/Makefile $(PROJ)/README.md \
	    $(PROJ)/SCREENSHOTS.md $(PROJ)/src $(PROJ)/include $(PROJ)/data $(PROJ)/scripts
	unzip -l $(ZIPNAME)
	unzip -tq $(ZIPNAME)
