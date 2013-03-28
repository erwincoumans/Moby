/****************************************************************************
 * Copyright 2010 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#include <complex>
#include <fstream>
#include <set>
#include <cmath>
#include <algorithm>
#include <stack>
#include <queue>
#include <boost/tuple/tuple.hpp>
#include <Moby/CompGeom.h>
#include <Moby/CollisionDetection.h>
#include <Moby/LinAlg.h>
#include <Moby/Event.h>
#include <Moby/Constants.h>
#include <Moby/Polyhedron.h>
#include <Moby/RigidBody.h>
#include <Moby/ArticulatedBody.h>
#include <Moby/CollisionGeometry.h>  
#include <Moby/XMLTree.h>
#include <Moby/Integrator.h>
#include <Moby/OBB.h>
#include <Moby/NumericalException.h>
#include <Moby/MeshDCD.h>

// To delete
#include <Moby/SpherePrimitive.h>
#include <Moby/BoxPrimitive.h>

using namespace Moby;
using boost::dynamic_pointer_cast;
using boost::static_pointer_cast;
using boost::shared_ptr;
using boost::tuple;
using boost::make_tuple;
using std::cerr;
using std::endl;
using std::set;
using std::queue;
using std::map;
using std::list;
using std::vector;
using std::priority_queue;
using std::pair;
using std::make_pair;

/// Constructs a collision detector with default tolerances
/**
 * TOI tolerance is set to 1e-2.
 */
MeshDCD::MeshDCD()
{
  eps_tolerance = 1e-4;
  isect_tolerance = 1e-4;
  _rebuild_bounds_vecs = true;
  return_all_contacts = true;
}

void MeshDCD::add_collision_geometry(CollisionGeometryPtr cg)
{
  CollisionDetection::add_collision_geometry(cg);
  _rebuild_bounds_vecs = true;
}

void MeshDCD::add_rigid_body(RigidBodyPtr rb)
{
  CollisionDetection::add_rigid_body(rb);
  _rebuild_bounds_vecs = true;
}

void MeshDCD::add_deformable_body(DeformableBodyPtr db)
{
  CollisionDetection::add_deformable_body(db);
  _rebuild_bounds_vecs = true;
}

void MeshDCD::add_articulated_body(ArticulatedBodyPtr abody, bool disable_adjacent)
{
  CollisionDetection::add_articulated_body(abody, disable_adjacent);
  _rebuild_bounds_vecs = true;
}

void MeshDCD::remove_collision_geometry(CollisionGeometryPtr cg)
{
  CollisionDetection::remove_collision_geometry(cg);
  _rebuild_bounds_vecs = true;
}

void MeshDCD::remove_all_collision_geometries()
{
  CollisionDetection::remove_all_collision_geometries();
  _rebuild_bounds_vecs = true;
}

void MeshDCD::remove_rigid_body(RigidBodyPtr rb)
{
  CollisionDetection::remove_rigid_body(rb);
  _rebuild_bounds_vecs = true;
}

void MeshDCD::remove_deformable_body(DeformableBodyPtr db)
{
  CollisionDetection::remove_deformable_body(db);
  _rebuild_bounds_vecs = true;
}

void MeshDCD::remove_articulated_body(ArticulatedBodyPtr abody)
{
  CollisionDetection::remove_articulated_body(abody);
  _rebuild_bounds_vecs = true;
}

/// Determines whether there is a contact in the given time interval
/**
 * \pre body states are at time tf
 */
bool MeshDCD::is_contact(Real dt, const vector<pair<DynamicBodyPtr, VectorN> >& q0, const vector<pair<DynamicBodyPtr, VectorN> >& q1, vector<Event>& contacts)
{
  VectorN qd;

  // clear the contact set 
  contacts.clear();

  FILE_LOG(LOG_COLDET) << "MeshDCD::is_contact() entered" << endl;

  // do broad phase; NOTE: broad phase yields updated BVs
  vector<pair<CollisionGeometryPtr, CollisionGeometryPtr> > to_check;
  broad_phase(to_check);

  // check the geometries
  for (unsigned i=0; i< to_check.size(); i++)
  {
    // get the two geometries
    CollisionGeometryPtr a = to_check[i].first;
    CollisionGeometryPtr b = to_check[i].second;

    // test the geometries for contact
    check_geoms(dt, a, b, q0, q1, contacts);
  } 

  // check all geometries of deformable bodies for self-intersection
  BOOST_FOREACH(CollisionGeometryPtr cg, _geoms)
    if (dynamic_pointer_cast<DeformableBody>(cg))
      check_geom(dt, cg, q0, q1, contacts);

  // remove contacts with degenerate normals
  for (unsigned i=0; i< contacts.size(); )
    if (std::fabs(contacts[i].contact_normal.norm() - (Real) 1.0) > NEAR_ZERO)
    {
      contacts[i] = contacts.back();
      contacts.pop_back();
    }
    else
      i++; 

  FILE_LOG(LOG_COLDET) << "contacts:" << endl;
  if (contacts.empty())
    FILE_LOG(LOG_COLDET) << " -- no contacts in narrow phase" << endl;
  if (LOGGING(LOG_COLDET))
    for (unsigned i=0; i< contacts.size(); i++)
      FILE_LOG(LOG_COLDET) << contacts[i] << std::endl;

  // sort the vector of contacts
  std::sort(contacts.begin(), contacts.end());

  FILE_LOG(LOG_COLDET) << "MeshDCD::is_contact() exited" << endl << endl;

  // indicate whether impact has occurred
  return !contacts.empty();
}

/// Does a collision check for a geometry for a deformable body
void MeshDCD::check_geom(Real dt, CollisionGeometryPtr cg, const vector<pair<DynamicBodyPtr, VectorN> >& q0, const vector<pair<DynamicBodyPtr, VectorN> >& q1, vector<Event>& contacts)
{
  FILE_LOG(LOG_COLDET) << "MeshDCD::check_geom() entered" << endl;
  SAFESTATIC VectorN q, qtmp;

  // get the body
  DynamicBodyPtr db = cg->get_single_body();

  // get the old and new configurations of the body (qa, qb)
  const unsigned db_idx = find_body(q0, db);
  assert(db_idx != std::numeric_limits<unsigned>::max());
  assert(db_idx == find_body(q1, db));
  const VectorN& qa = q0[db_idx].second;
  const VectorN& qb = q1[db_idx].second;

  // check for contact at qb
  db->set_generalized_coordinates(DynamicBody::eRodrigues, qb);
  bool contact = is_collision(cg);

  // if there is contact, we want to find TOC to within sufficient tolerance
  if (contact)
  {
    // setup t and h
    Real t = (Real) 0.0;
    Real h = (Real) 1.0;

    // loop invariant: contact at t0+h
    while (h > eps_tolerance)
    {
      // divide h by 2
      h *= 0.5;

      // step forward by h
      q.copy_from(qa) *= ((Real) 1.0 - t+h);
      qtmp.copy_from(qb) *= (t+h);
      q += qtmp;
      db->set_generalized_coordinates(DynamicBody::eRodrigues, q);

      // check for contact
      bool contact = is_collision(cg);

      // if there is no contact, we update t 
      if (!contact)
        t += h;
    }

    // set the coordinates for the deformable body
    q.copy_from(qa) *= ((Real) 1.0 - t);
    qtmp.copy_from(qb) *= t;
    q += qtmp;
    db->set_generalized_coordinates(DynamicBody::eRodrigues, q);

    // set the generalized velocity for the deformable body
    q.copy_from(qb) -= qa;
    q /= dt;
    db->set_generalized_velocity(DynamicBody::eRodrigues, q);

    // determine contacts for the deformable body
    determine_contacts_deformable(cg, cg, t, h, contacts);
  }

  // remove duplicate contact points
  for (unsigned i=0; i< contacts.size(); i++)
    for (unsigned j=i+1; j< contacts.size(); )
    {
      if (std::fabs(contacts[i].t - contacts[j].t) < NEAR_ZERO && 
          (contacts[i].contact_point - contacts[j].contact_point).norm() < NEAR_ZERO)
      {
        contacts[j] = contacts.back();
        contacts.pop_back();
      }
      else 
        j++;
    }

  FILE_LOG(LOG_COLDET) << "MeshDCD::check_geom() exited" << endl;
}


/// Gets the "super" body for a collision geometry
DynamicBodyPtr MeshDCD::get_super_body(CollisionGeometryPtr geom)
{
  RigidBodyPtr rb = dynamic_pointer_cast<RigidBody>(geom->get_single_body());
  assert(rb);
  ArticulatedBodyPtr ab = rb->get_articulated_body();
  if (ab)
    return ab;
  else
    return rb;
}

