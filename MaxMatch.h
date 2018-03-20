// Copyright Erik Hvatum 2012
// License: GPL v2

#pragma once

template<typename NameType = std::string>
class MaxMatch
{
public:
    public:
    typedef std::ptrdiff_t VertexIndex;
    typedef std::ptrdiff_t EdgeIndex;
    typedef std::ptrdiff_t Layer;

    struct Edge
    {
        Edge();
        Edge(const VertexIndex& u_vertex_, const VertexIndex& v_vertex_, const EdgeIndex& idx_);
        VertexIndex u_vertex, v_vertex;
        // Index of this edge in m_edges
        EdgeIndex idx;
    };

    typedef std::vector<Edge> Edges;
    typedef std::vector<EdgeIndex> EdgeIndexes;

    enum VertexType
    {
        U_Vertex,
        V_Vertex
    };

    struct Vertex
    {
        Vertex();
        Vertex(const VertexType& type_, const NameType& name_, const VertexIndex& idx_);
        VertexType type;
        NameType name;
        // Index of this vertex in m_vertexes
        VertexIndex idx;
        EdgeIndexes edges;
    };

    enum
    {
        NillVertIdx = 0
    };

    typedef std::vector<Vertex> Vertexes;
    typedef std::vector<VertexIndex> VertexIndexes;

    typedef std::vector<Layer> Layers;

    MaxMatch();
    const Vertexes& u_vertexes() const {
        return m_u_vertexes;
    }
    const Vertexes& v_vertexes() const {
        return m_v_vertexes;
    }
    const Edges& edges() const;
    void addVertex(const VertexType& type, const NameType& name);
    void addEdge(const NameType& u_vertexName, const NameType& v_vertexName);
    const VertexIndexes& us_to_vs() const;
    const VertexIndexes& vs_to_us() const;
    // Uses the Hopcroft-Karp algorithm to find a maximal matching set; returns the number of edges in the matching set
    // found.
    int hopcroftKarp();
    // Helper for hopcroftKarp - uses BFS to put U vertexes in layers (layer 0 for free U vertexes, layer 1 for the U
    // vertexes matched to V vertexes with edges incident to a layer 0 U vertex, etc).  Returns true if at least one
    // potential augmenting path exists.
    bool makeLayers();
    // Helper for hopcroftKarp - uses DFS guided by layer number to find augmenting paths.  Returns true if an
    // augmenting path from uIdx was found.
    bool findPath(const VertexIndex& uIdx);
    

protected:
    typedef std::map<NameType, VertexIndex> VertexNamesToIndexes;

    static const Layer InfLayer;

    Vertexes m_u_vertexes;
    Vertexes m_v_vertexes;
    VertexNamesToIndexes m_u_vertNamesToIdxs;
    VertexNamesToIndexes m_v_vertNamesToIdxs;
    Edges m_edges;
    // u -> v matchings.  For example, m_us_to_vs[2] == 3 indicates that vertex m_u_vertexes[2] is matched to
    // m_v_vertexes[3]
    VertexIndexes m_us_to_vs;
    // v -> u matchings.
    VertexIndexes m_vs_to_us;
    // u vertex layer values.  m_layers[2] == 1 indicates that m_u_vertexes[2] has been placed in layer 1 by the most
    // recent call to makeLayers().
    Layers m_layers;
}; 

template<typename NameType>
MaxMatch<NameType>::Vertex::Vertex(const VertexType& type_, const NameType& name_, const VertexIndex& idx_)
    : type(type_),
    name(name_),
    idx(idx_) {
}

template<typename NameType>
void MaxMatch<NameType>::addVertex(const VertexType& type, const NameType& name) {
    VertexNamesToIndexes& namesToIdxs(type == U_Vertex ? m_u_vertNamesToIdxs : m_v_vertNamesToIdxs);
    Vertexes& vertexes(type == U_Vertex ? m_u_vertexes : m_v_vertexes);

    VertexNamesToIndexes::iterator nameToIdx(namesToIdxs.find(name));
    if (nameToIdx != namesToIdxs.end()) {
        char partname(type == U_Vertex ? 'u' : 'v');
        throw std::string("MaxMatch::addVertex(const VertexType& type, const string& name): A ") + partname +
            " vertex already exists with the specified name.";
    }

    VertexIndex idx(vertexes.size());
    vertexes.push_back(Vertex(type, name, idx));
    namesToIdxs[name] = idx;
}

template<typename NameType>
void MaxMatch<NameType>::addEdge(const NameType& u_vertexName, const NameType& v_vertexName) {
    VertexNamesToIndexes::iterator nameToIdx(m_u_vertNamesToIdxs.find(u_vertexName));
    if (nameToIdx == m_u_vertNamesToIdxs.end()) {
        throw std::string("MaxMatch::addEdge(const string& u_vertexName, const string& v_vertexName): "
                          "no u vertex with u_vertexName exists.");
    }
    VertexIndex uIdx(nameToIdx->second);
    Vertex& u(m_u_vertexes[uIdx]);

    nameToIdx = m_v_vertNamesToIdxs.find(v_vertexName);
    if (nameToIdx == m_v_vertNamesToIdxs.end()) {
        throw std::string("MaxMatch::addEdge(const string& u_vertexName, const string& v_vertexName): "
                          "no v vertex with v_vertexName exists.");
    }
    VertexIndex vIdx(nameToIdx->second);
    Vertex& v(m_v_vertexes[vIdx]);

    EdgeIndex edgeIdx(m_edges.size());
    m_edges.push_back(Edge(uIdx, vIdx, edgeIdx));
    u.edges.push_back(edgeIdx);
    v.edges.push_back(edgeIdx);
}


