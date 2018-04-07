#include "precompiled.hpp"
#include "MaxMatch.h"
#include "AStar.h"
#include "xy.hpp"
#include "astarrtree.hpp"

#define WORLDMAP_RTREE_MMAP_MAX_SIZE_RATIO (sizeof(size_t) / 4)
#define DATA_ROOT "assets/"
#define WORLDMAP_LAND_RTREE_FILENAME "land.dat"
#define WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE (160 * 1024 * 1024 * WORLDMAP_RTREE_MMAP_MAX_SIZE_RATIO)
#define WORLDMAP_WATER_RTREE_FILENAME "water.dat"
#define WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE (160 * 1024 * 1024 * WORLDMAP_RTREE_MMAP_MAX_SIZE_RATIO)
#define WORLDMAP_LAND_MAX_RECT_RTREE_RTREE_FILENAME "land_max_rect.dat"
#define WORLDMAP_LAND_MAX_RECT_RTREE_MMAP_MAX_SIZE (160 * 1024 * 1024 * WORLDMAP_RTREE_MMAP_MAX_SIZE_RATIO)
#define WORLDMAP_WATER_MAX_RECT_RTREE_RTREE_FILENAME "water_max_rect.dat"
#define WORLDMAP_WATER_MAX_RECT_RTREE_MMAP_MAX_SIZE (160 * 1024 * 1024 * WORLDMAP_RTREE_MMAP_MAX_SIZE_RATIO)

//#define WORLDMAP_INPUT_PNG DATA_ROOT "water_16384x8192.png"
//#define WORLDMAP_INPUT_PNG "C:\\sea-server\\modis\\png-8x4\\w6-h2.png"
#define WORLDMAP_INPUT_PNG "C:\\sea-server\\modis\\png-2x1\\w0-h0.png"

enum LINE_CHECK_RESULT {
    LCR_GOOD_CUT,
    LCR_NEIGHBOR_VERTEX,
    LCR_DISCONNECTED,
    LCR_BUG,
};

enum VERTEX_TYPE {
    VT_CONVEX,
    VT_CONCAVE,
};

struct xyv {
    xy coords;
    VERTEX_TYPE v;
};

typedef std::unordered_map<int, std::vector<xyv> > int_xyvvector_map;
typedef std::vector<xyxy> xyxyvector;
typedef std::set<xy> xyset;
typedef MaxMatch<std::string> MaxMatchString;
typedef MaxMatch<int> MaxMatchInt;

template <class T>
inline void hash_combine(std::size_t & s, const T & v) {
    std::hash<T> h;
    s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
}

struct RECTPIXEL {
    int x : 16;
    int y : 16;
    int w : 16;
    int h : 16;
};

namespace std {
    template<>
    struct hash<RECTPIXEL> {
        std::size_t operator()(const RECTPIXEL& s) const {
            std::size_t res = 0;
            hash_combine(res, s.x);
            hash_combine(res, s.y);
            hash_combine(res, s.w);
            hash_combine(res, s.h);
            return res;
        }
    };
    template<>
    struct equal_to<RECTPIXEL> {
        inline bool operator()(const RECTPIXEL& a, const RECTPIXEL& b) const {
            std::size_t res = 0;
            hash_combine(res, a.x);
            hash_combine(res, a.y);
            hash_combine(res, a.w);
            hash_combine(res, a.h);
            std::size_t res_other = 0;
            hash_combine(res_other, b.x);
            hash_combine(res_other, b.y);
            hash_combine(res_other, b.w);
            hash_combine(res_other, b.h);
            return res == res_other;
        }
    };
}

std::vector<RECTPIXEL> rect_pixel_set;

namespace bi = boost::interprocess;
namespace bg = boost::geometry;
namespace bgm = bg::model;
namespace bgi = bg::index;

typedef bgm::point<unsigned int, 2, bg::cs::cartesian> point_t;
typedef bgm::box<point_t> box_t;
typedef std::pair<box_t, int> value_t;
typedef bgi::linear<32, 8> params_t;
typedef bgi::indexable<value_t> indexable_t;
typedef bgi::equal_to<value_t> equal_to_t;
typedef bi::allocator<value_t, bi::managed_mapped_file::segment_manager> allocator_t;
typedef bgi::rtree<value_t, params_t, indexable_t, equal_to_t, allocator_t> rtree_t;

int x, y;

int width, height;
png_byte color_type;
png_byte bit_depth;

png_structp png_ptr;
png_infop info_ptr;
int number_of_passes;
png_bytep * row_pointers;

int_xyvvector_map row_key_convex_concaves;
int_xyvvector_map col_key_convex_concaves;
xyxyvector hori_lines;
xyxyvector vert_lines;
MaxMatchInt bipartite;
int land_color_index;
xyset cut_boundary_pixels;
std::vector<xyset> seed_pixels;
#define PIXELBIT(row, x) (((((row)[(x) / 8] >> (7 - ((x) % 8))) & 1) == land_color_index) ? 1 : 0)
#define PIXELSETBIT(row, x) (row)[(x) / 8] |= (1 << (7 - ((x) % 8)))
#define PIXELCLEARBIT(row, x) (row)[(x) / 8] &= ~(1 << (7 - ((x) % 8)))
#define PIXELINVERTBIT(row, x) (row)[(x) / 8] ^= (1 << (7 - ((x) % 8)))
#define PIXELBITXY(x, y) PIXELBIT(row_pointers[(y)], (x))
#define PIXELCLEARBITXY(x, y) PIXELCLEARBIT(row_pointers[(y)], (x))
#define PIXELSETBITXY(x, y) PIXELSETBIT(row_pointers[(y)], (x))
#define PIXELINVERTBITXY(x, y) PIXELINVERTBIT(row_pointers[(y)], (x))

void abort_(const char * s, ...) {
    va_list args;
    va_start(args, s);
    vfprintf(stderr, s, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
}

void write_png_file(const char *filename) {

    FILE *fp = fopen(filename, "wb");
    if (!fp) abort();

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) abort();

    png_infop info = png_create_info_struct(png);
    if (!info) abort();

    if (setjmp(png_jmpbuf(png))) abort();

    png_init_io(png, fp);

    // Output is 1bit depth, palette format.
    int num_palette = 2;
    png_colorp palettep;
    png_get_PLTE(png_ptr, info_ptr, &palettep, &num_palette);
    png_set_PLTE(png, info, palettep, num_palette);

    png_set_IHDR(png,
                 info,
                 width, height,
                 1,
                 PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT
    );
    png_write_info(png, info);

    // To remove the alpha channel for PNG_COLOR_TYPE_RGB format,
    // Use png_set_filler().
    //png_set_filler(png, 0, PNG_FILLER_AFTER);

    png_write_image(png, row_pointers);
    png_write_end(png, NULL);

    /*
    for(int y = 0; y < height; y++) {
        free(row_pointers[y]);
    }
    free(row_pointers);
     */

    fclose(fp);
}