/// Finds the index of the body / state pair for the given body
unsigned MeshDCD::find_body(const vector<pair<DynamicBodyPtr, VectorN> >& q, DynamicBodyPtr body)
{
  for (unsigned i=0; i< q.size(); i++)
    if (q[i].first == body)
      return i;

  return std::numeric_limits<unsigned>::max();
}

/// Does a collision check for a pair of geometries
/**
 * \param dt the time interval
 * \param a the first geometry
 * \param b the second geometry
 * \param aTb the transform from b's frame to a's frame
 * \param bTa the transform from a's frame to b's frame
 * \param vels linear and angular velocities of bodies
 * \param contacts on return
 */
void MeshDCD::check_geoms(Real dt, CollisionGeometryPtr a, CollisionGeometryPtr b, const vector<pair<DynamicBodyPtr, VectorN> >& q0, const vector<pair<DynamicBodyPtr, VectorN> >& q1, vector<Event>& contacts)
{
  FILE_LOG(LOG_COLDET) << "MeshDCD::check_geoms() entered" << endl;
  SAFESTATIC VectorN q, qda, qdb;

  // get the two super bodies
  DynamicBodyPtr sba = get_super_body(a);
  DynamicBodyPtr sbb = get_super_body(b); 

  // get the states at times t=0 and t=1
  const unsigned idx_a = find_body(q0, sba);
  const unsigned idx_b = find_body(q0, sbb);
  assert(idx_a == find_body(q1, sba));
  assert(idx_b == find_body(q1, sbb));
  assert(idx_a != std::numeric_limits<unsigned>::max());
  assert(idx_b != std::numeric_limits<unsigned>::max());
  const VectorN& qa0 = q0[idx_a].second;
  const VectorN& qa1 = q1[idx_a].second;
  const VectorN& qb0 = q0[idx_b].second;
  const VectorN& qb1 = q1[idx_b].second;

  // compute the velocities
  qda.copy_from(qa1) -= qa0;
  qdb.copy_from(qb1) -= qb0;

  // check for contact at q1 states 
  sba->set_generalized_coordinates(DynamicBody::eRodrigues, qa1);
  sbb->set_generalized_coordinates(DynamicBody::eRodrigues, qb1);
  bool contact = is_collision(a, b);

  // if there is contact, we want to find TOC to within sufficient tolerance
  if (contact)
  {
    // setup t0 and h
    Real t = (Real) 0.0;
    Real h = (Real) 1.0;

    // loop invariant: contact at t0+h
    while (h > eps_tolerance)
    {
      // divide h by 2
      h *= 0.5;

      // set new state for sba
      q.copy_from(qda) *= (t+h);
      q += qa0;
      sba->set_generalized_coordinates(DynamicBody::eRodrigues, q);

      // set new state for sbb
      q.copy_from(qdb) *= (t+h);
      q += qb0;
      sbb->set_generalized_coordinates(DynamicBody::eRodrigues, q);

      // check for contact
      bool contact = is_collision(a, b);

      // if there is no contact, we update t 
      if (!contact)
        t += h;
    }

    // set the first body's coordinates and velocity at the time-of-contact
    q.copy_from(qda) *= t;
    q += qa0;
    sba->set_generalized_coordinates(DynamicBody::eRodrigues, q);
    sba->set_generalized_velocity(DynamicBody::eRodrigues, qda /= dt);

    // set the second body's coordinates at the time-of-contact
    q.copy_from(qdb) *= t;
    q += qb0;
    sbb->set_generalized_coordinates(DynamicBody::eRodrigues, q);
    sbb->set_generalized_velocity(DynamicBody::eRodrigues, qdb /= dt);

    // determine the types of the two bodies
    RigidBodyPtr rba = dynamic_pointer_cast<RigidBody>(sba);
    RigidBodyPtr rbb = dynamic_pointer_cast<RigidBody>(sbb);
    DeformableBodyPtr dba = dynamic_pointer_cast<DeformableBody>(sba);
    DeformableBodyPtr dbb = dynamic_pointer_cast<DeformableBody>(sbb);

    // now determine contacts
    if (rba && rbb)
      determine_contacts_rigid(a, b, t, h, contacts);
    else if (rba && dbb)
    {
      determine_contacts_rigid_deformable(a, b, t, h, contacts);
      determine_contacts_deformable_rigid(b, a, t, h, contacts);
    }
    else if (rbb && dba)
    {
      determine_contacts_rigid_deformable(b, a, t, h, contacts);
      determine_contacts_deformable_rigid(a, b, t, h, contacts);
    }
    else
    {
      assert(dba && dbb);
      determine_contacts_deformable(a, b, t, h, contacts);
    }
  }
  
  // remove duplicate contact points
  for (unsigned i=0; i< contacts.size(); i++)
  {
    for (unsigned j=i+1; j< contacts.size(); )
    {
      if (std::fabs(contacts[i].t - contacts[j].t) < NEAR_ZERO && 
          (contacts[i].contact_point - contacts[j].contact_point).norm() < NEAR_ZERO)
      {
        contacts[j] = contacts.back();
        contacts.pop_back();
      }
      else 
        j++;
    }
  } 

  FILE_LOG(LOG_COLDET) << "MeshDCD::check_geoms() exited" << endl;
}

/// Computes the real roots of the cubic polynomial x^3 + ax^2 + bx + c
/**
 * \return the number of real roots
 */
unsigned MeshDCD::determine_cubic_roots(Real a, Real b, Real c, Real x[3])
{
  // determine p, q
  Real p = b - a*a/3;
  Real q = c + (2*a*a*a - 9*a*b)/27;

  // typedef the Real complex type
  typedef std::complex<Real> rcomplex;

  // determine the six values of u (not all unique)
  rcomplex croot(q*q/4 + p*p*p/27);
  croot = std::sqrt(croot);
  rcomplex u1a = std::pow(rcomplex(-q/2) + croot, 1.0/3.0);
  rcomplex u2a = std::pow(rcomplex(-q/2) - croot, 1.0/3.0);
  rcomplex umul1(-.5,sqrt(3)/2); 
  rcomplex umul2(-.5,-sqrt(3)/2); 
  rcomplex u1b = u1a * umul1; 
  rcomplex u1c = u1a * umul2; 
  rcomplex u2b = u2a * umul1; 
  rcomplex u2c = u2a * umul2; 

  // determine the six values of x (not all unique)
  Real xx[6];
  unsigned xidx = 0;
  xx[xidx++] = (u1a - p/((Real) 3.0*u1a) - a/3).real();
  xx[xidx++] = (u1b - p/((Real) 3.0*u1b) - a/3).real();
  xx[xidx++] = (u1c - p/((Real) 3.0*u1c) - a/3).real();
  xx[xidx++] = (u2a - p/((Real) 3.0*u2a) - a/3).real();
  xx[xidx++] = (u2b - p/((Real) 3.0*u2b) - a/3).real();
  xx[xidx++] = (u2c - p/((Real) 3.0*u2c) - a/3).real();

  // find real roots
  unsigned nreal = 0;
  for (unsigned i=0; i< 6; i++)
    if (std::fabs(xx[i]*xx[i]*xx[i] + a*xx[i]*xx[i] + b*xx[i] + c) < NEAR_ZERO)
    {
      // it's a root; make sure that the root doesn't already exist
      bool found = false;
      for (unsigned j=0; j< nreal && !found; j++)
        if (std::fabs(xx[i] - x[j]) < NEAR_ZERO)
          found = true;

      // if not found, add the root
      if (!found)
        x[nreal++] = xx[i];
    }

  return nreal;
}

