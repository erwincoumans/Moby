/****************************************************************************
 * Copyright 2010 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#include <iostream>
#include <Moby/XMLTree.h>
#include <Moby/SingleBody.h>
#include <Moby/RigidBody.h>
#include <Moby/ArticulatedBody.h>
#include <Moby/DampingForce.h>

using namespace Moby;
using std::map;
using std::cerr;
using std::endl;
using std::list;
using boost::dynamic_pointer_cast;
using std::vector;

/// Copy constructor
DampingForce::DampingForce(const DampingForce& source)
{
  // copy the maps
  this->kl = source.kl;
  this->ka = source.ka;
}

/// Adds gravity to a body
void DampingForce::add_force(DynamicBodyPtr body)
{
  // get the linear and angular damping constants
  map<DynamicBodyPtr, Real>::const_iterator diter;
  Real ldamp = (Real) 0.0, adamp = (Real) 0.0;
  Real lsqdamp = (Real) 0.0, asqdamp = (Real) 0.0;
  if ((diter = kl.find(body)) != kl.end())
    ldamp = diter->second;
  if ((diter = ka.find(body)) != ka.end())
    adamp = diter->second; 
  if ((diter = klsq.find(body)) != klsq.end())
    lsqdamp = diter->second;
  if ((diter = kasq.find(body)) != kasq.end())
    asqdamp = diter->second;

  // check whether the body is a single body 
  SingleBodyPtr sb = dynamic_pointer_cast<SingleBody>(body);
  if (sb)
  {
    // see whether the body is a rigid body
    RigidBodyPtr rb = dynamic_pointer_cast<RigidBody>(body);
    if (rb)
    {
      rb->add_force(rb->get_lvel() * -ldamp);
      rb->add_force(rb->get_lvel() * rb->get_lvel().norm() * -lsqdamp);
      rb->add_torque(rb->get_avel() * -adamp);
      rb->add_torque(rb->get_avel() * rb->get_avel().norm() * -asqdamp);
    }
    else
    {
      // body is deformable, skip it
    }
  }
  else
  {
    // it's an articulated body, get it as such
    ArticulatedBodyPtr ab = dynamic_pointer_cast<ArticulatedBody>(body);
      
    // get the vector of links
    const std::vector<RigidBodyPtr>& links = ab->get_links();
      
    // apply damping force to all links
    BOOST_FOREACH(RigidBodyPtr rb, links)
    {
      // get linear and angular damping constants for this body, if different
      Real ldamp2 = ldamp, adamp2 = adamp;
      Real lsqdamp2 = lsqdamp, asqdamp2 = asqdamp;
      if ((diter = kl.find(rb)) != kl.end())
        ldamp2 = diter->second;
      if ((diter = ka.find(rb)) != ka.end())
        adamp2 = diter->second; 
      if ((diter = klsq.find(rb)) != klsq.end())
        lsqdamp2 = diter->second;
      if ((diter = kasq.find(rb)) != kasq.end())
        asqdamp2 = diter->second; 

      rb->add_force(rb->get_lvel() * -ldamp2);
      rb->add_torque(rb->get_avel() * -adamp2);
      rb->add_force(rb->get_lvel() * rb->get_lvel().norm() * -lsqdamp);
      rb->add_torque(rb->get_avel() * rb->get_avel().norm() * -asqdamp);
    }        
  }
}

/// Implements Base::load_from_xml()
void DampingForce::load_from_xml(XMLTreeConstPtr node, map<std::string, BasePtr>& id_map)
{
  map<std::string, BasePtr>::const_iterator id_iter;

  // load XML data for the parent class
  RecurrentForce::load_from_xml(node, id_map);

  // verify that the name of this node is correct
  assert(strcasecmp(node->name.c_str(), "DampingForce") == 0);

  // get the sets of gains
  list<XMLTreeConstPtr> gain_nodes = node->find_child_nodes("Gains");
  if (!gain_nodes.empty())
  {
    // get the gains
    for (list<XMLTreeConstPtr>::const_iterator i = gain_nodes.begin(); i != gain_nodes.end(); i++)
    {
      // make sure the child node has the body ID
      const XMLAttrib* id_attr = (*i)->get_attrib("body-id");
      if (!id_attr)
      {
        cerr << "DampingForce::load_from_xml() - Gains node has no body-id attribute!" << endl;
        cerr << "  offending node: " << endl << *node;
        continue;
      }

      // get the ID
      const std::string& ID = id_attr->get_string_value();

      // attempt to find the ID
      if ((id_iter = id_map.find(ID)) == id_map.end())
      {
        cerr << "DampingForce::load_from_xml() - body id: " << ID << " not found!" << endl;
        cerr << "  offending node: " << endl << *node;
        continue;
      }

      // get the body
      DynamicBodyPtr body = dynamic_pointer_cast<DynamicBody>(id_iter->second);
      if (!body)
      {
        cerr << "DampingForce::load_from_xml() - object with id: " << ID << " not castable " << endl;
        cerr << "  to type DynamicBody; offending node: " << endl << *node;
        continue;
      }

      // try to read the gains in
      const XMLAttrib* lgain_attr = (*i)->get_attrib("klinear");
      if (lgain_attr) 
        kl[body] = lgain_attr->get_real_value();
      const XMLAttrib* again_attr = (*i)->get_attrib("kangular");
      if (again_attr) 
        ka[body] = again_attr->get_real_value();
      const XMLAttrib* lgainsq_attr = (*i)->get_attrib("klinear-sq");
      if (lgainsq_attr) 
        klsq[body] = lgainsq_attr->get_real_value();
      const XMLAttrib* againsq_attr = (*i)->get_attrib("kangular-sq");
      if (againsq_attr) 
        kasq[body] = againsq_attr->get_real_value();
    }  
  }    
}

/// Implements Base::save_to_xml()
void DampingForce::save_to_xml(XMLTreePtr node, std::list<BaseConstPtr>& shared_objects) const 
{
  // save XML data from the parent class
  RecurrentForce::save_to_xml(node, shared_objects);

  // (re)set the name of this node
  node->name = "DampingForce";

  // get list of unique bodies
  std::list<DynamicBodyPtr> bodies;
  for (map<DynamicBodyPtr, Real>::const_iterator i = kl.begin(); i != kl.end(); i++)
    bodies.push_back(i->first);
  for (map<DynamicBodyPtr, Real>::const_iterator i = ka.begin(); i != ka.end(); i++)
    bodies.push_back(i->first);
  bodies.erase(std::unique(bodies.begin(), bodies.end()), bodies.end());

  // iterate through all bodies
  BOOST_FOREACH(DynamicBodyPtr db, bodies)
  {
    // create a new node
    XMLTreePtr new_node(new XMLTree("Gains"));
    node->add_child(new_node);
    
    // get the gains for the body
    Real lgain = (Real) 0.0, again = (Real) 0.0;
    Real lsqgain = (Real) 0.0, asqgain = (Real) 0.0;
    map<DynamicBodyPtr, Real>::const_iterator giter;
    if ((giter = kl.find(db)) != kl.end())
      lgain = giter->second;
    if ((giter = ka.find(db)) != ka.end())
      again = giter->second;
    if ((giter = klsq.find(db)) != klsq.end())
      lsqgain = giter->second;
    if ((giter = kasq.find(db)) != kasq.end())
      asqgain = giter->second;

    // save the gains
    new_node->attribs.insert(XMLAttrib("body-id", db->id));
    new_node->attribs.insert(XMLAttrib("klinear", lgain));
    new_node->attribs.insert(XMLAttrib("kangular", again));
    new_node->attribs.insert(XMLAttrib("klinear-sq", lsqgain));
    new_node->attribs.insert(XMLAttrib("kangular-sq", asqgain));
  }
}
 
