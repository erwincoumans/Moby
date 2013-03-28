/****************************************************************************
 * Copyright 2006 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

/// Utility method for triangulate_polygon_2D()
template <class BidirectionalIterator>
bool CompGeom::diagonalie(BidirectionalIterator a, BidirectionalIterator b, BidirectionalIterator begin, BidirectionalIterator end, Real tol)
{
  BidirectionalIterator c, c1;
  
  c = begin;
  do
  {
    c1 = c;
    c1++;

    // skip edges incident to a or b
    if ((c != a) && (c1 != a) && (c != b) && (c1 != b) &&
        intersect(**a, **b, **c, **c1, tol))
      return false;

    c++;
  }
  while (c != end);

  return true;
}

/// Utility method for triangulate_polygon_2D()
template <class BidirectionalIterator>
bool CompGeom::diagonal(BidirectionalIterator a, BidirectionalIterator b, BidirectionalIterator begin, BidirectionalIterator end, Real tol)
{
  return in_cone(a, b, begin, end, tol) && 
          in_cone(b, a, begin, end, tol) && 
          diagonalie(a, b, begin, end, tol);
}

/// Utility method for triangulate_polygon_2D()
template <class BidirectionalIterator>
bool CompGeom::in_cone(BidirectionalIterator a, BidirectionalIterator b, BidirectionalIterator begin, BidirectionalIterator end, Real tol)
{
  // get the vertices before and after a
  BidirectionalIterator a0 = (a == begin) ? end : a;
  BidirectionalIterator a1 = a;

  a0--;
  a1++;
  if (a1 == end)
    a1 = begin;

  if (area_sign(**a, **a1, **a0, tol) != eRight)
    return area_sign(**a, **b, **a0, tol) == eLeft && area_sign(**b, **a, **a1, tol) == eLeft;
  else
    return !(area_sign(**a, **b, **a1, tol) != eRight && area_sign(**b, **a, **a0, tol) == eRight);
}

/// Triangulates a polygon (in 2D)
/**
 * \param begin a bidirectional iterator to a container of Vector2 objects
 * \param end a bidirectional iterator to a container of Vector2 objects
 * \param outbegin an iterator to a container of type 
 *         std::pair<const Vector2*, const Vector2*>; output will be written here
 * \param tol tolerance to test for zero
 * \return an iterator to the end of the output
 * \todo re-implement using faster O(n lg n) algorithm (currently runs in O(n^2) time)
 */
template <class BidirectionalIterator, class OutputIterator>
OutputIterator CompGeom::triangulate_polygon_2D(BidirectionalIterator begin, BidirectionalIterator end, OutputIterator outbegin, Real tol)
{
  // make a list out of the polygon; simultaneously get # of vertices
  unsigned n = 0;
  std::list<const Vector2*> poly;
  for (BidirectionalIterator v = begin; v != end; v++, n++)
    poly.push_back(&*v);

  // initialize ear for all vertices
  std::map<const Vector2*, bool> ear;
  for (std::list<const Vector2*>::iterator v1 = poly.begin(); v1 != poly.end(); v1++)
  {
    // get the next vertex
    std::list<const Vector2*>::iterator v2 = v1;
    if (++v2 == poly.end())
      v2 = poly.begin();

    // get the previous vertex
    std::list<const Vector2*>::iterator v0 = (v1 == poly.begin()) ? poly.end() : v1;
    v0--;

    // set the ear
    ear[*v1] = diagonal(v0, v2, poly.begin(), poly.end(), tol);
  }

  // get the number of vertices
  while (n > 3)
  {
    // inner loop searches for an ear
    std::list<const Vector2*>::iterator v2 = poly.begin();
    do
    {
      if (ear[*v2])
      {
        // ear found.  get the next and previous vertices
        std::list<const Vector2*>::iterator v3 = v2;
        std::list<const Vector2*>::iterator v1 = (v2 == poly.begin()) ? poly.end() : v2;
        v1--;
        v3++;
        if (v3 == poly.end())
          v3 = poly.begin();

        // get next and previous vertices v3 and v1, respectively
        std::list<const Vector2*>::iterator v4 = v3;
        std::list<const Vector2*>::iterator v0 = (v1 == poly.begin()) ? poly.end() : v1;
        v0--;
        v4++;
        if (v4 == poly.end())
          v4 = poly.begin();

        // add the diagonal
        *outbegin++ = std::make_pair(*v1, *v3);

        // update earity of diagonal endpoints
        ear[*v1] = diagonal(v0, v3, poly.begin(), poly.end(), tol);
        ear[*v3] = diagonal(v1, v4, poly.begin(), poly.end(), tol);

        // cut off v2
        poly.erase(v2);
        n--;
        break;
      }
      v2++;

    }
    while (v2 != poly.end());
  }

  return outbegin;
}

/// Computes the centroid of a set of facets
/**
 * The facets may represent a polygon, a polyhedron, or even an open polyhedron.  However, the facets may not intersect.
 */
template <class InputIterator>
Vector3 CompGeom::calc_centroid_3D(InputIterator first, InputIterator last)
{
  Real area_sum = 0;
  
  // compute the area of each facet and contribution from each facet
  Vector3 centroid = ZEROS_3;
  for (InputIterator i = first; i != last; i++)
  {
    Real area = i->calc_area();
    area_sum += area;
    centroid += area * (i->a + i->b + i->c);
  }
  
  // divide the centroid by 3*area_sum
  centroid /= (area_sum * 3);
  
  return centroid;
}

/// Determines the dimensionality of a set of points (Vector3 objects in a STL collection)
/**
 * \return the dimensionality (0 [point], 1 [line], 2 [plane], 3 [full space])
 */
template <class InputIterator>
unsigned CompGeom::calc_dimensionality(InputIterator first, InputIterator last, Real tol)
{
  assert(tol >= 0.0);

  // make sure we can handle the case of no points
  if (first == last)
    return 0;

  // determine whether all of the points are equal (0 dimensionality)
  InputIterator j = first;
  for (InputIterator i = first; ; i++)
  {
    // if there are no more points left, everything up to this point
    // has been approximately equal
    j++;
    if (j == last)
      return 0;      

    // if the points are not equal, we can go ahead and break out
    if ((*i - *j).norm() > tol)
      break;
  }  

  // determine whether all of the points are colinear (1 dimensionality)
  // all points from first .. j-1 are coincident, we don't need to check those...
  InputIterator k = j;
  while (true)
  {
    // if there are no more points left, everything up to this point has been
    // colinear
    k++;
    if (k == last)
      return 1;

    // if the points are not collinear, we can go ahead and break out
    if (!CompGeom::collinear(*first, *j, *k, tol))
      break;
  }
  
  // determine whether all of the points are coplanar (2 dimensionality)
  // points first, j, k are not colinear, so these will be the basis for our plane
  Vector3 v1 = *j - *first;
  Vector3 v2 = *k - *j;
  Vector3 n = Vector3::normalize(Vector3::cross(v1, v2));
  Real d = Vector3::dot(n, v1);  
  InputIterator i = k;
  while (true)
  {
    // if there are no more points left, everything up to this point has been coplanar
    i++;
    if (i == last)
      return 2;

    // if the points are not coplanar, we can go ahead and break out
    if (std::fabs(Vector3::dot(n, *i) - d) > tol)
      break;
  }

  // still here?  full dimensionality
  return 3;
}

/// Computes the 3D convex hull of a set of points
/**
 * \param first a forward iterator for type Vector3
 * \param last a forward iterator for type Vector3
 * \return a pointer to the newly created polyhedron
 */
template <class InputIterator>
PolyhedronPtr CompGeom::calc_convex_hull_3D(InputIterator first, InputIterator last)
{
  const unsigned X = 0, Y = 1, Z = 2;
  int exit_code;
  int curlong, totlong;
  FILE* outfile, * errfile;

  // setup qhull outputs
  if (LOGGING(LOG_COMPGEOM))
  {
    outfile=stdout;  
    errfile=stderr;
  }
  else
  {
    outfile=NULL;
    errfile=fopen("/dev/null", "w");
    assert(errfile);
  } 

  // determine how many points we are processing
  unsigned sz = 0;
  for (InputIterator i = first; i != last; i++)
    sz++;

  // setup constants
  const int DIM = 3;
  const int N_POINTS = sz;
  const boolT IS_MALLOC = false;
  char flags[] = "qhull ";

  // make sure there are enough points
  if (N_POINTS < 4)
  {
    if (!LOGGING(LOG_COMPGEOM))
      fclose(errfile);

    return PolyhedronPtr();
  }
  
  // setup the points
  boost::shared_array<coordT> qhull_points(new coordT[N_POINTS*DIM]);
  unsigned j=0;
  for (InputIterator i = first; i != last; i++)
  {
    qhull_points[j] = (*i)[X];
    qhull_points[j+1] = (*i)[Y];
    qhull_points[j+2] = (*i)[Z];
    j += DIM;
  }
  
  FILE_LOG(LOG_COMPGEOM) << "computing 3D convex hull of: " << std::endl;
  for (InputIterator i = first; i != last; i++)
    FILE_LOG(LOG_COMPGEOM) << *i << std::endl;

  // lock the qhull mutex -- qhull is non-reentrant
  #ifdef THREADSAFE
  pthread_mutex_lock(&_qhull_mutex);
  #endif  

  // execute qhull  
  exit_code = qh_new_qhull(DIM, N_POINTS, qhull_points.get(), IS_MALLOC, flags, outfile, errfile);
  if (exit_code)
  {
    // free qhull memory
    qh_freeqhull(!qh_ALL);
    qh_memfreeshort(&curlong, &totlong);

    // qhull failed -- perhaps the dimensionality is 2 rather than 3?
    #ifdef THREADSAFE
    pthread_mutex_unlock(&_qhull_mutex);
    #endif  

    // close the error stream, if necessary
    if (!LOGGING(LOG_COMPGEOM))
      fclose(errfile);

    return PolyhedronPtr();
  }

  // construct a new vector of vertices
  std::vector<Vector3> vertices; 

  // get all of the vertices
  std::map<vertexT*, unsigned> vertex_map;
  for (vertexT* vertex=qh vertex_list;vertex && vertex->next;vertex= vertex->next)
  {
    Vector3 v;
    for (unsigned i=0; i< (unsigned) DIM; i++)
      v[i] = vertex->point[i];
    vertices.push_back(v);
    vertex_map[vertex] = vertices.size()-1;
  }

  // triangulate
  qh_triangulate();
 
  // setup list of facets
  std::list<IndexedTri> facets;
 
  // get the facet information
  for (facetT* facet=qh facet_list;facet && facet->next;facet=facet->next)
  {
    if (!facet->vertices)
      continue;

    // setup a facet
    facets.push_back(IndexedTri());
    
    // get all (three) vertices in the facet
    vertexT** vertex_pointer = (vertexT**)& ((facet->vertices)->e[0].p); 
    vertexT* vertex = *vertex_pointer++;
    assert(vertex);
    facets.back().a = vertex_map[vertex];
    vertex = *vertex_pointer++;
    assert(vertex);
    facets.back().b = vertex_map[vertex];
    vertex = *vertex_pointer++;
    assert(vertex);
    facets.back().c = vertex_map[vertex];

    // must be no more than three vertices in the facet
    assert(!*vertex_pointer);

    // reverse order of the vertices if necessary 
    if (facet->toporient ^ qh_ORIENTclock)
      std::swap(facets.back().b, facets.back().c);
  }
  
  // free qhull memory
  qh_freeqhull(!qh_ALL);
  qh_memfreeshort(&curlong, &totlong);
  assert(!curlong && !totlong);
  
  // release the qhull mutex
  #ifdef THREADSAFE
  pthread_mutex_unlock(&_qhull_mutex);
  #endif
  
  // if the there aren't enough triangles, can't create the polyhedron
  assert(facets.size() >= 4);

  // create the polyhedron and verify that it is consistent
  PolyhedronPtr polyhedron(new Polyhedron(vertices.begin(), vertices.end(), facets.begin(), facets.end()));
  assert(polyhedron->consistent());

  FILE_LOG(LOG_COMPGEOM) << "3D convex hull is:" << std::endl << *polyhedron;

  // close the error stream, if necessary
  if (!LOGGING(LOG_COMPGEOM))
    fclose(errfile);

  return polyhedron;  
}