/// Intersects a line segment against a triangle with vertices moving at different velocities
Real MeshDCD::calc_first_isect(const Vector3& p, const Vector3& pdot, const Triangle& T, const Vector3& Tadot, const Vector3& Tbdot, const Vector3& Tcdot, Real dt) 
{
  const unsigned X = 0, Y = 1, Z = 2;
  const Real INF = std::numeric_limits<Real>::max();

  // setup everything to match Mathematica output
  Real P0X = p[X];
  Real P0Y = p[Y];
  Real P0Z = p[Z];
  Real PdotX = pdot[X];
  Real PdotY = pdot[Y];
  Real PdotZ = pdot[Z];
  Real A0X = T.a[X];
  Real A0Y = T.a[Y];
  Real A0Z = T.a[Z];
  Real AdotX = Tadot[X];
  Real AdotY = Tadot[Y];
  Real AdotZ = Tadot[Z];
  Real B0X = T.b[X];
  Real B0Y = T.b[Y];
  Real B0Z = T.b[Z];
  Real BdotX = Tbdot[X];
  Real BdotY = Tbdot[Y];
  Real BdotZ = Tbdot[Z];
  Real C0X = T.c[X];
  Real C0Y = T.c[Y];
  Real C0Z = T.c[Z];
  Real CdotX = Tcdot[X];
  Real CdotY = Tcdot[Y];
  Real CdotZ = Tcdot[Z];

  // setup terms for cubic polynomial
  Real d =  -A0Z*B0Y*P0X + A0Y*B0Z*P0X + A0Z*C0Y*P0X - B0Z*C0Y*P0X - 
            A0Y*C0Z*P0X + B0Y*C0Z*P0X + A0Z*B0X*P0Y - A0X*B0Z*P0Y - A0Z*C0X*P0Y 
            + B0Z*C0X*P0Y + A0X*C0Z*P0Y - B0X*C0Z*P0Y - A0Y*B0X*P0Z + 
            A0X*B0Y*P0Z + A0Y*C0X*P0Z - B0Y*C0X*P0Z - A0X*C0Y*P0Z + B0X*C0Y*P0Z;
  Real c = (-(AdotZ*B0Y*P0X) + AdotY*B0Z*P0X - A0Z*BdotY*P0X + 
       A0Y*BdotZ*P0X + AdotZ*C0Y*P0X - BdotZ*C0Y*P0X - 
       AdotY*C0Z*P0X + BdotY*C0Z*P0X + A0Z*CdotY*P0X - 
       B0Z*CdotY*P0X - A0Y*CdotZ*P0X + B0Y*CdotZ*P0X + 
       AdotZ*B0X*P0Y - AdotX*B0Z*P0Y + A0Z*BdotX*P0Y - 
       A0X*BdotZ*P0Y - AdotZ*C0X*P0Y + BdotZ*C0X*P0Y + 
       AdotX*C0Z*P0Y - BdotX*C0Z*P0Y - A0Z*CdotX*P0Y + 
       B0Z*CdotX*P0Y + A0X*CdotZ*P0Y - B0X*CdotZ*P0Y - 
       AdotY*B0X*P0Z + AdotX*B0Y*P0Z - A0Y*BdotX*P0Z + 
       A0X*BdotY*P0Z + AdotY*C0X*P0Z - BdotY*C0X*P0Z - 
       AdotX*C0Y*P0Z + BdotX*C0Y*P0Z + A0Y*CdotX*P0Z - 
       B0Y*CdotX*P0Z - A0X*CdotY*P0Z + B0X*CdotY*P0Z - 
       A0Z*B0Y*PdotX + A0Y*B0Z*PdotX + A0Z*C0Y*PdotX - 
       B0Z*C0Y*PdotX - A0Y*C0Z*PdotX + B0Y*C0Z*PdotX + 
       A0Z*B0X*PdotY - A0X*B0Z*PdotY - A0Z*C0X*PdotY + 
       B0Z*C0X*PdotY + A0X*C0Z*PdotY - B0X*C0Z*PdotY - 
       A0Y*B0X*PdotZ + A0X*B0Y*PdotZ + A0Y*C0X*PdotZ - 
       B0Y*C0X*PdotZ - A0X*C0Y*PdotZ + B0X*C0Y*PdotZ);
  Real b = (-(AdotZ*BdotY*P0X) + AdotY*BdotZ*P0X + 
       AdotZ*CdotY*P0X - BdotZ*CdotY*P0X - AdotY*CdotZ*P0X + 
       BdotY*CdotZ*P0X + AdotZ*BdotX*P0Y - AdotX*BdotZ*P0Y - 
       AdotZ*CdotX*P0Y + BdotZ*CdotX*P0Y + AdotX*CdotZ*P0Y - 
       BdotX*CdotZ*P0Y - AdotY*BdotX*P0Z + AdotX*BdotY*P0Z + 
       AdotY*CdotX*P0Z - BdotY*CdotX*P0Z - AdotX*CdotY*P0Z + 
       BdotX*CdotY*P0Z - AdotZ*B0Y*PdotX + AdotY*B0Z*PdotX - 
       A0Z*BdotY*PdotX + A0Y*BdotZ*PdotX + AdotZ*C0Y*PdotX - 
       BdotZ*C0Y*PdotX - AdotY*C0Z*PdotX + BdotY*C0Z*PdotX + 
       A0Z*CdotY*PdotX - B0Z*CdotY*PdotX - A0Y*CdotZ*PdotX + 
       B0Y*CdotZ*PdotX + AdotZ*B0X*PdotY - AdotX*B0Z*PdotY + 
       A0Z*BdotX*PdotY - A0X*BdotZ*PdotY - AdotZ*C0X*PdotY + 
       BdotZ*C0X*PdotY + AdotX*C0Z*PdotY - BdotX*C0Z*PdotY - 
       A0Z*CdotX*PdotY + B0Z*CdotX*PdotY + A0X*CdotZ*PdotY - 
       B0X*CdotZ*PdotY - AdotY*B0X*PdotZ + AdotX*B0Y*PdotZ - 
       A0Y*BdotX*PdotZ + A0X*BdotY*PdotZ + AdotY*C0X*PdotZ - 
       BdotY*C0X*PdotZ - AdotX*C0Y*PdotZ + BdotX*C0Y*PdotZ + 
       A0Y*CdotX*PdotZ - B0Y*CdotX*PdotZ - A0X*CdotY*PdotZ + 
       B0X*CdotY*PdotZ);
  Real a = (-(AdotZ*BdotY*PdotX) + AdotY*BdotZ*PdotX + 
       AdotZ*CdotY*PdotX - BdotZ*CdotY*PdotX - 
       AdotY*CdotZ*PdotX + BdotY*CdotZ*PdotX + 
       AdotZ*BdotX*PdotY - AdotX*BdotZ*PdotY - 
       AdotZ*CdotX*PdotY + BdotZ*CdotX*PdotY + 
       AdotX*CdotZ*PdotY - BdotX*CdotZ*PdotY - 
       AdotY*BdotX*PdotZ + AdotX*BdotY*PdotZ + 
       AdotY*CdotX*PdotZ - BdotY*CdotX*PdotZ - 
       AdotX*CdotY*PdotZ + BdotX*CdotY*PdotZ);

  // look for case of quadratic
  if (std::fabs(a) < NEAR_ZERO)
  {
    // find the roots of the quadratic eqn
    a = b;
    b = c;
    c = d;
    Real r1 = (-b + std::sqrt(b*b - 4*a*c))/(2*a);
    Real r2 = (-b - std::sqrt(b*b - 4*a*c))/(2*a);
 
    // if root is negative or greater than dt, make it inf
    if (r1 < 0.0 || r1 > dt || std::isnan(r1))
      r1 = INF;
    if (r2 < 0.0 || r2 > dt || std::isnan(r2))
      r2 = INF;

    // verify that point corresponding to r1 is inside triangle
    if (r1 < INF && T.determine_feature(p + pdot*r1) == Triangle::eNone)
      r1 = INF;

    // verify that point corresponding to r2 is inside triangle
    if (r2 < INF && T.determine_feature(p + pdot*r2) == Triangle::eNone)
      r2 = INF;

    // return the first root
    return std::min(r1, r2); 
  }
  else
  {
    // divide through by a
    b /= a;
    c /= a;
    d /= a;

    // find the (up to three) roots
    Real r[3];
    unsigned nroots = determine_cubic_roots(b, c, d, r);

    // if a root is negative or greater than dt, make it inf
    for (unsigned i=0; i< nroots; i++)
      if (r[i] < 0.0 || r[i] > dt || std::isnan(r[i]))
        r[i] = INF;

    // verify that points corresponding to roots are inside triangle
    for (unsigned i=0; i< nroots; i++)
      if (r[i] < INF && T.determine_feature(p + pdot*r[i]) == Triangle::eNone)
        r[i] = INF;

    // find and return the minimum 
    Real minimum = r[0];
    for (unsigned i=1; i< nroots; i++)
      if (r[i] < minimum)
        minimum = r[i];

    return minimum;
  }
}

/// Determines the contacts between a deformable body and a rigid body
/**
 * \param a the collision geometry for a deformable body
 * \param b the collision geometry for a rigid body
 * \param t the time-of-contact
 * \param dt the time step
 */
