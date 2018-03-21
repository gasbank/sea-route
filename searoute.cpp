#include "precompiled.hpp"
#include "MaxMatch.h"

enum VERTEX_TYPE {
    VT_CONVEX,
    VT_CONCAVE,
};

struct xy {
    int x : 16;
    int y : 16;
};
struct xyv {
    xy xy;
    VERTEX_TYPE v;
};
struct xyxy {
    xy xy0;
    xy xy1;
};
typedef std::unordered_map<int, std::vector<xyv> > int_xyvvector_map;
typedef std::vector<xyxy> xyxyvector;
typedef MaxMatch<std::string> MaxMatchString;
typedef MaxMatch<int> MaxMatchInt;

void abort_(const char * s, ...) {
    va_list args;
    va_start(args, s);
    vfprintf(stderr, s, args);
    fprintf(stderr, "\n");
    va_end(args);
    abort();
}

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

#define PIXELBIT(row, x) (((row)[(x) / 8] >> (7 - ((x) % 8))) & 1) == land_color_index ? 1 : 0

void read_png_file(const char* file_name) {
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
            if (palette[i].red == 0) {
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
                xycombined.xy.x = x;
                xycombined.xy.y = y;
                xycombined.v = concave ? VT_CONCAVE : VT_CONVEX;
                row_key_convex_concaves[y].push_back(xycombined);
                col_key_convex_concaves[x].push_back(xycombined);
            }
        }
    }
    printf("Total concave vertices: %d\n", total_concave_vertices_count);
    printf("Total convex vertices: %d\n", total_convex_vertices_count);
}

bool check_neighbor(const xyxy& line) {
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
                    return false;
                }
            }
            return true;
        } else if (!b01 && b11) {
            // lower is 1
            for (int i = 0; i < dx; i++) {
                b11 = PIXELBIT(row1, x + i); // ((row1[(x + i) / 8] >> (7 - ((x + i) % 8))) & 1) == land_color_index ? 1 : 0;
                if (b11 == 0) {
                    // discontinuity detected. not neighbor.
                    return false;
                }
            }
            return true;
        } else {
            return false;
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
                    return false;
                }
            }
            return true;
        } else if (!b10 && b11) {
            // right is 1
            for (int i = 0; i < dy; i++) {
                row1 = row_pointers[y + i];
                b11 = PIXELBIT(row1, x + 1); // ((row1[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) == land_color_index ? 1 : 0;
                if (b11 == 0) {
                    // discontinuity detected. not neighbor.
                    return false;
                }
            }
            return true;
        } else {
            return false;
        }
    } else {
        // ???!?!?!?!?!?
        abort();
    }
    return false;
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
            line.xy0 = cit2->xy; // FIRST CIT2
            ++cit2;
            if (cit2 != cit->second.cend()) {
                // skip concave-convex vertex pair
                if (cit2->v == VT_CONVEX) {
                    continue;
                }

                line.xy1 = cit2->xy; // SECOND CIT2
                
                // check line.xy0 and line.xy1 are neighbors.
                if (check_neighbor(line)) {
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
    return hori.xy0.x < vert.xy0.x && vert.xy0.x < hori.xy1.x
        && vert.xy0.y < hori.xy0.y && hori.xy0.y < vert.xy1.y;
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
    bipartite.findMinimumVertexCover();
}

#ifdef __APPLE__
#define DATA_ROOT "/Users/kimgeoyeob/laidoff-art/"
#else
#define DATA_ROOT "c:\\laidoff-art\\"
#endif

int main(int argc, char **argv) {
    //read_png_file(DATA_ROOT "water_land_20k.png");
    //read_png_file(DATA_ROOT "water_16k.png");
    //read_png_file(DATA_ROOT "bw.png");
    //read_png_file(DATA_ROOT "dissection_1.png");
    //read_png_file(DATA_ROOT "dissection_2.png");
    //read_png_file(DATA_ROOT "dissection_3.png");
    //read_png_file(DATA_ROOT "dissection_4.png");
    //read_png_file(DATA_ROOT "dissection_5.png");
    //read_png_file(DATA_ROOT "dissection_6.png");
    read_png_file(DATA_ROOT "dissection_7.png");
    count_total_inverts();
    detect_concave_vertices();
    get_lines(hori_lines, row_key_convex_concaves);
    get_lines(vert_lines, col_key_convex_concaves);
    maximum_matching();
    //test_bipartite();
    //max_match_test();
    return 0;
}
