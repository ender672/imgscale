#include "resample.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

/** Interlaced PNGs need to be fully decompressed before we can scale the image.
  *
  * We scale along the y-axis first because it is more memory efficient in this
  * case.
  */
static void png_interlaced(png_structp rpng, png_infop rinfo, png_structp wpng,
	png_infop winfo)
{
	uint8_t **sl, *yscaled, *out;
	uint32_t i, in_width, in_height, out_width, out_height;
	size_t buf_len, outbuf_len;
	png_byte cmp;
	struct xscaler xs;

	in_width = png_get_image_width(rpng, rinfo);
	in_height = png_get_image_height(rpng, rinfo);
	out_width = png_get_image_width(wpng, winfo);
	out_height = png_get_image_height(wpng, winfo);
	cmp = png_get_channels(rpng, rinfo);

	sl = malloc(in_height * sizeof(uint8_t *));

	buf_len = png_get_rowbytes(rpng, rinfo);
	for (i=0; i<in_height; i++) {
		sl[i] = malloc(buf_len);
	}

	outbuf_len = out_width * cmp;
	out = malloc(outbuf_len);
	xscaler_init(&xs, in_width, out_width, cmp, 1);
	yscaled = xscaler_psl_pos0(&xs);

	png_read_image(rpng, sl);

	for (i=0; i<out_height; i++) {
		yscaler_prealloc_scale(in_height, out_height, sl, yscaled, i,
			in_width, cmp, 1);
		xscaler_scale(&xs, out);
		png_write_row(wpng, out);
	}

	for (i=0; i<in_height; i++) {
		free(sl[i]);
	}
	free(sl);
	free(out);
	xscaler_free(&xs);
}

static void png_noninterlaced(png_structp rpng, png_infop rinfo,
	png_structp wpng, png_infop winfo)
{
	uint32_t i, in_width, in_height, out_width, out_height;
	uint8_t *inbuf, *outbuf, *tmp;
	size_t outbuf_len;
	struct xscaler xs;
	struct yscaler ys;
	png_byte cmp;

	in_width = png_get_image_width(rpng, rinfo);
	in_height = png_get_image_height(rpng, rinfo);
	out_width = png_get_image_width(wpng, winfo);
	out_height = png_get_image_height(wpng, winfo);
	cmp = png_get_channels(rpng, rinfo);

	outbuf_len = out_width * cmp;
	outbuf = malloc(outbuf_len);

	xscaler_init(&xs, in_width, out_width, cmp, 1);
	yscaler_init(&ys, in_height, out_height, outbuf_len);
	inbuf = xscaler_psl_pos0(&xs);
	for(i=0; i<out_height; i++) {
		while ((tmp = yscaler_next(&ys))) {
			png_read_row(rpng, inbuf, NULL);
			xscaler_scale(&xs, tmp);
		}
		yscaler_scale(&ys, outbuf, i, cmp, 1);
		png_write_row(wpng, outbuf);
	}

	free(outbuf);
	yscaler_free(&ys);
	xscaler_free(&xs);
}

static void png(FILE *input, FILE *output, uint32_t width, uint32_t height)
{
	png_structp rpng, wpng;
	png_infop rinfo, winfo;
	png_uint_32 in_width, in_height;
	png_byte ctype;

	rpng = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (setjmp(png_jmpbuf(rpng))) {
		fprintf(stderr, "PNG Decoding Error.\n");
		exit(1);
	}

	rinfo = png_create_info_struct(rpng);
	png_init_io(rpng, input);
	png_read_info(rpng, rinfo);

	png_set_packing(rpng);
	png_set_strip_16(rpng);
	png_set_expand(rpng);

	ctype = png_get_color_type(rpng, rinfo);
	if (ctype == PNG_COLOR_TYPE_RGB) {
		png_set_filler(rpng, 0, PNG_FILLER_AFTER);
	}
	png_read_update_info(rpng, rinfo);

	in_width = png_get_image_width(rpng, rinfo);
	in_height = png_get_image_height(rpng, rinfo);
	fix_ratio(in_width, in_height, &width, &height);

	wpng = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	winfo = png_create_info_struct(wpng);
	png_init_io(wpng, output);

	png_set_IHDR(wpng, winfo, width, height, 8, ctype, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_write_info(wpng, winfo);

	if (ctype == PNG_COLOR_TYPE_RGB) {
		png_set_filler(wpng, 0, PNG_FILLER_AFTER);
	}

	switch (png_get_interlace_type(rpng, rinfo)) {
	case PNG_INTERLACE_NONE:
		png_noninterlaced(rpng, rinfo, wpng, winfo);
		break;
	case PNG_INTERLACE_ADAM7:
		png_interlaced(rpng, rinfo, wpng, winfo);
		break;
	}

	png_write_end(wpng, winfo);
	png_destroy_write_struct(&wpng, &winfo);
	png_destroy_read_struct(&rpng, &rinfo, NULL);
}

int main(int argc, char *argv[])
{
	uint32_t width, height;
	char *end;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s WIDTH HEIGHT\n", argv[0]);
		return 1;
	}

	width = strtoul(argv[1], &end, 10);
	if (*end) {
		fprintf(stderr, "Error: Invalid width.\n");
		return 1;
	}

	height = strtoul(argv[2], &end, 10);
	if (*end) {
		fprintf(stderr, "Error: Invalid height.\n");
		return 1;
	}

	png(stdin, stdout, width, height);

	fclose(stdin);
	return 0;
}