void MeshDCD::determine_contacts_deformable_rigid(CollisionGeometryPtr a, CollisionGeometryPtr b, Real t, Real dt, vector<Event>& contacts)
{
  Vector3 p;

  FILE_LOG(LOG_COLDET) << "MeshDCD::determine_contacts_deformable_rigid() entered" << endl;

  // get the bodies
  SingleBodyPtr sba = a->get_single_body();
  SingleBodyPtr sbb = b->get_single_body();

  // get the transform from and into b's frame
  const Matrix4& wTb = b->get_transform();

  // get the meshes from a and b
  const IndexedTriArray& mesh_a = *a->get_geometry()->get_mesh();
  const IndexedTriArray& mesh_b = *b->get_geometry()->get_mesh();

  // get all vertices of mesh a
  const vector<Vector3>& verts_a = mesh_a.get_vertices();

  // loop over all vertices
  for (unsigned i=0; i< verts_a.size(); i++)
  {
    // get the vertex -- it's a world coordinate (no transforms are used for
    // deformable bodies/geometries)
    const Vector3& v = verts_a[i];

    // get the velocity of the vertex relative to the rigid body
    Vector3 vdot = sba->calc_point_vel(v) - sbb->calc_point_vel(v);

    FILE_LOG(LOG_COLDET) << " -- testing vertex " << v << " with relative velocity: " << vdot << endl;

    // loop over all triangles in mesh b
    for (unsigned j=0; j< mesh_b.num_tris(); j++)
    {
      // get the triangle transformed into the world frame
      Triangle tri = Triangle::transform(mesh_b.get_triangle(j), wTb);

      // do line segment triangle intersection in b's frame
      Real t = calc_first_isect(tri, LineSeg3(v, v+vdot*dt), p);

      FILE_LOG(LOG_COLDET) << "  ++ against tri: " << tri << endl;
      FILE_LOG(LOG_COLDET) << "     intersection parameter: " << t << endl;

      // see whether to create a contact
      if (t <= (Real) 1.0)
        contacts.push_back(create_contact(t, a, b, p, vdot, tri));
    }
  }
  
  FILE_LOG(LOG_COLDET) << "MeshDCD::determine_contacts_deformable_rigid() exited" << endl;
}

/// Determines the contacts between a rigid body and a deformable body
/**
 * \param a the collision geometry for a rigid body
 * \param b the collision geometry for a deformable body
 * \param t the time-of-contact
 * \param dt the time step
 */
void MeshDCD::determine_contacts_rigid_deformable(CollisionGeometryPtr a, CollisionGeometryPtr b, Real t, Real dt, vector<Event>& contacts)
{
  // we can just use the deformable / deformable method for this
  determine_contacts_deformable(a, b, t, dt, contacts);
}

/// Determines the contacts between two geometries for deformable bodies
/**
 * \param a the collision geometry for a rigid body
 * \param b the collision geometry for a deformable body
 * \param t the time-of-contact
 * \param dt the time step
 * \note checks vertices from geometry a against geometry b
 */
void MeshDCD::determine_contacts_deformable(CollisionGeometryPtr a, CollisionGeometryPtr b, Real t, Real dt, vector<Event>& contacts)
{
  // get the transform for the second collision geometry
  const Matrix4& wTb = b->get_transform();

  // get the deformable bodies
  SingleBodyPtr sba = a->get_single_body();
  SingleBodyPtr sbb = b->get_single_body();

  // get the meshes from a and b
  const IndexedTriArray& mesh_a = *a->get_geometry()->get_mesh();
  const IndexedTriArray& mesh_b = *b->get_geometry()->get_mesh();

  // get all vertices of mesh a
  const vector<Vector3>& verts_a = mesh_a.get_vertices();

  // loop over all vertices
  for (unsigned i=0; i< verts_a.size(); i++)
  {
    // get the vertex
    const Vector3& v = verts_a[i];

    // get the velocity of the vertex
    Vector3 vdot = sba->calc_point_vel(v);

    // loop over all triangles in mesh b
    for (unsigned j=0; j< mesh_b.num_tris(); j++)
    {
      // if a == b (self collision check) and v is a vertex of the j'th
      // triangle, skip it: we don't want to check a vertex against its own tri
      if (a == b)
      {
        const IndexedTri& itri = mesh_b.get_facets()[j];
        if (itri.a == i || itri.b == i || itri.c == i)
          continue;
      }

      // get the triangle, transformed into global frame
      Triangle tri = Triangle::transform(mesh_b.get_triangle(j), wTb);

      // get the velocity of the three vertices of the triangle
      Vector3 adot = sbb->calc_point_vel(tri.a);
      Vector3 bdot = sbb->calc_point_vel(tri.b);
      Vector3 cdot = sbb->calc_point_vel(tri.c);

      // find the first time of intersection, if any
      Real t0 = calc_first_isect(v, vdot, tri, adot, bdot, cdot, t);
      if (t0 <= t)
      {
        // determine the point of contact at time t0 + t
        Vector3 p = v + vdot*t0;

        // determine the triangle at time t0 + t
        Triangle abc(tri.a + adot*t0, tri.b + bdot*t0, tri.c + cdot*t0);

        // create the contact
        contacts.push_back(create_contact(t0, a, b, p, vdot, abc));
      }
    }
  }
}

/// Determines the contacts between two geometries for rigid bodies
/*
 * \param a the collision geometry for a rigid body
 * \param b the collision geometry for a deformable body
 * \param t the time-of-contact
 * \param dt the time step
 */
void MeshDCD::determine_contacts_rigid(CollisionGeometryPtr a, CollisionGeometryPtr b, Real t, Real dt, vector<Event>& contacts)
{
  // get the two rigid bodies
  RigidBodyPtr rba = dynamic_pointer_cast<RigidBody>(a->get_single_body());
  RigidBodyPtr rbb = dynamic_pointer_cast<RigidBody>(b->get_single_body());

  // get the relative linear and angular velocities
  Vector3 rlv = rba->get_lvel() - rbb->get_lvel();

  // get the angular velocities of the two bodies
  const Vector3& omegaA = rba->get_avel();
  const Vector3& omegaB = rbb->get_avel();

  // get the meshes from a and b
  const IndexedTriArray& mesh_a = *a->get_geometry()->get_mesh();
  const IndexedTriArray& mesh_b = *b->get_geometry()->get_mesh();

  // check all tris of a against all tris of b
  for (unsigned i=0; i< mesh_a.num_tris(); i++)
  {
    // get the transformed triangle
    Triangle tA = mesh_a.get_triangle(i);
    Triangle TtA = Triangle::transform(tA, a->get_transform());

    for (unsigned j=0; j< mesh_b.num_tris(); j++)
    {
      // get the transformed triangle
      Triangle tB = mesh_b.get_triangle(j);
      Triangle TtB = Triangle::transform(tB, b->get_transform());

      // determine angular velocities at a's vertices
      Vector3 wAa = Vector3::cross(omegaA, TtA.a - rba->get_position())
                  - Vector3::cross(omegaB, TtA.a - rbb->get_position());
      Vector3 wAb = Vector3::cross(omegaA, TtA.b - rba->get_position())
                  - Vector3::cross(omegaB, TtA.b - rbb->get_position());
      Vector3 wAc = Vector3::cross(omegaA, TtA.c - rba->get_position())
                  - Vector3::cross(omegaB, TtA.c - rbb->get_position());

      // determine angular velocities at b's vertices
      Vector3 wBa = Vector3::cross(omegaA, TtB.a - rbb->get_position())
                  - Vector3::cross(omegaB, TtB.a - rbb->get_position());
      Vector3 wBb = Vector3::cross(omegaA, TtB.b - rbb->get_position());
                  - Vector3::cross(omegaB, TtB.b - rbb->get_position());
      Vector3 wBc = Vector3::cross(omegaA, TtB.c - rbb->get_position());
                  - Vector3::cross(omegaB, TtB.c - rbb->get_position());

      // determine point velocities at a's vertices
      Vector3 pAadot = rlv + wAa;
      Vector3 pAbdot = rlv + wAb;
      Vector3 pAcdot = rlv + wAc;

      // determine point velocities at b's vertices
      Vector3 pBadot = rlv + wBa;
      Vector3 pBbdot = rlv + wBb;
      Vector3 pBcdot = rlv + wBc;

      // determine line segments for a's vertices
      Vector3 pAa = TtA.a + pAadot*t;
      Vector3 pAb = TtA.b + pAbdot*t;
      Vector3 pAc = TtA.c + pAcdot*t;

      // determine line segments for a's vertices
      Vector3 pBa = TtB.a + pBadot*t;
      Vector3 pBb = TtB.b + pBbdot*t;
      Vector3 pBc = TtB.c + pBcdot*t;

      // calculate line segment triangle intersections
      Real t0;
      Vector3 p;

      // calculate intersections between vertex a of A and triangle B
      t0 = calc_first_isect(TtB, LineSeg3(TtA.a, pAa), p);
      if (t0 <= (Real) 1.0)
        contacts.push_back(create_contact(t0, a, b, p, TtA.a, TtB));

      // calculate intersections between vertex b of A and triangle B
      t0 = calc_first_isect(TtB, LineSeg3(TtA.b, pAb), p);
      if (t0 <= (Real) 1.0)
        contacts.push_back(create_contact(t0, a, b, p, TtA.b, TtB));

      // calculate intersections between vertex c of A and triangle B
      t0 = calc_first_isect(TtB, LineSeg3(TtA.c, pAc), p);
      if (t0 <= (Real) 1.0)
        contacts.push_back(create_contact(t0, a, b, p, TtA.c, TtB));

       // calculate intersections between vertex a of B and triangle A
      t0 = calc_first_isect(TtA, LineSeg3(TtB.a, pBa), p);
      if (t0 <= (Real) 1.0)
        contacts.push_back(create_contact(t0, a, b, p, TtB.a, TtA));

      // calculate intersections between vertex b of B and triangle A
      t0 = calc_first_isect(TtA, LineSeg3(TtB.b, pBb), p);
      if (t0 <= (Real) 1.0)
        contacts.push_back(create_contact(t0, a, b, p, TtB.b, TtA));

      // calculate intersections between vertex c of B and triangle A
      t = calc_first_isect(TtA, LineSeg3(TtB.c, pBc), p);
      if (t <= 1.0)
        contacts.push_back(create_contact(t, a, b, p, TtB.c, TtA));
    }
  }
}

