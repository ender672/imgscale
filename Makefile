CFLAGS += -Os -Wall -pedantic

jpgscale: resample.o jpgscale.c
	$(CC) $(CFLAGS) resample.o jpgscale.c -o $@ -ljpeg
pngscale: resample.o pngscale.c
		$(CC) $(CFLAGS) resample.o pngscale.c -o $@ -lpng
clean:
	rm -f resample.o jpgscale pngscale
