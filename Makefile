CFLAGS += -Os -Wall -pedantic
LDLIBS += -lpng -ljpeg

imgscale: resample.o imgscale.c
	$(CC) $(CFLAGS) resample.o imgscale.c -o $@ $(LDLIBS)
clean:
	rm -f resample.o imgscale