void read_png_file(const char* file_name, png_byte red) {
    char header[8];    // 8 is the maximum size that can be checked

                       /* open file and test for it being a png */
    FILE *fp = fopen(file_name, "rb");
    if (!fp)
        abort_("[read_png_file] File %s could not be opened for reading", file_name);
    fread(header, 1, 8, fp);
    if (png_sig_cmp((png_const_bytep)header, 0, 8))
        abort_("[read_png_file] File %s is not recognized as a PNG file", file_name);


    /* initialize stuff */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr)
        abort_("[read_png_file] png_create_read_struct failed");

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
        abort_("[read_png_file] png_create_info_struct failed");

    if (setjmp(png_jmpbuf(png_ptr)))
        abort_("[read_png_file] Error during init_io");

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);

    png_read_info(png_ptr, info_ptr);

    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    number_of_passes = png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    /* read file */
    if (setjmp(png_jmpbuf(png_ptr)))
        abort_("[read_png_file] Error during read_image");

    row_pointers = (png_bytep*)calloc(height, sizeof(png_bytep));
    for (y = 0; y < height; y++)
        row_pointers[y] = (png_byte*)calloc(1, png_get_rowbytes(png_ptr, info_ptr));

    png_read_image(png_ptr, row_pointers);

    int num_palette;
    png_colorp palette;
    if (png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette)) {
        printf("Palette color count: %d\n", num_palette);
        if (num_palette != 2) {
            abort_("[read_png_file] palette size should be 2.\n");
        }
        for (int i = 0; i < num_palette; i++) {
            printf("  Color #%d: %d,%d,%d\n",
                   i, palette[i].red, palette[i].green, palette[i].blue);
            if (palette[i].red == red) {
                land_color_index = i;
            }
        }
    }

    fclose(fp);
}

void count_total_inverts(void) {
    if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_PALETTE)
        abort_("[count_total_inverts] color_type of input file must be PNG_COLOR_TYPE_PALETTE (%d) (is %d)",
               PNG_COLOR_TYPE_PALETTE, png_get_color_type(png_ptr, info_ptr));
    int verbose = height <= 64 && width <= 64;
    int total_invert_count = 0;
    for (y = 0; y < height; y++) {
        png_byte* row = row_pointers[y];
        int prev_b = 0;
        int invert_count = 0;
        for (x = 0; x < width; x++) {
            int b = PIXELBIT(row, x);
            if ((prev_b == 0 && b == 1) // 0 -> 1
                || (x == width - 1 && prev_b == 0 && b == 1) // last column 1
                ) {
                invert_count++;
            }
            if (verbose) {
                printf("%d", b);
            }
            prev_b = b;
        }
        if (verbose) {
            printf(" : %d inverted.\n", invert_count);
        }
        total_invert_count += invert_count;
    }
    printf("Total inverts: %d\n", total_invert_count);
}

void detect_concave_vertices(void) {
    if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_PALETTE)
        abort_("[detect_concave_vertices] color_type of input file must be PNG_COLOR_TYPE_PALETTE (%d) (is %d)",
               PNG_COLOR_TYPE_PALETTE, png_get_color_type(png_ptr, info_ptr));

    if (height <= 1 || width <= 1) {
        abort_("[detect_concave_vertices] image size must be larger than 2x2.");
    }

    int verbose = height <= 64 && width <= 64;

    int total_concave_vertices_count = 0;
    int total_convex_vertices_count = 0;
    for (y = 0; y < height - 1; y++) {
        png_byte* row0 = row_pointers[y + 0];
        png_byte* row1 = row_pointers[y + 1];
        for (x = 0; x < width - 1; x++) {
            char b00 = PIXELBIT(row0, x + 0); // ((row0[(x + 0) / 8] >> (7 - ((x + 0) % 8))) & 1) == land_color_index ? 1 : 0;
            char b01 = PIXELBIT(row0, x + 1); // ((row0[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) == land_color_index ? 1 : 0;
            char b10 = PIXELBIT(row1, x + 0); // ((row1[(x + 0) / 8] >> (7 - ((x + 0) % 8))) & 1) == land_color_index ? 1 : 0;
            char b11 = PIXELBIT(row1, x + 1); // ((row1[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) == land_color_index ? 1 : 0;
            char bcc = (b11 << 3) | (b10 << 2) | (b01 << 1) | b00;
            bool concave = bcc == 0b0111 || bcc == 0b1011 || bcc == 0b1101 || bcc == 0b1110;
            bool convex = bcc == 0b1000 || bcc == 0b0100 || bcc == 0b0010 || bcc == 0b0001;
            if (concave) {
                if (verbose) {
                    printf("Concave vertex at row %d col %d\n", y, x);
                }
                total_concave_vertices_count++;
            } else if (convex) {
                if (verbose) {
                    printf("Convex vertex at row %d col %d\n", y, x);
                }
                total_convex_vertices_count++;
            }
            if (concave || convex) {
                xyv xycombined;
                xycombined.coords.x = x;
                xycombined.coords.y = y;
                xycombined.v = concave ? VT_CONCAVE : VT_CONVEX;
                row_key_convex_concaves[y].push_back(xycombined);
                col_key_convex_concaves[x].push_back(xycombined);
            }
        }
    }
    printf("Total concave vertices: %d\n", total_concave_vertices_count);
    printf("Total convex vertices: %d\n", total_convex_vertices_count);
}

