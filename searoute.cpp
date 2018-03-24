#include "precompiled.hpp"
#include "MaxMatch.h"
#include "AStar.h"

#define WORLDMAP_RTREE_MMAP_MAX_SIZE_RATIO (sizeof(size_t) / 4)
#define DATA_ROOT "assets/"
#define WORLDMAP_LAND_RTREE_FILENAME "worldmap_land.dat"
#define WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE (7 * 1024 * 1024 * WORLDMAP_RTREE_MMAP_MAX_SIZE_RATIO)
#define WORLDMAP_WATER_RTREE_FILENAME "worldmap_water.dat"
#define WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE (7 * 1024 * 1024 * WORLDMAP_RTREE_MMAP_MAX_SIZE_RATIO)

enum VERTEX_TYPE {
    VT_CONVEX,
    VT_CONCAVE,
};


enum LINE_CHECK_RESULT {
    LCR_GOOD_CUT,
    LCR_NEIGHBOR_VERTEX,
    LCR_DISCONNECTED,
    LCR_BUG,
};

struct xy {
    int x : 16;
    int y : 16;
};

inline bool operator < (const xy& lhs, const xy& rhs) {
    return (lhs.y << 16 | lhs.x) < (rhs.y << 16 | rhs.x);
}

struct xyv {
    xy coords;
    VERTEX_TYPE v;
};
struct xyxy {
    xy xy0;
    xy xy1;
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

typedef bgm::point<short, 2, bg::cs::cartesian> point_t;
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
    if(!fp) abort();
    
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
    
