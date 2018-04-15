#include "precompiled.h"

#define PIXELSET1BIT(row, x) (row)[(x) / 8] |= (1 << (7 - ((x) % 8)))
#define PIXELGETBIT(row, x) ((((row)[(x) / 8] >> (7 - ((x) % 8))) & 1))
#define PIXELSET0BIT(row, x) (row)[(x) / 8] &= ~(1 << (7 - ((x) % 8)))

typedef struct _PNGCONTEXT {
    int x, y;

    int width, height;
    png_byte color_type;
    png_byte bit_depth;

    png_structp png_ptr;
    png_infop info_ptr;
    int number_of_passes;
    png_bytep * row_pointers;
    FILE *fp;
} PNGCONTEXT;

static void abort_(const char * s, ...) {
    va_list args;
    va_start(args, s);
    vfprintf(stderr, s, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
}

static void free_raw_pointers(PNGCONTEXT* context) {
    /* cleanup heap allocation */
    for (context->y = 0; context->y < context->height; context->y++)
        free(context->row_pointers[context->y]);
    free(context->row_pointers);
    context->row_pointers = 0;
}

static void malloc_raw_pointers(PNGCONTEXT* context) {
    const png_size_t bytes_per_row = png_get_rowbytes(context->png_ptr, context->info_ptr);
    context->row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * context->height);
    for (context->y = 0; context->y < context->height; context->y++)
        context->row_pointers[context->y] = (png_byte*)malloc(bytes_per_row);
}

void open_to_read_png_file(const char* file_name, PNGCONTEXT* context) {
    printf("Open to read %s...\n", file_name);
    memset(context, 0, sizeof(PNGCONTEXT));

    char header[8];    // 8 is the maximum size that can be checked

                       /* open file and test for it being a png */
    context->fp = fopen(file_name, "rb");
    if (!context->fp)
        abort_("[read_png_file] File %s could not be opened for reading", file_name);
    fread(header, 1, 8, context->fp);
    if (png_sig_cmp(header, 0, 8))
        abort_("[read_png_file] File %s is not recognized as a PNG file", file_name);


    /* initialize stuff */
    context->png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!context->png_ptr)
        abort_("[read_png_file] png_create_read_struct failed");

    context->info_ptr = png_create_info_struct(context->png_ptr);
    if (!context->info_ptr)
        abort_("[read_png_file] png_create_info_struct failed");

    if (setjmp(png_jmpbuf(context->png_ptr)))
        abort_("[read_png_file] Error during init_io");

    png_init_io(context->png_ptr, context->fp);
    png_set_sig_bytes(context->png_ptr, 8);

    png_read_info(context->png_ptr, context->info_ptr);

    context->width = png_get_image_width(context->png_ptr, context->info_ptr);
    context->height = png_get_image_height(context->png_ptr, context->info_ptr);
    context->color_type = png_get_color_type(context->png_ptr, context->info_ptr);
    context->bit_depth = png_get_bit_depth(context->png_ptr, context->info_ptr);

    context->number_of_passes = png_set_interlace_handling(context->png_ptr);
    png_read_update_info(context->png_ptr, context->info_ptr);


    /* read file */
    if (setjmp(png_jmpbuf(context->png_ptr)))
        abort_("[read_png_file] Error during read_image");

    malloc_raw_pointers(context);

    png_read_image(context->png_ptr, context->row_pointers);

    fclose(context->fp);
}

static void open_to_write_png_file(const char* output_filename, PNGCONTEXT* context, int width, int height) {
    printf("Open to write %s...\n", output_filename);
    memset(context, 0, sizeof(PNGCONTEXT));

    /* create file */
    context->fp = fopen(output_filename, "wb");
    if (!context->fp)
        abort_("[write_png_file] File %s could not be opened for writing", output_filename);


    /* initialize stuff */
    context->png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!context->png_ptr)
        abort_("[write_png_file] png_create_write_struct failed");

    context->info_ptr = png_create_info_struct(context->png_ptr);
    if (!context->info_ptr)
        abort_("[write_png_file] png_create_info_struct failed");

    if (setjmp(png_jmpbuf(context->png_ptr)))
        abort_("[write_png_file] Error during init_io");

    png_init_io(context->png_ptr, context->fp);


    /* write header */
    if (setjmp(png_jmpbuf(context->png_ptr)))
        abort_("[write_png_file] Error during writing header");

    context->color_type = PNG_COLOR_TYPE_PALETTE;
    context->bit_depth = 1;
    context->width = width;
    context->height = height;

    png_set_IHDR(context->png_ptr, context->info_ptr, width, height,
                 context->bit_depth, context->color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);


    int num_palette = 2;
    png_color palettep[] = { { 255, 255, 255 }, { 0, 0, 0 }, };
    //png_get_PLTE(png_ptr, info_ptr, &palettep, &num_palette);
    png_set_PLTE(context->png_ptr, context->info_ptr, palettep, num_palette);

    png_write_info(context->png_ptr, context->info_ptr);

    malloc_raw_pointers(context);
}

static void write_and_close_png_file(PNGCONTEXT* context) {
    printf("Writing to disk...\n");

    /* write bytes */
    if (setjmp(png_jmpbuf(context->png_ptr)))
        abort_("[write_png_file] Error during writing bytes");

    png_write_image(context->png_ptr, context->row_pointers);

    /* end write */
    if (setjmp(png_jmpbuf(context->png_ptr)))
        abort_("[write_png_file] Error during end of write");

    png_write_end(context->png_ptr, NULL);

    free_raw_pointers(context);

    fclose(context->fp);
}

static void merge_data(PNGCONTEXT* input1, PNGCONTEXT* input2, PNGCONTEXT* output) {
    const png_size_t bytes_per_row1 = png_get_rowbytes(input1->png_ptr, input1->info_ptr);
    const png_size_t bytes_per_row2 = png_get_rowbytes(input2->png_ptr, input2->info_ptr);
    if (input1->width != input2->width) {
        abort_("width not match: %d != %d", input1->width, input2->width);
    }
    if (bytes_per_row1 != bytes_per_row2) {
        abort_("bytes per row not match: %zu != %zu", bytes_per_row1, bytes_per_row2);
    }
    for (int y = 0; y < output->height; y++) {
        memcpy(output->row_pointers[y], input1->row_pointers[y], bytes_per_row1);
        // could not use simple 'memcpy' because of it cannot be used with 'bit'-level addressing...
        for (int x2 = 0; x2 < input2->width; x2++) {
            if (PIXELGETBIT(input2->row_pointers[y], x2)) {
                PIXELSET1BIT(output->row_pointers[y], input1->width + x2);
            } else {
                PIXELSET0BIT(output->row_pointers[y], input1->width + x2);
            }
        }
    }
}

// output resolution: 172824 x 86412 = 14934067488
// output bytes: 14934067488 / 8 = 1866758436
static void merge_png(const char* input1_filename, const char* input2_filename, const char* output_filename) {
    PNGCONTEXT input1, input2, output;
    open_to_read_png_file(input1_filename, &input1);
    open_to_read_png_file(input2_filename, &input2);
    open_to_write_png_file(output_filename, &output, input1.width * 2, input1.height);
    merge_data(&input1, &input2, &output);
    write_and_close_png_file(&output);
    free_raw_pointers(&input1);
    free_raw_pointers(&input2);
}

int main() {
    merge_png("C:\\sea-server\\modis\\png-2x1-offset\\w0-h0.png", "C:\\sea-server\\modis\\png-2x1-offset\\w1-h0.png", "C:\\sea-server\\modis\\png-2x1-offset\\merged.png");
    return 0;
}
