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

static void jpeg(FILE *input, FILE *output, uint32_t width_out,
	uint32_t height_out)
{
	struct jpeg_decompress_struct dinfo;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	uint32_t i, scale_factor, width_in;
	uint8_t cmp, *psl_buf, *psl_pos0, *outbuf, *tmp;
	size_t outbuf_len, psl_len, psl_offset;
	struct yscaler ys;
	jpeg_saved_marker_ptr marker;

	dinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&dinfo);
	jpeg_stdio_src(&dinfo, input);

	/* Save custom headers for the compressor, but ignore APP0 & APP14 so
	 * libjpeg can handle them.
	 */
	jpeg_save_markers(&dinfo, JPEG_COM, 0xFFFF);
	for (i=1; i<14; i++) {
		jpeg_save_markers(&dinfo, JPEG_APP0+i, 0xFFFF);
	}
	jpeg_save_markers(&dinfo, JPEG_APP0+15, 0xFFFF);
	jpeg_read_header(&dinfo, TRUE);

#ifdef JCS_EXTENSIONS
	if (dinfo.out_color_space == JCS_RGB) {
		dinfo.out_color_space = JCS_EXT_RGBX;
	}
#endif

	fix_ratio(dinfo.image_width, dinfo.image_height, &width_out,
		&height_out);

	scale_factor = dinfo.image_width / width_out;
	if (scale_factor >= 8 * 4) {
		dinfo.scale_denom = 8;
	} else if (scale_factor >= 4 * 4) {
		dinfo.scale_denom = 4;
	} else if (scale_factor >= 2 * 4) {
		dinfo.scale_denom = 2;
	}

	jpeg_start_decompress(&dinfo);

	cmp = dinfo.output_components;
	width_in = dinfo.output_width;
	psl_len = padded_sl_len_offset(width_in, width_out, cmp, &psl_offset);
	psl_buf = malloc(psl_len);
	psl_pos0 = psl_buf + psl_offset;
	outbuf_len = width_out * cmp;
	outbuf = malloc(outbuf_len);

	cinfo.err = dinfo.err;
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, output);
	cinfo.image_width = width_out;
	cinfo.image_height = height_out;
	cinfo.input_components = cmp;
	cinfo.in_color_space = dinfo.out_color_space;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 95, FALSE);
	jpeg_start_compress(&cinfo, TRUE);

	/* Write custom headers */
	for (marker=dinfo.marker_list; marker; marker=marker->next) {
		jpeg_write_marker(&cinfo, marker->marker, marker->data,
			marker->data_length);
	}

	yscaler_init(&ys, dinfo.output_height, height_out, outbuf_len);
	for(i=0; i<height_out; i++) {
		while ((tmp = yscaler_next(&ys))) {
			jpeg_read_scanlines(&dinfo, &psl_pos0, 1);
			padded_sl_extend_edges(psl_buf, width_in, psl_offset, cmp);
			xscale_padded(psl_pos0, width_in, tmp, width_out, cmp);
		}
		yscaler_scale(&ys, outbuf, i);
		jpeg_write_scanlines(&cinfo, (JSAMPARRAY)&outbuf, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	jpeg_finish_decompress(&dinfo);
	jpeg_destroy_decompress(&dinfo);
	free(psl_buf);
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
