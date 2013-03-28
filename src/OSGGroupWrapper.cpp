/****************************************************************************
 * Copyright 2007 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#include <cstring>
#include <iostream>
#ifdef USE_OSG
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osg/MatrixTransform>
#endif
#include <Moby/Matrix4.h>
#include <Moby/XMLTree.h>
#include <Moby/InvalidTransformException.h>
#include <Moby/OSGGroupWrapper.h>

using namespace Moby;

OSGGroupWrapper::OSGGroupWrapper()
{
  #ifdef USE_OSG
  _group = new osg::MatrixTransform;
  _group->ref();
  #endif
}

OSGGroupWrapper::OSGGroupWrapper(osg::Node* n)
{
  #ifdef USE_OSG
  _group = new osg::MatrixTransform;
  _group->addChild(n);
  _group->ref();
  #endif
}

/// Creates an OSGGroup wrapper given a filename 
OSGGroupWrapper::OSGGroupWrapper(const std::string& fname)
{
  #ifdef USE_OSG
  // open the filename and read in the file
  osg::Node* node = osgDB::readNodeFile(fname);
  if (!node)
  {
    std::cerr << "OSGGroupWrapper::OSGGroupWrapper() - unable to read ";
    std::cerr << "from " << fname << "!" << std::endl;
    return;
  }

  // process the node 
  osg::Group* group = dynamic_cast<osg::Group*>(node);
  if (!group)
  {
    _group = new osg::Group;
    _group->addChild(node);
  }
  else
    _group = group;
  _group->ref();
  #endif
}

OSGGroupWrapper::~OSGGroupWrapper()
{
  #ifdef USE_OSG
  _group->unref();
  #endif
}

#ifdef USE_OSG
/// Copies this matrix to an OpenSceneGraph Matrixd object
static void to_osg_matrix(const Matrix4& src, osg::Matrixd& tgt)
{
  const unsigned X = 0, Y = 1, Z = 2, W = 3;
  for (unsigned i=X; i<= Z; i++)
    for (unsigned j=X; j<= W; j++)
      tgt(j,i) = src(i,j);

  // set constant values of the matrix
  tgt(X,W) = tgt(Y,W) = tgt(Z,W) = (Real) 0.0;
  tgt(W,W) = (Real) 1.0;
}
#endif

/// Implements Base::load_from_xml()
void OSGGroupWrapper::load_from_xml(XMLTreeConstPtr node, std::map<std::string, BasePtr>& id_map)
{
  // load the Base data
  Base::load_from_xml(node, id_map);

  // verify the node name
  assert(strcasecmp(node->name.c_str(), "OSGGroup") == 0);

  // if there is no visualization data filename, return now
  const XMLAttrib* viz_fname_attr = node->get_attrib("filename");
  if (!viz_fname_attr)
    return;

  // get the filename
  const std::string& fname = viz_fname_attr->get_string_value();

  // open the filename and read in the file
  #ifdef USE_OSG
  osg::Node* osgnode = osgDB::readNodeFile(fname);
  if (!osgnode)
  {
    std::cerr << "OSGGroupWrapper::load_from_xml() - unable to read from ";
    std::cerr  << fname << "!" << std::endl;
    return;
  }

  // remove all children from the root separator
  _group->removeChildren(0, _group->getNumChildren());

  // read in the transform, if specified
  const XMLAttrib* transform_attr = node->get_attrib("transform");
  if (transform_attr)
  {
    Matrix4 T;
    transform_attr->get_matrix_value(T);
    if (!Matrix4::valid_transform(T))
      throw InvalidTransformException(T);

    // create the SoTransform and add it to the root separator
    osg::Matrixd m;
    to_osg_matrix(T, m);
    osg::MatrixTransform* mgroup = new osg::MatrixTransform;
    mgroup->setMatrix(m);
    _group->unref();
    _group = mgroup;
    _group->ref();
  }

  // add the read node to the group
  _group->addChild(osgnode);
  #endif
}

/// Implements Base::save_to_xml()
void OSGGroupWrapper::save_to_xml(XMLTreePtr node, std::list<BaseConstPtr>& shared_objects) const
{
  // save the Base data
  Base::save_to_xml(node, shared_objects);

  // rename this node
  node->name = "OSGGroup";

  // form the filename using the ID 
  std::string filename = "vizdata_" + id + ".osg";

  // save the visualization data 
  node->attribs.insert(XMLAttrib("filename", filename));
  #ifdef USE_OSG
  if (!osgDB::writeNodeFile(*_group, filename))
    std::cerr << "OSGGroupWrapper::save_to_xml() - unable to write scene graph to " << filename << std::endl;
  #endif
}