template<typename NameType>
int MaxMatch<NameType>::hopcroftKarp() {
    int matches(0);

    m_layers.resize(m_u_vertexes.size());

    m_us_to_vs.resize(m_u_vertexes.size());
    std::fill(m_us_to_vs.begin(), m_us_to_vs.end(), NillVertIdx);

    m_vs_to_us.resize(m_v_vertexes.size());
    std::fill(m_vs_to_us.begin(), m_vs_to_us.end(), NillVertIdx);

    VertexIndex uIdx, uIdxEnd(m_u_vertexes.size());
    while (makeLayers()) {
        for (uIdx = 1; uIdx < uIdxEnd; ++uIdx) {
            if (m_us_to_vs[uIdx] == NillVertIdx) {
                if (findPath(uIdx)) {
                    ++matches;
                }
            }
        }
    }

    return matches;
}


template<typename NameType>
bool MaxMatch<NameType>::makeLayers() {
    std::list<VertexIndex> searchQ;
    VertexIndex uIdx, uIdxEnd(m_u_vertexes.size()), nextUIdx, vIdx;
    // Put all free u vertexes in layer 0 and queue them for searching
    for (uIdx = 1; uIdx < uIdxEnd; ++uIdx) {
        if (m_us_to_vs[uIdx] == NillVertIdx) {
            m_layers[uIdx] = 0;
            searchQ.push_front(uIdx);
        } else {
            m_layers[uIdx] = InfLayer;
        }
    }
    m_layers[NillVertIdx] = InfLayer;
    EdgeIndexes::const_iterator edgeIdx;
    while (!searchQ.empty()) {
        uIdx = searchQ.back();
        searchQ.pop_back();
        EdgeIndexes& edges(m_u_vertexes[uIdx].edges);
        for (edgeIdx = edges.cbegin(); edgeIdx != edges.cend(); ++edgeIdx) {
            vIdx = m_edges[*edgeIdx].v_vertex;
            nextUIdx = m_vs_to_us[vIdx];
            Layer& nextULayer(m_layers[nextUIdx]);
            if (nextULayer == InfLayer) {
                nextULayer = m_layers[uIdx] + 1;
                searchQ.push_front(nextUIdx);
            }
        }
    }
    // If an augmenting path exists, m_layers[NillVertexIdx] represents the depth of the findPath DFS when the u vertex
    // at the end of an augmenting path is found.  Otherwise, this value is InfLayer.
    return m_layers[NillVertIdx] != InfLayer;
}

template<typename NameType>
bool MaxMatch<NameType>::findPath(const VertexIndex& uIdx) {
    if (uIdx == NillVertIdx) {
        return true;
    } else {
        VertexIndex vIdx;
        Layer nextULayer(m_layers[uIdx] + 1);
        EdgeIndexes& edges(m_u_vertexes[uIdx].edges);
        for (EdgeIndexes::const_iterator edgeIdx(edges.begin()); edgeIdx != edges.end(); ++edgeIdx) {
            vIdx = m_edges[*edgeIdx].v_vertex;
            VertexIndex& nextUIdx(m_vs_to_us[vIdx]);
            if (m_layers[nextUIdx] == nextULayer) {
                if (findPath(nextUIdx)) {
                    // This edge belongs to an augmenting path - add it to the matching set
                    nextUIdx = uIdx;
                    m_us_to_vs[uIdx] = vIdx;
                    return true;
                }
            }
        }
        // m_u_vertexes[uIdx] does not belong to an augmenting path and need not be searched further during the DFS
        // phase
        m_layers[uIdx] = InfLayer;
        return false;
    }
}

template<typename NameType>
const MaxMatch<>::VertexIndexes& MaxMatch<NameType>::vs_to_us() const {
    return m_vs_to_us;
}

template<typename NameType>
const MaxMatch<>::VertexIndexes& MaxMatch<NameType>::us_to_vs() const {
    return m_us_to_vs;
}

template<typename NameType>
MaxMatch<NameType>::Edge::Edge()
    : u_vertex(std::numeric_limits<VertexIndex>::min()),
    v_vertex(std::numeric_limits<VertexIndex>::min()),
    idx(std::numeric_limits<EdgeIndex>::min()) {
}

template<typename NameType>
MaxMatch<NameType>::Edge::Edge(const VertexIndex& u_vertex_, const VertexIndex& v_vertex_, const EdgeIndex& idx_)
    : u_vertex(u_vertex_),
    v_vertex(v_vertex_),
    idx(idx_) {
}

template<typename NameType>
MaxMatch<NameType>::Vertex::Vertex()
    : idx(std::numeric_limits<VertexIndex>::min()) {
}
