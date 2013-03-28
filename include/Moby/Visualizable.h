/****************************************************************************
 * Copyright 2006 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _VISUALIZABLE_H
#define _VISUALIZABLE_H

#include <vector>
#include <Moby/Types.h>
#include <Moby/Base.h>
#include <Moby/Matrix4.h>
#include <Moby/OSGGroupWrapper.h>

namespace osg { 
  class Node;
  class MatrixTransform; 
  class Group;
}

namespace Moby {

/// Class that allows for visualizing simulation data
/**
 * This class uses the OSGGroupWrapper class to permit sharing and
 * serialization of visualization data.
 */
class Visualizable : public virtual Base 
{
  public:
    Visualizable();
    Visualizable(const Visualizable* v) : Base(v) { }
    virtual ~Visualizable(); 
    virtual void update_visualization();

    virtual void set_visualization_data(osg::Node* vdata); 
    virtual void set_visualization_data(OSGGroupWrapperPtr vdata); 
    static osg::Group* construct_from_node(XMLTreeConstPtr node, const std::map<std::string, BasePtr>& id_map);
    osg::Group* get_visualization_data() const;
    virtual void save_to_xml(XMLTreePtr node, std::list<BaseConstPtr>& shared_objects) const;
    virtual void load_from_xml(XMLTreeConstPtr node, std::map<std::string, BasePtr>& id_map);

  protected:

    /// Implementing classes must implement this method to get the transform for the object
    virtual const Matrix4* get_visualization_transform() = 0;

    /// The underlying visualization data
    OSGGroupWrapperPtr _vizdata;

    /// The top-level group (containing _vizdata)
    osg::MatrixTransform* _group;
}; // end class
} // end namespace

#endif // (ends #ifndef _VISUALIZABLE_H)