/// Determines the endpoints for a container of collinear Vector2 or Vector3 objects
/**
 * \param begin iterator to beginning of container of type Vector2 or Vector3
 * \param end iterator to end of container of type Vector2 or Vector3
 * \param endpoints the two farthest points on the segment on return
 */
template <class InputIterator, class T>
void CompGeom::determine_seg_endpoints(InputIterator begin, InputIterator end, std::pair<T, T>& endpoints)
{
  // make sure that we have been given valid input
  assert(begin != end);

  // setup initial endpoints
  endpoints.first = *begin;
  endpoints.second = *begin;
  Real dist = 0; 

  for (InputIterator i = ++begin; i != end; i++)
  {
    // get distance from i to both current bounding points
    Real dist_e = (*i - endpoints.second).norm();
    Real dist_s = (*i - endpoints.first).norm();

    // see which distance would be greatest
    if (dist > dist_e)
    {
      // check for no change
      if (dist > dist_s)
        continue;
      else
      {
        dist = dist_s;
        endpoints.second = *i;
      }
    }
    else
    {
      if (dist_e > dist_s)
      {
        dist = dist_e;
        endpoints.first = *i;
      }
      else
      {
        dist = dist_s;
        endpoints.second = *i;
      }
    }
  }
}

/// Finds an interior point of a set of halfspaces using linear programming
/**
 * The method used to find the interior point is of order O(n), where n is the
 * number of halfspace constraints.
 * \param start an iterator to the start of a collection of halfspaces, each of type std::pair<Vector3, Real>, where the vector [nx ny nz] is the normal to the halfspace and the scalar is the offset 'd'; each halfspace will satisfy the equation nx*x + ny*y + nz*z <= d
 * \param end an iterator to the end of the collection
 * \param point contains the interior point on return if successful
 * \return the minimum distance from a halfspace of the interior point; if
 *         the point is negative, there is no interior point
 */
template <class InputIterator>
Real CompGeom::find_hs_interior_point(InputIterator start, InputIterator end, Vector3& point)
{
  assert(std::numeric_limits<Real>::has_infinity);
  const Real inf = std::numeric_limits<Real>::max();
  const unsigned D = 5;
  const unsigned N = distance(start, end);

  // setup the limits on the variables
  VectorN l(D), u(D);
  l[0] = -1.0;        u[0] = 1.0;
  l[1] = -1.0;        u[1] = 1.0;
  l[2] = -1.0;        u[2] = 1.0;
  l[3] = 0;            u[3] = inf;
  l[4] = 0;            u[4] = 1.0;

  // setup the optimization vector
  VectorN c = VectorN::zero(D);
  c[D-1] = 1.0;

  // setup b
  VectorN b = VectorN::zero(N);

  // setup A
  MatrixN A(N,D);
  unsigned i = 0;
  for (; start != end; start++, i++)
  {
    A(i,0) = start->first[0];
    A(i,1) = start->first[1];
    A(i,2) = start->first[2];
    A(i,3) = -start->second;
    A(i,4) = 1.0;
  }

  // do linear programming
  VectorN x;
  if (!Optimization::lp(A, b, c, l, u, x))
    return -1.0;

  // verify that x[3] is not zero
  if (x[3] <= std::numeric_limits<Real>::epsilon())
    return -1.0;

  // determine interior point
  point = Vector3(x[0]/x[3], x[1]/x[3], x[2]/x[3]);

  // return the distance
  return x[4]/x[3];
}

/// Computes the halfspace intersection, returning the result as a convex polyhedron
/**
 * \param start an iterator to the start of a collection of halfspaces, each of type std::pair<Vector3, Real>, where the vector [nx ny nz] is the normal to the halfspace and the scalar is the offset 'd'; each halfspace will satisfy the equation nx*x + ny*y + nz*z <= d
 * \param end an iterator to the end of the collection
 * \param interior_point a point interior to all of the halfspaces
 * \return a pointer to the created polyhedron or a NULL pointer if unsuccessful
 */
template <class InputIterator>
PolyhedronPtr CompGeom::calc_hs_intersection(InputIterator start, InputIterator end, const VectorN& interior_point)
{
  const int DIM = 4;
  const boolT IS_MALLOC = false;
  int curlong, totlong;
  const unsigned X = 0, Y = 1, Z = 2;
  FILE* outfile, * errfile;

  // setup qhull flags
  std::ostringstream flags;
  flags << "qhull H";
  flags << interior_point[X] << "," << interior_point[Y] << "," << interior_point[Z];

  // setup qhull outputs
  if (LOGGING(LOG_COMPGEOM))
  {
    outfile=stdout;  
    errfile=stderr;
  }
  else
  {
    outfile=NULL;
    errfile=fopen("/dev/null", "w");
    assert(errfile);
  } 

  FILE_LOG(LOG_COMPGEOM) << "computing halfspace intersection of: " << std::endl;
  for (InputIterator i = start; i != end; i++)
    FILE_LOG(LOG_COMPGEOM) << "  halfspace normal: " << i->first << "  d: " << i->second << std::endl;

  // allocate memory for halfspaces
  int nspaces = (int) distance(start, end);
  boost::shared_array<coordT> qhull_hs(new coordT[nspaces*DIM]);

  // setup halfspaces
  unsigned j=0;
  for (InputIterator i = start; i != end; i++)
  {
    qhull_hs[j] = i->first[X];
    qhull_hs[j+1] = i->first[Y];
    qhull_hs[j+2] = i->first[Z];
    qhull_hs[j+3] = -i->second;
    j += DIM;
  }

  // lock the qhull mutex -- qhull is non-reentrant
  #ifdef THREADSAFE
  pthread_mutex_lock(&_qhull_mutex);
  #endif

  // execute qhull
  int exit_code = qh_new_qhull(DIM, nspaces, qhull_hs.get(), IS_MALLOC, (char*) flags.str().c_str(), outfile, errfile);
  if (exit_code)
  {
    // free qhull memory
    qh_freeqhull(!qh_ALL);
    qh_memfreeshort(&curlong, &totlong);

    // qhull failed
    #ifdef THREADSAFE
    pthread_mutex_unlock(&_qhull_mutex);
    #endif

    // close the error stream, if necessary
    if (!LOGGING(LOG_COMPGEOM))
      fclose(errfile);

    return PolyhedronPtr();
  }

  // verify that the qhull dimension is correct
  assert(qh hull_dim == 3);

  // determine the intersection vertices; NOTE: this code was motivated by
  // qhull's qh_printafacet() and qh_printfacets() functions
  std::list<Vector3> points;
  for (facetT* facet=qh facet_list;facet && facet->next;facet=facet->next)
  {
    if (facet->offset > 0)
    {
      // facet has infinite offset
      return PolyhedronPtr();
    }
    coordT* point = (coordT*) qh_memalloc(qh normal_size);
    coordT* coordp = point;
    coordT* normp = facet->normal;
    coordT* feasiblep = qh feasible_point;
    if (facet->offset < -qh MINdenom)
      for (int k=qh hull_dim; k--; )
        *(coordp++) = (*(normp++) / -facet->offset) + *(feasiblep++);
    else
      for (int k=qh hull_dim; k--;)
      {
        boolT zerodiv;
        coordT* feasiblep1 = feasiblep+1;
        feasiblep = feasiblep1+1;
        *(coordp++) = qh_divzero(*(normp++), facet->offset, qh MINdenom_1, &zerodiv) + *feasiblep1  + *feasiblep;
        feasiblep++;
        if (zerodiv)
        {
          // facet has infinite offset
          qh_memfree(point, qh normal_size);
          return PolyhedronPtr();
        }
      }

    // add the point
    points.push_back(Vector3(*point, *(point+1), *(point+2)));

    // free the temporary memory
    qh_memfree(point, qh normal_size);
  }

  // free qhull memory
  qh_freeqhull(!qh_ALL);
  qh_memfreeshort(&curlong, &totlong);
  assert(!curlong && !totlong);
  
  // release the qhull mutex
  #ifdef THREADSAFE
  pthread_mutex_unlock(&_qhull_mutex);
  #endif

  // now, calculate the convex hull of the intersection points  
  PolyhedronPtr p = calc_convex_hull_3D(points.begin(), points.end());

  // close the error stream, if necessary
  if (!LOGGING(LOG_COMPGEOM))
    fclose(errfile);

  return p;
}

/// Computes the intersection of a polygon and a line segment
/**
 * \param begin iterator pointing to start of collection of Vector2 elements
 * \param end iterator pointing to end of collection of Vector2 elements
 * \param seg the line segment
 * \param outbegin an iterator to a container of type LineSeg2 that will 
 *        store line segments on/in the polygon on return 
 * \param tol the tolerance for parallel lines
 * \return ePolygonSegInside if the polygon 
 * \note polygon must be given in counter-clockwise order
 * \note first vertex should not appear twice
 */
