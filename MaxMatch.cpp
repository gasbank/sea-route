// Copyright Erik Hvatum 2012
// License: GPL v2
#include "precompiled.hpp"
#include "MaxMatch.h"

const MaxMatch<>::Layer MaxMatch<>::InfLayer(std::numeric_limits<MaxMatch<>::Layer>::max());
const MaxMatch<int>::Layer MaxMatch<int>::InfLayer(std::numeric_limits<MaxMatch<int>::Layer>::max());



MaxMatch<std::string>::MaxMatch()
{
    addVertex(U_Vertex, "NILL U Vertex");
    addVertex(V_Vertex, "NILL V Vertex");
}

MaxMatch<int>::MaxMatch() {
    addVertex(U_Vertex, 0);
    addVertex(V_Vertex, 0);
}