    for(int y = 0; y < height; y++) {
        free(row_pointers[y]);
    }
    free(row_pointers);
    
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
    for (y = 0; y<height; y++)
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
    for (y = 0; y<height; y++) {
        png_byte* row = row_pointers[y];
        int prev_b = 0;
        int invert_count = 0;
        for (x = 0; x<width; x++) {
            int b = PIXELBIT(row, x);
            if ((prev_b == 0 && b == 1) // 0 -> 1
                || (x == width-1 && prev_b == 0 && b == 1) // last column 1
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
    for (y = 0; y<height-1; y++) {
        png_byte* row0 = row_pointers[y + 0];
        png_byte* row1 = row_pointers[y + 1];
        for (x = 0; x<width-1; x++) {
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


xyxy xyxy_from_box_t(const box_t& v) {
    xyxy r;
    r.xy0.x = v.min_corner().get<0>();
    r.xy0.y = v.min_corner().get<1>();
    r.xy1.x = v.max_corner().get<0>();
    r.xy1.y = v.max_corner().get<1>();
    return r;
}

box_t box_t_from_xyxy(const xyxy& v) {
    box_t r;
    r.min_corner().set<0>(v.xy0.x);
    r.min_corner().set<1>(v.xy0.y);
    r.max_corner().set<0>(v.xy1.x);
    r.max_corner().set<1>(v.xy1.y);
    return r;
}

void RTreePathNodeNeighbors(ASNeighborList neighbors, void *node, void *context) {
    xyxy* n = reinterpret_cast<xyxy*>(node);
    rtree_t* rtree_ptr = reinterpret_cast<rtree_t*>(context);
    box_t query_box = box_t_from_xyxy(*n);
    std::vector<value_t> result_s;
    rtree_ptr->query(bgi::intersects(query_box), std::back_inserter(result_s));
    for (const auto& v : result_s) {
        auto n2 = xyxy_from_box_t(v.first);
        ASNeighborListAdd(neighbors, &n2, 1);
    }
}

float RTreePathNodeHeuristic(void *fromNode, void *toNode, void *context) {
    xyxy* from = reinterpret_cast<xyxy*>(fromNode);
    xyxy* to = reinterpret_cast<xyxy*>(toNode);
    float fromMedX = (from->xy1.x - from->xy0.x) / 2.0f;
    float fromMedY = (from->xy1.y - from->xy0.y) / 2.0f;
    float toMedX = (to->xy1.x - to->xy0.x) / 2.0f;
    float toMedY = (to->xy1.y - to->xy0.y) / 2.0f;
    return fabsf(fromMedX - toMedX) + fabsf(fromMedY - toMedY);
}

int RTreePathNodeComparator(void *node1, void *node2, void *context) {
    xyxy* n1 = reinterpret_cast<xyxy*>(node1);
    int n1v = n1->xy0.y << 16 | n1->xy0.x;
    xyxy* n2 = reinterpret_cast<xyxy*>(node2);
    int n2v = n2->xy0.y << 16 | n2->xy0.x;
    int d = n1v - n2v;
    if (d == 0) {
        return 0;
    } else if (d > 0) {
        return 1;
    } else {
        return -1;
    }
}

void create_worldmap_rtrees() {
    create_worldmap_rtree(DATA_ROOT "water_16384x8192.png", DATA_ROOT WORLDMAP_LAND_RTREE_FILENAME, WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE, 0);
    create_worldmap_rtree(DATA_ROOT "water_16384x8192.png", DATA_ROOT WORLDMAP_WATER_RTREE_FILENAME, WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE, 255);
}

void astar_rtree(const char* output, size_t output_max_size, xy from, xy to) {
    bi::managed_mapped_file file(bi::open_or_create, output, output_max_size);
    allocator_t alloc(file.get_segment_manager());
    rtree_t * rtree_ptr = file.find_or_construct<rtree_t>("rtree")(params_t(), indexable_t(), equal_to_t(), alloc);
    printf("R Tree size: %zu\n", rtree_ptr->size());

    if (rtree_ptr->size() == 0) {
        abort();
    }

    box_t from_box(point_t(from.x, from.y), point_t(from.x + 1, from.y + 1));
    std::vector<value_t> from_result_s;
    rtree_ptr->query(bgi::contains(from_box), std::back_inserter(from_result_s));

    box_t to_box(point_t(to.x, to.y), point_t(to.x + 1, to.y + 1));
    std::vector<value_t> to_result_s;
    rtree_ptr->query(bgi::contains(to_box), std::back_inserter(to_result_s));

    auto s = rtree_ptr->qbegin(bgi::contains(to_box));

    if (from_result_s.size() == 1 && to_result_s.size() == 1) {
        // Phase 1 - R Tree rectangular node searching
        ASPathNodeSource PathNodeSource =
        {
            sizeof(xyxy),
            RTreePathNodeNeighbors,
            RTreePathNodeHeuristic,
            NULL,
            RTreePathNodeComparator
        };
        xyxy from_rect = xyxy_from_box_t(from_result_s[0].first);
        xyxy to_rect = xyxy_from_box_t(to_result_s[0].first);
        ASPath path = ASPathCreate(&PathNodeSource, rtree_ptr, &from_rect, &to_rect);
        size_t pathCount = ASPathGetCount(path);
        if (pathCount > 0) {
            printf("Path Count: %zu\n", pathCount);
            float pathCost = ASPathGetCost(path);
            printf("Path Cost: %f\n", pathCost);
            if (pathCost < 6000) {
                for (size_t i = 0; i < pathCount; i++) {
                    xyxy* node = reinterpret_cast<xyxy*>(ASPathGetNode(path, i));
                    printf("Path %zu: (%d, %d)-(%d, %d) [%d x %d = %d]\n",
                           i,
                           node->xy0.x,
                           node->xy0.y,
                           node->xy1.x,
                           node->xy1.y,
                           node->xy1.x - node->xy0.x,
                           node->xy1.y - node->xy0.y,
                           (node->xy1.x - node->xy0.x) * (node->xy1.y - node->xy0.y));
                }
            }
            // Phase 2 - per-pixel node searching
            /*std::unordered_set<int> pixels;
            for (size_t i = 0; i < pathCount; i++) {
                xyxy* node = reinterpret_cast<xyxy*>(ASPathGetNode(path, i));
                for (int pi = node->xy0.x; pi < node->xy1.x; pi++) {
                    for (int pj = node->xy0.y; pj < node->xy1.y; pj++) {
                        pixels.insert(pj << 16 | pi);
                    }
                }
            }
            printf("Search pixel set element count: %zu\n", pixels.size());*/
        } else {
            std::cerr << "No path found." << std::endl;
        }
    } else {
        std::cerr << "From-to node error." << std::endl;
    }
}

void test_astar_rtree() {
    {
        // TEST POS
        xy pathFrom = { 1, 1 };
        xy pathTo = { 4, 4 };
        astar_rtree(DATA_ROOT WORLDMAP_LAND_RTREE_FILENAME, WORLDMAP_LAND_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
    {
        // KOREA -> AUSTRAILIA
        xy pathFrom = { 14085, 2450 };
        xy pathTo = { 14472, 5800 };
        astar_rtree(DATA_ROOT WORLDMAP_WATER_RTREE_FILENAME, WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
    {
        // KOREA -> USA
        xy pathFrom = { 14085, 2450 };
        xy pathTo = { 2856, 2928 };
        astar_rtree(DATA_ROOT WORLDMAP_WATER_RTREE_FILENAME, WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
    }
    {
        // ORIGIN -> END OF THE WORLD
        xy pathFrom = { 0, 0 };
        xy pathTo = { 15640, 7564 };
        astar_rtree(DATA_ROOT WORLDMAP_WATER_RTREE_FILENAME, WORLDMAP_WATER_RTREE_MMAP_MAX_SIZE, pathFrom, pathTo);
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
    for (pos = 0; pos < histogram.size(); pos++) {
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

height_width_row_col max_size() {
    auto hist = histogram_from_row(0);
    auto max_size = max_rectangle_size(hist);
    int last_row = max_size.area() > 0 ? 0 : -1;
    for (int rowindex = 0; rowindex < height - 1; rowindex++) {
        png_bytep row = row_pointers[rowindex + 1];
        for (int x = 0; x < width; x++) {
            auto h = hist[x];
            auto el = PIXELBIT(row, x);
            hist[x] = el ? (1 + h) : 0;
        }
        auto new_size = max_rectangle_size(hist);
        if (max_size.area() < new_size.area()) {
            last_row = rowindex + 1;
            max_size = new_size;
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

int main(int argc, char **argv) {
    std::cout << "sea-route v0.1" << std::endl;
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
    read_png_file(DATA_ROOT "water_16384x8192.png", 0);
    //read_png_file(DATA_ROOT "max_rect_1.png", 0);
    
    //first_pass();
    //second_pass(); //-------!!!
    //write_png_file(DATA_ROOT "dissection_output.png");

    //create_worldmap_rtrees();

    //test_astar_rtree();
    
    //test_astar();
    
    //auto r1 = max_rectangle_size(histogram_from_row(4000));
    
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
    printf("Total land pixel count: %d\n", old_land_pixel_count);
    int remaining_land_pixel_count = old_land_pixel_count;
    while (true) {
        auto r2 = max_size();
        int area = r2.width * r2.height;
        remaining_land_pixel_count -= area;
        printf("x=%d, y=%d, w=%d, h=%d, area=%d (Remaining %d - %.2f%%)\n", r2.col, r2.row, r2.width, r2.height, area, remaining_land_pixel_count, (float)remaining_land_pixel_count / old_land_pixel_count * 100);
        invert_area(r2.col, r2.row, r2.width, r2.height);
        if (remaining_land_pixel_count <= 0) {
            break;
        }
    }
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
    return 0;
}