template <class ForwardIterator, class OutputIterator>
OutputIterator CompGeom::intersect_seg_polygon(ForwardIterator begin, ForwardIterator end, const LineSeg2& seg, OutputIterator outbegin)
{
  std::list<Real> points;

  // determine whether one (or both) of the endpoints is within the polygon
  if (polygon_location(begin, end, seg.first) == ePolygonInside)
    points.push_back(0.0);
  if (polygon_location(begin, end, seg.second) == ePolygonInside)
    points.push_back(1.0);

  // determine the inverse of the length (squared) of the line segment
  Real inv_seg_len_sq = 1.0/(seg.first - seg.second).norm_sq();

  // intersect all line segments
  Vector2 isect1, isect2;
  for (ForwardIterator i = begin; i != end; i++)
  {
    // get the next vertex
    ForwardIterator j = i;
    j++;
    if (j == end)
      j = begin;
  
    // intersect the two segments
    switch (intersect_segs(seg, LineSeg2(*i, *j), isect1, isect2))
    {
      case eSegSegNoIntersect:
        break;

      case eSegSegIntersect:
      case eSegSegVertex: 
        points.push_back((isect1 - seg.second).norm_sq() * inv_seg_len_sq);
        break;

      case eSegSegEdge:
        points.push_back((isect1 - seg.second).norm_sq() * inv_seg_len_sq);
        points.push_back((isect2 - seg.second).norm_sq() * inv_seg_len_sq);
        break;
    }
  }

  // sort the points
  points.sort();

  // make segments out of the points
  for (std::list<Real>::const_iterator i = points.begin(); i != points.rbegin().base(); i++)
  {
    std::list<Real>::const_iterator j = i;
    j++;
    Vector2 p1 = seg.first * (*i) + seg.second * (1.0 - (*i));
    Vector2 p2 = seg.first * (*j) + seg.second * (1.0 - (*j));
    *outbegin++ = LineSeg2(p1, p2);
  }

  return outbegin;
}

/// Computes the intersection of a convex polygon and a line segment
/**
 * \param begin iterator pointing to start of collection of Vector2 elements
 * \param end iterator pointing to end of collection of Vector2 elements
 * \param seg the line segment
 * \param te the parameter of the line segment for the beginning of the
 *        intersection; (1-te)*seg.first + te*seg.second is point of 
 *        intersection
 * \param tl the parameter of the line segment for the end of the intersection;
 *        (1-tl)*seg.first + tl*seg.second is point of intersection
 * \param tol the tolerance for parallel lines
 * \return <b>true</b> if the two intersect and <b>false</b> otherwise
 * \note polygon must be given in counter-clockwise order
 * \note first vertex should not appear twice
 * \note taken from http://geometryalgorithms.com 
 */
template <class InputIterator>
bool CompGeom::intersect_seg_convex_polygon(InputIterator begin, InputIterator end, const LineSeg2& seg, Real& te, Real& tl, Real tol)
{
  assert(tol >= 0.0);

  // initialize te and tl
  te = 0;
  tl = 1;

  Vector2 dS = seg.second - seg.first;

  // iterate over all vertices
  for (InputIterator i = begin; i != end; i++)
  {
    // get the next vertex
    InputIterator j = i;
    j++;
    if (j == end)
      j = begin;

    // get the edge
    Vector2 edge = *j - *i;

    // determine the outward normal of the edge
    Vector2 ni(edge[1], -edge[0]);
    Real N = -Vector2::dot(ni, seg.first-*i);
    Real D = Vector2::dot(dS, ni);
    if (std::fabs(D) < 0.0)
    {
      // segment is parallel to this edge
      if (N < tol)
        // first point is outside of the edge; segment cannot intersect poly
        return false;
      else
        // segment cannot enter or leave poly across this edge, process next
        continue;
    }

    Real t = N / D;
    if (D < 0.0)
    {
      // segment enters polygon across this edge
      te = std::max(te, t);
      if (te > tl)
        // segment enters polygon after leaving
        return false;
    }
    else
    {
      assert(D > 0.0);
      tl = std::min(tl, t);
      if (tl < te)
        // segment leaves polygon before entering
        return false;
    }  
  }

  return true;
}

/**
 * Converts a collection of Vector3 objects to Vector2 objects
 * \param source_begin an iterator pointing to the beginning of the Vector3 
 *        objects
 * \param source_end an iterator pointing to the end of the Vector3 objects
 * \param begin_target an iterator pointing to the beginning of the Vector2
 *        objects
 * \param R the projection matrix from 3D to 2D (on return)
 * \return the end of the output range
 * \note the size of the target collection must be equal to the size of the
 *       source collection
 */
template <class InputIterator, class OutputIterator>
OutputIterator CompGeom::to_2D(InputIterator begin_source, InputIterator end_source, OutputIterator begin_target, const Matrix3& R)
{
  // project the points to 2D
  for (InputIterator i = begin_source; i != end_source; i++, begin_target++)
    *begin_target = to_2D(*i, R);

  return begin_target;
}

/**
 * Converts a collection of Vector2 objects to Vector3 objects
 * \param source_begin an iterator pointing to the beginning of the Vector3 
 *        objects
 * \param source_end an iterator pointing to the end of the Vector3 objects
 * \param begin_target an iterator pointing to the beginning of the Vector2
 *        objects
 * \param R the projection matrix from 2D to 3D
 * \param offset the offset that must be added to the Z-coordinate of points
 *        projected back from 2D to 3D
 * \return the end of the output range
 * \note the size of the target collection must be equal to the size of the
 *       source collection
 */
template <class InputIterator, class OutputIterator>
OutputIterator CompGeom::to_3D(InputIterator begin_source, InputIterator end_source, OutputIterator begin_target, const Matrix3& RT, Real offset)
{
  // project the points back to 3D
  for (InputIterator i = begin_source; i != end_source; i++, begin_target++)
    *begin_target = to_3D(*i, RT, offset);

  return begin_target;
}

/**
 * Determines whether a polygon in 3D is counter-clockwise
 * \note Degenerate polygons (alternating representation) will fail!
 * \note This method is simple and could most certainly be made faster and
 *       more robust
 */
template <class InputIterator>
bool CompGeom::ccw(InputIterator begin, InputIterator end, const Vector3& normal, Real tol)
{
  assert(tol >= 0.0);

  for (InputIterator i = begin; i != end; i++)
  {
    InputIterator j = i;
    j++;
    if (j == end)
      j = begin;

    InputIterator k = j;
    k++;
    if (k == end)
      k = begin; 

    // take the cross product of the normal and the vector j i
    Vector3 c = Vector3::cross(normal, *j - *i);    

    // determine whether k j is to the left or right of j i
    Real dprod = Vector3::dot(c, *k - *j);
    if (dprod > tol)
      return true;
    else if (dprod < -tol)
      return false;

    // still here-- can't tell for sure, keep going
  }

  // if we're here, we've encountered a degenerate polygon
  assert(false);
  return false;
}

/**
 * Determines whether a polygon in 2D is counter-clockwise
 * \note Degenerate polygons (alternating representation) will fail!
 */
template <class InputIterator>
bool CompGeom::ccw(InputIterator begin, InputIterator end, Real tol)
{
  assert(tol >= 0.0);

  for (InputIterator i = begin; i != end; i++)
  {
    InputIterator j = i;
    j++;
    if (j == end)
      j = begin;

    InputIterator k = j;
    k++;
    if (k == end)
      k = begin; 
    
    OrientationType ori = area_sign(*i, *j, *k, tol);
    if (ori == eRight)
      return false;
  }

  // still here?  polygon may be degenerate!
  return true;
}

/// Intersects two coplanar triangles
/**
 * \param normal a normal to both triangles
 * \param begin an iterator to a container that can hold the points of 
 *         intersection (i.e., a container of size 6 or greater); on return,
 *         the container will contain a ccw polygon (in 3D) [orientation with
 *         respect to the given normal]
 * \return the end of the container output
 */
template <class OutputIterator>
OutputIterator CompGeom::intersect_coplanar_tris(const Triangle& t1, const Triangle& t2, const Vector3& normal, OutputIterator begin)
{
  const unsigned TRI_VERTS = 3;

  // project triangles to 2D
  Matrix3 R = calc_3D_to_2D_matrix(normal);
  Real offset = determine_3D_to_2D_offset(t1.a, R);
  Vector2 t1_2D[TRI_VERTS], t2_2D[TRI_VERTS];
  for (unsigned i=0; i< TRI_VERTS; i++)
  {
    t1_2D[i] = to_2D(t1.get_vertex(i), R);
    t2_2D[i] = to_2D(t2.get_vertex(i), R);
  } 

  // verify triangles are ccw and reverse if necessary
  if (!ccw(t1_2D, t1_2D + TRI_VERTS))
    std::swap(t1_2D[1], t1_2D[2]);
  if (!ccw(t2_2D, t2_2D + TRI_VERTS))
    std::swap(t2_2D[1], t2_2D[2]);

  // intersect triangles
  std::list<Vector2> points;
  intersect_tris(t1_2D, t2_2D, std::back_inserter(points));

  // project points back to 3D
  Matrix3 RT = Matrix3::transpose(R);
  BOOST_FOREACH(const Vector2& v, points)
    *begin++ = to_3D(v, RT, offset);

  return begin;
}

/// Intersects two polygons in 3D
/**
 * \param pbegin an iterator pointing to the container holding
 *        a ccw polygon
 * \param pend the end of the container holding the first polygon
 * \param qbegin an iterator pointing to the container holding
 *        a ccw polygon
 * \param qend the end of the container holding the second polygon
 * \param normal the normal to the polygons
 * \param isect_begin on return, the polygon of intersection will be placed
 *         here with ccw orientation; this container must be big enough to hold
 *         the result (i.e., it must be at least of size min(p,q)
 * \return the iterator pointing to the end of the container of intersection
 */