/* version that attempted to do edge contacts
/// Determines the contacts between two geometries for rigid bodies
void MeshDCD::determine_contacts_rigid(CollisionGeometryPtr a, CollisionGeometryPtr b, Real dt, vector<Event>& contact_map)
{
  // get the two rigid bodies
  RigidBodyPtr rba = dynamic_pointer_cast<RigidBody>(a->get_single_body());
  RigidBodyPtr rbb = dynamic_pointer_cast<RigidBody>(b->get_single_body());

  // get the relative linear and angular velocities
  Vector3 rlv = rba->get_lvel() - rbb->get_lvel();

  // get the angular velocities of the two bodies
  const Vector3& omegaA = rba->get_avel();
  const Vector3& omegaB = rbb->get_avel();

  // get the meshes from a and b
  const IndexedTriArray& mesh_a = a->get_geometry()->get_mesh();
  const IndexedTriArray& mesh_b = b->get_geometry()->get_mesh();

  // check all tris of a against all tris of b
  for (unsigned i=0; i< mesh_a.num_tris(); i++)
  {
    // get the transformed triangle
    Triangle tA = mesh_a.get_triangle(i);
    Triangle TtA = Triangle::transform(tA, a->get_transform());

    for (unsigned j=0; j< mesh_b.num_tris(); j++)
    {
      // get the transformed triangle
      Triangle tB = mesh_b.get_triangle(j);
      Triangle TtB = Triangle::transform(tB, b->get_transform());

      // determine angular velocities at a's vertices
      Vector3 wAa = Vector3::cross(omegaA, TtA.a - rba->get_position())
                  - Vector3::cross(omegaB, TtA.a - rbb->get_position());
      Vector3 wAb = Vector3::cross(omegaA, TtA.b - rba->get_position())
                  - Vector3::cross(omegaB, TtA.b - rbb->get_position());
      Vector3 wAc = Vector3::cross(omegaA, TtA.c - rba->get_position())
                  - Vector3::cross(omegaB, TtA.c - rbb->get_position());

      // determine angular velocities at b's vertices
      Vector3 wBa = Vector3::cross(omegaA, TtB.a - rbb->get_position())
                  - Vector3::cross(omegaB, TtB.a - rbb->get_position());
      Vector3 wBb = Vector3::cross(omegaA, TtB.b - rbb->get_position());
                  - Vector3::cross(omegaB, TtB.b - rbb->get_position());
      Vector3 wBc = Vector3::cross(omegaA, TtB.c - rbb->get_position());
                  - Vector3::cross(omegaB, TtB.c - rbb->get_position());

      // determine point velocities at a's vertices
      Vector3 pAadot = rlv + wAa; 
      Vector3 pAbdot = rlv + wAb;
      Vector3 pAcdot = rlv + wAc;

      // determine point velocities at b's vertices
      Vector3 pBadot = rlv + wBa;
      Vector3 pBbdot = rlv + wBb;
      Vector3 pBcdot = rlv + wBc;

      // determine mean velocity vectors for a
      Vector3 pAabdot = (pAadot + pAbdot)*(Real) 0.5 * dt;
      Vector3 pAacdot = (pAadot + pAcdot)*(Real) 0.5 * dt;
      Vector3 pAbcdot = (pAbdot + pAcdot)*(Real) 0.5 * dt;

      // determine mean velocity vectors for b
      Vector3 pBabdot = (pBadot + pBbdot)*(Real) 0.5 * dt;
      Vector3 pBacdot = (pBadot + pBcdot)*(Real) 0.5 * dt;
      Vector3 pBbcdot = (pBbdot + pBcdot)*(Real) 0.5 * dt;

      // calculate line segment triangle intersections
      Real t;
      Vector3 p1, p2;

      // calculate intersections between edge ab of A and triangle B
      t = calc_first_isect(TtB, LineSeg3(TtA.a, TtA.a + pAabdot), LineSeg3(TtA.b, TtA.b + pAabdot), p1, p2);
      if (t <= 1.0)
      {
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p1, pAabdot, TtB)));
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p2, pAabdot, TtB)));
      }

      // calculate intersections between edge bc of A and triangle B
      t = calc_first_isect(TtB, LineSeg3(TtA.b, TtA.b + pAbcdot), LineSeg3(TtA.c, TtA.c + pAbcdot), p1, p2);
      if (t <= 1.0)
      {
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p1, pAbcdot, TtB)));
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p2, pAbcdot, TtB)));
      }

      // calculate intersections between edge ac of A and triangle B
      t = calc_first_isect(TtB, LineSeg3(TtA.a, TtA.a + pAacdot), LineSeg3(TtA.c, TtA.c + pAacdot), p1, p2);
      if (t <= 1.0)
      {
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p1, pAacdot, TtB)));
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p2, pAacdot, TtB)));
      }

       // calculate intersections between edge ab of B and triangle A
      t = calc_first_isect(TtA, LineSeg3(TtB.a, TtB.a+pBabdot), LineSeg3(TtB.b, TtB.b+pBabdot), p1, p2);
      if (t <= 1.0)
      {
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p1, pBabdot, TtA)));
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p2, pBabdot, TtA)));
      }

      // calculate intersections between edge bc of B and triangle A
      t = calc_first_isect(TtA, LineSeg3(TtB.b, TtB.b+pBbcdot), LineSeg3(TtB.c, TtB.c+pBbcdot), p1, p2);
      if (t <= 1.0)
      {
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p1, pBbcdot, TtA)));
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p2, pBbcdot, TtA)));
      }

      // calculate intersections between edge ac of B and triangle A
      t = calc_first_isect(TtA, LineSeg3(TtB.c, TtB.c+pBacdot), LineSeg3(TtB.a, TtB.a+pBacdot), p1, p2);
      if (t <= 1.0)
      {
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p1, pBacdot, TtA)));
        contact_map.insert(make_pair(t*dt, create_contact(a, b, p2, pBacdot, TtA)));
      }
    }
  }
}
*/

/// Creates a contact
Event MeshDCD::create_contact(Real toi, CollisionGeometryPtr a, CollisionGeometryPtr b, const Vector3& p, const Vector3& pdot, const Triangle& t)
{
  Event e;
  e.event_type = Event::eContact;
  e.contact_geom1 = a;
  e.contact_geom2 = b;
  e.contact_point = p;
  e.contact_normal = t.calc_normal();

  // see whether to reverse the normal
  if (pdot.dot(e.contact_normal) > 0)
    e.contact_normal = -e.contact_normal;

  return e;
}

/// Calculates the first point of intersection between two line segments and a triangle
/**
 * \param t the triangle to test against
 * \param s1 the first line segment to intersect
 * \param s2 the second line segment to intersect
 * \param p1 the first point of intersection
 * \param p2 the second point of intersection
 * \return the parameter of intersection
 */