LINE_CHECK_RESULT check_line(const xyxy& line) {
    int dx = line.xy1.x - line.xy0.x;
    int dy = line.xy1.y - line.xy0.y;
    if (dx && !dy) {
        // horizontal line
        int x = line.xy0.x;
        int y = line.xy0.y;
        png_byte* row0 = row_pointers[y + 0];
        png_byte* row1 = row_pointers[y + 1];
        //char b00 = ((row0[(x + 0) / 8] >> (7 - ((x + 0) % 8))) & 1) == land_color_index ? 1 : 0;
        char b01 = PIXELBIT(row0, x + 1); // ((row0[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) == land_color_index ? 1 : 0;
        //char b10 = ((row1[(x + 0) / 8] >> (7 - ((x + 0) % 8))) & 1) == land_color_index ? 1 : 0;
        char b11 = PIXELBIT(row1, x + 1); // ((row1[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) == land_color_index ? 1 : 0;
        if (b01 && !b11) {
            // upper is 1
            for (int i = 0; i < dx; i++) {
                b01 = PIXELBIT(row0, x + i); // ((row0[(x + i) / 8] >> (7 - ((x + i) % 8))) & 1) == land_color_index ? 1 : 0;
                if (b01 == 0) {
                    // discontinuity detected. not neighbor.
                    return LCR_GOOD_CUT;
                }
            }
            return LCR_NEIGHBOR_VERTEX;
        } else if (!b01 && b11) {
            // lower is 1
            for (int i = 0; i < dx; i++) {
                b11 = PIXELBIT(row1, x + i); // ((row1[(x + i) / 8] >> (7 - ((x + i) % 8))) & 1) == land_color_index ? 1 : 0;
                if (b11 == 0) {
                    // discontinuity detected. not neighbor.
                    return LCR_GOOD_CUT;
                }
            }
            return LCR_NEIGHBOR_VERTEX;
        } else {
            // should be upper/lower both are 1
            // if not, disconnected
            for (int i = 0; i < dx; i++) {
                b01 = PIXELBIT(row0, x + i);
                b11 = PIXELBIT(row1, x + i);
                if (b01 == 0 && b11 == 0) {
                    // disconnection detected.
                    return LCR_DISCONNECTED;
                }
            }
            return LCR_GOOD_CUT;
        }
    } else if (!dx && dy) {
        // vertical line
        int x = line.xy0.x;
        int y = line.xy0.y;
        //png_byte* row0 = row_pointers[y + 0];
        png_byte* row1 = row_pointers[y + 1];
        //char b00 = ((row0[(x + 0) / 8] >> (7 - ((x + 0) % 8))) & 1) == land_color_index ? 1 : 0;
        //char b01 = ((row0[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) == land_color_index ? 1 : 0;
        char b10 = PIXELBIT(row1, x + 0); // ((row1[(x + 0) / 8] >> (7 - ((x + 0) % 8))) & 1) == land_color_index ? 1 : 0;
        char b11 = PIXELBIT(row1, x + 1); // ((row1[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) == land_color_index ? 1 : 0;
        if (b10 && !b11) {
            // left is 1
            for (int i = 0; i < dy; i++) {
                row1 = row_pointers[y + i];
                b10 = PIXELBIT(row1, x + 0); // ((row1[(x + 0) / 8] >> (7 - ((x + 0) % 8))) & 1) == land_color_index ? 1 : 0;
                if (b10 == 0) {
                    // discontinuity detected. not neighbor.
                    return LCR_GOOD_CUT;
                }
            }
            return LCR_NEIGHBOR_VERTEX;
        } else if (!b10 && b11) {
            // right is 1
            for (int i = 0; i < dy; i++) {
                row1 = row_pointers[y + i];
                b11 = PIXELBIT(row1, x + 1); // ((row1[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) == land_color_index ? 1 : 0;
                if (b11 == 0) {
                    // discontinuity detected. not neighbor.
                    return LCR_GOOD_CUT;
                }
            }
            return LCR_NEIGHBOR_VERTEX;
        } else {
            // should be left/right both are 1
            // if not, disconnected
            for (int i = 0; i < dy; i++) {
                row1 = row_pointers[y + i];
                b10 = PIXELBIT(row1, x + 0);
                b11 = PIXELBIT(row1, x + 1);
                if (b10 == 0 && b11 == 0) {
                    // disconnection detected.
                    return LCR_DISCONNECTED;
                }
            }
            return LCR_GOOD_CUT;
        }
    } else {
        // ???!?!?!?!?!?
        abort();
    }
    // ???!?!?!?!?!?
    abort();
    return LCR_BUG;
}

void get_lines(xyxyvector& lines, const int_xyvvector_map& concaves) {

    int verbose = height <= 64 && width <= 64;

    for (auto cit = concaves.cbegin(); cit != concaves.cend(); ++cit) {
        for (auto cit2 = cit->second.cbegin(); cit2 != cit->second.cend(); ++cit2) {
            // skip convex vertex
            if (cit2->v == VT_CONVEX) {
                continue;
            }
            // pull two elements at once
            xyxy line;
            line.xy0 = cit2->coords; // FIRST CIT2
            ++cit2;
            if (cit2 != cit->second.cend()) {
                // skip concave-convex vertex pair
                if (cit2->v == VT_CONVEX) {
                    continue;
                }

                line.xy1 = cit2->coords; // SECOND CIT2

                // check line.xy0 and line.xy1 are neighbors.
                auto check_result = check_line(line);
                if (check_result == LCR_NEIGHBOR_VERTEX || check_result == LCR_DISCONNECTED) {
                    // skip FIRST CIT2
                    --cit2;
                } else {
                    lines.push_back(line);
                    if (verbose) {
                        printf("Line (%d,%d)-(%d,%d) added.\n",
                               line.xy0.y, line.xy0.x, line.xy1.y, line.xy1.x);
                    }
                }
            } else {
                break;
            }
        }
    }
}

int max_match_test() {

    MaxMatchInt mint;
    mint.addVertex(mint.U_Vertex, 100);
    mint.addVertex(mint.V_Vertex, 200);
    mint.addEdge(100, 200);
    int c(mint.hopcroftKarp());
    std::cout << "Match size: " << c << std::endl;

    try {
        MaxMatchString m;

        m.addVertex(m.U_Vertex, "A");
        m.addVertex(m.U_Vertex, "B");
        m.addVertex(m.U_Vertex, "C");
        m.addVertex(m.U_Vertex, "D");
        m.addVertex(m.U_Vertex, "E");
        m.addVertex(m.U_Vertex, "F");
        m.addVertex(m.U_Vertex, "G");
        m.addVertex(m.U_Vertex, "H");
        m.addVertex(m.U_Vertex, "I");
        m.addVertex(m.U_Vertex, "J");
        m.addVertex(m.U_Vertex, "K");
        m.addVertex(m.U_Vertex, "L");
        m.addVertex(m.V_Vertex, "1");
        m.addVertex(m.V_Vertex, "2");
        m.addVertex(m.V_Vertex, "3");
        m.addVertex(m.V_Vertex, "4");
        m.addVertex(m.V_Vertex, "5");
        m.addVertex(m.V_Vertex, "6");
        m.addVertex(m.V_Vertex, "7");
        m.addVertex(m.V_Vertex, "8");
        m.addVertex(m.V_Vertex, "9");
        m.addVertex(m.V_Vertex, "10");
        m.addVertex(m.V_Vertex, "11");

        m.addEdge("A", "1");
        m.addEdge("A", "2");
        m.addEdge("A", "3");
        m.addEdge("B", "1");
        m.addEdge("B", "4");
        m.addEdge("B", "5");
        m.addEdge("C", "1");
        m.addEdge("C", "3");
        m.addEdge("D", "1");
        m.addEdge("D", "5");
        m.addEdge("E", "6");
        m.addEdge("E", "7");
        m.addEdge("E", "8");
        m.addEdge("F", "3");
        m.addEdge("F", "5");
        m.addEdge("G", "6");
        m.addEdge("G", "8");
        m.addEdge("H", "7");
        m.addEdge("I", "9");
        m.addEdge("J", "10");
        m.addEdge("K", "11");

        int c(m.hopcroftKarp());
        std::cout << "Match size: " << c << std::endl;

        MaxMatchString::VertexIndex uIdx(0);
        for (MaxMatchString::VertexIndexes::const_iterator u_to_v(m.us_to_vs().begin());
             u_to_v != m.us_to_vs().end();
             ++u_to_v, ++uIdx) {
            std::cout << m.u_vertexes()[uIdx].name << " -> " << m.v_vertexes()[*u_to_v].name << std::endl;
        }
    } catch (const std::string &e) {
        std::cerr << e << std::endl;
        return -1;
    }

    return 0;
}

