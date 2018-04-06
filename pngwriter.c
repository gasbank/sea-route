#include "precompiled.h"

int x, y;

int width, height;
png_byte color_type;
png_byte bit_depth;

png_structp png_ptr;
png_infop info_ptr;
int number_of_passes;
png_bytep * row_pointers;

#define PIXELSETBIT(row, x) (row)[(x) / 8] |= (1 << (7 - ((x) % 8)))

static void abort_(const char * s, ...) {
    va_list args;
    va_start(args, s);
    vfprintf(stderr, s, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
}

static int nearest_8_mul(int v) {
    return (v + ((1 << 3) - 1)) >> 3;
}

static void write_png_file(const char* input_filename, const char* output_filename) {
    /* create file */
    FILE *fp = fopen(output_filename, "wb");
    if (!fp)
        abort_("[write_png_file] File %s could not be opened for writing", output_filename);


    /* initialize stuff */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr)
        abort_("[write_png_file] png_create_write_struct failed");

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
        abort_("[write_png_file] png_create_info_struct failed");

    if (setjmp(png_jmpbuf(png_ptr)))
        abort_("[write_png_file] Error during init_io");

    png_init_io(png_ptr, fp);
    

    /* write header */
    if (setjmp(png_jmpbuf(png_ptr)))
        abort_("[write_png_file] Error during writing header");

    color_type = PNG_COLOR_TYPE_PALETTE;
    bit_depth = 1;
    width = 21603;
    height = 21603;

    row_pointers = calloc(height, sizeof(void*));
    FILE* fin = fopen(input_filename, "rb");
    unsigned char* row_in = malloc(width);
    for (int y = 0; y < height; y++) {
        png_bytep row_out = calloc(nearest_8_mul(width), 1);
        fread(row_in, 1, width, fin);
        for (int x = 0; x < width; x++) {
            if (row_in[x] == 0) {
                PIXELSETBIT(row_out, x);
            }
        }
        row_pointers[y] = row_out;
    }
    free(row_in);
    row_in = 0;
    fclose(fin);

    png_set_IHDR(png_ptr, info_ptr, width, height,
                 bit_depth, color_type, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);


    int num_palette = 2;
    png_color palettep[] = { { 255, 255, 255 }, { 0, 0, 0 },  };
    //png_get_PLTE(png_ptr, info_ptr, &palettep, &num_palette);
    png_set_PLTE(png_ptr, info_ptr, palettep, num_palette);

    png_write_info(png_ptr, info_ptr);

    /* write bytes */
    if (setjmp(png_jmpbuf(png_ptr)))
        abort_("[write_png_file] Error during writing bytes");

    png_write_image(png_ptr, row_pointers);


    /* end write */
    if (setjmp(png_jmpbuf(png_ptr)))
        abort_("[write_png_file] Error during end of write");

    png_write_end(png_ptr, NULL);

    /* cleanup heap allocation */
    for (y = 0; y<height; y++)
        free(row_pointers[y]);
    free(row_pointers);

    fclose(fp);
}

int main() {
    for (int hi = 0; hi < 4; hi++) {
        for (int wi = 0; wi < 8; wi++) {
            char input_filename[128];
            sprintf(input_filename, "C:\\sea-server\\modis\\w%d-h%d.dat", wi, hi);
            char output_filename[128];
            sprintf(output_filename, "C:\\sea-server\\modis\\w%d-h%d.png", wi, hi);
            printf("Writing %s...\n", output_filename);
            write_png_file(input_filename, output_filename);
        }
    }
    
    return 0;
}
