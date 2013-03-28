/****************************************************************************
 * Copyright 2005 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#include <iostream>
#ifdef USE_OSG
#include <osg/Group>
#endif
#include <Moby/RecurrentForce.h>
#include <Moby/ArticulatedBody.h>
#include <Moby/RCArticulatedBody.h>
#include <Moby/RigidBody.h>
#include <Moby/DeformableBody.h>
#include <Moby/Joint.h>
#include <Moby/XMLTree.h>
#include <Moby/Simulator.h>

using namespace Moby;
using std::vector;
using boost::shared_ptr;
using boost::dynamic_pointer_cast;

/// Sets up the simulator
/**
 * The simulator properties are set as follows:
 * <ul>
 * <li>simulator time = 0</li>
 * <li>no integrator</li>
 * </ul>
 */
Simulator::Simulator()
{
  this->current_time = 0;
  post_step_callback_fn = NULL;

  // setup the persistent and transient visualization data
  #ifdef USE_OSG
  _persistent_vdata = new osg::Group;
  _transient_vdata = new osg::Group;

  // add references to the visualization data
  _persistent_vdata->ref();
  _transient_vdata->ref();
  #endif
}

Simulator::~Simulator()
{
  #ifdef USE_OSG
  _persistent_vdata->unref();
  _transient_vdata->unref();
  #endif
}

/// Steps the Simulator forward in time without contact
/**
 * This pseudocode was inspired from [Baraff 1997] and [Mirtich 1996].
 * \param step_size the step size
 * \return step_size
 */
Real Simulator::step(Real step_size)
{
  #ifdef USE_OSG
  // clear one-step visualization data
  _transient_vdata->removeChildren(0, _transient_vdata->getNumChildren());
  #endif

  // compute forward dynamics and integrate 
  current_time += integrate(step_size);

  // call the callback
  if (post_step_callback_fn)
    post_step_callback_fn(this);

  return step_size;
}

/// Finds the dynamic body in the simulator, if any
/**
 * Searches unarticulated bodies, articulated bodies, and links of
 * articulated bodies.
 */
DynamicBodyPtr Simulator::find_dynamic_body(const std::string& name) const
{
  BOOST_FOREACH(DynamicBodyPtr body, _bodies)
    if (body->id == name)
      return body;

  // failed, look through all links of articulated bodies
  BOOST_FOREACH(DynamicBodyPtr body, _bodies)
  {
    // try to cast the i'th DynamicBody as an ArticulatedBody
    ArticulatedBodyPtr ab = dynamic_pointer_cast<ArticulatedBody>(body);
    if (!ab)
      continue;
    
    // it was castable, get all links
    const vector<RigidBodyPtr>& links = ab->get_links();
    
    // look through all links for one matching the name
    BOOST_FOREACH(RigidBodyPtr rb, links)  
      if (rb->id == name)
        return rb;
  }
    
  return DynamicBodyPtr();
}

/// Removes a dynamic body from the simulator
void Simulator::remove_dynamic_body(DynamicBodyPtr body)
{
  // remove the body from the list of bodies
  std::vector<DynamicBodyPtr>::iterator i = std::find(_bodies.begin(), _bodies.end(), body);
  if (i == _bodies.end())
    return;
  else
    _bodies.erase(i);

  #ifdef USE_OSG
  // see whether the body is articulated 
  ArticulatedBodyPtr abody = dynamic_pointer_cast<ArticulatedBody>(body);
  if (abody)
  {
    
    // remove visualization data for all links to the persistent visualization data
    const vector<RigidBodyPtr>& links = abody->get_links();
    for (unsigned i=0; i< links.size(); i++)
    {
      osg::Node* link_vdata = links[i]->get_visualization_data();
      if (link_vdata)
        _persistent_vdata->removeChild(link_vdata);
    }
    
    // remove visualization data for all joints
    const vector<JointPtr>& joints = abody->get_joints();
    for (unsigned i=0; i< joints.size(); i++)
    {
      osg::Node* joint_vdata = joints[i]->get_visualization_data();
      if (joint_vdata)
        _persistent_vdata->removeChild(joint_vdata);
    }
  }
  else
  {
    // try to cast it as a deformable body
    shared_ptr<DeformableBody> db = dynamic_pointer_cast<DeformableBody>(body);
    if (db)
    {
      osg::Node* db_vdata = db->get_visualization_data();
      if (db_vdata)
        _persistent_vdata->removeChild(db_vdata);
    }
    else
    {
      // try rigid body
      RigidBodyPtr rigidbody = dynamic_pointer_cast<RigidBody>(body);
      assert(rigidbody);
      osg::Node* rb_vdata = rigidbody->get_visualization_data();
      if (rb_vdata)
        _persistent_vdata->removeChild(rb_vdata);
    }
  }
  #endif
}

