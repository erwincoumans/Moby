/****************************************************************************
 * Copyright 2009 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#include <iostream>
#ifdef USE_OSG
#include <osg/Geode>
#include <osg/Shape>
#include <osg/ShapeDrawable>
#endif
#include <Moby/Constants.h>
#include <Moby/CompGeom.h>
#include <Moby/XMLTree.h>
#include <Moby/OBB.h>
#include <Moby/ConePrimitive.h>

using namespace Moby;
using std::pair;
using boost::shared_ptr;
using std::list;
using std::vector;

/// Constructs a cone centered at the origin, with the longitudinal axis aligned with the y-axis, radius 1.0, height 1.0, 1 ring and 10 circle points 
ConePrimitive::ConePrimitive()
{
  _radius = 1.0;
  _height = 1.0;
  _npoints = 10;
  _nrings = 1;
  calc_mass_properties();
}

/// Constructs a cone along the y-axis with specified radius and height, centered at the origin, with 1 ring and 10 circle points 
ConePrimitive::ConePrimitive(Real radius, Real height)
{
  if (height < (Real) 0.0)
    throw std::runtime_error("Attempting to set negative height in ConePrimitive (constructor)");
  if (radius < (Real) 0.0)
    throw std::runtime_error("Attempting to set negative radius in ConePrimitive (constructor)");

  _radius = radius;
  _height = height;
  _npoints = 10;
  _nrings = 1;
  calc_mass_properties();
}

/// Constructs a cone along the y-axis with specified radius and height, centered at the origin, with 1 ring and 10 circle points 
ConePrimitive::ConePrimitive(Real radius, Real height, const Matrix4& T) : Primitive(T)
{
  if (height < (Real) 0.0)
    throw std::runtime_error("Attempting to set negative height in ConePrimitive (constructor)");
  if (radius < (Real) 0.0)
    throw std::runtime_error("Attempting to set negative radius in ConePrimitive (constructor)");

  _radius = radius;
  _height = height;
  _npoints = 10;
  _nrings = 1;
  calc_mass_properties();
}

/// Constructs a cone along the y-axis and centered at the origin with specified, radius, height, points and rings
ConePrimitive::ConePrimitive(Real radius, Real height, unsigned npoints, unsigned nrings, const Matrix4& T) : Primitive(T)
{
  if (height < (Real) 0.0)
    throw std::runtime_error("Attempting to set negative height in ConePrimitive (constructor)");
  if (radius < (Real) 0.0)
    throw std::runtime_error("Attempting to set negative radius in ConePrimitive (constructor)");
  if (npoints < 3)
    throw std::runtime_error("Attempting to set number of circle points < 3 in ConePrimitive (constructor)");
  if (nrings < 1)
    throw std::runtime_error("Attempting to set number of rings < 1 in ConePrimitive (constructor)");

  _radius = radius;
  _height = height;
  _npoints = npoints;
  _nrings = nrings;
  calc_mass_properties();
}

/// Sets the radius for this cone
void ConePrimitive::set_radius(Real radius)
{
  _radius = radius;
  if (_radius < 0.0)
    throw std::runtime_error("Attempting to pass negative radius to ConePrimitive::set_radius()");

  // mesh, vertices are no longer valid
  _mesh = shared_ptr<IndexedTriArray>();
  _vertices = shared_ptr<vector<Vector3> >();
  _smesh = pair<shared_ptr<IndexedTriArray>, list<unsigned> >();
  _invalidated = true;

  // need to recalculate mass properties
  calc_mass_properties();

  // need to update visualization
  update_visualization();
}

/// Sets the intersection tolerance
void ConePrimitive::set_intersection_tolerance(Real tol)
{
  Primitive::set_intersection_tolerance(tol);

  // vertices are no longer valid
  _vertices = shared_ptr<vector<Vector3> >();
}

/// Sets the height for this cone
void ConePrimitive::set_height(Real height)
{
  _height = height;
  if (_height < 0.0)
    throw std::runtime_error("Attempting to pass negative height to ConePrimitive::set_height()");

  // mesh, vertices are no longer valid
  _mesh = shared_ptr<IndexedTriArray>();
  _vertices = shared_ptr<vector<Vector3> >();
  _smesh = pair<shared_ptr<IndexedTriArray>, list<unsigned> >();
  _invalidated = true;

  // need to recalculate mass properties
  calc_mass_properties();

  // need to update visualization
  update_visualization();
}

/// Sets the number of points in the rings of the cone 
void ConePrimitive::set_circle_points(unsigned n)
{
  _npoints = n;
  if (_npoints < 4)
    throw std::runtime_error("Too few points to represent a circle in ConePrimitive::set_circle_points()");

  // vertices are no longer valid
  _vertices = shared_ptr<vector<Vector3> >();
  _invalidated = true;
}

/// Sets the number of rings in the cone 
void ConePrimitive::set_num_rings(unsigned n)
{
  _nrings = n;
  if (_npoints < 1)
    throw std::runtime_error("Too few rings in ConePrimitive::set_num_rings()");

  // vertices are no longer valid
  _vertices = shared_ptr<vector<Vector3> >();
  _invalidated = true;
}

/// Transforms the primitive
void ConePrimitive::set_transform(const Matrix4& T)
{
  // determine the transformation from the old to the new transform 
  Matrix4 Trel = T * Matrix4::inverse_transform(_T);

  // go ahead and set the new transform
  Primitive::set_transform(T);

  // transform mesh
  if (_mesh)
  {
    _mesh = shared_ptr<IndexedTriArray>(new IndexedTriArray(_mesh->transform(Trel)));
    _smesh.first = _mesh;
  }

  // transform vertices
  if (_vertices)
    for (unsigned i=0; i< _vertices->size(); i++)
      (*_vertices)[i] = Trel.mult_point((*_vertices)[i]);

  // indicate that this primitive has become invalidated
  _invalidated = true;

  // recalculate the mass properties
  calc_mass_properties();
}

/// Gets the triangle mesh for the cone, computing it if necessary
shared_ptr<const IndexedTriArray> ConePrimitive::get_mesh()
{
  // compute the mesh if necessary
  if (!_mesh)
  {
    const Real R = _radius;
    const Real HH = _height * (Real) 0.5;

    // if radius or height = 0, # of rings < 1 or circle points < 3, return now
    if (_radius <= 0.0 || _height <= 0.0 || _nrings < 1 || _npoints < 3)
    {
      _mesh = shared_ptr<IndexedTriArray>(new IndexedTriArray());
      _smesh = make_pair(_mesh, list<unsigned>());
      return _mesh;
    }

    // get the current transform
    const Matrix4& T = get_transform();

    // determine the vertices in the mesh
    list<Vector3> points; 
    for (unsigned i=0; i< _npoints; i++)
    {
      const Real THETA = i * (M_PI * (Real) 2.0/_npoints);
      const Real CT = std::cos(THETA);
      const Real ST = std::sin(THETA);
      points.push_back(T.mult_point(Vector3(CT*R, -HH, ST*R)));
    }

    // create one more vertex for the tip of the cone
    points.push_back(T.mult_point(Vector3(0.0, HH, 0.0)));

    // compute the convex hull
    PolyhedronPtr hull = CompGeom::calc_convex_hull_3D(points.begin(), points.end());

    // set the mesh
    const std::vector<Vector3>& v = hull->get_vertices();
    const std::vector<IndexedTri>& f = hull->get_facets();
    _mesh = boost::shared_ptr<IndexedTriArray>(new IndexedTriArray(v.begin(), v.end(), f.begin(), f.end()));

    // setup sub mesh (it will be just the standard mesh)
    list<unsigned> all_tris;
    for (unsigned i=0; i< _mesh->num_tris(); i++)
      all_tris.push_back(i);
    _smesh = make_pair(_mesh, all_tris);
  }

  return _mesh;
}

/// Creates the visualization for this primitive
osg::Node* ConePrimitive::create_visualization()
{
  #ifdef USE_OSG
  osg::Cone* cone = new osg::Cone;
  cone->setRadius((float) _radius);
  cone->setHeight((float) _height);
  osg::Geode* geode = new osg::Geode;
  geode->addDrawable(new osg::ShapeDrawable(cone));
  return geode;
  #else
  return NULL;
  #endif
}

/// Implements Base::load_from_xml() for serialization
void ConePrimitive::load_from_xml(XMLTreeConstPtr node, std::map<std::string, BasePtr>& id_map)
{
  // verify that the node type is cone
  assert(strcasecmp(node->name.c_str(), "Cone") == 0);

  // load the parent data
  Primitive::load_from_xml(node, id_map);

  // read in the radius, if specified
  const XMLAttrib* radius_attr = node->get_attrib("radius");
  if (radius_attr)
    _radius = radius_attr->get_real_value();

  // read in the height, if specified
  const XMLAttrib* height_attr = node->get_attrib("height");
  if (height_attr)
    _height = height_attr->get_real_value();

  // read in the number of circle points, if specified
  const XMLAttrib* npoints_attr = node->get_attrib("num-circle-points");
  if (npoints_attr)
    _npoints = npoints_attr->get_unsigned_value();

  // read in the number of rings of the cylinder, if specified
  const XMLAttrib* nrings_attr = node->get_attrib("num-rings");
  if (nrings_attr)
    _nrings = nrings_attr->get_unsigned_value();

  // recompute mass properties
  calc_mass_properties();
}

/// Implements Base::save_to_xml() for serialization
void ConePrimitive::save_to_xml(XMLTreePtr node, std::list<BaseConstPtr>& shared_objects) const
{
  // save the parent data
  Primitive::save_to_xml(node, shared_objects);

  // (re)set the node name
  node->name = "Cone";

  // save the radius
  node->attribs.insert(XMLAttrib("radius", _radius));

  // save the height
  node->attribs.insert(XMLAttrib("height", _height));

  // save the number of circle points 
  node->attribs.insert(XMLAttrib("num-circle-points", _npoints));

  // save the number of rings
  node->attribs.insert(XMLAttrib("num-rings", _nrings));
}

/// Calculates mass properties for the cone
void ConePrimitive::calc_mass_properties()
{
  // get the current transform
  const Matrix4& T = get_transform();

  // determine the radius squared (we'll need this) 
  const Real RSQ = _radius * _radius;

  // compute the mass if density is given
  if (_density)
  {
    const Real volume = 1.0/3.0 * M_PI * RSQ * _height;
    _mass = *_density * volume;
  }

  // compute the non-longitudinal elements
  const Real HSQ = _height * _height;
  const Real NL_ELM = 0.1 * _mass * HSQ + (3.0/20.0) * _mass * RSQ;
  const Real LONG_ELM = (1.0/3.0) * _mass * RSQ;

  // compute the inertia matrix
  Matrix3 J(NL_ELM, 0, 0, 0, LONG_ELM, 0, 0, 0, NL_ELM);

  // transform the inertia matrix using the current transform
  transform_inertia(_mass, J, ZEROS_3, T, _J, _com);
}

/// Gets vertices from the primitive
void ConePrimitive::get_vertices(BVPtr bv, std::vector<const Vector3*>& vertices)
{
  // create the vector of vertices if necessary
  if (!_vertices)
  {
    // setup constant for the expanded radius
    const Real H = _height + (_intersection_tolerance * (Real) 2.0);

    // if the radius, height, or rings = 0 or circle points < 3, return now
    if (_radius == 0.0 || _height == 0.0 || _nrings == 0 || _npoints < 3)
    {
      vertices.clear();
      return; 
    }

    // get the current transform for the primitive
    const Matrix4& T = get_transform();

    // create the vector of vertices
    _vertices = shared_ptr<vector<Vector3> >(new vector<Vector3>());

    // create vertices
    for (unsigned j=0; j< _nrings; j++)
    {
      const Real HEIGHT = -(H * (Real) 0.5) + (j*H)/_nrings;
      const Real R = (_radius + _intersection_tolerance) * (Real) (_nrings - j)/_nrings;
      for (unsigned i=0; i< _npoints; i++)
      {
        const Real THETA = i*(M_PI * (Real) 2.0/_npoints);
        const Real CT = std::cos(THETA);
        const Real ST = std::sin(THETA);
        _vertices->push_back(T.mult_point(Vector3(CT*R, HEIGHT, ST*R)));
      }
    }

    // create one more vertex for the tip of the cone
    _vertices->push_back(T.mult_point(Vector3(0.0, H * (Real) 0.5, 0.0)));
  }

  // copy the addresses of the computed vertices into 'vertices' 
  vertices.resize(_vertices->size());
  for (unsigned i=0; i< _vertices->size(); i++)
    vertices[i] = &(*_vertices)[i];
}

/// Gets a sub-mesh for the primitive
const std::pair<boost::shared_ptr<const IndexedTriArray>, std::list<unsigned> >& ConePrimitive::get_sub_mesh(BVPtr bv)
{
  if (!_smesh.first)
    get_mesh(); 
  return _smesh;
}

/// Gets the OBB
BVPtr ConePrimitive::get_BVH_root()
{
  const unsigned X = 0, Y = 1, Z = 2;

  // cone not applicable for deformable bodies 
  if (is_deformable())
    throw std::runtime_error("ConePrimitive::get_BVH_root() - primitive unusable for deformable bodies!");

  // create the OBB if necessary
  if (!_obb)
    _obb = shared_ptr<OBB>(new OBB);

  // setup the center of the OBB 
  _obb->center = get_transform().get_translation();
  
  // setup the orientation of the OBB
  _obb->R(X,X) = get_transform()(X,X);
  _obb->R(X,Y) = get_transform()(X,Y);
  _obb->R(X,Z) = get_transform()(X,Z);
  _obb->R(Y,X) = get_transform()(Y,X);
  _obb->R(Y,Y) = get_transform()(Y,Y);
  _obb->R(Y,Z) = get_transform()(Y,Z);
  _obb->R(Z,X) = get_transform()(Z,X);
  _obb->R(Z,Y) = get_transform()(Z,Y);
  _obb->R(Z,Z) = get_transform()(Z,Z);

  // must orthonormalize OBB orientation, b/c T may have scaling applied
  _obb->R.orthonormalize();

  // cone nominally points upward
  _obb->l[X] = _radius;
  _obb->l[Y] = _height*0.5;
  _obb->l[Z] = _radius;

  return _obb;
}

/// The signum function
namespace Moby {
static Real sgn(Real x) { return x / std::fabs(x); }
}

/// Determines whether a point is inside the cone; if so, determines the normal
/**
 * Derived/adapted from Eberly. D.  "Intersection of a Line and a Cone"
 */
