#include "resample.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>
#include <png.h>

static void fix_ratio(uint32_t sw, uint32_t sh, uint32_t *dw, uint32_t *dh)
{
	double x, y;

	x = *dw / (double)sw;
	y = *dh / (double)sh;

	if (x && (!y || x<y)) {
		*dh = (sh * x) + 0.5;
	} else {
		*dw = (sw * y) + 0.5;
	}

	if (!*dh) {
		*dh = 1;
	}
	if (!*dw) {
		*dw = 1;
	}
}

/** Interlaced PNGs need to be fully decompressed before we can scale the image.
  *
  * We scale along the y-axis first because it is more memory efficient in this
  * case.
  */
static void png_interlaced(png_structp rpng, png_infop rinfo, png_structp wpng,
	png_infop winfo, int opts)
{
	uint8_t **sl, *yscaled, *out;
	uint32_t i, in_width, in_height, out_width, out_height;
	size_t buf_len, outbuf_len;
	png_byte cmp;

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
	yscaled = malloc(buf_len);

	outbuf_len = out_width * cmp;
	out = malloc(outbuf_len);

	png_read_image(rpng, sl);

	for (i=0; i<out_height; i++) {
		yscaler_prealloc_scale(in_height, out_height, sl, yscaled, i,
			in_width, cmp, opts);
		xscale(yscaled, in_width, out, out_width, cmp, opts);
		png_write_row(wpng, out);
	}

	for (i=0; i<in_height; i++) {
		free(sl[i]);
	}
	free(yscaled);
	free(sl);
	free(out);
}

static void png_noninterlaced(png_structp rpng, png_infop rinfo,
	png_structp wpng, png_infop winfo, int opts)
{
	uint32_t i, in_width, in_height, out_width, out_height;
	uint8_t *inbuf, *outbuf, *tmp;
	size_t inbuf_len, outbuf_len;
	struct yscaler ys;
	png_byte cmp;

	in_width = png_get_image_width(rpng, rinfo);
	in_height = png_get_image_height(rpng, rinfo);
	out_width = png_get_image_width(wpng, winfo);
	out_height = png_get_image_height(wpng, winfo);
	cmp = png_get_channels(rpng, rinfo);

	inbuf_len = in_width * cmp;
	inbuf = malloc(inbuf_len);
	outbuf_len = out_width * cmp;
	outbuf = malloc(outbuf_len);

	yscaler_init(&ys, in_height, out_height, outbuf_len);
	for(i=0; i<out_height; i++) {
		while ((tmp = yscaler_next(&ys))) {
			png_read_row(rpng, inbuf, NULL);
			xscale(inbuf, in_width, tmp, out_width, cmp, opts);
		}
		yscaler_scale(&ys, outbuf, out_width, cmp, opts, i);
		png_write_row(wpng, outbuf);
	}

	free(inbuf);
	free(outbuf);
	yscaler_free(&ys);
}

static void png(FILE *input, FILE *output, uint32_t width, uint32_t height)
{
	png_structp rpng, wpng;
	png_infop rinfo, winfo;
	png_uint_32 in_width, in_height;
	png_byte ctype;
	int opts;

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
	opts = 0;
	if (ctype == PNG_COLOR_TYPE_RGB) {
		opts = OIL_FILLER;
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

	if (opts == OIL_FILLER) {
		png_set_filler(wpng, 0, PNG_FILLER_AFTER);
	}

	switch (png_get_interlace_type(rpng, rinfo)) {
	case PNG_INTERLACE_NONE:
		png_noninterlaced(rpng, rinfo, wpng, winfo, opts);
		break;
	case PNG_INTERLACE_ADAM7:
		png_interlaced(rpng, rinfo, wpng, winfo, opts);
		break;
	}

	png_write_end(wpng, winfo);
	png_destroy_write_struct(&wpng, &winfo);
	png_destroy_read_struct(&rpng, &rinfo, NULL);
}

static void jpeg(FILE *input, FILE *output, uint32_t width, uint32_t height)
{
	struct jpeg_decompress_struct dinfo;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	uint32_t i, scale_factor;
	uint8_t *inbuf, *outbuf, *tmp;
	size_t inbuf_len, outbuf_len;
	int opts, cmp;
	struct yscaler ys;

	opts = 0;

	dinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&dinfo);
	jpeg_stdio_src(&dinfo, input);
	jpeg_read_header(&dinfo, TRUE);

#ifdef JCS_EXTENSIONS
	if (dinfo.out_color_space == JCS_RGB) {
		dinfo.out_color_space = JCS_EXT_RGBX;
		opts = OIL_FILLER;
	}
#endif

	fix_ratio(dinfo.image_width, dinfo.image_height, &width, &height);

	scale_factor = dinfo.image_width / width;
	if (scale_factor >= 8 * 4) {
		dinfo.scale_denom = 8;
	} else if (scale_factor >= 4 * 4) {
		dinfo.scale_denom = 4;
	} else if (scale_factor >= 2 * 4) {
		dinfo.scale_denom = 2;
	}

	jpeg_start_decompress(&dinfo);

	cmp = dinfo.output_components;
	inbuf_len = dinfo.output_width * cmp;
	inbuf = malloc(inbuf_len);
	outbuf_len = width * cmp;
	outbuf = malloc(outbuf_len);

	cinfo.err = dinfo.err;
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, output);
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = cmp;
	cinfo.in_color_space = dinfo.out_color_space;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 95, FALSE);
	jpeg_start_compress(&cinfo, TRUE);

	yscaler_init(&ys, dinfo.output_height, height, outbuf_len);
	for(i=0; i<height; i++) {
		while ((tmp = yscaler_next(&ys))) {
			jpeg_read_scanlines(&dinfo, &inbuf, 1);
			xscale(inbuf, dinfo.output_width, tmp, width, cmp, opts);
		}
		yscaler_scale(&ys, outbuf, width, cmp, opts, i);
		jpeg_write_scanlines(&cinfo, (JSAMPARRAY)&outbuf, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	jpeg_finish_decompress(&dinfo);
	jpeg_destroy_decompress(&dinfo);
	free(inbuf);
	free(outbuf);
	yscaler_free(&ys);
}

int main(int argc, char *argv[])
{
	FILE *input;
	int ret, c;
	uint32_t width, height;
	char *end;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s WIDTH HEIGHT FILE\n", argv[0]);
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

	input = fopen(argv[3], "r+");

	if (!input) {
		fprintf(stderr, "Error: Unable to open file.\n");
		return 1;
	}

	c = getc(input);

	if (c == EOF) {
		fprintf(stderr, "Error: Unable to read file signature.\n");
		return 1;
	}

	ret = ungetc(c, input);

	if (ret == EOF) {
		fprintf(stderr, "Error: Unable to push signature to stream.\n");
		return 1;
	}

	if (c == 0xFF) {
		jpeg(input, stdout, width, height);
	} else if (c == 0x89) {
		png(input, stdout, width, height);
	} else {
		fprintf(stderr, "Error: Unrecognized file signature.\n");
		return 1;
	}

	fclose(input);
	return 0;
}