Real MeshDCD::calc_first_isect(const Triangle& t, const LineSeg3& s1, const LineSeg3& s2, Vector3& p1, Vector3& p2)
{
  const unsigned TRI_VERTS = 3;
  const Real INF = std::numeric_limits<Real>::max();

  FILE_LOG(LOG_COLDET) << "MeshDCD::calc_first_isect() entered" << endl;
  FILE_LOG(LOG_COLDET) << "  triangle: " << t << endl;
  FILE_LOG(LOG_COLDET) << "  seg 1: " << s1.first << ", " << s1.second << endl;
  FILE_LOG(LOG_COLDET) << "  seg 2: " << s2.first << ", " << s2.second << endl;

  // check for colinear segments
  Vector3 a = s2.first - s1.first;
  Vector3 b = s2.second - s2.first;
  Vector3 ahat = Vector3::normalize(a);
  Vector3 bhat = Vector3::normalize(b);
  if (std::fabs(std::fabs(ahat.dot(bhat)) - (Real) 1.0) < std::sqrt(NEAR_ZERO)) 
  {
    Real t1 = calc_first_isect(t, s1, p1);
    Real t2 = calc_first_isect(t, s2, p2);
    if (t1 < (Real) 0.0 || t1 > (Real) 1.0)
    {
      t1 = INF;
      p1 = p2;
    }
    if (t2 < (Real) 0.0 || t2 > (Real) 1.0)
    {
      t2 = INF;
      p2 = p1;
    }
    return (t1 < t2) ? t1 : t2;
  }

  // get the plane of the rectangle
  Vector3 normal = Vector3::normalize(Vector3::cross(a, b));
  Plane plane(normal, Vector3::dot(normal, s2.first));

  // redetermine b to make it orthogonal to a and the normal
  b = -Vector3::cross(a, normal);

  // compute the signed distance of the triangle vertices to the plane; use an
  // epsilon-thick plane test
  int pos = 0, neg = 0, zero = 0, sign[TRI_VERTS];
  Real dist[TRI_VERTS];
  for (unsigned i=0; i< TRI_VERTS; i++)
  {
    dist[i] = plane.calc_signed_distance(t.get_vertex(i));
    if (dist[i] > NEAR_ZERO)
    {
      pos++;
      sign[i] = 1;
    }
    else if (dist[i] < -NEAR_ZERO)
    {
      neg++;
      sign[i] = -1;
    }
    else
    {
      zero++;
      sign[i] = 0;
    }
  }

  FILE_LOG(LOG_COLDET) << "  plane/triangle relations, pos: " << pos << " neg:  " << neg << " zero: " << zero << endl;

  // check whether all triangle vertices on one side
  if (pos == 3 || neg == 3)
    return INF;

  // if triangle and rectangle are coplanar, return no intersection
  if (zero == 3)
    return INF;

  // we ignore grazing contact between triangle and rectangle plane 
  if (pos == 0 || neg == 0)
    return INF;

  FILE_LOG(LOG_COLDET) << "... triangle transversely intersects plane; doing rectangle intersection..." << endl;

  Real tx;
  Vector3 isect0, isect1;
  if (zero == 0)
  {
    // determine the single vertex on one side of the plane
    int isign = (pos == 1 ? +1 : -1);
    if (sign[0] == isign)
    {
      tx = dist[0]/(dist[0] - dist[1]);
      isect0 = t.a + tx*(t.b - t.a);
      tx = dist[0]/(dist[0] - dist[2]);
      isect1 = t.a + tx*(t.c - t.a);
    }
    else if (sign[1] == isign)
    {
      tx = dist[1]/(dist[1] - dist[0]);
      isect0 = t.b + tx*(t.a - t.b);
      tx = dist[1]/(dist[1] - dist[2]);
      isect1 = t.b + tx*(t.c - t.b);
    }
    else
    {
      assert(sign[2] == isign);
      tx = dist[2]/(dist[2] - dist[0]);
      isect0 = t.c + tx*(t.a - t.c);
      tx = dist[2]/(dist[2] - dist[1]);
      isect1 = t.c + tx*(t.b - t.c);
    }

    return intersect_rect(normal, a, b, s1, s2, LineSeg3(isect0, isect1), p1, p2);
  }
  else
  {
    assert(zero == 1);
    if (sign[0] == 0)
    {
      tx = dist[2]/(dist[2] - dist[1]);
      isect0 = t.c + tx*(t.b - t.c);
      return intersect_rect(normal, a, b, s1, s2, LineSeg3(isect0, t.a), p1, p2);
    }
    else if (sign[1] == 0)
    {
      tx = dist[0]/(dist[0] - dist[2]);
      isect0 = t.a + tx*(t.c - t.a);
      return intersect_rect(normal, a, b, s1, s2, LineSeg3(isect0, t.b), p1, p2);
    }
    else
    {
      assert(sign[2] == 0);
      tx = dist[1]/(dist[1] - dist[0]);
      isect0 = t.b + tx*(t.a - t.b);
      return intersect_rect(normal, a, b, s1, s2, LineSeg3(isect0, t.c), p1, p2);
    }
  }

/*
  // at this point, triangle transversely intersects the rectangle plane;
  // compute the line segment of intersection between the triangle and the
  // plane.  Then test for intersection between this segment and the rectangle.
  Real tx;
  Vector3 isect0, isect1;
  if (zero == 0)
  {
    // determine the single vertex on one side of the plane
    int isign = (pos == 1 ? +1 : -1);
    if (sign[0] == isign)
    {
      tx = dist[0]/(dist[0] - dist[1]);
      isect0 = t.a + tx*(t.b - t.a);
      tx = dist[0]/(dist[0] - dist[2]);
      isect1 = t.a + tx*(t.c - t.a);
    }
    else if (sign[1] == isign)
    {
      tx = dist[1]/(dist[1] - dist[0]);
      isect0 = t.b + tx*(t.a - t.b);
      tx = dist[1]/(dist[1] - dist[2]);
      isect1 = t.b + tx*(t.c - t.b);
    }
    else
    {
      assert(sign[2] == isign);
      tx = dist[2]/(dist[2] - dist[0]);
      isect0 = t.c + tx*(t.a - t.c);
      tx = dist[2]/(dist[2] - dist[1]);
      isect1 = t.c + tx*(t.b - t.c);
    }

    return intersect_rect(normal, a, b, s1, s2, LineSeg3(isect0, isect1), p1, p2);
  }
  else
  {
    assert(zero == 1);
    if (sign[0] == 0)
    {
      tx = dist[2]/(dist[2] - dist[1]);
      isect0 = t.c + tx*(t.b - t.c);
      return intersect_rect(normal, a, b, s1, s2, LineSeg3(isect0, t.a), p1, p2);
    }
    else if (sign[1] == 0)
    {
      tx = dist[0]/(dist[0] - dist[2]);
      isect0 = t.a + tx*(t.c - t.a);
      return intersect_rect(normal, a, b, s1, s2, LineSeg3(isect0, t.b), p1, p2);
    }
    else
    {
      assert(sign[2] == 0);
      tx = dist[1]/(dist[1] - dist[0]);
      isect0 = t.b + tx*(t.a - t.b);
      return intersect_rect(normal, a, b, s1, s2, LineSeg3(isect0, t.c), p1, p2);
    }
  }
*/
}

/// Performs the intersection between a rectangle and a line segment
/**
 * \param normal normal to the plane that the rectangle is embedded within
 * \param axis1 the first axis and length of the rectangle 
 * \param axis2 the second axis and length of the rectangle
 * \param rs1 one edge of the rectangle
 * \param rs2 another, parallel edge to the rectangle (rs1 + axis1)
 * \param qs the line section to query for intersection
 * \param isect1 the first point of intersection
 * \param isect2 the second point of intersection
 * \return the lowest line segment parameter t of intersection or INF if no intersection
 */