template <class InputIterator, class OutputIterator>
OutputIterator CompGeom::intersect_polygons(InputIterator pbegin, InputIterator pend, InputIterator qbegin, InputIterator qend, const Vector3& normal, OutputIterator isect_begin)
{
  // **************************************************************
  // first, we need to project the 3D triangles to 2D polygons
  // **************************************************************

  // R will project the points such that they lie in the plane z=0
  Matrix3 R = calc_3D_to_2D_matrix(normal);
  Real offset = determine_3D_to_2D_offset(*pbegin, R);
  
  // convert the two polygons to 2D
  std::vector<Vector2> p(std::distance(pbegin, pend));
  std::vector<Vector2> q(std::distance(qbegin, qend));
  to_2D(pbegin, pend, p.begin(), R);
  to_2D(qbegin, qend, q.begin(), R);

  // do the intersection
  std::list<Vector2> isect_2D;
  std::insert_iterator<std::list<Vector2> > ii(isect_2D, isect_2D.begin());
  intersect_polygons(p.begin(), p.end(), q.begin(), q.end(), ii);

  // transform the polygon of intersection to 3D
  R.transpose();
  std::list<Vector3> polygon;
  for (std::list<Vector2>::const_iterator i = isect_2D.begin(); i != isect_2D.end(); i++)
    polygon.push_back(to_3D(*i, R, offset));

  // verify that the polygon is ccw; if not, make it so
  for (std::list<Vector3>::const_iterator i = polygon.begin(); i != polygon.end(); i++)
  {
    // get the next two points
    std::list<Vector3>::const_iterator j = i;
    j++;
    if (j == polygon.end())
      j = polygon.begin();
    std::list<Vector3>::const_iterator k = j;
    k++;
    if (k == polygon.end())
      k = polygon.begin();

    // check the cross product
    Vector3 cprod = Vector3::cross((*j - *i), (*k - *j));
    
    // make sure that the cross product is not zero
    if (Vector3::norm(cprod) < NEAR_ZERO)
      continue;

    // check the dot product of the cross product and the normal
    Real dp = Vector3::dot(cprod, normal);

    // if the dot product of the cross product and the normal is less than zero, then the polygon is
    // oriented incorrectly
    if (dp > NEAR_ZERO)
      break;
    else if (dp < -NEAR_ZERO)
    {
      polygon.reverse();
      break;
    }
    else
    {
      assert(false);
    }
  }

  // copy the polygon to the output
  return std::copy(polygon.begin(), polygon.end(), isect_begin);
}

/// Intersects two polygons in 2D
/**
 * \param pbegin a random access iterator pointing to the container holding
 *        a ccw polygon
 * \param pend the end of the container holding the first polygon
 * \param qbegin a random access iterator pointing to the container holding
 *        a ccw polygon
 * \param qend the end of the container holding the second polygon
 * \param isect_begin on return, the polygon of intersection will be placed
 *         here with ccw orientation; this container must be big enough to hold
 *         the result (i.e., it must be at least of size min(p,q)
 * \return the iterator pointing to the end of the container of intersection
 */
template <class InputIterator, class OutputIterator>
OutputIterator CompGeom::intersect_polygons(InputIterator pbegin, InputIterator pend, InputIterator qbegin, InputIterator qend, OutputIterator isect_begin)
{
  enum tInFlag { Pin, Qin, Unknown };

  // verify that both polygons are ccw
  assert(ccw(pbegin, pend));
  assert(ccw(qbegin, qend));  

  // get how many points in p and q
  unsigned np = std::distance(pbegin, pend);
  unsigned nq = std::distance(qbegin, qend);

  // now compute their intersections  
  unsigned a = 0, b = 0, aa = 0, ba = 0;
  tInFlag inflag = Unknown;
  bool first_point = true;
  Vector2 origin(0,0);
  OutputIterator current = isect_begin;

  do
  {
    unsigned a1 = (a + np-1) % np;
    unsigned b1 = (b + nq-1) % nq;

    Vector2 AX = pbegin[a] - pbegin[a1];
    Vector2 BX = qbegin[b] - qbegin[b1];

    // determine signs of cross-products
    OrientationType cross = area_sign(origin, AX, BX);
    OrientationType aHB = area_sign(qbegin[b1], qbegin[b], pbegin[a]);
    OrientationType bHA = area_sign(pbegin[a1], pbegin[a], qbegin[b]);
    
    // if A and B intersect, update inflag
    Vector2 p, q;
    SegSegIntersectType code = intersect_segs(std::make_pair(pbegin[a1], pbegin[a]), std::make_pair(qbegin[b1], qbegin[b]), p, q);
    if (code == eSegSegVertex || code == eSegSegIntersect)
    {
      if (inflag == Unknown && first_point)
      {
        aa = ba = 0;
        first_point = false;
      }

      *current++ = p;
      if (aHB == eLeft)
        inflag = Pin;
      else if (bHA == eLeft)
        inflag = Qin;
    }

    // --------- Advance rules --------------
    // special cases: O'Rourke p. 262
    // special case: A and B overlap and oppositely oriented; intersection
    // is only a line segment
    if ((code == eSegSegEdge) && Vector2::dot(AX,BX) < 0)
    {
      *current++ = p;
      *current++ = q;
      return current;
    }
    // special case: A and B are parallel and disjoint
    else if ((cross == eOn) && (aHB == eRight) && (bHA == eRight))
      return isect_begin;
    // special case: A and B are collinear
    else if ((cross == eOn) && (aHB == eOn) && (bHA == eOn))
    {
      // advance but do not add point to intersecting polygon
      if (inflag == Pin)
        b = advance(b, &ba, nq, inflag == Qin, qbegin[b], current);
      else
        a = advance(a, &aa, np, inflag == Pin, pbegin[a], current);
    }
    // generic cases (continued from p. 258)
    else if (cross == eOn || cross == eLeft)
    {
      if (bHA == eLeft)
        a = advance(a, &aa, np, inflag == Pin, pbegin[a], current);
      else
        b = advance(b, &ba, nq, inflag == Qin, qbegin[b], current);
    }
    else
    {
      if (aHB == eLeft)
        b = advance(b, &ba, nq, inflag == Qin, qbegin[b], current);
      else
        a = advance(a, &aa, np, inflag == Pin, pbegin[a], current);
    }
  }
  while (((aa < np) || (ba < nq)) && (aa < 2*np) && (ba < 2*nq));

  // deal with remaining special cases: not implemented
  if (inflag == Unknown)
    return isect_begin;
  
  return current;
}

/// Utility function for intersect_coplanar_tris()
/**
 * Taken from O'Rourke, p. 259.
 */
template <class OutputIterator>
unsigned CompGeom::advance(unsigned a, unsigned* aa, unsigned n, bool inside, const Vector2& v, OutputIterator& current)
{
  if (inside)
    *current = v;

  (*aa)++;
  return (a+1) % n;
}

/// Intersects two triangles in 3D and returns the points of intersection
/**
 * \param begin an iterator to a container that can hold the points of 
 *         intersection (i.e., a container of size 6 or greater of Vector3
 *         objects); on return, the container will contain a ccw polygon (in 3D) 
 *         [orientation with respect to the given normal]
 * \return the end of the container output
 */
template <class OutputIterator>
OutputIterator CompGeom::intersect_tris(const Triangle& t1, const Triangle& t2, OutputIterator begin)
{
  // determine whether the triangles are coplanar
  if (CompGeom::coplanar(t1, t2))
    return intersect_coplanar_tris(t1, t2, t1.calc_normal(), begin);

  // intersect the triangles
  Vector3 p1, p2;
  if (!intersect_noncoplanar_tris(t1, t2, p1, p2))
    // no intersection, clear the list and return
    return begin;
  else
  {
    *begin++ = p1;
    *begin++ = p2;
    return begin;
  }
}

/// Calculates the convex hull of a set of points in 2D using quickhull
/**
 * \param source_begin an iterator to the beginning of a container of Vector2 
 * \param source_end an iterator pointing to the end of a container of Vector2
 * \param target_begin an iterator to the beginning of a container of indices;
 *         on return, contains the convex hull (NOTE: size of this container
 *         must be as large as the source container)
 * \return the new end of the target container
 */
template <class InputIterator, class OutputIterator>
OutputIterator CompGeom::calc_convex_hull_2D(InputIterator source_begin, InputIterator source_end, OutputIterator target_begin)
{
  const unsigned X = 0, Y = 1;
  int exit_code;
  int curlong, totlong;
  char flags[] = "qhull Fx";
  FILE* outfile, * errfile;
  
  FILE_LOG(LOG_COMPGEOM) << "computing 2D convex hull of following points:" << std::endl;
  for (InputIterator i = source_begin; i != source_end; i++)
    FILE_LOG(LOG_COMPGEOM) << "  " << *i << std::endl;
  
  // setup qhull outputs
  if (LOGGING(LOG_COMPGEOM))
  {
    outfile=stdout;  
    errfile=stderr;
  }
  else
  {
    outfile=NULL;
    errfile=fopen("/dev/null", "w");
    assert(errfile);
  } 

  // setup constants for qhull
  const int DIM = 2;
  const int N_POINTS = (int) std::distance(source_begin, source_end);
  const boolT IS_MALLOC = false;
  assert(N_POINTS > 2);
  
  // setup the points
  coordT* qhull_points = new coordT[N_POINTS*DIM];
  unsigned j=0;
  for (InputIterator i = source_begin; i != source_end; i++)
  {
    qhull_points[j] = (*i)[X];
    qhull_points[j+1] = (*i)[Y];
    j += DIM;
  }
  
  // lock the qhull mutex -- qhull is non-reentrant
  #ifdef THREADSAFE
  pthread_mutex_lock(&_qhull_mutex);
  #endif

  // execute qhull  
  exit_code = qh_new_qhull(DIM, N_POINTS, qhull_points, IS_MALLOC, flags, outfile, errfile);
  if (exit_code != 0)
  {
    // points are not collinear.. unsure of the error...
    FILE_LOG(LOG_COMPGEOM) << "CompGeom::calc_convex_hull_2D() - unable to execute qhull on points:" << std::endl;
    for (InputIterator i = source_begin; i != source_end; i++)
      FILE_LOG(LOG_COMPGEOM) << "  " << *i << std::endl;

    // free qhull memory
    qh_freeqhull(!qh_ALL);
    qh_memfreeshort(&curlong, &totlong);

    // release the mutex, since we're not using qhull anymore
    #ifdef THREADSAFE
    pthread_mutex_unlock(&_qhull_mutex);
    #endif

    // close the error stream, if necessary
    if (!LOGGING(LOG_COMPGEOM))
      fclose(errfile);

    return target_begin; 
  }

  // get all of the vertices
  std::vector<Vector2> vertices;
  std::map<vertexT*, unsigned> vertex_map;
  for (vertexT* vertex=qh vertex_list;vertex && vertex->next;vertex= vertex->next)
  {
    vertex_map[vertex] = vertices.size();
    vertices.push_back(Vector2());
    for (unsigned i=0; i< (unsigned) DIM; i++)
      vertices.back()[i] = vertex->point[i];    
  }

  // ordered list of edges
  std::map<unsigned, std::list<unsigned> > edges;
  
  // iterate through all facets  
  for (facetT* facet=qh facet_list;facet && facet->next;facet=facet->next)
  {
    // setup a list of vertices for the facet
    std::list<unsigned> facet_vertices;
    
    // get all vertices in the facet
    vertexT* vertex;
    for (vertexT** vertex_pointer = (vertexT**)& ((facet->vertices)->e[0].p); (vertex = (*vertex_pointer++));)
      facet_vertices.push_back(vertex_map[vertex]);
    
    // should be exactly two vertices in the list
    assert(facet_vertices.size() == 2);
    
    // store the edge in the list of edges
    edges[facet_vertices.front()].push_back(facet_vertices.back());
    edges[facet_vertices.back()].push_back(facet_vertices.front());
  }    
  
  // free memory for the points
  delete [] qhull_points;
  
  // free qhull memory
  qh_freeqhull(!qh_ALL);
  qh_memfreeshort(&curlong, &totlong);

  // release the qhull mutex
  #ifdef THREADSAFE
  pthread_mutex_unlock(&_qhull_mutex);
  #endif
  
  // construct the set of processed vertex
  std::set<unsigned> processed;
  
  // construct the hull; compute the area at the same time of the 2D polygon
  unsigned current_vertex = edges.begin()->first;
  std::list<Vector2> hull;

  while (true)
  {
    // add the current vertex to the list
    hull.push_back(vertices[current_vertex]);
    
    // mark this vertex as processed
    processed.insert(current_vertex);
    
    // get adjacent vertices
    std::list<unsigned>& adj_v = edges[current_vertex];
    
    // see which vertices have been processed
    if (processed.find(adj_v.front()) == processed.end())
      current_vertex = adj_v.front();
    else if (processed.find(adj_v.back()) == processed.end())
      current_vertex = adj_v.back();
    else
      break;    
  }

  // close the error stream, if necessary
  if (!LOGGING(LOG_COMPGEOM))
    fclose(errfile);

  // reverse the hull if necessary
  if (!ccw(hull.begin(), hull.end()))  
    return std::copy(hull.rbegin(), hull.rend(), target_begin);    
  else
    return std::copy(hull.begin(), hull.end(), target_begin);
}

