/****************************************************************************
 * Copyright 2005 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _SIMULATOR_H
#define _SIMULATOR_H

#include <list>
#include <map>
#include <set>
#include <boost/shared_ptr.hpp>

#include <Moby/Base.h>
#include <Moby/Log.h>
#include <Moby/Integrator.h>
#include <Moby/RigidBody.h>
#include <Moby/VectorN.h>
#include <Moby/ArticulatedBody.h>

namespace osg { 
  class Node;
  class Group; 
}

namespace Moby {

class RigidBody;
class ArticulatedBody;
class VisualizationData;
class DynamicBody;

/// Simulator for both unarticulated and articulated rigid bodies without contact
/**
 * Class used for performing dynamics simulation of rigid bodies without contact.
 * Rigid body simulation of articulated bodies is supported using both
 * maximal and reduced coordinate approaches.
 */
class Simulator : public virtual Base
{
  public:
    Simulator();
    virtual ~Simulator(); 
    virtual Real step(Real step_size);
    DynamicBodyPtr find_dynamic_body(const std::string& name) const;
    void add_dynamic_body(DynamicBodyPtr body);
    void remove_dynamic_body(DynamicBodyPtr body);
    void update_visualization();
    virtual void save_to_xml(XMLTreePtr node, std::list<BaseConstPtr>& shared_objects) const;
    virtual void load_from_xml(XMLTreeConstPtr node, std::map<std::string, BasePtr>& id_map);  

    /// The current simulation time
    Real current_time;

    /// The integrator used to step the simulation
    boost::shared_ptr<Integrator<VectorN> > integrator;

    /// Gets the list of dynamic bodies in the simulator
    /**
     * \note if a dynamic body is articulated, only the articulated body is
     *       returned, not the links
     */
    const std::vector<DynamicBodyPtr>& get_dynamic_bodies() const { return _bodies; }

    void add_transient_vdata(osg::Node* vdata);

    /// Gets the persistent visualization data
    osg::Node* get_persistent_vdata() const { return (osg::Node*) _persistent_vdata; } 

    /// Gets the transient (one-step) visualization data
    osg::Node* get_transient_vdata() const { return (osg::Node*) _transient_vdata; } 

    /// Callback function after a step is completed
    void (*post_step_callback_fn)(Simulator* s);

  protected:
    osg::Group* _persistent_vdata;
    osg::Group* _transient_vdata;

    /// The set of bodies in the simulation
    std::vector<DynamicBodyPtr> _bodies;
  
    template <class ForwardIterator>
    Real integrate(Real step_size, ForwardIterator begin, ForwardIterator end);

    /// Integrates all dynamic bodies
    Real integrate(Real step_size) { return integrate(step_size, _bodies.begin(), _bodies.end()); }
}; // end class

// include inline functions
#include "Simulator.inl"

} // end namespace 

#endif

