/****************************************************************************
 * Copyright 2009 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _INDEXED_TRIANGLE_ARRAY_H
#define _INDEXED_TRIANGLE_ARRAY_H

#include <iostream>
#include <cmath>
#include <list>
#include <string>
#include <boost/foreach.hpp>
#include <Moby/sorted_pair>
#include <Moby/Types.h>
#include <Moby/Triangle.h>
#include <Moby/InvalidIndexException.h>
#include <Moby/IndexedTri.h>

namespace Moby {

/// An array of triangles indexed into shared vertices 
class IndexedTriArray
{
  public:
    IndexedTriArray() {}
    IndexedTriArray(boost::shared_ptr<const std::vector<Vector3> > vertices, const std::vector<IndexedTri>& facets);
    IndexedTriArray(boost::shared_ptr<const std::vector<Vector3> > vertices, boost::shared_ptr<const std::vector<IndexedTri> > facets);

    template <class InputIterator1, class InputIterator2>
    IndexedTriArray(InputIterator1 vertices, InputIterator1 verts_end, InputIterator2 facets_begin, InputIterator2 facets_end);

    template <class OutputIterator>
    OutputIterator get_tris(OutputIterator output_begin) const; 

    template <class OutputIterator>
    static OutputIterator intersect(const IndexedTriArray& mesh_a, const IndexedTriArray& mesh_b, OutputIterator output_begin, bool exit_early);

    unsigned num_tris() const { return (_facets) ? _facets->size() : 0; }
    Triangle get_triangle(unsigned i) const;
    IndexedTriArray transform(const Matrix4& T) const;
    IndexedTriArray rotate_scale(const Matrix3& T) const;
    IndexedTriArray translate(const Vector3& v) const;
    IndexedTriArray compress_vertices() const;
    static IndexedTriArray read_from_obj(const std::string& filename);
    static void write_to_obj(const IndexedTriArray& mesh, const std::string& filename);
    void write_to_obj(const std::string& filename) const { write_to_obj(*this, filename); }
    static IndexedTriArray merge(const IndexedTriArray& mesh1, const IndexedTriArray& mesh2, Real equal_tol = 0.0);
    IndexedTriArray& operator=(const IndexedTriArray& mesh);
    std::vector<std::list<unsigned> > determine_vertex_edge_map() const;
    std::vector<std::list<unsigned> > determine_vertex_facet_map() const;
    std::map<sorted_pair<unsigned>, std::list<unsigned> > determine_edge_facet_map() const;
    void calc_volume_ints(Real volume_ints[10]) const;

    /// Gets the indices of facets incident to a vertex
    const std::list<unsigned>& get_incident_facets(unsigned i) const { if (i >= _vertices->size()) throw InvalidIndexException(); return (*_incident_facets)[i]; }

    /// Gets the pointer to the vector of facets
    boost::shared_ptr<const std::vector<IndexedTri> > get_facets_pointer() const { return _facets; }

    /// Gets the pointer to the vector of vertices
    boost::shared_ptr<const std::vector<Vector3> > get_vertices_pointer() const { return _vertices; }

    /// Gets the vector of facets
    const std::vector<IndexedTri>& get_facets() const { return *_facets; }

    /// Gets the vector of verties
    const std::vector<Vector3>& get_vertices() const { return *_vertices; }

    /// Determines whether a vertex is coplanar (all faces touching the vertex are coplanar)
    bool is_coplanar(unsigned vidx) const { return std::binary_search(_coplanar_verts.begin(), _coplanar_verts.end(), vidx); }

   /// Determines whether an edge (v1,v2) is coplanar (all faces touching the edge are coplanar)
   bool is_coplanar(unsigned v1, unsigned v2) const { return std::binary_search(_coplanar_edges.begin(), _coplanar_edges.end(), make_sorted_pair(v1, v2)); }

  private:
    void determine_coplanar_features();
    static bool query_intersect_tri_tri(const Triangle& t1, const Triangle& t2);
    void validate() const;
    void calc_incident_facets();

    /// Sorted vector of coplanar edges (all facets touching each edge are coplanar)
    std::vector<sorted_pair<unsigned> > _coplanar_edges;

    /// Sorted vector of coplanar vertices (all faces touching each vertex are coplanar)
    std::vector<unsigned> _coplanar_verts;

    boost::shared_ptr<const std::vector<IndexedTri> > _facets;
    boost::shared_ptr<const std::vector<Vector3> > _vertices;
    boost::shared_ptr<const std::vector<std::list<unsigned > > > _incident_facets;
}; // end class

// include inline methods
#include "IndexedTriArray.inl"

} // end namespace 

#endif

