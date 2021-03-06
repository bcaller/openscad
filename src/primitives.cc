/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "module.h"
#include "node.h"
#include "polyset.h"
#include "evalcontext.h"
#include "Polygon2d.h"
#include "builtin.h"
#include "printutils.h"
#include "visitor.h"
#include "context.h"
#include "calc.h"
#include "mathc99.h"
#include <sstream>
#include <assert.h>
#include <boost/foreach.hpp>
#include <boost/assign/std/vector.hpp>
using namespace boost::assign; // bring 'operator+=()' into scope

#include <boost/math/special_functions/fpclassify.hpp>
#define isinf boost::math::isinf

#define F_MINIMUM 0.01

enum primitive_type_e {
	CUBE,
	SPHERE,
	CYLINDER,
	POLYHEDRON,
	SQUARE,
	CIRCLE,
	POLYGON
};

class PrimitiveModule : public AbstractModule
{
public:
	primitive_type_e type;
	PrimitiveModule(primitive_type_e type) : type(type) { }
	virtual AbstractNode *instantiate(const Context *ctx, const ModuleInstantiation *inst, const EvalContext *evalctx) const;
private:
	Value lookup_radius(const Context &ctx, const std::string &radius_var, const std::string &diameter_var) const;
};

class PrimitiveNode : public LeafNode
{
public:
	PrimitiveNode(const ModuleInstantiation *mi, primitive_type_e type) : LeafNode(mi), type(type) { }
  virtual Response accept(class State &state, Visitor &visitor) const {
		return visitor.visit(state, *this);
	}
	virtual std::string toString() const;
	virtual std::string name() const {
		switch (this->type) {
		case CUBE:
			return "cube";
			break;
		case SPHERE:
			return "sphere";
			break;
		case CYLINDER:
			return "cylinder";
			break;
		case POLYHEDRON:
			return "polyhedron";
			break;
		case SQUARE:
			return "square";
			break;
		case CIRCLE:
			return "circle";
			break;
		case POLYGON:
			return "polygon";
			break;
		default:
			assert(false && "PrimitiveNode::name(): Unknown primitive type");
			return AbstractPolyNode::name();
		}
	}

	bool center;
	double x, y, z, h, r1, r2;
	double fn, fs, fa;
	primitive_type_e type;
	int convexity;
	Value points, paths, faces;
	virtual Geometry *createGeometry() const;
};

/**
 * Return a radius value by looking up both a diameter and radius variable.
 * The diameter has higher priority, so if found an additionally set radius
 * value is ignored.
 * 
 * @param ctx data context with variable values.
 * @param radius_var name of the variable to lookup for the radius value.
 * @param diameter_var name of the variable to lookup for the diameter value.
 * @return radius value of type Value::NUMBER or Value::UNDEFINED if both
 *         variables are invalid or not set.
 */
Value PrimitiveModule::lookup_radius(const Context &ctx, const std::string &diameter_var, const std::string &radius_var) const
{
	const Value d = ctx.lookup_variable(diameter_var, true);
	const Value r = ctx.lookup_variable(radius_var, true);
	const bool r_defined = (r.type() == Value::NUMBER);
	
	if (d.type() == Value::NUMBER) {
		if (r_defined) {
			PRINTB("WARNING: Ignoring radius variable '%s' as diameter '%s' is defined too.", radius_var % diameter_var);
		}
		return Value(d.toDouble() / 2.0);
	} else if (r_defined) {
		return r;
	} else {
		return Value();
	}
}