Real MeshDCD::intersect_rect(const Vector3& normal, const Vector3& axis1, const Vector3& axis2, const LineSeg3& rs1, const LineSeg3& rs2, const LineSeg3& qs, Vector3& isect1, Vector3& isect2)
{
  const unsigned X = 0, Y = 1, Z = 2;
  const Real INF = std::numeric_limits<Real>::max();

std::cout << "intersect rect!" << std::endl;
  // determine the length of the rectangle axes
  Real l1 = axis1.norm();
  Real l2 = axis2.norm();

  // get the direction of the two axes of the rectangle
  Vector3 u1 = axis1/l1;
  Vector3 u2 = axis2/l2;

  // half the axis lengths
  l1 *= (Real) 0.5;
  l2 *= (Real) 0.5;

  // determine the center of the rectangle
  Vector3 center = rs1.first + axis1*(Real) 0.5 + axis2*(Real) 0.5;

  // determine the projection matrix to convert to box coordinates
  Matrix3 R;
  R.set_row(X, u1);
  R.set_row(Y, u2);
  R.set_row(Z, normal);

  // project the query segment to the rectangle coordinates
  Vector3 q1 = R * (qs.first - center);
  Vector3 q2 = R * (qs.second - center);

  // clip
  Real mt0 = (Real) 0.0, mt1 = (Real) 1.0;
  Real dx = q2[X] - q1[X];
  Real dy = q2[Y] - q1[Y];
  
  // clip against left edge
  Real mp = -dx, mq = -(-l1 - q1[X]);
  Real mr = mq/mp;
  if (std::fabs(mp) < NEAR_ZERO && mq < (Real) 0.0)
    return INF;
  if (mp < (Real) 0.0)
  {
    if (mr > mt1)
      return INF;
    else if (mr > mt0)
      mt0 = mr;
  }
  else if (mp > (Real) 0.0)
  {
    if (mr < mt0)
      return INF;
    else if (mr < mt1)
      mt1 = mr;
  }

  // clip against right edge
  mp = dx, mq = (l1 - q1[X]);
  mr = mq/mp;
  if (std::fabs(mp) < NEAR_ZERO && mq < (Real) 0.0)
    return INF;
  if (mp < (Real) 0.0)
  {
    if (mr > mt1)
      return INF;
    else if (mr > mt0)
      mt0 = mr;
  }
  else if (mp > (Real) 0.0)
  {
    if (mr < mt0)
      return INF;
    else if (mr < mt1)
      mt1 = mr;
  }

  // clip against bottom edge
  mp = -dy, mq = -(-l2 - q1[Y]);
  mr = mq/mp;
  if (std::fabs(mp) < NEAR_ZERO && mq < (Real) 0.0)
    return INF;
  if (mp < (Real) 0.0)
  {
    if (mr > mt1)
      return INF;
    else if (mr > mt0)
      mt0 = mr;
  }
  else if (mp > (Real) 0.0)
  {
    if (mr < mt0)
      return INF;
    else if (mr < mt1)
      mt1 = mr;
  }

  // clip against top edge
  mp = dy, mq = (l2 - q1[Y]);
  mr = mq/mp;
  if (std::fabs(mp) < NEAR_ZERO && mq < (Real) 0.0)
    return INF;
  if (mp < (Real) 0.0)
  {
    if (mr > mt1)
      return INF;
    else if (mr > mt0)
      mt0 = mr;
  }
  else if (mp > (Real) 0.0)
  {
    if (mr < mt0)
      return INF;
    else if (mr < mt1)
      mt1 = mr;
  }

  // determine new q1 and q2
  Vector3 dxyz(dx, dy, (Real) 0.0);
  q2 = q1 + dxyz*mt1;
  q1 += dxyz*mt0;

  // transform points back from box coordinates
  isect1 = Matrix3::transpose(R)*q1 + center; 
  isect2 = Matrix3::transpose(R)*q2 + center;

  // determine intersection parameter
  // q1 + (q2 - q1)*t = z
  // (q1 - z)/(q2 - q1) = t
  Real denom1 = (rs1.second - rs1.first).norm_sq();
  Real denom2 = (rs2.second - rs2.first).norm_sq();
  Real s1 = (rs1.first - isect1).norm_sq();
  Real s2 = (rs1.first - isect2).norm_sq();
  Real t1 = (rs2.first - isect1).norm_sq();
  Real t2 = (rs2.first - isect2).norm_sq(); 
  Real s = (s1 < s2) ? std::sqrt(s1/denom1) : std::sqrt(s2/denom1);
  Real t = (t1 < t2) ? std::sqrt(t1/denom2) : std::sqrt(t2/denom2);

  // finally, determine the true t
  if (s < (Real) 0.0)
    s = INF;
  if (t < (Real) 0.0)
    t = INF;
  if (s > 1 || t > 1)
    return INF;
  t += s;
  t *= (Real) 0.5;

  // now, determine the true intersection points
  isect1 = rs1.first + (rs1.second - rs1.first)*t;
  isect2 = rs2.first + (rs2.second - rs2.first)*t;

  return t;
}

/// Intersects two rectangles
Real MeshDCD::intersect_rects(const Vector3& normal, const Vector3 r1[4], const Vector3 r2[4], Vector3& isect1, Vector3& isect2)
{
  const unsigned RECT_VERTS = 4;
  const Real INF = std::numeric_limits<Real>::max();

  // determine the projection matrix from 3D to 2D 
  Matrix3 R = CompGeom::calc_3D_to_2D_matrix(normal);

  // determine the offset when converting from 2D back to 3D
  Real offset = CompGeom::determine_3D_to_2D_offset(r1[0], R);

  // convert r1 and r2 to 2D
  Vector2 r1_2D[RECT_VERTS], r2_2D[RECT_VERTS];
  for (unsigned i=0; i< RECT_VERTS; i++)
  {
    r1_2D[i] = CompGeom::to_2D(r1[i], R);
    r2_2D[i] = CompGeom::to_2D(r2[i], R);
  }

  // intersect the polygons -- determine the points of intersection
  Vector2 isects[RECT_VERTS*2];
  Vector2* iend = CompGeom::intersect_polygons(r1_2D, r1_2D+RECT_VERTS,
                                               r2_2D, r2_2D+RECT_VERTS,
                                               isects);

  // determine number of intersection points
  unsigned nisects = std::distance(isects, iend);
  if (nisects == 0)
    return INF;

  // project the points of intersection back to 3D
  Vector3 isects_3D[RECT_VERTS*2];
  for (unsigned i=0; i< nisects; i++)
    isects_3D[i] = CompGeom::to_3D(isects[i], Matrix3::transpose(R), offset);

  // determine the first times of intersection
  // r1[1]+(r1[2]-r1[1])*t = p
  // r1[0]+(r1[3]-r1[0])*t = p
  Real t[RECT_VERTS*2];
  for (unsigned i=0; i< nisects; i++)
  {
    t[i] = (isects_3D[i] - r1[1]).norm()/(r1[2]-r1[1]).norm();
    t[i] += (isects_3D[i] - r1[0]).norm()/(r1[3]-r1[0]).norm();
    t[i] *= (Real) 0.5;
  }

  // get minimum t
  Real mint = *std::min_element(t, t+nisects);

  // determine points of intersection
  isect1 = r1[1] + (r1[2] - r1[1])*mint;
  isect2 = r1[0] + (r1[3] - r1[0])*mint;

  return mint;
}

/// Calculates the first point of intersection between a line segment and a triangle
Real MeshDCD::calc_first_isect(const Triangle& t, const LineSeg3& seg, Vector3& p)
{
  const Real INF = std::numeric_limits<Real>::max();

  // setup the thick triangle
  ThickTriangle ttri(t, isect_tolerance);

  Real tnear;
  bool success = ttri.intersect_seg(seg, tnear, p);
  if (!success)
    return INF;
  else
    return tnear;
/*
  Vector3 p1, p2;

  FILE_LOG(LOG_COLDET) << "     segment: " << seg.first << " / " << seg.second << endl;

  // do the seg/tri intersection with a thick triangle
  CompGeom::SegTriIntersectType isect;
  try
  {
    isect = CompGeom::intersect_seg_tri(seg, t, p1, p2);
    FILE_LOG(LOG_COLDET) << "    intersection type: " << isect << endl;
  }
  catch (NumericalException e)
  {
    return std::numeric_limits<Real>::max();
  }

  // look for cases we care about
  if (isect == CompGeom::eSegTriFace || isect == CompGeom::eSegTriInclFace)
  {
    p = p1;
    return calc_param(seg, p);
  }
  else if (isect == CompGeom::eSegTriInside || isect == CompGeom::eSegTriPlanarIntersect)
  {
    Real t1 = calc_param(seg, p1);
    Real t2 = calc_param(seg, p2);
    if (t1 < t2)
    {
      p = p1;
      return t1;
    }
    else
    {
      p = p2;
      return t2;
    }
  }

  // still here?  no intersection
  return std::numeric_limits<Real>::max();
*/
}

/// Calculates the parameter t of a line segment
/**
 * p = seg.first + (seg.second-seg.first)*t
 */
Real MeshDCD::calc_param(const LineSeg3& seg, const Vector3& p)
{
  // t = (p - seg.first)/(seg.second - seg.first) 
  return (p - seg.first).norm() / (seg.second - seg.first).norm();
}

/// Determines whether a geometry for a deformable body is in self-collision
bool MeshDCD::is_collision(CollisionGeometryPtr cg)
{
  // get the primitive
  PrimitivePtr primitive = cg->get_geometry();

  // get the mesh
  const IndexedTriArray& tarray = *primitive->get_mesh();

  // iterate over all triangles
  for (unsigned i=0; i< tarray.num_tris(); i++)
  {
    // get the indexed triangle
    const IndexedTri& ti = tarray.get_facets()[i];

    // iterate over all triangles
    for (unsigned j=i+1; j< tarray.num_tris(); j++)
    {
      // get the indexed triangle
      const IndexedTri& tj = tarray.get_facets()[j];

      // if the triangles share an edge, do not check
      unsigned ncommon = 0;
      if (ti.a == tj.a || ti.a == tj.b || ti.a == tj.c)
        ncommon++;
      if (ti.b == tj.a || ti.b == tj.b || ti.b == tj.c)
        ncommon++;
      if (ti.c == tj.a || ti.c == tj.b || ti.c == tj.c)
        ncommon++;

      if (ncommon > 0)
        continue;

      // check triangle intersection
      if (CompGeom::query_intersect_tri_tri(tarray.get_triangle(i), tarray.get_triangle(j)))
        return true;
    }
  } 

  return false;
}

