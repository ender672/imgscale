#include "resample.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

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

	jpeg(stdin, stdout, width, height);

	fclose(stdin);
	return 0;
}