bool line_cross(const xyxy& hori, const xyxy& vert) {
    return hori.xy0.x <= vert.xy0.x && vert.xy0.x <= hori.xy1.x
        && vert.xy0.y <= hori.xy0.y && hori.xy0.y <= vert.xy1.y;
}

void maximum_matching() {
    int verbose = height <= 64 && width <= 64;

    int i = 1;
    for (const auto& it : hori_lines) {
        bipartite.addVertex(bipartite.U_Vertex, i);
        if (verbose) {
            printf("U vertex (hori) #%d [(%d,%d)-(%d,%d)]\n", i, it.xy0.y, it.xy0.x, it.xy1.y, it.xy1.x);
        }
        i++;
    }
    int j = 1;
    for (const auto& it : vert_lines) {
        bipartite.addVertex(bipartite.V_Vertex, j);
        if (verbose) {
            printf("V vertex (vert) #%d [(%d,%d)-(%d,%d)]\n", j, it.xy0.y, it.xy0.x, it.xy1.y, it.xy1.x);
        }
        j++;
    }
    printf("Horizontal lines: %zu\n", hori_lines.size());
    printf("Vertical lines: %zu\n", vert_lines.size());
    printf("Adding crossing edges...\n");
    i = 1;
    int edge_count = 0;
    for (const auto& it : hori_lines) {
        j = 1;
        for (const auto& it2 : vert_lines) {
            if (line_cross(it, it2)) {
                bipartite.addEdge(i, j);
                if (verbose) {
                    printf("HORI #%d [(%d,%d)-(%d,%d)] and VERT #%d [(%d,%d)-(%d,%d)] crossed.\n",
                           i, it.xy0.y, it.xy0.x, it.xy1.y, it.xy1.x,
                           j, it2.xy0.y, it2.xy0.x, it2.xy1.y, it2.xy1.x);
                }
                edge_count++;
                if (edge_count % 500000 == 0) {
                    printf("  %d edges...\n", edge_count);
                }
            }
            j++;
        }
        i++;
    }
    printf("Get total edge count: %zu\n", bipartite.getEdgeCount());
    printf("Solving maximum matching using Hopcroft-Karp...\n");
    int c(bipartite.hopcroftKarp());
    std::cout << "Match size: " << c << std::endl;

    if (verbose) {
        MaxMatchString::VertexIndex uIdx(0);
        for (MaxMatchString::VertexIndexes::const_iterator u_to_v(bipartite.us_to_vs().begin());
             u_to_v != bipartite.us_to_vs().end();
             ++u_to_v, ++uIdx) {
            std::cout << bipartite.u_vertexes()[uIdx].name << " -> " << bipartite.v_vertexes()[*u_to_v].name << std::endl;
        }
    }
    printf("Solving minimum vertex cover from maximum matching...\n");
    MaxMatchInt::VertexIndexSet hori_min_vertex, vert_min_vertex;
    bipartite.findMinimumVertexCover(hori_min_vertex, vert_min_vertex);
    i = 1;
    int hori_cut_count = 0;
    for (const auto& it : hori_lines) {
        if (hori_min_vertex.find(i) == hori_min_vertex.end()) {
            // 'it' is not minimum cover vertex, that is, 'it' belongs to independent set
            // use 'it' as cut
            if (verbose) {
                printf("[CUT] U vertex (hori) #%d [(%d,%d)-(%d,%d)]\n", i, it.xy0.y, it.xy0.x, it.xy1.y, it.xy1.x);
            }

            //for (int x = it.xy0.x; x < it.xy1.x; x++) {
            //    PIXELINVERTBITXY(x + 1, it.xy0.y + 1);
            //}

            // cut caching
            hori_cut_count++;

            // cut boundary caching
            xyset boundary_up, boundary_down;
            for (int k = it.xy0.x; k < it.xy1.x; k++) {
                xy boundary;
                boundary.x = k + 1;
                boundary.y = it.xy0.y + 0;
                cut_boundary_pixels.insert(boundary);
                boundary_up.insert(boundary);

                boundary.y = it.xy0.y + 1;
                cut_boundary_pixels.insert(boundary);
                boundary_down.insert(boundary);
            }
            seed_pixels.push_back(boundary_up);
            seed_pixels.push_back(boundary_down);
        }
        i++;
        if (i % 1000 == 0) {
            printf("Adding cuts from horizontal lines %d...\n", i);
        }
    }
    j = 1;
    int vert_cut_count = 0;
    for (const auto& it : vert_lines) {
        if (vert_min_vertex.find(j) == vert_min_vertex.end()) {
            // 'it' is not minimum cover vertex, that is, 'it' belongs to independent set
            // use 'it' as cut
            if (verbose) {
                printf("[CUT] V vertex (vert) #%d [(%d,%d)-(%d,%d)]\n", j, it.xy0.y, it.xy0.x, it.xy1.y, it.xy1.x);
            }

            //for (int y = it.xy0.y; y < it.xy1.y; y++) {
            //    PIXELINVERTBITXY(it.xy0.x + 1, y + 1);
            //}

            // cut caching
            vert_cut_count++;
            // cut boundary caching
            xyset boundary_left, boundary_right;
            for (int k = it.xy0.y; k < it.xy1.y; k++) {
                xy boundary;
                boundary.x = it.xy0.x + 0;
                boundary.y = k + 1;
                cut_boundary_pixels.insert(boundary);
                boundary_left.insert(boundary);

                boundary.x = it.xy0.x + 1;
                cut_boundary_pixels.insert(boundary);
                boundary_right.insert(boundary);
            }
            seed_pixels.push_back(boundary_left);
            seed_pixels.push_back(boundary_right);
        }
        j++;
        if (j % 1000 == 0) {
            printf("Adding cuts from vertical lines %d...\n", j);
        }
    }
    printf("Total horizontal cut count: %d\n", hori_cut_count);
    printf("Total vertical cut count: %d\n", vert_cut_count);
}