/// Calculates the convex hull of a set of points that lie on a 2D manifold using quickhull
/**
 * \param source_begin an iterator to the beginning of a container of points
 * \param source_end an iterator pointing to the end of a container of points
 * \param normal the (optional) normal of the points; this will be computed if normal is zero vector
 * \param target_begin an iterator to the beginning of a container of points;
 *         on return, contains the convex hull (NOTE: size of this container
 *         must be as large as the source container)
 * \return the new end of the target container
 */
template <class InputIterator, class OutputIterator>
OutputIterator CompGeom::calc_convex_hull_2D(InputIterator source_begin, InputIterator source_end, const Vector3& normal, OutputIterator target_begin)
{
  FILE_LOG(LOG_COMPGEOM) << "computing 2D convex hull of following points:" << std::endl;
  for (InputIterator i = source_begin; i != source_end; i++)
    FILE_LOG(LOG_COMPGEOM) << "  " << *i << std::endl;
  
  // **************************************************************
  // first, we need to project the 3D surface to a 2D polygon
  // **************************************************************
  
  // determine the normal, if necessary 
  Vector3 n = normal;
  if (std::fabs(n.norm() - 1.0) > NEAR_ZERO)
  {
    Real offset;
    CompGeom::fit_plane(source_begin, source_end, n, offset);
  }

  // compute the 3D to 2D projection matrix
  Matrix3 R = calc_3D_to_2D_matrix(n);

  // get the 2D to 3D offset
  Real offset = determine_3D_to_2D_offset(*source_begin, R);

  // get the transpose (i.e., inverse) of the rotation matrix
  Matrix3 RT = Matrix3::transpose(R);

  // project the points to 2D
  unsigned sz = std::distance(source_begin, source_end);
  std::vector<Vector2> points_2D(sz);
  to_2D(source_begin, source_end, points_2D.begin(), R);

  FILE_LOG(LOG_COMPGEOM) << "2D points:" << std::endl;
  for (unsigned i=0; i< points_2D.size(); i++)
    FILE_LOG(LOG_COMPGEOM) << "  " << points_2D[i] << std::endl;

  // compute the convex hull
  std::list<Vector2> hull(sz);
  std::list<Vector2>::iterator hull_end = calc_convex_hull_2D(points_2D.begin(), points_2D.end(), hull.begin());

  // reverse the hull if necessary
  std::list<Vector3> hull3D;
  std::insert_iterator<std::list<Vector3> > ii(hull3D, hull3D.begin());
  to_3D(hull.begin(), hull_end, ii, RT, offset);
  if (!ccw(hull3D.begin(), hull3D.end(), normal))
    std::reverse(hull3D.begin(), hull3D.end());  

  // return the hull
  return std::copy(hull3D.begin(), hull3D.end(), target_begin);
}

/// Determines whether a polygon in 2D is convex
template <class InputIterator>
bool CompGeom::is_convex_polygon(InputIterator begin, InputIterator end, Real tol)
{
  assert(tol >= 0.0);

  // check whether every third point is to the left of the line segment
  // formed by the two preceeding points
  for (InputIterator i = begin; i != end; i++)
  {
    // get the next point -- if we've gone to the end, recycle
    InputIterator j = i;
    j++;
    if (j == end)
      j = begin;

    // get the following point -- if we've gone past the end, recycle
    InputIterator k = j;  
    k++;
    if (k == end)
      k = begin;

    // verify that k is not to the right of j and k
    if (area_sign(*i, *j, *k, tol) == eRight)
      return false;
  }

  // all checks passed.. convex
  return true;
}

/// Determines whether a polygon (in 3D) is convex
template <class InputIterator>
bool CompGeom::is_convex_polygon(InputIterator begin, InputIterator end, const Vector3& normal, Real tol)
{
  const unsigned X = 0, Y = 1, Z = 2;
  assert(tol >= 0.0);

  // get the 3D to 2D projection matrix
  Matrix3 R = calc_3D_to_2D_matrix(normal);

  // project the points to 2D
  std::list<Vector2> points_2D(std::distance(begin, end));
  to_2D(begin, end, points_2D.begin(), R);

  // if the 2D polygon is not ccw, make it so
  assert(ccw(points_2D.begin(), points_2D.end()));
//  if (!ccw(points_2D.begin(), points_2D.end()))
//    std::reverse(points_2D.begin(), points_2D.end());

  // check whether the 2D polygon is convex
  return is_convex_polygon(points_2D.begin(), points_2D.end(), tol);
}

/// Triangulates a convex polygon in O(n)
/**
 * \param source_begin the starting iterator to a connected, ccw-oriented, 
 *         convex polygon; additionally, the first point is assumed to be 
 *         connected to the last
 * \param source_end the ending iterator to the polygon container
 * \param target_begin the starting iterator to the container of triangles
 * \return the ending iterator to the container of triangles
 */
template <class InputIterator, class OutputIterator>
OutputIterator CompGeom::triangulate_convex_polygon(InputIterator source_begin, InputIterator source_end, OutputIterator target_begin)
{
  FILE_LOG(LOG_COMPGEOM) << "computing triangulation of polygon:" << std::endl;
  for (InputIterator i = source_begin; i != source_end; i++)
    FILE_LOG(LOG_COMPGEOM) << "  " << *i << std::endl;

  // special case: polygon is empty (return nothing)
  if (std::distance(source_begin, source_end) == 0)
    return target_begin;

  // compute the center of the points, and create new points
  std::list<Vector3> new_points;
  unsigned sz = 0;
  Vector3 center = ZEROS_3;
  for (InputIterator i = source_begin; i != source_end; i++, sz++)
  {
    center += *i;
    new_points.push_back(*i);
  }

  // special case: empty polygon (return nothing)
  if (sz == 0)
    return target_begin;

  // setup the output iterator pointer
  OutputIterator current = target_begin;

  // scale center by the mean
  center /= sz;

  // now, create triangles
  for (std::list<Vector3>::const_iterator i = new_points.begin(); i != new_points.end(); i++)
  {
    // get the next point
    std::list<Vector3>::const_iterator j = i;
    j++;
    if (j == new_points.end())
      j = new_points.begin();    
  
    // create a triangle
    *current++ = Triangle(*i, *j, center);
  }

  return current;
}

/// Attempts to fit a plane to a set of points 
/**
 * The singular value decomposition is used to determine the plane that fits
 * the points best in a least-squares sense.
 * \param points the set of points (in 3D)
 * \param normal contains the "best" normal, on return
 * \param offset the offset such that, for any point on the plane x, 
 *        <normal, x> = offset
 * \return the maximum deviation from the plane
 */
template <class InputIterator>
Real CompGeom::fit_plane(InputIterator begin, InputIterator end, Vector3& normal, Real& offset)
{
  const unsigned THREE_D = 3, X = 0, Y = 1, Z = 2;
  
  // compute the mean of the data
  unsigned n = 0;
  Vector3 mu = ZEROS_3;
  for (InputIterator i = begin; i != end; i++, n++)
    mu += *i;
  mu /= n;

  // create a matrix subtracting each point from the mean
  SAFESTATIC FastThreadable<MatrixN> Mx, Ux, Vx;
  SAFESTATIC FastThreadable<VectorN> Sx;
  MatrixN& M = Mx();
  M.resize(n, THREE_D);
  unsigned idx = 0;
  for (InputIterator i = begin; i != end; i++)
  {
    M.set_row(idx, *i - mu);
    idx++;
  }

  // take the svd of the matrix
  MatrixN& U = Ux();
  MatrixN& V = Vx();
  VectorN& S = Sx();
  LinAlg::svd(M, U, S, V);

  // last column of V should have the singular value we want; normalize it just in case
  normal[X] = V(X,Z);
  normal[Y] = V(Y,Z);
  normal[Z] = V(Z,Z);
  normal.normalize();
  
  // determine offset
  offset = Vector3::dot(normal, mu);

  // compute distance from all points
  Real max_dev = 0;
  for (InputIterator i = begin; i != end; i++)
    max_dev = std::max(max_dev, std::fabs(Vector3::dot(normal, *i) - offset));

  return max_dev;
}