/// Determines whether two geometries are in collision
bool MeshDCD::is_collision(CollisionGeometryPtr a, CollisionGeometryPtr b)
{
  // get the first primitive 
  PrimitivePtr a_primitive = a->get_geometry();

  // get the transform and the inverse transform for this geometry
  const Matrix4& wTa = a->get_transform();
  Matrix4 aTw = Matrix4::inverse_transform(wTa);

  // get the second primitive 
  PrimitivePtr b_primitive = b->get_geometry();

  // get the two BV trees
  BVPtr bva = a_primitive->get_BVH_root();
  BVPtr bvb = b_primitive->get_BVH_root();

  // get the transform for b and its inverse
  const Matrix4& wTb = b->get_transform(); 

  // if intersects, add to colliding pairs
  return intersect_BV_trees(bva, bvb, aTw * wTb, a, b);
}

/// Implements Base::load_from_xml()
void MeshDCD::load_from_xml(XMLTreeConstPtr node, map<std::string, BasePtr>& id_map)
{
  map<std::string, BasePtr>::const_iterator id_iter;

  // verify that the node name is correct
  assert(strcasecmp(node->name.c_str(), "MeshDCD") == 0);

  // call parent method
  CollisionDetection::load_from_xml(node, id_map);

  // get the nu tolerance, if specified
  const XMLAttrib* nu_attr = node->get_attrib("eps-tolerance");
  if (nu_attr)
    this->eps_tolerance = nu_attr->get_real_value();
}

/// Implements Base::save_to_xml()
/**
 * \note neither the contact cache nor the pairs currently in collision are 
 *       saved
 */
void MeshDCD::save_to_xml(XMLTreePtr node, list<BaseConstPtr>& shared_objects) const
{
  // call parent save_to_xml() method first
  CollisionDetection::save_to_xml(node, shared_objects);

  // (re)set the node name
  node->name = "MeshDCD";

  // save the nu tolerance
  node->attribs.insert(XMLAttrib("eps-tolerance", eps_tolerance));
}


/// Does "broad phase" for discrete collision checking
/**
 * Since MeshDCD is just a debugging tool, broad phase is nonexistent.  This
 * function just adds all pairs of (non disabled) geometries for checking.
 */
void MeshDCD::broad_phase(vector<pair<CollisionGeometryPtr, CollisionGeometryPtr> >& to_check)
{
  FILE_LOG(LOG_COLDET) << "MeshDCD::broad_phase() entered" << std::endl;

  // clear the vector of pairs to check
  to_check.clear();

  // now setup pairs to check
  for (set<CollisionGeometryPtr>::const_iterator a = _geoms.begin(); a != _geoms.end(); a++)
  {
    // if a is disabled, skip it
    if (this->disabled.find(*a) != this->disabled.end())
      continue;

    // loop over all other geometries
    set<CollisionGeometryPtr>::const_iterator b = a;
    for (b++; b != _geoms.end(); b++)
    {
      // if b is disabled, skip it
      if (this->disabled.find(*b) != this->disabled.end())
        continue;
 
      // if the pair is disabled, continue looping
      if (this->disabled_pairs.find(make_sorted_pair(*a, *b)) != this->disabled_pairs.end())
        continue;

      // get the rigid bodies (if any) corresponding to the geometries
      RigidBodyPtr rb1 = dynamic_pointer_cast<RigidBody>((*a)->get_single_body());
      RigidBodyPtr rb2 = dynamic_pointer_cast<RigidBody>((*b)->get_single_body());

      // don't check pairs from the same rigid body
      if (rb1 && rb1 == rb2)
        continue;

      // if both rigid bodies are disabled, don't check
      if (rb1 && !rb1->is_enabled() && rb2 && !rb2->is_enabled())
        continue;

      // if we're here, we have a candidate for the narrow phase
      to_check.push_back(make_pair(*a, *b));
    }
  }
}

/****************************************************************************
 Methods for static geometry intersection testing begin 
****************************************************************************/

/// Determines whether there is a collision at the current position and orientation of the bodies
/**
 * \note the epsilon parameter is ignored
 */
bool MeshDCD::is_collision(Real epsilon)
{
  // clear the set of colliding pairs and list of colliding triangles
  colliding_pairs.clear();
  colliding_tris.clear();

  // iterate over geometries 
  for (std::set<CollisionGeometryPtr>::const_iterator i = _geoms.begin(); i != _geoms.end(); i++)
  {
    // get the first geometry and its primitive 
    CollisionGeometryPtr g1 = *i;
    PrimitivePtr g1_primitive = g1->get_geometry();

    // get the transform and the inverse transform for this geometry
    const Matrix4& wTg1 = g1->get_transform();
    Matrix4 g1Tw = Matrix4::inverse_transform(wTg1);

    // loop through all other geometries
    std::set<CollisionGeometryPtr>::const_iterator j = i;
    j++;
    for (; j != _geoms.end(); j++)
    {
      // get the second geometry and its primitive 
      CollisionGeometryPtr g2 = *j;
      PrimitivePtr g2_primitive = g2->get_geometry();

      // see whether to check
      if (!is_checked(g1, g2))
        continue; 

      // get the two BV trees
      BVPtr bv1 = g1_primitive->get_BVH_root();
      BVPtr bv2 = g2_primitive->get_BVH_root();

      // get the transform for g2 and its inverse
      const Matrix4& wTg2 = g2->get_transform(); 

      // if intersects, add to colliding pairs
      if (intersect_BV_trees(bv1, bv2, g1Tw * wTg2, g1, g2))
        colliding_pairs.insert(make_sorted_pair(g1, g2));
    } 
  }

  return !colliding_pairs.empty();
}

/// Intersects two BV trees; returns <b>true</b> if one (or more) pair of the underlying triangles intersects
bool MeshDCD::intersect_BV_trees(BVPtr a, BVPtr b, const Matrix4& aTb, CollisionGeometryPtr geom_a, CollisionGeometryPtr geom_b) 
{
  std::queue<tuple<BVPtr, BVPtr, bool> > q;

  // get address of the last colliding triangle pair on the queue
  CollidingTriPair* last = (colliding_tris.empty()) ? NULL : &colliding_tris.back();

  FILE_LOG(LOG_COLDET) << "MeshDCD::intersect_BV_trees() entered" << endl;

  // intersect the BVs at the top level
  if (!BV::intersects(a, b, aTb))
  {
    FILE_LOG(LOG_COLDET) << "  no intersection at top-level BVs" << endl;
    FILE_LOG(LOG_COLDET) << "MeshDCD::intersect_BV_trees() exited" << endl;

    return false;
  }

  // add a and b to the queue
  q.push(make_tuple(a, b, false));

  // drill down alternatingly until both trees are exhausted
  while (!q.empty())
  {
    // get the two nodes off of the front of the queue
    BVPtr bv1 = q.front().get<0>();
    BVPtr bv2 = q.front().get<1>();
    bool rev = q.front().get<2>();

    // no longer need the elm on the front of the queue
    q.pop();

    // check for bv1 and bv2 both leafs
    if (bv1->is_leaf() && bv2->is_leaf())
    {
      if (!rev)
        intersect_BV_leafs(bv1, bv2, aTb, geom_a, geom_b, std::back_inserter(colliding_tris));
      else
        intersect_BV_leafs(bv2, bv1, aTb, geom_a, geom_b, std::back_inserter(colliding_tris));

      // see whether we want to exit early
      if (mode == eFirstContact && !colliding_tris.empty() && last != &colliding_tris.back())
        return true;
    }

    // drill down through bv2, if possible
    if (bv2->is_leaf())
    {
      // check the children of o1
      BOOST_FOREACH(BVPtr child, bv1->children)
        if ((!rev && BV::intersects(child, bv2, aTb)) || (rev && BV::intersects(bv2, child, aTb)))
          q.push(make_tuple(child, bv2, rev));
    }
    else
      BOOST_FOREACH(BVPtr child, bv2->children)
      {
        if ((!rev && BV::intersects(bv1, child, aTb)) || (rev && BV::intersects(child, bv1, aTb)))
          q.push(make_tuple(child, bv1, !rev));
      }
  }

  // see whether we have an intersection
  if (!colliding_tris.empty() && last != &colliding_tris.back())
    return true;

  FILE_LOG(LOG_COLDET) << "  -- all intersection checks passed; no intersection" << endl;
  FILE_LOG(LOG_COLDET) << "MeshDCD::intersect_BV_trees() exited" << endl;

  return false;
} 

/****************************************************************************
 Methods for static geometry intersection testing end 
****************************************************************************/