void propagate_seed_pixels() {
    xyset covered;
    std::vector<xyset> segment_list;
    int seed_pixel_index = 0;
    for (const auto& seed : seed_pixels) {
        seed_pixel_index++;
        if (seed_pixel_index % 20000 == 0) {
            printf("Propagating seed pixel index %d...\n", seed_pixel_index);
        }
        xyset segment;
        // this seed already covered by previous segments
        bool skip_this_seed = false;
        for (const auto& seed_pixel : seed) {
            if (covered.find(seed_pixel) != covered.end()) {
                skip_this_seed = true;
                break;
            }
        }
        if (skip_this_seed) {
            covered.insert(seed.cbegin(), seed.cend());
            continue;
        }

        // all seed pixels compose a segment
        segment.insert(seed.cbegin(), seed.cend());
        covered.insert(seed.cbegin(), seed.cend());

        xyset start_seed = seed;
        xyset new_seed;
        do {
            new_seed.clear();
            for (const auto& pixel : start_seed) {
                int offsets[][2] = { { -1, 0 },{ 1, 0 },{ 0, -1 },{ 0, 1 } };
                for (const auto& off : offsets) {
                    xy pixel_pos = { pixel.x + off[0], pixel.y + off[1] };
                    // out of bounds
                    if (pixel_pos.x < 0 || pixel_pos.x >= width || pixel_pos.y < 0 || pixel_pos.y >= height) {
                        continue;
                    }
                    // cleared pixel
                    if (PIXELBITXY(pixel_pos.x, pixel_pos.y) == 0) {
                        continue;
                    }
                    // already covered
                    if (covered.find(pixel_pos) != covered.end()) {
                        continue;
                    }
                    // another seed boundary
                    if (cut_boundary_pixels.find(pixel_pos) != cut_boundary_pixels.end()) {
                        continue;
                    }
                    // new pixel found!
                    covered.insert(pixel_pos);
                    segment.insert(pixel_pos);
                    new_seed.insert(pixel_pos);
                }
            }
            start_seed = new_seed;
        } while (start_seed.size() > 0);
        if (segment.size() > 0) {
            segment_list.push_back(segment);
        }
    }
    printf("Segment count: %zu\n", segment_list.size());
    for (const auto& segment : segment_list) {
        for (const auto& seg_pixel : segment) {
            PIXELINVERTBITXY(seg_pixel.x, seg_pixel.y);
        }
    }
    /*
    for (y = 0; y < height; y++) {
        png_byte* row = row_pointers[y];
        int prev_b = 0;
        int invert_count = 0;
        for (x = 0; x < width; x++) {
            int b = PIXELBIT(row, x);
        }
    }*/
}

void first_pass() {
    count_total_inverts();
    detect_concave_vertices();
    get_lines(hori_lines, row_key_convex_concaves);
    get_lines(vert_lines, col_key_convex_concaves);
    maximum_matching();
    propagate_seed_pixels();
}

void second_pass(const char* output, size_t output_max_size) {
    int old_land_pixel_count = 0;
    for (int y = 0; y < height; y++) {
        png_byte* row = row_pointers[y];
        for (int x = 0; x < width; x++) {
            int b = PIXELBIT(row, x);
            if (b) {
                old_land_pixel_count++;
            }
        }
    }
    printf("Before land pixel count: %d\n", old_land_pixel_count);
    int max_area = 0;
    rect_pixel_set.clear();
    for (int y = 0; y < height; y++) {
        png_byte* row = row_pointers[y];
        for (int x = 0; x < width; x++) {
            int b = PIXELBIT(row, x);
            if (b) {
                int min_subheight = INT_MAX;
                int xs = x;
                for (; xs < width; xs++) {
                    int bx = PIXELBIT(row, xs);
                    if (bx) {
                        int ys = y;
                        for (; ys < height; ys++) {
                            png_byte* rowsub = row_pointers[ys];
                            int by = PIXELBIT(rowsub, xs);
                            if (by == 0) {
                                break;
                            }
                        }
                        int subheight = ys - y;
                        if (min_subheight > subheight) {
                            min_subheight = subheight;
                        }
                    } else {
                        break;
                    }
                }
                int max_subwidth = xs - x;
                RECTPIXEL rp;
                rp.x = x;
                rp.y = y;
                rp.w = max_subwidth;
                rp.h = min_subheight;
                int area = max_subwidth * min_subheight;
                if (max_area < area) {
                    max_area = area;
                }
                if (max_subwidth <= 0 || min_subheight <= 0) {
                    abort();
                }
                //                if (rect_pixel_set.find(rp) != rect_pixel_set.end()) {
                //
                //                    for (const auto& rps : rect_pixel_set) {
                //                        if (rps.x == rp.x && rps.y == rp.y && rps.w == rp.w && rps.h == rp.h) {
                //                            abort();
                //                        }
                //                    }
                //                    // wtf?
                //                    abort();
                //                }
                rect_pixel_set.push_back(rp);
                // clear rectpixel
                for (int ys = y; ys < y + min_subheight; ys++) {
                    png_byte* rowsub = row_pointers[ys];
                    for (int xs = x; xs < x + max_subwidth; xs++) {
                        int before = PIXELBIT(rowsub, xs);
                        if (before == 0) {
                            abort();
                        }
                        PIXELINVERTBIT(rowsub, xs);
                        int after = PIXELBIT(rowsub, xs);
                        if (after == 1) {
                            abort();
                        }
                    }
                }
            }
        }
    }
    printf("rect_pixel_set count: %zu\n", rect_pixel_set.size());
    printf("rect max area: %d\n", max_area);
    int new_land_pixel_count = 0;
    for (int y = 0; y < height; y++) {
        png_byte* row = row_pointers[y];
        for (int x = 0; x < width; x++) {
            int b = PIXELBIT(row, x);
            if (b) {
                new_land_pixel_count++;
            }
        }
    }
    printf("After land pixel count: %d (should be zero)\n", new_land_pixel_count);

    {
        bi::managed_mapped_file file(bi::open_or_create, output, output_max_size);
        allocator_t alloc(file.get_segment_manager());
        rtree_t * rtree_ptr = file.find_or_construct<rtree_t>("rtree")(params_t(), indexable_t(), equal_to_t(), alloc);
        rtree_ptr->clear();

        int reconstructed_pixel_count = 0;
        for (const auto& pixel_set : rect_pixel_set) {
            for (int y = pixel_set.y; y < pixel_set.y + pixel_set.h; y++) {
                png_byte* row = row_pointers[y];
                for (int x = pixel_set.x; x < pixel_set.x + pixel_set.w; x++) {
                    if (PIXELBIT(row, x) != 0) {
                        // already set???
                        abort();
                    }
                    PIXELSETBIT(row, x);
                    reconstructed_pixel_count++;
                }
            }

            box_t b(point_t(pixel_set.x, pixel_set.y), point_t(pixel_set.x + pixel_set.w, pixel_set.y + pixel_set.h));
            rtree_ptr->insert(std::make_pair(b, reconstructed_pixel_count));
        }
        printf("Reconstructed pixel count: %d\n", reconstructed_pixel_count);
        printf("R Tree size: %zu\n", rtree_ptr->size());
    }
}

void create_worldmap_rtree(const char* png_file, const char* output, size_t output_max_size, png_byte red) {
    bi::managed_mapped_file file(bi::open_or_create, output, output_max_size);
    allocator_t alloc(file.get_segment_manager());
    rtree_t * rtree_ptr = file.find_or_construct<rtree_t>("rtree")(params_t(), indexable_t(), equal_to_t(), alloc);
    printf("R Tree size: %zu\n", rtree_ptr->size());

    if (rtree_ptr->size() == 0) {
        // populate rtree
        read_png_file(png_file, red);
        second_pass(output, output_max_size);
    }

    box_t query_box(point_t(5, 5), point_t(6, 6));
    std::vector<value_t> result_s;
    rtree_ptr->query(bgi::contains(query_box), std::back_inserter(result_s));
    for (const auto& r : result_s) {
        printf("X0: %d, Y0: %d, X1: %d, Y1: %d --- %d\n",
               r.first.min_corner().get<0>(),
               r.first.min_corner().get<1>(),
               r.first.max_corner().get<0>(),
               r.first.max_corner().get<1>(),
               r.second);
    }
    printf("Total rects: %zu\n", result_s.size());
}