/// Projects a set of points onto a plane
/**
 * This method is intended to be used with fit_plane() to project the set of points exactly onto
 * a plane; note that this method is unnecessary if the points fit a plane exactly.
 */
template <class ForwardIterator>
void CompGeom::project_plane(ForwardIterator begin, ForwardIterator end, const Vector3& normal, Real offset)
{
  // form the projection matrix P = I - normal*normal'
  Matrix3 P;
  Vector3::outer_prod(normal, -normal, &P);
  P += Matrix3::identity(); 
 
  // project each point onto the plane
  for (ForwardIterator i = begin; i != end; i++)
  {
    // compute the projection
    Vector3 x = P * (*i);
    
    // P projects onto a plane parallel to the one we want; project directly onto the one
    // we want
    Real remainder = offset - Vector3::dot(x, normal);
    
    // add the remainder times the normal to x, and store it
    *i = x + (normal * remainder);
  }
}

/// Determines whether a 2D point is inside a polygon
/**
 * Adapted from O'Rourke, p. 244.
 * \param polygon a vector of points that describe a polygon (orientation irrelevant); each successive 
 *         vertex in the vector is connected to the previous vector to make the 
 *         polygon (polygon.front() and polygon.back() are connected as well)
 * \param point the point to test
 * \return inside_poly if point is inside the polygon, on_vertex if the point 
 *         coincides with a vertex, on_edge if the point lies on an edge of
 *         the polygon (but not a vertex), or outside_poly if the point is 
 *         outside of the polygon
 */
template <class InputIterator>
CompGeom::PolygonLocationType CompGeom::polygon_location(InputIterator begin, InputIterator end, const Vector2& point)
{
  const unsigned X = 0, Y = 1;
  int l_cross = 0, r_cross = 0;  
  
  // copy the polygon to a vector, shifted so that the point is at the origin
  std::vector<Vector2> poly_copy;
  for (InputIterator i=begin; i != end; i++)
    poly_copy.push_back(*i - point);
  
  // for each edge e = (i-1,i); see if crosses ray
  for (unsigned i=0; i< poly_copy.size(); i++)
  {
    // check whether the point is equal to a vertex
    if (std::fabs(poly_copy[i][X]) < NEAR_ZERO && std::fabs(poly_copy[i][Y]) < NEAR_ZERO)
      return ePolygonOnVertex;
          
    // determine i1
    unsigned i1 = (i + poly_copy.size() - 1) % poly_copy.size();

     // check whether e "straddles" the x axis, with bias above, below
    bool r_strad = (poly_copy[i][Y] > 0) != (poly_copy[i1][Y] > 0);
    bool l_strad = (poly_copy[i][Y] < 0) != (poly_copy[i1][Y] < 0);
    
    if (r_strad || l_strad)
    {
      // compute intersection of e with x axis  
      LongReal x = (poly_copy[i][X] * poly_copy[i1][Y] - poly_copy[i1][X] * poly_copy[i][Y]) / (poly_copy[i1][Y] - poly_copy[i][Y]);

      // crosses ray if strictly positive intersection
       if (r_strad && x > 0)
            r_cross++;
      if (l_strad && x < 0)
        l_cross++;
    }
  }

  // point on an edge if L/R cross counts are not the same parity
  if ((r_cross % 2) != (l_cross % 2))
    return ePolygonOnEdge;

  // otherwise, point is inside iff an odd number of crossings
  if (r_cross % 2 == 1)
    return ePolygonInside;
  else
    return ePolygonOutside;
}

/// Computes the area of a polygon in 2D
/**
 * \param poly a counter-clockwise oriented polygon in 3D
 */
template <class ForwardIterator>
Real CompGeom::calc_polygon_area(ForwardIterator begin, ForwardIterator end)
{
  const unsigned X = 0, Y = 1, Z = 2;

  FILE_LOG(LOG_COMPGEOM) << "CompGeom::calc_polygon_area() entered" << std::endl;
  FILE_LOG(LOG_COMPGEOM) << "  points: " << std::endl;
  for (ForwardIterator i = begin; i != end; i++)
    FILE_LOG(LOG_COMPGEOM) << "    " << *i << std::endl;
  FILE_LOG(LOG_COMPGEOM) << "CompGeom::calc_polygon_area() exited" << std::endl;
  
  // compute the area of the polygon
  Real area = 0;
  for (ForwardIterator i = begin; i != end; i++)
  {
    // get the next element; wrap around
    ForwardIterator j = i;
    j++;
    if (j == end)
      j = begin;

    // update the area
    area += (*i)[X] * (*j)[Y]  -  (*j)[X] * (*i)[Y];
  }
  area *= 0.5;

  return area;
}

/// Computes the area of a polygon in 3D
/**
 * \param poly a counter-clockwise oriented polygon in 3D
 * \param normal a vector normal to the plane that contains the points
 */
template <class ForwardIterator>
Real CompGeom::calc_polygon_area(ForwardIterator begin, ForwardIterator end, const Vector3& normal)
{
  const unsigned X = 0, Y = 1, Z = 2;

  // get the 3D to 2D projection matrix
  Matrix3 R = calc_3D_to_2D_matrix(normal);

  // project the points to 2D
  std::list<Vector2> points_2D;
  to_2D(begin, end, std::back_inserter(points_2D), R);

  // make sure that 2D polygon is ccw
  assert(ccw(points_2D.begin(), points_2D.end()));

  FILE_LOG(LOG_COMPGEOM) << "CompGeom::calc_polygon_area() entered" << std::endl;
  FILE_LOG(LOG_COMPGEOM) << "  points (2D): " << std::endl;
  for (std::list<Vector2>::const_iterator i = points_2D.begin(); i != points_2D.end(); i++)
    FILE_LOG(LOG_COMPGEOM) << "    " << *i << std::endl;
  FILE_LOG(LOG_COMPGEOM) << "CompGeom::calc_polygon_area() exited" << std::endl;
  
  return calc_polygon_area(points_2D.begin(), points_2D.end());
}

/// Computes the centroid of points on a plane
/**
 * \param begin an iterator to a polygon in 2D
 * \param end an iterator to a polygon in 2D
 */
template <class ForwardIterator>
Vector2 CompGeom::calc_centroid_2D(ForwardIterator begin, ForwardIterator end)
{
  const unsigned X = 0, Y = 1;

  // now, compute the area of the polygon
  Real area = 0;
  std::list<Real> a;
  for (std::list<Vector2>::const_iterator i = begin; i != end; i++)
  {
    // get the next element; wrap around
    std::list<Vector2>::const_iterator j = i;
    j++;
    if (j == end)
      j = begin;

    // update the area
    a.push_back((*i)[X] * (*j)[Y]  -  (*j)[X] * (*i)[Y]);
    area += a.back();
  }
  area *= 0.5;

  // if the area is negative, negate it
  if (area < 0.0)
    area = -area;  
  
  // compute the 2D centroid
  Vector2 centroid(0.0,0.0);
  std::list<Real>::const_iterator i;
  std::list<Vector2>::const_iterator j;
  for (i = a.begin(), j = begin; i != a.end(); i++, j++)
  {
    // get the next point
    std::list<Vector2>::const_iterator k = j;
    k++;
    if (k == end)
      k = begin;

    centroid += (*j + *k) * (*i);      
  }
  centroid /= (area*6.0);
  
  return centroid;
}

/// Computes the 3D (2D) centroid of points on a plane
/**
 * \param poly a counter-clockwise oriented polygon in 3D
 * \param normal a vector normal to the plane that contains the points
 */
template <class InputIterator>
Vector3 CompGeom::calc_centroid_2D(InputIterator begin, InputIterator end, const Vector3& normal)
{
  const unsigned X = 0, Y = 1;

  // get the 3D to 2D projection matrix
  Matrix3 R = calc_3D_to_2D_matrix(normal);

  // get the offset
  Real offset = determine_3D_to_2D_offset(*begin, R);

  // project the points to 2D
  std::list<Vector2> points_2D;
  std::insert_iterator<std::list<Vector2> > ii(points_2D, points_2D.begin());
  to_2D(begin, end, ii, R);

  // make sure that 2D polygon is ccw
  assert(ccw(points_2D.begin(), points_2D.end()));

  FILE_LOG(LOG_COMPGEOM) << "polygon: " << std::endl;
  for (InputIterator i = begin; i != end; i++)
    FILE_LOG(LOG_COMPGEOM) << *i << std::endl;
  FILE_LOG(LOG_COMPGEOM) << "2D points:" << std::endl;
  for (std::list<Vector2>::const_iterator i = points_2D.begin(); i != points_2D.end(); i++)
    FILE_LOG(LOG_COMPGEOM) << "    " << *i << std::endl;

  // now, compute the area of the polygon
  Real area = 0;
  std::list<Real> a;
  for (std::list<Vector2>::const_iterator i = points_2D.begin(); i != points_2D.end(); i++)
  {
    // get the next element; wrap around
    std::list<Vector2>::const_iterator j = i;
    j++;
    if (j == points_2D.end())
      j = points_2D.begin();

    // update the area
    a.push_back((*i)[X] * (*j)[Y]  -  (*j)[X] * (*i)[Y]);
    area += a.back();
  }
  area *= 0.5;

  FILE_LOG(LOG_COMPGEOM) << "normal: " << normal << std::endl;
  FILE_LOG(LOG_COMPGEOM) << "area: " << area << std::endl;
  assert(area >= 0);
  
  // compute the 2D centroid
  Vector2 centroid(0,0);
  std::list<Real>::const_iterator i;
  std::list<Vector2>::const_iterator j;
  for (i = a.begin(), j = points_2D.begin(); i != a.end(); i++, j++)
  {
    // get the next point
    std::list<Vector2>::const_iterator k = j;
    k++;
    if (k == points_2D.end())
      k = points_2D.begin();

    centroid += (*j + *k) * (*i);      
  }
  centroid /= (area*6.0);
  
  // get the transpose (i.e., inverse) of the rotation matrix
  Matrix3 RT = Matrix3::transpose(R);
  
  FILE_LOG(LOG_COMPGEOM) << "2D centroid: " << centroid << std::endl;
  FILE_LOG(LOG_COMPGEOM) << "RT: " << std::endl << RT;
  
  // project the centroid back to 3D
  return to_3D(centroid, RT, offset);
}

/// Computes the minimum area bounding rectangle of a set of points
/**
 * Uses 2D convex hull and rotating calipers method; runs in O(N lg N) time
 * On return, x1, x2, x3, and x4 are the four vertices of the bounding
 * rectangle (ordered as edges).
 */