AbstractNode *PrimitiveModule::instantiate(const Context *ctx, const ModuleInstantiation *inst, const EvalContext *evalctx) const
{
	PrimitiveNode *node = new PrimitiveNode(inst, this->type);

	node->center = false;
	node->x = node->y = node->z = node->h = node->r1 = node->r2 = 1;

	AssignmentList args;

	switch (this->type) {
	case CUBE:
		args += Assignment("size"), Assignment("center");
		break;
	case SPHERE:
		args += Assignment("r");
		break;
	case CYLINDER:
		args += Assignment("h"), Assignment("r1"), Assignment("r2"), Assignment("center");
		break;
	case POLYHEDRON:
		args += Assignment("points"), Assignment("faces"), Assignment("convexity");
		break;
	case SQUARE:
		args += Assignment("size"), Assignment("center");
		break;
	case CIRCLE:
		args += Assignment("r");
		break;
	case POLYGON:
		args += Assignment("points"), Assignment("paths"), Assignment("convexity");
		break;
	default:
		assert(false && "PrimitiveModule::instantiate(): Unknown node type");
	}

	Context c(ctx);
	c.setVariables(args, evalctx);

	node->fn = c.lookup_variable("$fn").toDouble();
	node->fs = c.lookup_variable("$fs").toDouble();
	node->fa = c.lookup_variable("$fa").toDouble();

	if (node->fs < F_MINIMUM) {
		PRINTB("WARNING: $fs too small - clamping to %f", F_MINIMUM);
		node->fs = F_MINIMUM;
	}
	if (node->fa < F_MINIMUM) {
		PRINTB("WARNING: $fa too small - clamping to %f", F_MINIMUM);
		node->fa = F_MINIMUM;
	}

	switch (this->type)  {
	case CUBE: {
		Value size = c.lookup_variable("size");
		Value center = c.lookup_variable("center");
		size.getDouble(node->x);
		size.getDouble(node->y);
		size.getDouble(node->z);
		size.getVec3(node->x, node->y, node->z);
		if (center.type() == Value::BOOL) {
			node->center = center.toBool();
		}
		break;
	}
	case SPHERE: {
		const Value r = lookup_radius(c, "d", "r");
		if (r.type() == Value::NUMBER) {
			node->r1 = r.toDouble();
		}
		break;
	}
	case CYLINDER: {
		const Value h = c.lookup_variable("h");
		if (h.type() == Value::NUMBER) {
			node->h = h.toDouble();
		}

		const Value r = lookup_radius(c, "d", "r");
		const Value r1 = lookup_radius(c, "d1", "r1");
		const Value r2 = lookup_radius(c, "d2", "r2");
		if (r.type() == Value::NUMBER) {
			node->r1 = r.toDouble();
			node->r2 = r.toDouble();
		}
		if (r1.type() == Value::NUMBER) {
			node->r1 = r1.toDouble();
		}
		if (r2.type() == Value::NUMBER) {
			node->r2 = r2.toDouble();
		}
		
		const Value center = c.lookup_variable("center");
		if (center.type() == Value::BOOL) {
			node->center = center.toBool();
		}
		break;
	}
	case POLYHEDRON: {
		node->points = c.lookup_variable("points");
		node->faces = c.lookup_variable("faces");
		if (node->faces.type() == Value::UNDEFINED) {
			// backwards compatible
			node->faces = c.lookup_variable("triangles");
			if (node->faces.type() != Value::UNDEFINED) {
				printDeprecation("DEPRECATED: polyhedron(triangles=[]) will be removed in future releases. Use polyhedron(faces=[]) instead.");
			}
		}
		break;
	}
	case SQUARE: {
		Value size = c.lookup_variable("size");
		Value center = c.lookup_variable("center");
		size.getDouble(node->x);
		size.getDouble(node->y);
		size.getVec2(node->x, node->y);
		if (center.type() == Value::BOOL) {
			node->center = center.toBool();
		}
		break;
	}
	case CIRCLE: {
		const Value r = lookup_radius(c, "d", "r");
		if (r.type() == Value::NUMBER) {
			node->r1 = r.toDouble();
		}
		break;
	}
	case POLYGON: {
		node->points = c.lookup_variable("points");
		node->paths = c.lookup_variable("paths");
		break;
	}
	}

	node->convexity = c.lookup_variable("convexity", true).toDouble();
	if (node->convexity < 1)
		node->convexity = 1;

	return node;
}

struct point2d {
	double x, y;
};

static void generate_circle(point2d *circle, double r, int fragments)
{
	for (int i=0; i<fragments; i++) {
		double phi = (M_PI*2*i) / fragments;
		circle[i].x = r*cos(phi);
		circle[i].y = r*sin(phi);
	}
}