/// Adds a dynamic body to the simulator
/**
 * \pre list of bodies is sorted
 */
void Simulator::add_dynamic_body(DynamicBodyPtr body) 
{
  // if the body is already present in the simulator, skip it
  if (std::find(_bodies.begin(), _bodies.end(), body) != _bodies.end())
    return;

  #ifdef USE_OSG
  // see whether the body is articulated 
  ArticulatedBodyPtr abody = dynamic_pointer_cast<ArticulatedBody>(body);
  if (abody)
  {
    // add visualization data for all links to the persistent visualization data
    const vector<RigidBodyPtr>& links = abody->get_links();
    for (unsigned i=0; i< links.size(); i++)
    {
      osg::Node* link_vdata = links[i]->get_visualization_data();
      if (link_vdata)
        _persistent_vdata->addChild(link_vdata);
    }
    
    // get visualization data for all joints
    const vector<JointPtr>& joints = abody->get_joints();
    for (unsigned i=0; i< joints.size(); i++)
    {
      osg::Node* joint_vdata = joints[i]->get_visualization_data();
      if (joint_vdata)
        _persistent_vdata->addChild(joint_vdata);
    }
  }
  else
  {
    // try to cast it as a deformable body
    shared_ptr<DeformableBody> db = dynamic_pointer_cast<DeformableBody>(body);
    if (db)
    {
      osg::Node* db_vdata = db->get_visualization_data();
      if (db_vdata)
        _persistent_vdata->addChild(db_vdata);
    }
    else
    {
      // it must be a rigid body
      RigidBodyPtr rigidbody = dynamic_pointer_cast<RigidBody>(body);
      assert(rigidbody);

      // get the visualization data and add it to the simulator
      osg::Node* rb_vdata = rigidbody->get_visualization_data();
      if (rb_vdata)
        _persistent_vdata->addChild(rb_vdata);
    }
  }
  #endif
  
  // add the body to the list of bodies and sort the list of bodies
  _bodies.push_back(body); 
  std::sort(_bodies.begin(), _bodies.end());
}

/// Updates all visualization under the simulator
void Simulator::update_visualization()
{
  BOOST_FOREACH(DynamicBodyPtr body, _bodies)
    body->update_visualization();
}

/// Adds transient visualization data to the simulator
void Simulator::add_transient_vdata(osg::Node* vdata)
{
  #ifdef USE_OSG
  _transient_vdata->addChild(vdata);
  #endif
}

