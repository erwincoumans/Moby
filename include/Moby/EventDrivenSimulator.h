/****************************************************************************
 * Copyright 2011 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _EVENT_SIMULATOR_H
#define _EVENT_SIMULATOR_H

#include <map>
#include <Moby/sorted_pair>
#include <Moby/Simulator.h>
#include <Moby/ImpactEventHandler.h>
#include <Moby/Event.h>

namespace Moby {

class ContactParameters;
class CollisionDetection;
class CollisionGeometry;

/// An event-driven simulator
class EventDrivenSimulator : public Simulator
{
  friend class CollisionDetection;
  friend class DeformableCCD;

  private:
    // class for comparing two events for purposes of setting event tolerances
    class EventCompare
    {
      public: bool operator()(const Event& a, const Event& b)
      {
        // check for event type disparity
        if (a.event_type != b.event_type)
          return a.event_type < b.event_type;

        // event types are the same - now each event type must be processed
        // separately
        if (a.event_type == Event::eContact)
        {
          // see whether the bodies are the same
          return make_sorted_pair(a.contact_geom1->get_single_body(), a.contact_geom2->get_single_body()) < make_sorted_pair(b.contact_geom1->get_single_body(), b.contact_geom2->get_single_body());
        }
        else if (a.event_type == Event::eLimit)
        {
          // check whether the joints are the same
          if (a.limit_joint != b.limit_joint)
            return a.limit_joint < b.limit_joint;

          // check whether the limits are the same
          if (a.limit_upper != b.limit_upper)
            return a.limit_upper < b.limit_upper;

          // finally, check whether the DOFs are the same
          return a.limit_dof < b.limit_dof;
        }
        else // event is a constraint event
        {
          return a.constraint_joint < b.constraint_joint; 
        }  
      }
    };

  public:
    EventDrivenSimulator();
    virtual ~EventDrivenSimulator() {}
    virtual void load_from_xml(XMLTreeConstPtr node, std::map<std::string, BasePtr>& id_map);
    virtual void save_to_xml(XMLTreePtr node, std::list<BaseConstPtr>& shared_objects) const;
    virtual void output_object_state(std::ostream& out) const;
    virtual Real step(Real dt);

    /// Gets the shared pointer for this
    boost::shared_ptr<EventDrivenSimulator> get_this() { return boost::dynamic_pointer_cast<EventDrivenSimulator>(shared_from_this()); }
    
    /// The collision detection mechanisms
    std::list<boost::shared_ptr<CollisionDetection> > collision_detectors;

    /// Callback function after a mini-step is completed
    void (*post_mini_step_callback_fn)(EventDrivenSimulator* s);

    /// The callback function (called when events have been determined)
    /**
     * The callback function can remove events from the list, which will disable
     * their processing (however, doing so may prevent the simulation from
     * making progress, as the simulator attempts to disallow violations.
     */
    void (*event_callback_fn)(std::vector<Event>&, boost::shared_ptr<void>);

    /// The callback function (called after event impulses are applied)
    void (*event_post_impulse_callback_fn)(const std::vector<Event>&, boost::shared_ptr<void>);

    /// Data passed to event callback
    boost::shared_ptr<void> event_callback_data;
    
    /// Data passed to event impulse callback
    boost::shared_ptr<void> event_post_impulse_callback_data;
 
    /// Gets the (sorted) event data
    std::vector<Event>& get_events() { return _events; }

    /// Mapping from objects to contact parameters
    std::map<sorted_pair<BasePtr>, boost::shared_ptr<ContactParameters> > contact_params;

    /// If set to 'true' event driven simulator will process contact points for rendering
    bool render_contact_points;

    /// User time spent by collision detection on the last step
    Real coldet_utime;

    /// System time spent by collision detection on the last step
    Real coldet_stime;

    /// User time spent by event handling on the last step
    Real event_utime;

    /// System time spent by event handling on the last step
    Real event_stime;

  private:
    void preprocess_event(Event& e);
    void check_violation();
    Real find_and_handle_si_events(Real dt);
    void find_limit_events(const std::vector<VectorN>& q0, const std::vector<VectorN>& q1, Real dt, std::vector<Event>& limit_events);
    Real find_TOI(Real dt); 
    void handle_events();
    boost::shared_ptr<ContactParameters> get_contact_parameters(CollisionGeometryPtr geom1, CollisionGeometryPtr geom2) const;
    std::vector<VectorN> _q0, _qf, _qdf;
    std::vector<std::pair<DynamicBodyPtr, VectorN> > _x0, _x1;
    void integrate_si_Euler(double dt);
    void get_velocities(std::vector<VectorN>& qd) const;
    void get_coords(std::vector<VectorN>& q) const;

    // Visualization functions
    void visualize_contact( Event& event );

    /// Determines whether the simulation constraints have been violated
    bool _simulation_violated;

    /// The vector of events
    std::vector<Event> _events;

    /// Event tolerances
    std::map<Event, Real, EventCompare> _event_tolerances;

    /// Object for handling impact events
    ImpactEventHandler _impact_event_handler;
}; // end class

} // end namespace

#endif