template <class InputIterator>
void CompGeom::calc_min_area_bounding_rect(InputIterator begin, InputIterator end, Vector2& x1, Vector2& x2, Vector2& x3, Vector2& x4)
{
  enum Flag { F_NONE, F_LEFT, F_RIGHT, F_BOTTOM, F_TOP };
  const unsigned X = 0, Y = 1;

  // calculate the convex hull of the points in ccw order
  std::vector<Vector2> hull;
  calc_convex_hull_2D(begin, end, std::back_inserter(hull));
  if (hull.empty())
  {
    // convex hull is degenerate; compute line endpoints and make that the 
    // "hull"
    std::pair<Vector2, Vector2> ep;
    determine_seg_endpoints(begin, end, ep);
    hull.push_back(ep.first);
    hull.push_back(ep.second);
  }
  // get the hull in CCW order
  else if (!ccw(hull.begin(), hull.end()))
    std::reverse(hull.begin(), hull.end());

  // make sure that no three points are colinear
  unsigned n = hull.size();
  for (unsigned i = 0; i < n; i++)
  {
    // get the next two points
    unsigned j = (i < n-1) ? i+1 : 0;
    unsigned k = (j < n-1) ? j+1 : 0;
    if (collinear(hull[i], hull[j], hull[k]))
    {
      // erase the middle point
      hull.erase(hull.begin()+j);

      // decrement both n and i
      i--;
      n--;
      // if n < 3, bounding rectangle is degenerate; however, we can still
      // output something useful
      if (n < 3)
      {
        x1 = hull[0];
        x2 = hull[1];
        x3 = hull[0];
        x4 = hull[1];
        return;
      }
    }
  }

  // setup unit-length edge directions of the convex polygon
  unsigned nm1 = n - 1;
  std::vector<Vector2> edges(n);
  std::vector<bool> visited(n, false);
  for (unsigned i=0; i< nm1; i++)
  {
    edges[i] = hull[i+1] - hull[i];
    edges[i].normalize();
  }
  edges[nm1] = hull[0] - hull[nm1];
  edges[nm1].normalize();

  // find the smallest axis-aligned box containing the points.  Keep track
  // of the extremum indices, L (left), R (right), B (bottom), and T (top)
  // so that the following constraints are met:
  //   V[L].X <= V[i].X for all i and V[(L+1)%N].X > V[L].X
  //   V[L].X >= V[i].X for all i and V[(R+1)%N].X < V[R].X
  //   V[L].Y <= V[i].Y for all i and V[(B+1)%N].Y > V[B].X
  //   V[L].Y >= V[i].Y for all i and V[(T+1)%N].Y < V[T].X
  Real xmin = hull[0][X], xmax = xmin;
  Real ymin = hull[0][Y], ymax = ymin;
  unsigned Lindex = 0, Rindex = 0, Bindex = 0, Tindex = 0;
  for (unsigned i=1; i< n; i++)
  {
    if (hull[i][X] <= xmin)
    {
      xmin = hull[i][X];
      Lindex = i;
    }
    if (hull[i][X] >= xmax)
    {
      xmax = hull[i][X];
      Rindex = i;
    }
    if (hull[i][Y] <= ymin)
    {
      ymin = hull[i][Y];
      Bindex = i;
    }
    if (hull[i][Y] >= ymax)
    {
      ymax = hull[i][Y];
      Tindex = i;
    }
  }

  // apply wrap-around tests to ensure the constraints mentioned above 
  // are satisfied
  if (Lindex == nm1)
  {
    if (hull[0][X] <= xmin)
    {
      xmin = hull[0][X];
      Lindex = 0;
    }
  }
  if (Rindex == nm1)
  {
    if (hull[0][X] >= xmax)
    {
      xmax = hull[0][X];
      Rindex = 0;
    }
  }
  if (Bindex == nm1)
  {
    if (hull[0][Y] <= ymin)
    {
      ymin = hull[0][Y];
      Bindex = 0;
    }
  }
  if (Tindex == nm1)
  {
    if (hull[0][Y] >= ymax)
    {
      ymax = hull[0][Y];
      Tindex = 0;
    }
  }

  // the dimensions of the axis-aligned box; the extents store width and height
  // for now
  Vector2 center((Real) 0.5 * (xmin + xmax), (Real) 0.5 * (ymin + ymax));
  Vector2 axis[2] = { Vector2((Real) 1.0, (Real) 0.0), 
                      Vector2((Real) 0.0, (Real) 1.0) };
  Real extent[2] = { (Real) 0.5 * (xmax - xmin), (Real) 0.5 * (ymax - ymin) };
  Real min_area_div4 = extent[0]*extent[1];

  // the rotating calipers algorithm follows...
  Vector2 U((Real) 1.0, (Real) 0.0), V((Real) 0.0, (Real) 1.0);
  bool done = false;
  while (!done)
  {
    // determine the edge that forms the smallest angle with the current box
    // edges
    Flag flag = F_NONE;
    Real maxdot = (Real) 0.0;
    Real dot = U.dot(edges[Bindex]);
    if (dot > maxdot)
    {
      maxdot = dot;
      flag = F_BOTTOM;
    }
    dot = V.dot(edges[Rindex]);
    if (dot > maxdot)
    {
      maxdot = dot;
      flag = F_RIGHT;
    }
    dot = -U.dot(edges[Tindex]);
    if (dot > maxdot)
    {
      maxdot = dot;
      flag = F_TOP;
    }
    dot = -V.dot(edges[Lindex]);
    if (dot > maxdot)
    {
      maxdot = dot;
      flag = F_LEFT;
    }

    switch (flag)
    {
      case F_BOTTOM:
        if (visited[Bindex])
          done = true;
        else
        {
          // compute box axes with E[B] as an edge
          U = edges[Bindex];
          V = -U.perp();
          update_box(hull[Lindex], hull[Rindex], hull[Bindex], hull[Tindex], U, V, min_area_div4, center, axis, extent);

          // mark edge visited and rotate the calipers
          visited[Bindex] = true;
          if (++Bindex == n)
             Bindex = 0;
         }
         break;

      case F_RIGHT:
        if (visited[Rindex])
          done = true;
        else
        {
          // compute box axes with E[R] as an edge
          V = edges[Rindex];
          U = V.perp();
          update_box(hull[Lindex], hull[Rindex], hull[Bindex], hull[Tindex], U, V, min_area_div4, center, axis, extent);

          // mark edge visited and rotate the calipers
          visited[Rindex] = true;
          if (++Rindex == n)
            Rindex = 0;
        }
        break;

      case F_TOP:
        if (visited[Tindex])
          done = true;
        else
        {
          // compute box axes with E[T] as an edge
          U = -edges[Tindex];
          V = -U.perp();
          update_box(hull[Lindex], hull[Rindex], hull[Bindex], hull[Tindex], U, V, min_area_div4, center, axis, extent);

          // mark edge visited and rotate the calipers
          visited[Tindex] = true;
          if (++Tindex == n)
            Tindex = 0;
        }
        break;

      case F_LEFT:
        if (visited[Lindex])
          done = true;
        else
        {
          // compute box axes with E[L] as an edge
          V = -edges[Lindex];
          U = V.perp();
          update_box(hull[Lindex], hull[Rindex], hull[Bindex], hull[Tindex], U, V, min_area_div4, center, axis, extent);

          // mark edge visited and rotate the calipers
          visited[Lindex] = true;
          if (++Lindex == n)
            Lindex = 0;
        }
        break;

      case F_NONE:
        // the polygon is a rectangle
        done = true;
        break;
    }
  }

  // convert Eberly's representation to my own
  x1 =  center - axis[X]*extent[X] - axis[Y]*extent[Y];
  x2 =  center + axis[X]*extent[X] - axis[Y]*extent[Y];
  x3 =  center + axis[X]*extent[X] + axis[Y]*extent[Y];
  x4 =  center - axis[X]*extent[X] + axis[Y]*extent[Y];
}