bool ConePrimitive::point_inside(BVPtr bv, const Vector3& p, Vector3& normal) const
{
  // transform the point to cone frame 
  const Matrix4& T = get_transform();
  Vector3 query = T.inverse_mult_point(p);

  // determine the angle theta
  Real theta = std::atan(_radius/_height);

  // setup A
  Vector3 A(0,-1,0);

  // setup v
  Vector3 V(0,_height,0);

  // see whether the point is outside
  if (A.dot(Vector3::normalize(query - V)) < std::cos(theta))
    return false;

  // determine the normal (the gradient: derived using Mathematica)
  normal = determine_normal(query);

  return true;
}

/// Computes the penetration depth of a point inside the cone
/**
 * \return the penetration depth, or -INF if the point is outside the cone
 */
Real ConePrimitive::calc_penetration_depth(const Vector3& p) const
{
  const unsigned X = 0, Y = 1, Z = 2;
  const Real INF = std::numeric_limits<Real>::max();

  // verify the point is inside
  const Matrix4& T = get_transform();
  Vector3 query = T.inverse_mult_point(p);

  // determine the angle theta
  Real theta = std::atan(_radius/_height);

  // setup A
  Vector3 A(0,-1,0);

  // setup v
  Vector3 V(0,_height,0);

  // see whether the point is outside
  if (A.dot(Vector3::normalize(query - V)) < std::cos(theta))
    return -INF;

  // get the radius of the cone at the vertical location of the point
  // radius at +1/2 height = 0
  // radius at -1/2 height = R
  const Real RR = -_radius * (query[Y] / _height) + (Real) 0.5 * _radius ;

  // get the distance from the horizontal part of the cone
  Real dcone = RR - std::sqrt(sqr(query[X]) + sqr(query[Z]));

  // get the distance from the vertical parts of the cone
  Real dv1 = (Real) 0.5 * _height - query[Y];
  Real dv2 = (Real) 0.5 * _height + query[Y];

  return std::min(std::min(dv1, dv2), dcone);
}