/// Implements Base::load_from_xml()
void Simulator::load_from_xml(XMLTreeConstPtr node, std::map<std::string, BasePtr>& id_map)
{
  std::list<XMLTreeConstPtr> child_nodes;
  std::map<std::string, BasePtr>::const_iterator id_iter;

  // load parent data
  Base::load_from_xml(node, id_map);

  // ***********************************************************************
  // don't verify that the node is correct, b/c Simulator can be subclassed
  // ***********************************************************************
  
  // get the current time 
  const XMLAttrib* time_attr = node->get_attrib("current-time");
  if (time_attr)
    this->current_time = time_attr->get_real_value();

  // get the integrator, if specified
  const XMLAttrib* int_id_attr = node->get_attrib("integrator-id");
  if (int_id_attr)
  {
    const std::string& id = int_id_attr->get_string_value(); 
    if ((id_iter = id_map.find(id)) == id_map.end())
    {
      std::cerr << "Simulator::load_from_xml() - could not find" << std::endl;
      std::cerr << "  integrator w/ID: " << id << " from offending node: ";
      std::cerr << std::endl << *node;
    }
    else
      integrator = dynamic_pointer_cast<Integrator<VectorN> >(id_iter->second);
  }

  // get all dynamic bodies used in the simulator
  child_nodes = node->find_child_nodes("DynamicBody");
  if (!child_nodes.empty())
  {
    // safe to clear the vector of bodies
    _bodies.clear();

    // process all DynamicBody child nodes
    for (std::list<XMLTreeConstPtr>::const_iterator i = child_nodes.begin(); i != child_nodes.end(); i++)
    {
      // verify that the dynamic-body-id attribute exists
      const XMLAttrib* id_attr = (*i)->get_attrib("dynamic-body-id");

      // make sure that the ID exists
      if (!id_attr)
      {
        std::cerr << "Simulator::load_from_xml() - no dynamic-body-id ";
        std::cerr << "attribute in " << std::endl << "  offending node: ";
        std::cerr << *i << std::endl;
        continue;
      }

      // look for the dynamic body with that ID
      const std::string& id = id_attr->get_string_value(); 
      if ((id_iter = id_map.find(id))== id_map.end())
      {
        std::cerr << "Simulator::load_from_xml() - could not find" << std::endl;
        std::cerr << "  dynamic body w/ID: '" << id << "' from offending node:";
        std::cerr << std::endl << *node;
      }
      else
        add_dynamic_body(dynamic_pointer_cast<DynamicBody>(id_iter->second));
    }
  }

  // get all recurrent forces used in the simulator -- note: this must be done
  // *after* all bodies have been loaded
  child_nodes = node->find_child_nodes("RecurrentForce");
  if (!child_nodes.empty())
  {
    // process all child nodes
    for (std::list<XMLTreeConstPtr>::const_iterator i = child_nodes.begin(); i != child_nodes.end(); i++)
    {
      // verify that the dynamic-body-id attribute exists
      const XMLAttrib* id_attr = (*i)->get_attrib("recurrent-force-id");

      // make sure that the ID exists
      if (!id_attr)
      {
        std::cerr << "Simulator::load_from_xml() - no recurrent-force-id ";
        std::cerr << "attribute in tag: " << node << std::endl;
        continue;
      }

      // look for the recurrent force with that ID
      const std::string& id = id_attr->get_string_value(); 
      if ((id_iter = id_map.find(id)) == id_map.end())
      {
        std::cerr << "Simulator::load_from_xml() - could not find" << std::endl;
        std::cerr << "  recurrent force w/ID: " << id << " from offending node: " << std::endl << *node;
      }
      else
      {
        RecurrentForcePtr rf = dynamic_pointer_cast<RecurrentForce>(id_iter->second);
        BOOST_FOREACH(DynamicBodyPtr db, _bodies)
          db->get_recurrent_forces().push_back(rf);
      }
    }
  }
}

/// Implements Base::save_to_xml()
void Simulator::save_to_xml(XMLTreePtr node, std::list<BaseConstPtr>& shared_objects) const
{
  // call the parent save_to_xml() method
  Base::save_to_xml(node, shared_objects);

  // rename the node
  node->name = "Simulator";

  // save the current time 
  node->attribs.insert(XMLAttrib("current-time", this->current_time));

  // save the ID of the integrator
  if (integrator)
  {
    node->attribs.insert(XMLAttrib("integrator-id", integrator->id));
    shared_objects.push_back(integrator);
  }

  // save the IDs of all dynamic bodies in the simulator
  BOOST_FOREACH(DynamicBodyPtr body, _bodies)
  {
    XMLTreePtr child_node(new XMLTree("DynamicBody"));
    node->add_child(child_node);
    child_node->attribs.insert(XMLAttrib("dynamic-body-id", body->id));
    if (!body)
      throw std::runtime_error("dynamic-body-id does not belong to a dynamic body");
    shared_objects.push_back(body);
  }
}

