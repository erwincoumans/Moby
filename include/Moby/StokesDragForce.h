/****************************************************************************
 * Copyright 2009 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _STOKES_DRAG_FORCE_H
#define _STOKES_DRAG_FORCE_H

#include <Moby/Vector3.h>
#include <Moby/RecurrentForce.h>

namespace Moby {
class StokesDragForce : public RecurrentForce
{
  public:
    StokesDragForce();
    StokesDragForce(const StokesDragForce& source);
    virtual ~StokesDragForce() {}
    virtual void add_force(boost::shared_ptr<DynamicBody> body);
    virtual void load_from_xml(XMLTreeConstPtr node, std::map<std::string, BasePtr>& id_map);
    virtual void save_to_xml(XMLTreePtr node, std::list<BaseConstPtr>& shared_objects) const;

    /// The drag coefficient 
    Real b;
}; // end class
} // end namespace

#endif

