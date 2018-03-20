#include "precompiled.hpp"
#include "MaxMatch.h"

struct xy {
    int x : 16;
    int y : 16;
};
struct xyxy {
    xy xy0;
    xy xy1;
};
typedef std::unordered_map<int, std::vector<xy>> int_xyvector_map;
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

int_xyvector_map row_key_concaves;
int_xyvector_map col_key_concaves;
xyxyvector hori_lines;
xyxyvector vert_lines;
MaxMatchInt bipartite;

void read_png_file(char* file_name) {
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
            int b = ((row[x / 8] >> (7 - (x % 8))) & 1) ? 1 : 0;
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
    for (y = 0; y<height-1; y++) {
        png_byte* row0 = row_pointers[y + 0];
        png_byte* row1 = row_pointers[y + 1];
        for (x = 0; x<width-1; x++) {
            char b00 = ((row0[(x + 0) / 8] >> (7 - ((x + 0) % 8))) & 1) ? 1 : 0;
            char b01 = ((row0[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) ? 1 : 0;
            char b10 = ((row1[(x + 0) / 8] >> (7 - ((x + 0) % 8))) & 1) ? 1 : 0;
            char b11 = ((row1[(x + 1) / 8] >> (7 - ((x + 1) % 8))) & 1) ? 1 : 0;
            char bcc = (b11 << 3) | (b10 << 2) | (b01 << 1) | b00;
            if (bcc == 0b0111 || bcc == 0b1011 || bcc == 0b1101 || bcc == 0b1110) {
                if (verbose) {
                    printf("Concave vertex at row %d col %d\n", y, x);
                }
                xy xycombined;
                xycombined.x = x;
                xycombined.y = y;
                row_key_concaves[y].push_back(xycombined);
                col_key_concaves[x].push_back(xycombined);
                total_concave_vertices_count++;
            }
        }
    }
    printf("Total concave vertices: %d\n", total_concave_vertices_count);
}

void get_lines(xyxyvector& lines, const int_xyvector_map& concaves) {
    
    int verbose = height <= 64 && width <= 64;

    for (auto cit = concaves.cbegin(); cit != concaves.cend(); ++cit) {
        for (auto cit2 = cit->second.cbegin(); cit2 != cit->second.cend(); ++cit2) {
            // pull two elements at once
            xyxy line;
            line.xy0 = *cit2;
            ++cit2;
            if (cit2 != cit->second.cend()) {
                line.xy1 = *cit2;
                lines.push_back(line);
                printf("Line (%d,%d)-(%d,%d) added.\n",
                       line.xy0.y, line.xy0.x, line.xy1.y, line.xy1.x);
            } else {
                break;
            }
        }
    }
}

using namespace boost;

/// Example to test for bipartiteness and print the certificates.

template <typename Graph>
void print_bipartite(const Graph& g) {
    typedef graph_traits <Graph> traits;
    typename traits::vertex_iterator vertex_iter, vertex_end;

    /// Most simple interface just tests for bipartiteness. 

    bool bipartite = is_bipartite(g);

    if (bipartite) {
        typedef std::vector <default_color_type> partition_t;
        typedef vec_adj_list_vertex_id_map <no_property, unsigned int> index_map_t;
        typedef iterator_property_map <partition_t::iterator, index_map_t> partition_map_t;

        partition_t partition(num_vertices(g));
        partition_map_t partition_map(partition.begin(), get(vertex_index, g));

        /// A second interface yields a bipartition in a color map, if the graph is bipartite.

        is_bipartite(g, get(vertex_index, g), partition_map);

        for (boost::tie(vertex_iter, vertex_end) = vertices(g); vertex_iter != vertex_end; ++vertex_iter) {
            std::cout << "Vertex " << *vertex_iter << " has color " << (get(partition_map, *vertex_iter) == color_traits <
                                                                        default_color_type>::white() ? "white" : "black") << std::endl;
        }
    } else {
        typedef std::vector <typename traits::vertex_descriptor> vertex_vector_t;
        vertex_vector_t odd_cycle;

        /// A third interface yields an oddcycle if the graph is not bipartite.

        find_odd_cycle(g, get(vertex_index, g), std::back_inserter(odd_cycle));

        std::cout << "Odd cycle consists of the vertices:";
        for (size_t i = 0; i < odd_cycle.size(); ++i) {
            std::cout << " " << odd_cycle[i];
        }
        std::cout << std::endl;
    }
}

void test_bipartite() {
    typedef adjacency_list <vecS, vecS, undirectedS> vector_graph_t;
    typedef std::pair <int, int> E;

    /**
    * Create the graph drawn below.
    *
    *       0 - 1 - 2
    *       |       |
    *   3 - 4 - 5 - 6
    *  /      \   /
    *  |        7
    *  |        |
    *  8 - 9 - 10
    **/

    E bipartite_edges[] = { E(0, 1), E(0, 4), E(1, 2), E(2, 6), E(3, 4), E(3, 8), E(4, 5), E(4, 7), E(5, 6), E(
        6, 7), E(7, 10), E(8, 9), E(9, 10) };
    vector_graph_t bipartite_vector_graph(&bipartite_edges[0],
                                          &bipartite_edges[0] + sizeof(bipartite_edges) / sizeof(E), 11);

    /**
    * Create the graph drawn below.
    *
    *       2 - 1 - 0
    *       |       |
    *   3 - 6 - 5 - 4
    *  /      \   /
    *  |        7
    *  |       /
    *  8 ---- 9
    *
    **/

    E non_bipartite_edges[] = { E(0, 1), E(0, 4), E(1, 2), E(2, 6), E(3, 6), E(3, 8), E(4, 5), E(4, 7), E(5, 6),
        E(6, 7), E(7, 9), E(8, 9) };
    vector_graph_t non_bipartite_vector_graph(&non_bipartite_edges[0], &non_bipartite_edges[0]
                                              + sizeof(non_bipartite_edges) / sizeof(E), 10);

    /// Call test routine for a bipartite and a non-bipartite graph.

    print_bipartite(bipartite_vector_graph);

    print_bipartite(non_bipartite_vector_graph);

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
    int i = 1;
    for (const auto& it : hori_lines) {
        bipartite.addVertex(bipartite.U_Vertex, i);
        printf("U vertex (hori) #%d [(%d,%d)-(%d,%d)]\n", i, it.xy0.y, it.xy0.x, it.xy1.y, it.xy1.x);
        i++;
    }
    int j = 1;
    for (const auto& it : vert_lines) {
        bipartite.addVertex(bipartite.V_Vertex, j);
        printf("V vertex (vert) #%d [(%d,%d)-(%d,%d)]\n", j, it.xy0.y, it.xy0.x, it.xy1.y, it.xy1.x);
        j++;
    }
    
    int verbose = height <= 64 && width <= 64;

    i = 1;
    for (const auto& it : hori_lines) {
        j = 1;
        for (const auto& it2 : vert_lines) {
            if (line_cross(it, it2)) {
                bipartite.addEdge(i, j);
                printf("HORI #%d [(%d,%d)-(%d,%d)] and VERT #%d [(%d,%d)-(%d,%d)] crossed.\n",
                       i, it.xy0.y, it.xy0.x, it.xy1.y, it.xy1.x,
                       j, it2.xy0.y, it2.xy0.x, it2.xy1.y, it2.xy1.x);
            }
            j++;
        }
        i++;
    }
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
}

int main(int argc, char **argv) {
    //read_png_file("c:\\laidoff-art\\water_land_20k.png");
    //read_png_file("c:\\laidoff-art\\water_16k.png");
    //read_png_file("c:\\laidoff-art\\bw.png");
    //read_png_file("c:\\laidoff-art\\dissection_1.png");
    //read_png_file("c:\\laidoff-art\\dissection_2.png");
    //read_png_file("c:\\laidoff-art\\dissection_3.png");
    read_png_file("c:\\laidoff-art\\dissection_4.png");
    count_total_inverts();
    detect_concave_vertices();
    get_lines(hori_lines, row_key_concaves);
    get_lines(vert_lines, col_key_concaves);
    maximum_matching();
    //test_bipartite();
    //max_match_test();
    return 0;
}