/// Determines the normal to a point on the cone
/**
 * \note the normal may be degenerate (NaN)
 */
Vector3 ConePrimitive::determine_normal(const Vector3& query) const
{
  const unsigned X = 0, Y = 1, Z = 2;

  // determine the normal (the gradient: derived using Mathematica)
  const Real xx = query[X];
  const Real xy = query[Y];
  const Real xz = query[Z];
  const Real usqrt = xx*xx + (xy-_height)*(xy-_height) + xz*xz;
  const Real D = std::pow(usqrt, (Real) 1.5);

  // setup the normal
  Vector3 normal;
  normal[X] = (xy - _height)*std::fabs(xx)*Moby::sgn(xx)/D;
  normal[Y] = -1.0/std::sqrt(usqrt) + (xy - _height)*std::fabs(xy-_height)*Moby::sgn(xy-_height)/D;
  normal[Z] = (xy - _height)*std::fabs(xz)*Moby::sgn(xz)/D;

  return normal;
}

/// Intersects a cone with a line segment
/**
 * Adapted from Geometric Tools cone/line segment intersection
 * \note for line segments that are partially or fully inside the cone, the
 *       method only returns intersection if the second endpoint of the segment
 *       is farther inside than the first
 */
bool ConePrimitive::intersect_seg(BVPtr bv, const LineSeg3& seg, Real& t, Vector3& isect, Vector3& normal) const
{
  const unsigned Y = 1;

  // we'll need a second intersection point for temporary storage
  Vector3 isects[3];

  // first check whether the first point is inside/on the cone
  Real dp = calc_penetration_depth(seg.first);
  if (dp >= (Real) 0.0)
  {
    // first point is inside/on the cone
    // get the normal
    if (!point_inside(bv, seg.first, normal))
      return false;

    // indicate intersection
    t = (Real) 0.0;
    isect = seg.first;
    return true;
  }

  // transform the line segment to cone frame 
  const Matrix4& T = get_transform();
  Vector3 p = T.inverse_mult_point(seg.first);
  Vector3 q = T.inverse_mult_point(seg.second);

  // determine the angle theta
  Real theta = std::atan(_radius/_height);

  // determine the unit-length line direction vector
  Vector3 dir = Vector3::normalize(q - p);
  Real fAdD = -dir[Y];
  Real fCosSqr = std::cos(theta);
  fCosSqr *= fCosSqr;
  Vector3 kE = p - Vector3(0,_height,0);
  Real fAdE = -kE[Y];
  Real fDdE = dir.dot(kE);
  Real fEdE = kE.dot(kE);
  Real fC2 = fAdD*fAdD - fCosSqr;
  Real fC1 = fAdD*fAdE - fCosSqr*fDdE;
  Real fC0 = fAdE*fAdE - fCosSqr*fEdE;

  // solve the quadratic; keep only those points for which A dot X-V >= 0
  if (std::fabs(fC2) >= NEAR_ZERO)
  {
    // c2 != 0
    Real fDiscr = fC1*fC1 - fC0*fC2;
    if (fDiscr < (Real) 0.0)
    {
      // Q(t) = 0 has no real-valued roots; the line does not intersect
      // the double sided cone
      return false;
    }
    else if (fDiscr > NEAR_ZERO)
    {
      // Q(t) = 0 has two distinct real-valued roots.  However, one or
      // both o fthem might intersect the portion of the double-sided cone
      // behind the vertex.  We are interested only in those intersections in
      // "front" of the vertex
      Real fRoot = std::sqrt(fDiscr);
      Real fInvC2 = ((Real) 1.0)/fC2;
      unsigned nisect = 0;

      Real fT = (-fC1 - fRoot)*fInvC2;
      isects[nisect] = p + dir*fT;
      kE = isects[nisect] - Vector3(0,_height,0);          
      Real fDot = -kE[Y];
      if (fDot > (Real) 0.0)
        nisect++;

      fT = (-fC1 + fRoot)*fInvC2;
      isects[nisect] = p + dir*fT;
      kE = isects[nisect] - Vector3(0,_height,0);
      fDot = -kE[Y];
      if (fDot > (Real) 0.0)
        nisect++;

      if (nisect == 2)
      {
        // line intersects the single sided cone in front of the vertex twice
        // determine which point is closer to the first
        if ((isects[0]-p).norm() > (isects[1]-p).norm())
          std::swap(isects[0],isects[1]);

        // determine the intersection parameter
        t = std::sqrt((isects[0] - p).norm_sq()/dir.norm_sq());

        // transform that point from the cone frame
        isect = T.mult_point(isects[0]);

        // determine the normal to the point
        normal = determine_normal(isects[0]);

        return true;
      }
      else if (nisect == 1)
      {
        // line intersects the single-sided cone in front of the vertex once.
        // the other intersection is with the single-sided cone behind the
        // vertex

        // determine the intersection parameter
        t = std::sqrt((isects[0] - p).norm_sq()/dir.norm_sq());

        // transform the intersection point from the cone frame
        isect = T.mult_point(isects[0]);

        // determine the normal to the point
        normal = determine_normal(isects[0]);

        return true;
      }
      else
      {
        // line intersects single-sided cone behind the vertex twice
        return false;
      }
    }
    else
    {
      // one repeated real root (line is tangent to the cone)
      isects[0] = p - dir*(fC1/fC2);
      kE = isects[0] - Vector3(0,_height,0);
      if (-kE[Y] > (Real) 0.0)
      {
        // have a single point intersection
        // determine the intersection parameter
        t = std::sqrt((isects[0] - p).norm_sq()/dir.norm_sq());

        // transform the intersection point from the cone frame
        isect = T.mult_point(isects[0]);

        // determine the normal to the point
        normal = determine_normal(isects[0]);

        return true;
      }
      else
      {
        // no intersection
        return false;
      }
    }
  }
  else if (std::fabs(fC1) > NEAR_ZERO)
  {
    // c2 = 0, c1 != 0 (D is a direction vector on the cone boundary)
    isects[0] = p - (((Real) 0.5) * fC0/fC1)*dir;
    kE = isects[0] - Vector3(0,_height,0);
    Real fDot = -kE[Y];
    if (fDot > (Real) 0.0)
    {
      // ray intersection
      // transform point from cone frame
      isect = T.mult_point(isects[0]);

      // determine the intersection parameter
      t = std::sqrt((isects[0] - p).norm_sq()/dir.norm_sq());
      
      // determine the normal to the point
      normal = determine_normal(isects[0]);

      return true;
    }
  }
  else if (std::fabs(fC0) > NEAR_ZERO)
  {
    // c2 = c1 = 0, c0 != 0
    return false;
  }
  else
  {
    // c2 = c1 = c0 = 0, cone contains ray V+t*D, where V is cone vertex
    // and D is the line direction; NOTE: normal is degenerate, so we return
    // no intersection
    return false;
  }

  // shouldn't still be here
  return false;
}