/*
template <class InputIterator>
void CompGeom::calc_min_area_bounding_rect(InputIterator begin, InputIterator end, Vector2& x1, Vector2& x2, Vector2& x3, Vector2& x4)
{
  const Real INF = std::numeric_limits<Real>::max();
  const unsigned X = 0, Y = 1;
  Real i1, i2, i3, i4, dummy;

  // compute the convex hull of the points -- need it in ccw order
  std::vector<Vector2> hull;
  calc_convex_hull_2D(begin, end, std::back_inserter(hull));
  if (hull.empty())
  {
    // convex hull is degenerate; compute line endpoints and make that the 
    // "hull"
    std::pair<Vector2, Vector2> ep;
    determine_seg_endpoints(begin, end, ep);
    hull.push_back(ep.first);
    hull.push_back(ep.second);
  }
  // get the hull in CCW order
  else if (!ccw(hull.begin(), hull.end()))
    std::reverse(hull.begin(), hull.end());

  // determine the number of points in the hull
  unsigned n = hull.size();

  // determine the vector value to be added to the points
  // get the axis-aligned bounding rectangle
  Real min_x = std::numeric_limits<Real>::max();
  Real min_y = std::numeric_limits<Real>::max();
  Real max_x = -std::numeric_limits<Real>::max();
  Real max_y = -std::numeric_limits<Real>::max();
  BOOST_FOREACH(const Vector2& p, hull)
  for (std::vector<Vector2>::const_iterator i = hull.begin(); i != hull.end(); i++)
  {
    if (p[X] < min_x)
      min_x = p[X];
    if (p[X] > max_x)
      max_x = p[X];
    if (p[Y] < min_y)
      min_y = p[Y];
    if (p[Y] > max_y)
      max_y = p[Y];
  }
  Real offset = ((max_x-min_x)*(max_x-min_x) + (max_y-min_y)*(max_y-min_y))*2.0;

  // make sure that no three points are colinear
  for (unsigned i = 0; i < n; i++)
  {
    // get the next two points
    unsigned j = (i < n-1) ? i+1 : 0;
    unsigned k = (j < n-1) ? j+1 : 0;
    if (collinear(hull[i], hull[j], hull[k]))
    {
      // erase the middle point
      hull.erase(hull.begin()+j);

      // decrement both n and i
      i--;
      n--;
      // if n < 3, bounding rectangle is degenerate; however, we can still
      // output something useful
      if (n < 3)
      {
        x1 = hull[0];
        x2 = hull[1];
        x3 = hull[0];
        x4 = hull[1];
        return;
      }
    }
  }

  // compute all four extreme points for the polygon
  unsigned p1 = 0, p2 = 0, p3 = 0, p4 = 0;
  for (unsigned i = 1; i < n; i++)
  {
    if (hull[i][X] < hull[p1][X])
      p1 = i;
    if (hull[i][X] > hull[p3][X])
      p3 = i;
    if (hull[i][Y] < hull[p4][Y])
      p4 = i;
    if (hull[i][Y] > hull[p2][Y])
      p2 = i;
  }

  // setup the supports
  Vector2 s1(0,1);
  Vector2 s2(1,0);
  Vector2 s3(0,-1);
  Vector2 s4(-1,0);

  // setup the current minimum area
  Real min_area = INF;
  
  // repeat until lines have been rotated an angle greater than 90 degrees
  Real rotated_angle = 0.0;
  while (rotated_angle <= M_PI_2)
  {
    // determine the edges that the calipers would rotate onto
    unsigned q1 = (p1 > 0) ? p1-1 : n-1;
    unsigned q2 = (p2 > 0) ? p2-1 : n-1;
    unsigned q3 = (p3 > 0) ? p3-1 : n-1;
    unsigned q4 = (p4 > 0) ? p4-1 : n-1;
    Vector2 e1 = hull[q1] - hull[p1];
    Vector2 e2 = hull[q2] - hull[p2];
    Vector2 e3 = hull[q3] - hull[p3];
    Vector2 e4 = hull[q4] - hull[p4];
    e1.normalize();
    e2.normalize();
    e3.normalize();
    e4.normalize();

    // determine the minimum angle necessary to rotate the support lines to
    // coincide with an edge of the polygon
    // NOTE: we clip the dot products to interval [-1,1]
    Real theta1 = std::acos(std::max((Real) -1.0, std::min(s1.dot(e1),(Real) 1.0)));
    Real theta2 = std::acos(std::max((Real) -1.0, std::min(s2.dot(e2),(Real) 1.0)));
    Real theta3 = std::acos(std::max((Real) -1.0, std::min(s3.dot(e3),(Real) 1.0)));
    Real theta4 = std::acos(std::max((Real) -1.0, std::min(s4.dot(e4),(Real) 1.0)));
    if (theta1 <= theta2 && theta1 <= theta3 && theta1 <= theta4)
    {
      rotated_angle += theta1;
      Matrix2 R = Matrix2::rot_Z(-theta1);
      s1 = Vector2::normalize(R*s1);
      s2 = Vector2::normalize(R*s2);
      s3 = Vector2::normalize(R*s3);
      s4 = Vector2::normalize(R*s4);
      p1 = q1;
    }
    else if (theta2 <= theta1 && theta2 <= theta3 && theta2 <= theta4)
    {
      rotated_angle += theta2;
      Matrix2 R = Matrix2::rot_Z(-theta2);
      s1 = Vector2::normalize(R*s1);
      s2 = Vector2::normalize(R*s2);
      s3 = Vector2::normalize(R*s3);
      s4 = Vector2::normalize(R*s4);
      p2 = q2;
    }
    else if (theta3 <= theta1 && theta3 <= theta2 && theta3 <= theta4)
    {
      rotated_angle += theta3;
      Matrix2 R = Matrix2::rot_Z(-theta3);
      s1 = Vector2::normalize(R*s1);
      s2 = Vector2::normalize(R*s2);
      s3 = Vector2::normalize(R*s3);
      s4 = Vector2::normalize(R*s4);
      p3 = q3;
    }
    else
    {
      rotated_angle += theta4;
      Matrix2 R = Matrix2::rot_Z(-theta4);
      s1 = Vector2::normalize(R*s1);
      s2 = Vector2::normalize(R*s2);
      s3 = Vector2::normalize(R*s3);
      s4 = Vector2::normalize(R*s4);
      p4 = q4;
    }

    // compute the pseudo area of the new rectangle
    LineSeg2 v1(hull[p1] - s1*offset, hull[p1] + s1*offset);
    LineSeg2 v2(hull[p2] - s2*offset, hull[p2] + s2*offset);
    LineSeg2 v3(hull[p3] - s3*offset, hull[p3] + s3*offset);
    LineSeg2 v4(hull[p4] - s4*offset, hull[p4] + s4*offset);
    LineLineIntersectType t1 = intersect_lines(hull[p1], s1, -INF, INF, hull[p2], s2, -INF, INF, i1, dummy);
    LineLineIntersectType t2 = intersect_lines(hull[p2], s2, -INF, INF, hull[p3], s3, -INF, INF, i2, dummy);
    LineLineIntersectType t3 = intersect_lines(hull[p3], s3, -INF, INF, hull[p2], s4, -INF, INF, i3, dummy);
    assert(t1 == eLineLineIntersect);
    assert(t2 == eLineLineIntersect);
    assert(t3 == eLineLineIntersect);
    Vector2 pi1 = hull[p1] + s1*i1;
    Vector2 pi2 = hull[p2] + s2*i2;
    Vector2 pi3 = hull[p3] + s3*i3;
    Real area = (pi2 - pi1).norm_sq() * (pi3 - pi2).norm_sq();

    // update the minimum if necessary
    if (area < min_area)
    {
      // store the new minimum area
      min_area = area;

      // determine the last vertex and store the vertices
      LineLineIntersectType t4 = intersect_lines(hull[p4], s4, -INF, INF, hull[p1], s1, -INF, INF, i4, dummy);
      assert(t4 == eLineLineIntersect);
      x1 = hull[p1] + s1*i1;
      x2 = hull[p2] + s2*i2;
      x3 = hull[p3] + s3*i3;
      x4 = hull[p4] + s4*i4;
    }
  }

  // make sure that area is zero if hull is degenerate
  assert(n > 2 || std::fabs(min_area) < NEAR_ZERO);
}
*/

/**
 * Intersects two 2D triangles.
 * \note this method adapted from www.geometrictools.com
 */
template <class OutputIterator>
OutputIterator CompGeom::intersect_tris(const Vector2 t1[3], const Vector2 t2[3], OutputIterator output_begin)
{
  const unsigned X = 0, Y = 1;

  // verify that both triangles are ccw
  assert(ccw(t1, t1+3));
  assert(ccw(t2, t2+3));

  // init the potential intersection to t2 
  Vector2 isects[6];
  isects[0] = t2[0];
  isects[1] = t2[1];
  isects[2] = t2[2];
  unsigned nisects = 3;

  // clip against edges
  for (unsigned i1=2, i0=0; i0 < 3; i1 = i0, i0++)
  {
    Vector2 kN(t1[i1][Y] - t1[i0][Y], t1[i0][X] - t1[i1][X]);
    Real fC = kN.dot(t1[i1]);
    clip_convex_polygon_against_line(kN, fC, nisects, isects);

    // look for no intersection
    if (nisects == 0)
      return output_begin;
  }

  // copy to output
  for (unsigned i=0; i< nisects; i++)
    *output_begin++ = isects[i];

  return output_begin;
}

/// Intersects a line segment and a triangle in 2D
/*
 * \param seg the line segment
 * \param tri the triangle 
 * \param output_begin an iterator to the beginning of a container of Vector2; 
 *        points of intersection will be stored here on return
 * \return an iterator to the end of a container of Vector2; 
 *        points of intersection will be stored here on return
 * \note this code adapted from http://www.geometrictools.com
 */
template <class OutputIterator>
OutputIterator CompGeom::intersect_seg_tri(const LineSeg2& seg, const Vector2 tri[3], OutputIterator output_begin)
{
  Vector2 isect, isect2;
  CompGeom::SegTriIntersectType code = intersect_seg_tri(seg, tri, isect, isect2);
  
  switch (code)
  {
    case eSegTriNoIntersect:
      break;

    case eSegTriVertex:
    case eSegTriEdge:
    case eSegTriPlanarIntersect: 
      *output_begin++ = isect; 

    case eSegTriEdgeOverlap:
    case eSegTriInside:
      *output_begin++ = isect;
      *output_begin++ = isect2;
      break;

    default:
      break;
  }

  return output_begin;
}
    

/*
  Real afDist[3];
  int aiSign[3], iPositive, iNegative, iZero;

  // this block of code adapted from TriangleLineRelations()
  Vector2 origin = (seg.first + seg.second)*0.5;
  Vector2 seg_dir = Vector2::normalize(seg.second - seg.first);
  iPositive = 0;
  iNegative = 0;
  iZero = 0;
  for (unsigned i=0; i< 3; i++)
  {
    Vector2 kDiff = tri[i] - origin;
    afDist[i] = kDiff.dot_perp(seg_dir);
    if (afDist[i] > NEAR_ZERO)
    {
      aiSign[i] = 1;
      iPositive++;
    }
    else if (afDist[i] < -NEAR_ZERO)
    {
      aiSign[i] = -1;
      iNegative++;
    }
    else
    {
      afDist[i] = 0.0;
      aiSign[i] = 0;
      iZero++;
    }
  }

  if (iPositive == 3 || iNegative == 3)
  {
    // empty set
    return output_begin;
  }
  else
  {
    Real afParam[2];

}
*/

/// Gets the parameter of a point on a line, v = p0 + dir*t, -inf <= t <= inf
template <class T>
Real CompGeom::determine_line_param(const T& p0, const T& dir, const T& v)
{
  Real dir_norm = dir.norm();
  if (dir_norm < NEAR_ZERO)
    throw NumericalException("Attempting to normalize zero length vector");
  return sgn((v - p0).norm()/dir_norm, (v - p0).dot(dir));
}

/// Intersects two line segments in 2D
/**
 * \param seg1 the first line segment
 * \param seg2 the second line segment
 * \param output_begin an iterator to the beginning of a container of Vector2; 
 *        points of intersection will be stored here on return
 * \return an iterator to the end of a container of Vector2; 
 *        points of intersection will be stored here on return
 */ 
template <class OutputIterator>
OutputIterator CompGeom::intersect_segs(const LineSeg2& s1, const LineSeg2& s2, OutputIterator output_begin)
{
  // do the intersection
  Vector2 isect, isect2;
  SegSegIntersectType isect_type = intersect_segs(s1, s2, isect, isect2);

  // switch on the intersection type
  switch (isect_type)
  {
    case eSegSegNoIntersect:
      break;

    case eSegSegIntersect:
    case eSegSegVertex:
      *output_begin++ = isect;
      break; 

    case eSegSegEdge:
      *output_begin++ = isect;
      *output_begin++ = isect2;
      break; 
  }

  return output_begin;
}

