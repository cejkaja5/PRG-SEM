CFLAGS += -Wall -std=gnu99 -g -pedantic 
LDFLAGS = -pthread -lm
 
CFLAGS += $(shell sdl2-config --cflags)
LDFLAGS += $(shell sdl2-config --libs) -lSDL2_image  

HW = prgsem
BINARIES = control_app_exec computational_module_exec
COMMON = prg_io_nonblock.o common_lib.o queue.o messages.o

all: $(BINARIES)

# Build the control app (UI + SDL + pipe communication)
control_app_exec: control_app.o xwin_sdl.o $(COMMON)
	$(CC) $^ $(LDFLAGS) -o $@

# Build the computational module (headless, uses pipes)
computational_module_exec: computational_module.o $(COMMON)
	$(CC) $^ $(LDFLAGS) -o $@

# Generic rule to compile .c to .o
%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(BINARIES) *.o *.png