/*!
	Creates geometry for this node.
	May return an empty Geometry creation failed, but will not return NULL.
*/
Geometry *PrimitiveNode::createGeometry() const
{
	Geometry *g = NULL;

	switch (this->type) {
	case CUBE: {
		PolySet *p = new PolySet(3,true);
		g = p;
		if (this->x > 0 && this->y > 0 && this->z > 0 &&
			!isinf(this->x) > 0 && !isinf(this->y) > 0 && !isinf(this->z) > 0) {
			double x1, x2, y1, y2, z1, z2;
			if (this->center) {
				x1 = -this->x/2;
				x2 = +this->x/2;
				y1 = -this->y/2;
				y2 = +this->y/2;
				z1 = -this->z/2;
				z2 = +this->z/2;
			} else {
				x1 = y1 = z1 = 0;
				x2 = this->x;
				y2 = this->y;
				z2 = this->z;
			}

			p->append_poly(); // top
			p->append_vertex(x1, y1, z2);
			p->append_vertex(x2, y1, z2);
			p->append_vertex(x2, y2, z2);
			p->append_vertex(x1, y2, z2);

			p->append_poly(); // bottom
			p->append_vertex(x1, y2, z1);
			p->append_vertex(x2, y2, z1);
			p->append_vertex(x2, y1, z1);
			p->append_vertex(x1, y1, z1);

			p->append_poly(); // side1
			p->append_vertex(x1, y1, z1);
			p->append_vertex(x2, y1, z1);
			p->append_vertex(x2, y1, z2);
			p->append_vertex(x1, y1, z2);

			p->append_poly(); // side2
			p->append_vertex(x2, y1, z1);
			p->append_vertex(x2, y2, z1);
			p->append_vertex(x2, y2, z2);
			p->append_vertex(x2, y1, z2);

			p->append_poly(); // side3
			p->append_vertex(x2, y2, z1);
			p->append_vertex(x1, y2, z1);
			p->append_vertex(x1, y2, z2);
			p->append_vertex(x2, y2, z2);

			p->append_poly(); // side4
			p->append_vertex(x1, y2, z1);
			p->append_vertex(x1, y1, z1);
			p->append_vertex(x1, y1, z2);
			p->append_vertex(x1, y2, z2);
		}
	}
		break;
	case SPHERE: {
		PolySet *p = new PolySet(3,true);
		g = p;
		if (this->r1 > 0 && !isinf(this->r1)) {
			struct ring_s {
				point2d *points;
				double z;
			};

			int fragments = Calc::get_fragments_from_r(r1, fn, fs, fa);
			int rings = (fragments+1)/2;
// Uncomment the following three lines to enable experimental sphere tesselation
//		if (rings % 2 == 0) rings++; // To ensure that the middle ring is at phi == 0 degrees

			ring_s *ring = new ring_s[rings];

//		double offset = 0.5 * ((fragments / 2) % 2);
			for (int i = 0; i < rings; i++) {
//			double phi = (M_PI * (i + offset)) / (fragments/2);
				double phi = (M_PI * (i + 0.5)) / rings;
				double r = r1 * sin(phi);
				ring[i].z = r1 * cos(phi);
				ring[i].points = new point2d[fragments];
				generate_circle(ring[i].points, r, fragments);
			}

			p->append_poly();
			for (int i = 0; i < fragments; i++)
				p->append_vertex(ring[0].points[i].x, ring[0].points[i].y, ring[0].z);

			for (int i = 0; i < rings-1; i++) {
				ring_s *r1 = &ring[i];
				ring_s *r2 = &ring[i+1];
				int r1i = 0, r2i = 0;
				while (r1i < fragments || r2i < fragments)
				{
					if (r1i >= fragments)
						goto sphere_next_r2;
					if (r2i >= fragments)
						goto sphere_next_r1;
					if ((double)r1i / fragments <
							(double)r2i / fragments)
					{
					sphere_next_r1:
						p->append_poly();
						int r1j = (r1i+1) % fragments;
						p->insert_vertex(r1->points[r1i].x, r1->points[r1i].y, r1->z);
						p->insert_vertex(r1->points[r1j].x, r1->points[r1j].y, r1->z);
						p->insert_vertex(r2->points[r2i % fragments].x, r2->points[r2i % fragments].y, r2->z);
						r1i++;
					} else {
					sphere_next_r2:
						p->append_poly();
						int r2j = (r2i+1) % fragments;
						p->append_vertex(r2->points[r2i].x, r2->points[r2i].y, r2->z);
						p->append_vertex(r2->points[r2j].x, r2->points[r2j].y, r2->z);
						p->append_vertex(r1->points[r1i % fragments].x, r1->points[r1i % fragments].y, r1->z);
						r2i++;
					}
				}
			}

			p->append_poly();
			for (int i = 0; i < fragments; i++)
				p->insert_vertex(ring[rings-1].points[i].x, 
												 ring[rings-1].points[i].y, 
												 ring[rings-1].z);

			for (int i = 0; i < rings; i++) {
				delete[] ring[i].points;
			}
			delete[] ring;
		}
	}
		break;
	case CYLINDER: {
		PolySet *p = new PolySet(3,true);
		g = p;
		if (this->h > 0 && !isinf(this->h) &&
				this->r1 >=0 && this->r2 >= 0 && (this->r1 > 0 || this->r2 > 0) &&
				!isinf(this->r1) && !isinf(this->r2)) {
			int fragments = Calc::get_fragments_from_r(fmax(this->r1, this->r2), this->fn, this->fs, this->fa);

			double z1, z2;
			if (this->center) {
				z1 = -this->h/2;
				z2 = +this->h/2;
			} else {
				z1 = 0;
				z2 = this->h;
			}

			point2d *circle1 = new point2d[fragments];
			point2d *circle2 = new point2d[fragments];

			generate_circle(circle1, r1, fragments);
			generate_circle(circle2, r2, fragments);
		
			for (int i=0; i<fragments; i++) {
				int j = (i+1) % fragments;
				if (r1 == r2) {
					p->append_poly();
					p->insert_vertex(circle1[i].x, circle1[i].y, z1);
					p->insert_vertex(circle2[i].x, circle2[i].y, z2);
					p->insert_vertex(circle2[j].x, circle2[j].y, z2);
					p->insert_vertex(circle1[j].x, circle1[j].y, z1);
				} else {
					if (r1 > 0) {
						p->append_poly();
						p->insert_vertex(circle1[i].x, circle1[i].y, z1);
						p->insert_vertex(circle2[i].x, circle2[i].y, z2);
						p->insert_vertex(circle1[j].x, circle1[j].y, z1);
					}
					if (r2 > 0) {
						p->append_poly();
						p->insert_vertex(circle2[i].x, circle2[i].y, z2);
						p->insert_vertex(circle2[j].x, circle2[j].y, z2);
						p->insert_vertex(circle1[j].x, circle1[j].y, z1);
					}
				}
			}

			if (this->r1 > 0) {
				p->append_poly();
				for (int i=0; i<fragments; i++)
					p->insert_vertex(circle1[i].x, circle1[i].y, z1);
			}

			if (this->r2 > 0) {
				p->append_poly();
				for (int i=0; i<fragments; i++)
					p->append_vertex(circle2[i].x, circle2[i].y, z2);
			}

			delete[] circle1;
			delete[] circle2;
		}
	}
		break;
	case POLYHEDRON: {
		PolySet *p = new PolySet(3);
		g = p;
		p->setConvexity(this->convexity);
		for (size_t i=0; i<this->faces.toVector().size(); i++)
		{
			p->append_poly();
			const Value::VectorType &vec = this->faces.toVector()[i].toVector();
			for (size_t j=0; j<vec.size(); j++) {
				size_t pt = vec[j].toDouble();
				if (pt < this->points.toVector().size()) {
					double px, py, pz;
					if (!this->points.toVector()[pt].getVec3(px, py, pz) ||
							isinf(px) || isinf(py) || isinf(pz)) {
						PRINTB("ERROR: Unable to convert point at index %d to a vec3 of numbers", j);
						delete p;
						return NULL;
					}
					p->insert_vertex(px, py, pz);
				}
			}
		}
	}
		break;
	case SQUARE: {
		Polygon2d *p = new Polygon2d();
		g = p;
		if (this->x > 0 && this->y > 0) {
			Vector2d v1(0, 0);
			Vector2d v2(this->x, this->y);
			if (this->center) {
				v1 -= Vector2d(this->x/2, this->y/2);
				v2 -= Vector2d(this->x/2, this->y/2);
			}

			Outline2d o;
			o.vertices.resize(4);
			o.vertices[0] = v1;
			o.vertices[1] = Vector2d(v2[0], v1[1]);
			o.vertices[2] = v2;
			o.vertices[3] = Vector2d(v1[0], v2[1]);
			p->addOutline(o);
			p->setSanitized(true);
		}
	}
		break;
	case CIRCLE: {
		Polygon2d *p = new Polygon2d();
		g = p;
		if (this->r1 > 0)	{
			int fragments = Calc::get_fragments_from_r(this->r1, this->fn, this->fs, this->fa);

			Outline2d o;
			o.vertices.resize(fragments);
			for (int i=0; i < fragments; i++) {
				double phi = (M_PI*2*i) / fragments;
				o.vertices[i] = Vector2d(this->r1*cos(phi), this->r1*sin(phi));
			}
			p->addOutline(o);
			p->setSanitized(true);
		}
	}
		break;
	case POLYGON:	{
			Polygon2d *p = new Polygon2d();
			g = p;

			Outline2d outline;
			double x,y;
			const Value::VectorType &vec = this->points.toVector();
			for (unsigned int i=0;i<vec.size();i++) {
				const Value &val = vec[i];
				if (!val.getVec2(x, y) ||
						isinf(x) || isinf(y)) {
					PRINTB("ERROR: Unable to convert point %s at index %d to a vec2 of numbers", 
								 val.toString() % i);
					delete p;
					return NULL;
				}
				outline.vertices.push_back(Vector2d(x, y));
			}

			if (this->paths.toVector().size() == 0 && outline.vertices.size() > 2) {
				p->addOutline(outline);
			}
			else {
				BOOST_FOREACH(const Value &polygon, this->paths.toVector()) {
					Outline2d curroutline;
					BOOST_FOREACH(const Value &index, polygon.toVector()) {
						unsigned int idx = index.toDouble();
						if (idx < outline.vertices.size()) {
							curroutline.vertices.push_back(outline.vertices[idx]);
						}
						// FIXME: Warning on out of bounds?
					}
					p->addOutline(curroutline);
				}
			}
        
			if (p->outlines().size() == 0) {
				delete p;
				g = NULL;
			}
			else {
				p->setConvexity(convexity);
			}
	}
	}

	return g;
}

