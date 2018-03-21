// Copyright Erik Hvatum 2012
// License: GPL v2
#include "precompiled.hpp"
#include "MaxMatch.h"

template<>
const MaxMatch<>::Layer MaxMatch<>::InfLayer(std::numeric_limits<MaxMatch<>::Layer>::max());
template<>
const MaxMatch<int>::Layer MaxMatch<int>::InfLayer(std::numeric_limits<MaxMatch<int>::Layer>::max());


template<>
MaxMatch<std::string>::MaxMatch() {
    addVertex(U_Vertex, "NILL U Vertex");
    addVertex(V_Vertex, "NILL V Vertex");
}

template<>
MaxMatch<int>::MaxMatch() {
    addVertex(U_Vertex, 0);
    addVertex(V_Vertex, 0);
}


//template<>
//void MaxMatch<int>::addEdge(const int& u_vertexName, const int& v_vertexName) {
//    VertexIndex uIdx(u_vertexName);
//    Vertex& u(m_u_vertexes[uIdx]);
//    
//    VertexIndex vIdx(v_vertexName);
//    Vertex& v(m_v_vertexes[vIdx]);
//    
//    EdgeIndex edgeIdx(m_edges.size());
//    m_edges.push_back(Edge(uIdx, vIdx, edgeIdx));
//    u.edges.push_back(edgeIdx);
//    u.edgeMap[vIdx] = edgeIdx;
//    v.edges.push_back(edgeIdx);
//}

