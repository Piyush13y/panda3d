// Filename: graphicsStateGuardian.cxx
// Created by:  drose (02Feb99)
// 
////////////////////////////////////////////////////////////////////

#include "graphicsStateGuardian.h"
#include "renderBuffer.h"
#include "config_display.h"

#include <clockObject.h>
#include <geomNode.h>

#include <algorithm>

TypeHandle GraphicsStateGuardian::_type_handle;
TypeHandle GraphicsStateGuardian::GsgWindow::_type_handle;

GraphicsStateGuardian::GsgFactory *GraphicsStateGuardian::_factory = NULL;

GraphicsStateGuardian::GsgWindow::~GsgWindow(void) {}

TypeHandle GraphicsStateGuardian::GsgWindow::get_class_type(void) {
  return _type_handle;
}

void GraphicsStateGuardian::GsgWindow::init_type(void) {
  GsgParam::init_type();
  register_type(_type_handle, "GraphicsStateGuardian::GsgWindow",
		GsgParam::get_class_type());
}

TypeHandle GraphicsStateGuardian::GsgWindow::get_type(void) const {
  return get_class_type();
}

TypeHandle GraphicsStateGuardian::GsgWindow::force_init_type(void) {
  init_type();
  return get_class_type();
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::Constructor
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
GraphicsStateGuardian::
GraphicsStateGuardian(GraphicsWindow *win) {
  _win = win;
  _coordinate_system = default_coordinate_system;
  _current_display_region = (DisplayRegion*)0L;
  reset();
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::release_all_textures
//       Access: Public
//  Description: Frees the resources for all textures associated with
//               this GSG.
////////////////////////////////////////////////////////////////////
void GraphicsStateGuardian::
release_all_textures() {
  // We must get a copy of the _prepared_textures list first, because
  // each call to release_texture() will remove that texture from the
  // list, and we don't want to traverse a list while we're modifying
  // it!
  Textures temp = _prepared_textures;
  for (Textures::const_iterator ti = temp.begin();
       ti != temp.end();
       ++ti) {
    release_texture(*ti);
  }

  // Now that we've released all of the textures, the
  // _prepared_textures list should have completely emptied itself.
  nassertv(_prepared_textures.empty());
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::reset
//       Access: Public, Virtual
//  Description: Resets all internal state as if the gsg were newly
//               created.
////////////////////////////////////////////////////////////////////
void GraphicsStateGuardian::
reset() {
  _display_region_stack_level = 0;
  _frame_buffer_stack_level = 0;

  _state.clear();

  _buffer_mask = 0;
  _color_clear_value.set(gsg_clear_r, gsg_clear_g, gsg_clear_b, 0.0);
  _depth_clear_value = 1.0;
  _stencil_clear_value = 0.0;
  _accum_clear_value.set(0.0, 0.0, 0.0, 0.0);
  _normals_enabled = false;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::set_state
//       Access: Public
//  Description: Sets the graphics backend to the state represented by
//               the indicated set of attributes.  Only the minimal
//               number of graphics commands are issued--attributes
//               which have not changed since the last call to
//               set_state are detected and not issued again.
//
//               If complete is true, it means that the supplied state
//               is a complete description of the desired state--if an
//               attribute is absent, it should be taken to be the
//               same as the initial value for that attribute.  If
//               complete is false, it means that the supplied state
//               specifies only a subset of the desired state, and
//               that absent attributes should remain unchanged.
////////////////////////////////////////////////////////////////////
void GraphicsStateGuardian::
set_state(const NodeAttributes &new_state, bool complete) {
  if (gsg_cat.is_debug()) {
    gsg_cat.debug() << "\n";
    gsg_cat.debug() 
      << "Frame " << ClockObject::get_global_clock()->get_frame_count()
      << ", setting to (complete = " << complete << ")\n";
    new_state.write(gsg_cat.debug(false), 10);
  }

  NodeAttributes::const_iterator new_i;
  NodeAttributes::iterator current_i;

  new_i = new_state.begin();
  current_i = _state.begin();

  while (new_i != new_state.end() && current_i != _state.end()) {
    if ((*new_i).first < (*current_i).first) {
      // The user requested setting an attribute that we've never set
      // before.  Issue the command.

      if ((*new_i).second != (NodeAttribute *)NULL) {
	if (gsg_cat.is_debug()) {
	  gsg_cat.debug()
	    << "Issuing new attrib " << *(*new_i).second << "\n";
	}
	
	(*new_i).second->issue(this);
	
	// And store the new value.
	current_i = _state.insert(current_i, *new_i);
	++current_i;
      }

      ++new_i;

    } else if ((*current_i).first < (*new_i).first) {
      // Here's an attribute that we've set previously, but the user
      // didn't specify this time.

      if (complete) {
	// If we're in the "complete state" model, that means this
	// attribute should now get the default initial value.

	if (gsg_cat.is_debug()) {
	  gsg_cat.debug()
	    << "Unissuing attrib " << *(*current_i).second 
	    << " (previously set, not now)\n";
	}

	PT(NodeAttribute) initial = (*current_i).second->make_initial();
	initial->issue(this);

	NodeAttributes::iterator erase_i = current_i;
	++current_i;
	
	_state.erase(erase_i);

      } else {
	++current_i;
      }

    } else {  // (*current_i).first == (*new_i).first)

      if ((*new_i).second == (NodeAttribute *)NULL) {
	// Here's an attribute that we've set previously, which
	// appears in the new list, but is NULL indicating it should
	// be removed.

	if (complete) {
	  // Only remove it if we're in the "complete state" model.

	  if (gsg_cat.is_debug()) {
	    gsg_cat.debug()
	      << "Unissuing attrib " << *(*current_i).second 
	      << " (previously set, now NULL)\n";
	  }

	  // Issue the initial attribute before clearing the state.
	  PT(NodeAttribute) initial = (*current_i).second->make_initial();
	  initial->issue(this);

	  NodeAttributes::iterator erase_i = current_i;
	  ++current_i;
	  
	  _state.erase(erase_i);

	} else {
	  ++current_i;
	}
	++new_i;

      } else {
	// Here's an attribute that we've set previously, and the user
	// asked us to set it again.  Issue the command only if the new
	// attribute is different from that which we'd set before.
	if ((*new_i).second->compare_to(*(*current_i).second) != 0) {
	  if (gsg_cat.is_debug()) {
	    gsg_cat.debug()
	      << "Reissuing attrib " << *(*new_i).second << "\n";
	  }
	  (*new_i).second->issue(this);

	  // And store the new value.
	  (*current_i).second = (*new_i).second;

	} else if (gsg_cat.is_debug()) {
	  gsg_cat.debug()
	    << "Not reissuing unchanged attrib " << *(*new_i).second << "\n";
	}

	++current_i;
	++new_i;
      }
    }
  }

  while (new_i != new_state.end()) {
    // The user requested setting an attribute that we've never set
    // before.  Issue the command.

    if ((*new_i).second != (NodeAttribute *)NULL) {
      if (gsg_cat.is_debug()) {
	gsg_cat.debug()
	  << "Issuing new attrib " << *(*new_i).second << "\n";
      }
      
      (*new_i).second->issue(this);
      
      // And store the new value.
      _state.insert(_state.end(), *new_i);
    }
    ++new_i;
  }

  if (complete) {
    while (current_i != _state.end()) {
      // Here's an attribute that we've set previously, but the user
      // didn't specify this time.

      if (gsg_cat.is_debug()) {
	gsg_cat.debug()
	  << "Unissuing attrib " << *(*current_i).second 
	  << " (previously set, end of list)\n";
      }
      
      // If we're in the "complete state" model, that means this
      // attribute should now get the default initial value.
      PT(NodeAttribute) initial = (*current_i).second->make_initial();
      initial->issue(this);
      
      NodeAttributes::iterator erase_i = current_i;
      ++current_i;
      
      _state.erase(erase_i);
    }
  }
}


////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::get_render_buffer
//       Access: Public
//  Description: Returns a RenderBuffer object suitable for operating
//               on the requested set of buffers.  buffer_type is the
//               union of all the desired RenderBuffer::Type values.
////////////////////////////////////////////////////////////////////
RenderBuffer GraphicsStateGuardian::
get_render_buffer(int buffer_type) {
  return RenderBuffer(this, buffer_type & _buffer_mask);
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::set_color_clear_value
//       Access: Public
//  Description: Sets the color that the next clear() command will set
//		 the color buffer to
////////////////////////////////////////////////////////////////////
void GraphicsStateGuardian::
set_color_clear_value(const Colorf& value) {
  _color_clear_value = value;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::set_depth_clear_value
//       Access: Public
//  Description: Sets the depth that the next clear() command will set
//               the depth buffer to
////////////////////////////////////////////////////////////////////
void GraphicsStateGuardian::
set_depth_clear_value(const float value) {
  _depth_clear_value = value;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::set_stencil_clear_value
//       Access: Public
//  Description: Sets the value that the next clear() command will set
//               the stencil buffer to
////////////////////////////////////////////////////////////////////
void GraphicsStateGuardian::
set_stencil_clear_value(const bool value) {
  _stencil_clear_value = value;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::set_accum_clear_value
//       Access: Public
//  Description: Sets the color that the next clear() command will set
//               the accumulation buffer to
////////////////////////////////////////////////////////////////////
void GraphicsStateGuardian::
set_accum_clear_value(const Colorf& value) {
  _accum_clear_value = value;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::wants_normals
//       Access: Public, Virtual
//  Description: 
////////////////////////////////////////////////////////////////////
bool GraphicsStateGuardian::
wants_normals() const {
  return _normals_enabled;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::wants_texcoords
//       Access: Public, Virtual
//  Description: 
////////////////////////////////////////////////////////////////////
bool GraphicsStateGuardian::
wants_texcoords() const {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::wants_colors
//       Access: Public, Virtual
//  Description: 
////////////////////////////////////////////////////////////////////
bool GraphicsStateGuardian::
wants_colors() const {
  return false;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::begin_decal
//       Access: Public, Virtual
//  Description: This will be called to initiate decaling mode.  It is
//               passed the pointer to the GeomNode that will be the
//               destination of the decals, which it is expected that
//               the GSG will render normally; subsequent geometry
//               rendered up until the next call of end_decal() should
//               be rendered as decals of the base_geom.
////////////////////////////////////////////////////////////////////
void GraphicsStateGuardian::
begin_decal(GeomNode *base_geom) {
  base_geom->draw(this);
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::end_decal
//       Access: Public, Virtual
//  Description: This will be called to terminate decaling mode.  It
//               is passed the same base_geom that was passed to
//               begin_decal().
////////////////////////////////////////////////////////////////////
void GraphicsStateGuardian::
end_decal(GeomNode *) {
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::mark_prepared_texture
//       Access: Protected
//  Description: This is intended to be called from within
//               prepare_texture().  It adds the indicated
//               TextureContext pointer to the _prepared_textures set,
//               and returns true if it was successfully added
//               (i.e. it was not already in the set).
////////////////////////////////////////////////////////////////////
bool GraphicsStateGuardian::
mark_prepared_texture(TextureContext *tc) {
  return _prepared_textures.insert(tc).second;
}

////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::unmark_prepared_texture
//       Access: Protected
//  Description: This is intended to be called from within
//               release_texture().  It removes the indicated
//               TextureContext pointer from the _prepared_textures set,
//               and returns true if it was successfully removed
//               (i.e. it had been in the set).
////////////////////////////////////////////////////////////////////
bool GraphicsStateGuardian::
unmark_prepared_texture(TextureContext *tc) {
  return (_prepared_textures.erase(tc) != 0);
}


////////////////////////////////////////////////////////////////////
//     Function: GraphicsStateGuardian::get_factory
//       Access: Public, Static
//  Description: Returns the factory object that can be used to
//               register new kinds of GraphicsStateGuardian objects that may
//               be created.
////////////////////////////////////////////////////////////////////
GraphicsStateGuardian::GsgFactory &GraphicsStateGuardian::
get_factory() {
  if (_factory == (GsgFactory *)NULL) {
    _factory = new GsgFactory;
  }
  return (*_factory);
}

void GraphicsStateGuardian::read_priorities(void)
{
  GsgFactory &factory = get_factory();
  if (factory.get_num_preferred() == 0) {
    Config::ConfigTable::Symbol::iterator i;
    for (i = preferred_gsg_begin(); i != preferred_gsg_end(); ++i) {
      ConfigString type_name = (*i).Val();
      TypeHandle type = TypeRegistry::ptr()->find_type(type_name);
      if (type == TypeHandle::none()) {
	display_cat.warning()
	  << "Unknown type requested for GSG preference: " << type_name
	  << "\n";
      } else {
	display_cat.debug()
	  << "Specifying type " << type << " for GSG preference.\n";
	factory.add_preferred(type);
      }
    }
  }
}
