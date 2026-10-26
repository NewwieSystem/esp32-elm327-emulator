# Builds the ELM327 interpreter on a PC with a tiny Arduino shim and runs the checks.
# The Arduino String class is taken from arduino-esp32 2.0.17 (downloaded once into shim/).
CORE = https://raw.githubusercontent.com/espressif/arduino-esp32/2.0.17/cores/esp32
SRC  = ../src/elm327.cpp ../src/car.cpp ../src/faults.cpp ../src/profiles.cpp ../src/mazda_ext.cpp
CORE_FILES = shim/WString.h shim/WString.cpp shim/stdlib_noniso.h shim/stdlib_noniso.c

run: test_elm
	./test_elm

shim/%:
	curl -sSfL -o $@ $(CORE)/$*

test_elm: test_elm.cpp $(SRC) $(CORE_FILES) shim/itoa_shim.c
	gcc -c -Ishim shim/stdlib_noniso.c -o shim/stdlib_noniso.o
	gcc -c -Ishim shim/itoa_shim.c -o shim/itoa_shim.o
	g++ -std=gnu++17 -g -fsanitize=address,undefined -Ishim -I../src test_elm.cpp $(SRC) shim/WString.cpp \
	    shim/stdlib_noniso.o shim/itoa_shim.o -o $@

clean:
	rm -f test_elm shim/*.o

.PHONY: run clean