int out_of_bounds(int x, int y) {
    return x < 0 || x >= width || y < 0 || y >= height;
}


void create_worldmap_rtrees() {
    create_worldmap_rtree(DATA_ROOT "water_16384x8192.png", DATA_ROOT WORLDMAP_LAND_RTREE_FILENAME, WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE, 0);
    create_worldmap_rtree(DATA_ROOT "water_16384x8192.png", DATA_ROOT WORLDMAP_WATER_RTREE_FILENAME, WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE, 255);
}

void test_astar_rtree_water() {
    {
        // TEST POS (WATER: SHORT ROUTE)
        xy pathFrom = { 14065, 2496 };
        xy pathTo = { 14043, 2512 };
        astarrtree::astar_rtree(DATA_ROOT WORLDMAP_WATER_MAX_RECT_RTREE_RTREE_FILENAME, WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
    {
        // TEST POS (WATER: VERY LONG)
        xy pathFrom = { 14065, 2496 };
        xy pathTo = { 2693, 2501 };
        astarrtree::astar_rtree(DATA_ROOT WORLDMAP_WATER_MAX_RECT_RTREE_RTREE_FILENAME, WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
    {
        // TEST POS (WATER: VERY LONG II)
        xy pathFrom = { 9553, 2240 };
        xy pathTo = { 14348, 1604 };
        astarrtree::astar_rtree(DATA_ROOT WORLDMAP_WATER_MAX_RECT_RTREE_RTREE_FILENAME, WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
}

void test_astar_rtree_land() {
    {
        // TEST POS (LAND: VERY SHORT ROUTE - debugging)
        xy pathFrom = { 1, 1 };
        xy pathTo = { 4, 4 };
        astarrtree::astar_rtree(DATA_ROOT WORLDMAP_LAND_MAX_RECT_RTREE_RTREE_FILENAME, WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
    {
        // TEST POS (LAND: SHORT ROUTE)
        xy pathFrom = { 14066, 2488 };
        xy pathTo = { 14039, 2479 };
        astarrtree::astar_rtree(DATA_ROOT WORLDMAP_LAND_MAX_RECT_RTREE_RTREE_FILENAME, WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
    {
        // TEST POS (LAND: MID ROUTE)
        xy pathFrom = { 14066, 2488 };
        xy pathTo = { 13492, 753 };
        astarrtree::astar_rtree(DATA_ROOT WORLDMAP_LAND_MAX_RECT_RTREE_RTREE_FILENAME, WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
    {
        // TEST POS (LAND: LONG ROUTE)
        xy pathFrom = { 9031, 5657 };
        xy pathTo = { 16379, 955 };
        astarrtree::astar_rtree(DATA_ROOT WORLDMAP_LAND_MAX_RECT_RTREE_RTREE_FILENAME, WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
    {
        // TEST POS (LAND: NO ROUTE)
        xy pathFrom = { 13528, 5192 };
        xy pathTo = { 11716, 3620 };
        astarrtree::astar_rtree(DATA_ROOT WORLDMAP_LAND_MAX_RECT_RTREE_RTREE_FILENAME, WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
}

void PathNodeNeighbors(ASNeighborList neighbors, void *node, void *context) {
    xy* n = reinterpret_cast<xy*>(node);
    short offsets[][2] = { { -1, 0 },{ 1, 0 },{ 0, -1 },{ 0, 1 } };
    for (const auto& off : offsets) {
        short x2 = n->x + off[0];
        short y2 = n->y + off[1];
        if (out_of_bounds(x2, y2) == 0 && PIXELBITXY(x2, y2) == 0) {
            xy n2 = { x2, y2 };
            ASNeighborListAdd(neighbors, &n2, 1);
        }
    }
}

float PathNodeHeuristic(void *fromNode, void *toNode, void *context) {
    xy* from = reinterpret_cast<xy*>(fromNode);
    xy* to = reinterpret_cast<xy*>(toNode);

    // using the manhatten distance since this is a simple grid and you can only move in 4 directions
    return (fabsf(static_cast<float>(from->x - to->x)) + fabsf(static_cast<float>(from->y - to->y)));
}

int PathNodeComparator(void *node1, void *node2, void *context) {
    xy* n1 = reinterpret_cast<xy*>(node1);
    int n1v = n1->y << 16 | n1->x;
    xy* n2 = reinterpret_cast<xy*>(node2);
    int n2v = n2->y << 16 | n2->x;
    int d = n1v - n2v;
    if (d == 0) {
        return 0;
    } else if (d > 0) {
        return 1;
    } else {
        return -1;
    }
}

void test_astar() {
    read_png_file(DATA_ROOT "water_16384x8192.png", 0);

    ASPathNodeSource PathNodeSource =
    {
        sizeof(xy),
        PathNodeNeighbors,
        PathNodeHeuristic,
        NULL,
        PathNodeComparator
    };
    xy pathFrom = { 14092, 2452 };
    xy pathTo = { 13626, 2370 }; // china
                                 //xy pathTo = { 14601, 2466 }; // japan
                                 //xy pathTo = { 14956, 5826 }; // austrailia
    ASPath path = ASPathCreate(&PathNodeSource, NULL, &pathFrom, &pathTo);
    size_t pathCount = ASPathGetCount(path);
    printf("Path Count: %zu\n", pathCount);
    float pathCost = ASPathGetCost(path);
    printf("Path Cost: %f\n", pathCost);
    if (pathCost < 2000) {
        for (size_t i = 0; i < pathCount; i++) {
            xy* node = reinterpret_cast<xy*>(ASPathGetNode(path, i));
            printf("Path %zu: (%d, %d)\n", i, node->x, node->y);
        }
    }
}

struct height_width_start {
    int height;
    int width;
    int start;
    int area() const { return height * width; }
    int last_max_area;
};

struct height_width_row_col {
    int height;
    int width;
    int row;
    int col;
    int area() const { return height * width; }
    height_width_row_col(int h, int w, int r, int c) : height(h), width(w), row(r), col(c) {}
};

struct start_height {
    int start;
    int height;
    start_height(int s, int h) : start(s), height(h) {}
};

std::vector<int> histogram_from_row(int r) {
    png_bytep row = row_pointers[r];
    std::vector<int> histogram(width);
    for (int x = 0; x < width; x++) {
        histogram[x] = PIXELBIT(row, x);
    }
    return histogram;
}

height_width_start max_rectangle_size(const std::vector<int>& histogram) {
    std::stack<start_height> stack;
    height_width_start max_size = { 0, 0, -1 };
    int pos = 0;
    for (pos = 0; pos < static_cast<int>(histogram.size()); pos++) {
        int height = histogram[pos];
        int start = pos;
        while (true) {
            if (stack.empty() || height > stack.top().height) {
                stack.push(start_height(start, height));
            } else if (stack.empty() == false && height < stack.top().height) {
                height_width_start new_max_size = {
                    stack.top().height,
                    pos - stack.top().start,
                    stack.top().start,
                };
                if (max_size.area() < new_max_size.area()) {
                    max_size = new_max_size;
                }
                start = stack.top().start;
                stack.pop();
                continue;
            }
            break;
        }
    }
    //pos += 1;
    while (stack.empty() == false) {
        auto v = stack.top();
        height_width_start new_size = {
            v.height,
            pos - v.start,
            v.start,
        };
        if (max_size.area() < new_size.area()) {
            max_size = new_size;
        }
        stack.pop();
    }
    return max_size;
}

std::unordered_set<int> omit_row;

height_width_row_col get_max_size(const int last_max_area, const int last_max_area_row) {
    auto hist = histogram_from_row(last_max_area_row);
    auto max_size = max_rectangle_size(hist);
    int max_area = max_size.area();
    int last_row = max_area > 0 ? last_max_area_row : -1;
    if (max_area == last_max_area) {
        return height_width_row_col(max_size.height, max_size.width, last_row - max_size.height + 1, max_size.start);
    }

    for (int rowindex = last_max_area_row; rowindex < height - 1; rowindex++) {
        if (omit_row.find(rowindex + 1) != omit_row.end()) {
            std::fill(hist.begin(), hist.end(), 0);
            continue;
        }
        png_bytep row = row_pointers[rowindex + 1];
        for (int x = 0; x < width; x++) {
            auto h = hist[x];
            auto el = PIXELBIT(row, x);
            hist[x] = el ? (1 + h) : 0;
        }
        bool hist_zeros = std::all_of(hist.begin(), hist.end(), [](int i) { return i == 0; });
        if (hist_zeros) {
            omit_row.insert(rowindex + 1);
            //printf("Row %d omitted.\n", rowindex + 1);
        } else {
            auto new_size = max_rectangle_size(hist);
            if (max_size.area() < new_size.area()) {
                last_row = rowindex + 1;
                max_size = new_size;
                // early exit
                if (max_size.area() == last_max_area) {
                    //printf("Fast\n");
                    return height_width_row_col(max_size.height, max_size.width, last_row - max_size.height + 1, max_size.start);
                }
                //last_max_area = max_size.area();
            }
        }
    }
    if (max_size.area() == 0) {
        return height_width_row_col(0, 0, -1, -1);
    } else {
        return height_width_row_col(max_size.height, max_size.width, last_row - max_size.height + 1, max_size.start);
    }
}

void invert_area(int x, int y, int w, int h) {
    // clear rectpixel
    for (int ys = y; ys < y + h; ys++) {
        png_byte* rowsub = row_pointers[ys];
        for (int xs = x; xs < x + w; xs++) {
            int before = PIXELBIT(rowsub, xs);
            if (before == 0) {
                printf("B X (x=%d, y=%d)\n", xs, ys);
                abort();
            }
            PIXELINVERTBIT(rowsub, xs);
            int after = PIXELBIT(rowsub, xs);
            if (after == 1) {
                printf("A X (x=%d, y=%d)\n", xs, ys);
                abort();
            }
        }
    }
}

void load_from_dump_if_empty(rtree_t* rtree_ptr, const char* dump_filename) {
    if (rtree_ptr->size() == 0) {
        int rect_count = 0;
        FILE* fin = fopen(dump_filename, "rb");
        if (fin) {
            size_t read_max_count = 100000; // elements
            void* read_buf = malloc(sizeof(xyxy) * read_max_count);
            fseek(fin, 0, SEEK_SET);
            while (size_t read_count = fread(read_buf, sizeof(xyxy), read_max_count, fin)) {
                for (size_t i = 0; i < read_count; i++) {
                    rect_count++;
                    xyxy* r = reinterpret_cast<xyxy*>(read_buf) + i;
                    box_t box(point_t(r->xy0.x, r->xy0.y), point_t(r->xy1.x, r->xy1.y));
                    rtree_ptr->insert(std::make_pair(box, rect_count));
                }
            }
            fclose(fin);
            printf("Max rect R Tree size (after loaded from %s): %zu\n", dump_filename, rtree_ptr->size());
        } else {
            printf("Dump file %s not exist.\n", dump_filename);
        }
    }
}

void dump_max_rect(const char* input_png_filename, const char* rtree_filename, size_t rtree_memory_size, const char* dump_filename, int write_dump, png_byte red) {
    read_png_file(input_png_filename, red);

    bi::managed_mapped_file file(bi::open_or_create, rtree_filename, rtree_memory_size);
    allocator_t alloc(file.get_segment_manager());
    rtree_t * rtree_ptr = file.find_or_construct<rtree_t>("rtree")(params_t(), indexable_t(), equal_to_t(), alloc);
    printf("Max rect R Tree size: %zu\n", rtree_ptr->size());

    load_from_dump_if_empty(rtree_ptr, dump_filename);

    size_t old_pixel_count = 0;
    for (int y = 0; y < height; y++) {
        png_byte* row = row_pointers[y];
        for (int x = 0; x < width; x++) {
            int b = PIXELBIT(row, x);
            if (b) {
                old_pixel_count++;
            }
        }
    }

    printf("Total land pixel count (original): %zu\n", old_pixel_count);
    //printf("BUG PIXEL VALUE = %d\n", PIXELBITXY(5127, 6634));
    std::vector<value_t> to_be_removed;
    auto rtree_bounds = rtree_ptr->bounds();
    for (auto it = rtree_ptr->qbegin(bgi::intersects(rtree_bounds)); it != rtree_ptr->qend(); it++) {
        int x = it->first.min_corner().get<0>();
        int y = it->first.min_corner().get<1>();
        int w = it->first.max_corner().get<0>() - x;
        int h = it->first.max_corner().get<1>() - y;
        if (x < 0 || y < 0 || w == 0 || h == 0) {
            to_be_removed.push_back(*it);
            printf("Invalid R-tree node (x=%d,y=%d,w=%d,h=%d) will be removed.\n", x, y, w, h);
        } else {
            invert_area(x, y, w, h);
        }
    }
    for (auto v : to_be_removed) {
        rtree_ptr->remove(v);
    }
    //printf("BUG PIXEL VALUE = %d\n", PIXELBITXY(5127, 6634));
    size_t remaining_pixel_count = 0;
    for (int y = 0; y < height; y++) {
        png_byte* row = row_pointers[y];
        for (int x = 0; x < width; x++) {
            int b = PIXELBIT(row, x);
            if (b) {
                remaining_pixel_count++;
            }
        }
    }

    printf("Total land pixel count (remaining): %zu\n", remaining_pixel_count);

    int rect_count = 0;
    int last_max_area = -1;
    int scan_start_row = 0;
    while (true) {
        //printf("BUG PIXEL VALUE = %d\n", PIXELBITXY(5127, 6634));
        //printf("last_max_area = %d, scan_start_row = %d\n", last_max_area, scan_start_row);
        auto r2 = get_max_size(last_max_area, scan_start_row);
        if (r2.area() == last_max_area) {
            // turn on fast search next time
            scan_start_row = r2.row;
        } else if (scan_start_row != 0 && r2.area() != last_max_area) {
            // search again
            scan_start_row = 0;
            r2 = get_max_size(last_max_area, scan_start_row);
        }
        int area = r2.area();
        last_max_area = area;
        remaining_pixel_count -= area;

        if (area > 0) {
            printf("x=%d, y=%d, w=%d, h=%d, area=%d (Remaining %d - %.2f%%), omit row = %zu\n",
                   r2.col,
                   r2.row,
                   r2.width,
                   r2.height,
                   area,
                   remaining_pixel_count,
                   (float)remaining_pixel_count / old_pixel_count * 100,
                   omit_row.size());
            invert_area(r2.col, r2.row, r2.width, r2.height);

            rect_count++;
            box_t box(point_t(r2.col, r2.row), point_t(r2.col + r2.width, r2.row + r2.height));
            rtree_ptr->insert(std::make_pair(box, rect_count));
        }

        if (remaining_pixel_count <= 0) {
            break;
        }
        /*if (rect_count % 20 == 0) {
        write_png_file(DATA_ROOT "rect_output.png");
        }*/
    }
    size_t new_pixel_count = 0;
    for (int y = 0; y < height; y++) {
        png_byte* row = row_pointers[y];
        for (int x = 0; x < width; x++) {
            int b = PIXELBIT(row, x);
            if (b) {
                new_pixel_count++;
            }
        }
    }
    printf("After land pixel count: %zu (should be zero)\n", new_pixel_count);

    if (write_dump) {
        // dump final result to portable format
        std::vector<xyxy> write_buffer;
        write_buffer.reserve(rtree_ptr->size());
        rtree_bounds = rtree_ptr->bounds();
        for (auto it = rtree_ptr->qbegin(bgi::intersects(rtree_bounds)); it != rtree_ptr->qend(); it++) {
            xyxy v;
            v.xy0.x = it->first.min_corner().get<0>();
            v.xy0.y = it->first.min_corner().get<1>();
            v.xy1.x = it->first.max_corner().get<0>();
            v.xy1.y = it->first.max_corner().get<1>();
            write_buffer.push_back(v);
        }
        FILE* fout = fopen(dump_filename, "wb");
        fwrite(&write_buffer[0], sizeof(xyxy), write_buffer.size(), fout);
        fclose(fout);
        printf("Dumped. (element size: %zu, element count: %zu)\n", sizeof(xyxy), write_buffer.size());
    }
}

void change_working_directory() {
    auto cwd = boost::filesystem::current_path();
    do {
        auto assets = cwd;
        assets.append("assets");
        if (boost::filesystem::is_directory(assets)) {
            boost::filesystem::current_path(cwd);
            break;
        }
        cwd.remove_leaf();
    } while (!cwd.empty());

    if (cwd.empty()) {
        abort();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "sea-route v0.1" << std::endl;
    change_working_directory();

    try {
        boost::program_options::options_description desc{ "options" };
        desc.add_options()
            ("help,h", "Help screen")
            ("png2rtree", boost::program_options::value<std::string>(), "PNG to R-tree (max-rect)")
            ("land", boost::program_options::bool_switch(), "Build land mask")
            ("water", boost::program_options::bool_switch(), "Build water mask")
            ("dump", boost::program_options::bool_switch(), "Create dump file")
            ("test", boost::program_options::bool_switch(), "Test")
            ;

        boost::program_options::variables_map vm;
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
        boost::program_options::notify(vm);

        //read_png_file(DATA_ROOT "water_land_20k.png");
        //read_png_file(DATA_ROOT "water_16k.png");
        //read_png_file(DATA_ROOT "water_16k_first_pass.png");
        //read_png_file(DATA_ROOT "bw.png");
        //read_png_file(DATA_ROOT "dissection_1.png");
        //read_png_file(DATA_ROOT "dissection_2.png");
        //read_png_file(DATA_ROOT "dissection_3.png");
        //read_png_file(DATA_ROOT "dissection_stackoverflow_example.png");
        //read_png_file(DATA_ROOT "dissection_center_hole.png");
        //read_png_file(DATA_ROOT "dissection_6.png");
        //read_png_file(DATA_ROOT "dissection_big_arrow.png");
        //read_png_file(DATA_ROOT "dissection_8.png");
        //read_png_file(DATA_ROOT "dissection_9.png");
        //read_png_file(DATA_ROOT "dissection_cross.png");
        //read_png_file(DATA_ROOT "dissection_64x64.png");
        //read_png_file(DATA_ROOT "dissection_framed.png");
        //read_png_file(DATA_ROOT "dissection_framed_small.png");
        //read_png_file(DATA_ROOT "dissection_islands.png");
        //read_png_file(DATA_ROOT "dissection_four.png");
        //read_png_file(DATA_ROOT "dissection_tetris.png");
        //read_png_file(DATA_ROOT "water_16384x8192.png", 0);
        //read_png_file(DATA_ROOT "max_rect_1.png", 0);

        //first_pass();
        //second_pass(); //-------!!!
        //write_png_file(DATA_ROOT "dissection_output.png");

        //create_worldmap_rtrees();

        //test_astar();

        if (vm.empty() || vm.count("help")) {
            std::cout << desc << '\n';
            return 0;
        }

        if (vm.count("png2rtree")) {
            auto input_png_filename = vm["png2rtree"].as<std::string>();

            auto dump = vm.count("dump") && vm["dump"].as<bool>();

            if (vm.count("land") && vm["land"].as<bool>()) {
                auto rtree_filename = input_png_filename + ".land.rtree";
                auto dump_filename = input_png_filename + ".land.dump";

                dump_max_rect(input_png_filename.c_str(),
                              rtree_filename.c_str(),
                              WORLDMAP_LAND_MAX_RECT_RTREE_MMAP_MAX_SIZE,
                              dump_filename.c_str(),
                              dump ? 1 : 0,
                              0);
            }
        
            if (vm.count("water") && vm["water"].as<bool>()) {
                auto rtree_filename = input_png_filename + ".water.rtree";
                auto dump_filename = input_png_filename + ".water.dump";

                dump_max_rect(input_png_filename.c_str(),
                              rtree_filename.c_str(),
                              WORLDMAP_WATER_MAX_RECT_RTREE_MMAP_MAX_SIZE,
                              dump_filename.c_str(),
                              dump ? 1 : 0,
                              0);
            }
        }

        if (vm.count("test") && vm["test"].as<bool>()) {
            if (vm.count("land") && vm["land"].as<bool>()) {
                test_astar_rtree_land();
            }

            if (vm.count("water") && vm["water"].as<bool>()) {
                test_astar_rtree_water();
            }
        }
    } catch (const boost::program_options::error &ex) {
        std::cerr << ex.what() << '\n';
    }
    return 0;
}