std::string PrimitiveNode::toString() const
{
	std::stringstream stream;

	stream << this->name();

	switch (this->type) {
	case CUBE:
		stream << "(size = [" << this->x << ", " << this->y << ", " << this->z << "], "
					 <<	"center = " << (center ? "true" : "false") << ")";
		break;
	case SPHERE:
		stream << "($fn = " << this->fn << ", $fa = " << this->fa
					 << ", $fs = " << this->fs << ", r = " << this->r1 << ")";
			break;
	case CYLINDER:
		stream << "($fn = " << this->fn << ", $fa = " << this->fa
					 << ", $fs = " << this->fs << ", h = " << this->h << ", r1 = " << this->r1
					 << ", r2 = " << this->r2 << ", center = " << (center ? "true" : "false") << ")";
			break;
	case POLYHEDRON:
		stream << "(points = " << this->points
					 << ", faces = " << this->faces
					 << ", convexity = " << this->convexity << ")";
			break;
	case SQUARE:
		stream << "(size = [" << this->x << ", " << this->y << "], "
					 << "center = " << (center ? "true" : "false") << ")";
			break;
	case CIRCLE:
		stream << "($fn = " << this->fn << ", $fa = " << this->fa
					 << ", $fs = " << this->fs << ", r = " << this->r1 << ")";
		break;
	case POLYGON:
		stream << "(points = " << this->points << ", paths = " << this->paths << ", convexity = " << this->convexity << ")";
			break;
	default:
		assert(false);
	}

	return stream.str();
}

void register_builtin_primitives()
{
	Builtins::init("cube", new PrimitiveModule(CUBE));
	Builtins::init("sphere", new PrimitiveModule(SPHERE));
	Builtins::init("cylinder", new PrimitiveModule(CYLINDER));
	Builtins::init("polyhedron", new PrimitiveModule(POLYHEDRON));
	Builtins::init("square", new PrimitiveModule(SQUARE));
	Builtins::init("circle", new PrimitiveModule(CIRCLE));
	Builtins::init("polygon", new PrimitiveModule(POLYGON));
}
