/* Copyright (c) 2007 Scott Lembcke
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
 
#include <stdlib.h>
#include <math.h>
//#include <stdio.h>

#include "chipmunk_private.h"

typedef int (*collisionFunc)(const cpShape *, const cpShape *, cpContact *);

// Add contact points for circle to circle collisions.
// Used by several collision tests.
static int
circle2circleQuery(const cpVect p1, const cpVect p2, const cpFloat r1, const cpFloat r2, cpContact *con)
{
	cpFloat mindist = r1 + r2;
	cpVect delta = cpvsub(p2, p1);
	cpFloat distsq = cpvlengthsq(delta);
	if(distsq >= mindist*mindist) return 0;
	
	cpFloat dist = cpfsqrt(distsq);

	// Allocate and initialize the contact.
	cpContactInit(
		con,
		cpvadd(p1, cpvmult(delta, 0.5f + (r1 - 0.5f*mindist)/(dist ? dist : INFINITY))),
		(dist ? cpvmult(delta, 1.0f/dist) : cpv(1.0f, 0.0f)),
		dist - mindist,
		0
	);
	
	return 1;
}

// Collide circle shapes.
static int
circle2circle(const cpShape *shape1, const cpShape *shape2, cpContact *arr)
{
	cpCircleShape *circ1 = (cpCircleShape *)shape1; //TODO
	cpCircleShape *circ2 = (cpCircleShape *)shape2;
	
	return circle2circleQuery(circ1->tc, circ2->tc, circ1->r, circ2->r, arr);
}

static int
circle2segment(const cpCircleShape *circleShape, const cpSegmentShape *segmentShape, cpContact *con)
{
	cpVect seg_a = segmentShape->ta;
	cpVect seg_b = segmentShape->tb;
	cpVect center = circleShape->tc;
	
	cpVect seg_delta = cpvsub(seg_b, seg_a);
	cpFloat closest_t = cpfclamp01(cpvdot(seg_delta, cpvsub(center, seg_a))/cpvlengthsq(seg_delta));
	cpVect closest = cpvadd(seg_a, cpvmult(seg_delta, closest_t));
	
	if(circle2circleQuery(center, closest, circleShape->r, segmentShape->r, con)){
		cpVect n = con[0].n;
		
		// Reject endcap collisions if tangents are provided.
		if(
			(closest_t == 0.0f && cpvdot(n, segmentShape->a_tangent) < 0.0) ||
			(closest_t == 1.0f && cpvdot(n, segmentShape->b_tangent) < 0.0)
		) return 0;
		
		return 1;
	} else {
		return 0;
	}
}

// Helper function for working with contact buffers
// This used to malloc/realloc memory on the fly but was repurposed.
static cpContact *
nextContactPoint(cpContact *arr, int *numPtr)
{
	int index = *numPtr;
	
	if(index < CP_MAX_CONTACTS_PER_ARBITER){
		(*numPtr) = index + 1;
		return &arr[index];
	} else {
		return &arr[CP_MAX_CONTACTS_PER_ARBITER - 1];
	}
}

// Find the minimum separating axis for the give poly and axis list.
static inline int
findMSA(const cpPolyShape *poly, const cpSplittingPlane *planes, const int num, cpFloat *min_out)
{
	int min_index = 0;
	cpFloat min = cpPolyShapeValueOnAxis(poly, planes->n, planes->d);
	if(min > 0.0f) return -1;
	
	for(int i=1; i<num; i++){
		cpFloat dist = cpPolyShapeValueOnAxis(poly, planes[i].n, planes[i].d);
		if(dist > 0.0f) {
			return -1;
		} else if(dist > min){
			min = dist;
			min_index = i;
		}
	}
	
	(*min_out) = min;
	return min_index;
}

// Add contacts for probably penetrating vertexes.
// This handles the degenerate case where an overlap was detected, but no vertexes fall inside
// the opposing polygon. (like a star of david)
//static inline int
//findVertsFallback(cpContact *arr, const cpPolyShape *poly1, const cpPolyShape *poly2, const cpVect n, const cpFloat dist)
//{
//	int num = 0;
//	
//	for(int i=0; i<poly1->numVerts; i++){
//		cpVect v = poly1->tVerts[i];
//		if(cpPolyShapeContainsVertPartial(poly2, v, cpvneg(n)))
//			cpContactInit(nextContactPoint(arr, &num), v, n, dist, CP_HASH_PAIR(poly1->shape.hashid, i));
//	}
//	
//	for(int i=0; i<poly2->numVerts; i++){
//		cpVect v = poly2->tVerts[i];
//		if(cpPolyShapeContainsVertPartial(poly1, v, n))
//			cpContactInit(nextContactPoint(arr, &num), v, n, dist, CP_HASH_PAIR(poly2->shape.hashid, i));
//	}
//	
//	return num;
//}
//
//// Add contacts for penetrating vertexes.
//static inline int
//findVerts(cpContact *arr, const cpPolyShape *poly1, const cpPolyShape *poly2, const cpVect n, const cpFloat dist)
//{
//	int num = 0;
//	
//	for(int i=0; i<poly1->numVerts; i++){
//		cpVect v = poly1->tVerts[i];
//		if(cpPolyShapeContainsVert(poly2, v))
//			cpContactInit(nextContactPoint(arr, &num), v, n, dist, CP_HASH_PAIR(poly1->shape.hashid, i));
//	}
//	
//	for(int i=0; i<poly2->numVerts; i++){
//		cpVect v = poly2->tVerts[i];
//		if(cpPolyShapeContainsVert(poly1, v))
//			cpContactInit(nextContactPoint(arr, &num), v, n, dist, CP_HASH_PAIR(poly2->shape.hashid, i));
//	}
//	
//	return (num ? num : findVertsFallback(arr, poly1, poly2, n, dist));
//}

struct EdgePoint {
	cpVect v;
	int hash;
};

struct Edge {
	struct EdgePoint a, b;
	cpVect n;
};

static inline struct Edge
EdgeNew(cpVect va, cpVect vb, int ha, int hb)
{
	struct Edge edge = {{va, ha}, {vb, hb}, cpvnormalize(cpvperp(cpvsub(vb, va)))};
	return edge;
}

static struct Edge
SupportEdge(const cpPolyShape *poly, const cpVect n)
{
	int numVerts = poly->numVerts;
	
	int i1 = cpSupportPointIndex(poly, n);
	int i0 = (i1 - 1 + numVerts)%numVerts;
	int i2 = (i1 + 1)%numVerts;
	
	cpVect v0 = poly->tVerts[i0];
	cpVect v1 = poly->tVerts[i1];
	cpVect v2 = poly->tVerts[i2];
	
	if(cpvdot(n, cpvsub(v1, v0)) < cpvdot(n, cpvsub(v1, v2))){
		return EdgeNew(v0, v1, CP_HASH_PAIR(poly, i0), CP_HASH_PAIR(poly, i1));
	} else {
		return EdgeNew(v1, v2, CP_HASH_PAIR(poly, i1), CP_HASH_PAIR(poly, i2));
	}
}

static int
ClipContacts(const struct Edge ref, const struct Edge inc, cpFloat flipped, cpContact *arr)
{
	cpFloat cian = cpvcross(inc.a.v, ref.n);
	cpFloat cibn = cpvcross(inc.b.v, ref.n);
	cpFloat cran = cpvcross(ref.a.v, ref.n);
	cpFloat crbn = cpvcross(ref.b.v, ref.n);
	
	cpFloat dran = cpvdot(ref.a.v, ref.n);
	cpFloat dian = cpvdot(inc.a.v, ref.n) - dran;
	cpFloat dibn = cpvdot(inc.b.v, ref.n) - dran;
	
	int numContacts = 0;
	
	cpFloat t1 = cpfclamp01((cian - cran)/(cian - cibn));
	cpFloat d1 = cpflerp(dian, dibn, t1);
	if(d1 <= 0.0){
		cpContactInit(nextContactPoint(arr, &numContacts), t1 < 1.0 ? ref.a.v : inc.b.v, cpvmult(ref.n, flipped), d1, CP_HASH_PAIR(ref.a.hash, inc.b.hash));
	}
	
	cpFloat t2 = cpfclamp01((cibn - crbn)/(cibn - cian));
	cpFloat d2 = cpflerp(dibn, dian, t2);
	if(d2 <= 0.0){
		cpContactInit(nextContactPoint(arr, &numContacts), t2 < 1.0 ? ref.b.v : inc.a.v, cpvmult(ref.n, flipped), d2, CP_HASH_PAIR(ref.b.hash, inc.a.hash));
	}
	
	cpAssertWarn(numContacts > 0, "No contacts?");
	return numContacts;
}

static int
ContactPoints(const cpPolyShape *a, const cpPolyShape *b, cpVect n, cpContact *arr)
{
//	if(points.d > 0.0) return 0;
//	
//	cpVect n = cpvmult(cpvsub(points.b, points.a), 1.0f/points.d);
	
	struct Edge f1 = SupportEdge(a, n);
	struct Edge f2 = SupportEdge(b, cpvneg(n));
	
	if(cpvdot(f1.n, n) > -cpvdot(f2.n, n)){
		return ClipContacts(f1, f2,  1.0, arr);
	} else {
		return ClipContacts(f2, f1, -1.0, arr);
	}
}
// Collide poly shapes together.
static int
poly2poly(const cpShape *shape1, const cpShape *shape2, cpContact *arr)
{
	cpPolyShape *poly1 = (cpPolyShape *)shape1;
	cpPolyShape *poly2 = (cpPolyShape *)shape2;
	
	cpFloat min1;
	int mini1 = findMSA(poly2, poly1->tPlanes, poly1->numVerts, &min1);
	if(mini1 == -1) return 0;
	
	cpFloat min2;
	int mini2 = findMSA(poly1, poly2->tPlanes, poly2->numVerts, &min2);
	if(mini2 == -1) return 0;
	
	// There is overlap, find the penetrating verts
	if(min1 > min2){
		return ContactPoints(poly1, poly2, poly1->tPlanes[mini1].n, arr);
	} else {
		return ContactPoints(poly1, poly2, cpvneg(poly2->tPlanes[mini2].n), arr);
	}
	
//	// There is overlap, find the penetrating verts
//	if(min1 > min2)
//		return findVerts(arr, poly1, poly2, poly1->tPlanes[mini1].n, min1);
//	else
//		return findVerts(arr, poly1, poly2, cpvneg(poly2->tPlanes[mini2].n), min2);
}

// Like cpPolyValueOnAxis(), but for segments.
static inline cpFloat
segValueOnAxis(const cpSegmentShape *seg, const cpVect n, const cpFloat d)
{
	cpFloat a = cpvdot(n, seg->ta) - seg->r;
	cpFloat b = cpvdot(n, seg->tb) - seg->r;
	return cpfmin(a, b) - d;
}

// Identify vertexes that have penetrated the segment.
static inline void
findPointsBehindSeg(cpContact *arr, int *num, const cpSegmentShape *seg, const cpPolyShape *poly, const cpFloat pDist, const cpFloat coef) 
{
	cpFloat dta = cpvcross(seg->tn, seg->ta);
	cpFloat dtb = cpvcross(seg->tn, seg->tb);
	cpVect n = cpvmult(seg->tn, coef);
	
	for(int i=0; i<poly->numVerts; i++){
		cpVect v = poly->tVerts[i];
		if(cpvdot(v, n) < cpvdot(seg->tn, seg->ta)*coef + seg->r){
			cpFloat dt = cpvcross(seg->tn, v);
			if(dta >= dt && dt >= dtb){
				cpContactInit(nextContactPoint(arr, num), v, n, pDist, CP_HASH_PAIR(poly->shape.hashid, i));
			}
		}
	}
}

// This one is complicated and gross. Just don't go there...
// TODO: Comment me!
static int
seg2poly(const cpShape *shape1, const cpShape *shape2, cpContact *arr)
{
	cpSegmentShape *seg = (cpSegmentShape *)shape1;
	cpPolyShape *poly = (cpPolyShape *)shape2;
	cpSplittingPlane *planes = poly->tPlanes;
	
	cpFloat segD = cpvdot(seg->tn, seg->ta);
	cpFloat minNorm = cpPolyShapeValueOnAxis(poly, seg->tn, segD) - seg->r;
	cpFloat minNeg = cpPolyShapeValueOnAxis(poly, cpvneg(seg->tn), -segD) - seg->r;
	if(minNeg > 0.0f || minNorm > 0.0f) return 0;
	
	int mini = 0;
	cpFloat poly_min = segValueOnAxis(seg, planes->n, planes->d);
	if(poly_min > 0.0f) return 0;
	for(int i=0; i<poly->numVerts; i++){
		cpFloat dist = segValueOnAxis(seg, planes[i].n, planes[i].d);
		if(dist > 0.0f){
			return 0;
		} else if(dist > poly_min){
			poly_min = dist;
			mini = i;
		}
	}
	
	int num = 0;
	
	cpVect poly_n = cpvneg(planes[mini].n);
	
	cpVect va = cpvadd(seg->ta, cpvmult(poly_n, seg->r));
	cpVect vb = cpvadd(seg->tb, cpvmult(poly_n, seg->r));
	if(cpPolyShapeContainsVert(poly, va))
		cpContactInit(nextContactPoint(arr, &num), va, poly_n, poly_min, CP_HASH_PAIR(seg->shape.hashid, 0));
	if(cpPolyShapeContainsVert(poly, vb))
		cpContactInit(nextContactPoint(arr, &num), vb, poly_n, poly_min, CP_HASH_PAIR(seg->shape.hashid, 1));
	
	// Floating point precision problems here.
	// This will have to do for now.
//	poly_min -= cp_collision_slop; // TODO is this needed anymore?
	
	if(minNorm >= poly_min || minNeg >= poly_min) {
		if(minNorm > minNeg)
			findPointsBehindSeg(arr, &num, seg, poly, minNorm, 1.0f);
		else
			findPointsBehindSeg(arr, &num, seg, poly, minNeg, -1.0f);
	}
	
	// If no other collision points are found, try colliding endpoints.
	if(num == 0){
		cpVect poly_a = poly->tVerts[mini];
		cpVect poly_b = poly->tVerts[(mini + 1)%poly->numVerts];
		
		if(circle2circleQuery(seg->ta, poly_a, seg->r, 0.0f, arr)) return 1;
		if(circle2circleQuery(seg->tb, poly_a, seg->r, 0.0f, arr)) return 1;
		if(circle2circleQuery(seg->ta, poly_b, seg->r, 0.0f, arr)) return 1;
		if(circle2circleQuery(seg->tb, poly_b, seg->r, 0.0f, arr)) return 1;
	}

	return num;
}

// This one is less gross, but still gross.
// TODO: Comment me!
static int
circle2poly(const cpShape *shape1, const cpShape *shape2, cpContact *con)
{
	cpCircleShape *circ = (cpCircleShape *)shape1;
	cpPolyShape *poly = (cpPolyShape *)shape2;
	cpSplittingPlane *planes = poly->tPlanes;
	
	int mini = 0;
	cpFloat min = cpSplittingPlaneCompare(planes[0], circ->tc) - circ->r;
	for(int i=0; i<poly->numVerts; i++){
		cpFloat dist = cpSplittingPlaneCompare(planes[i], circ->tc) - circ->r;
		if(dist > 0.0f){
			return 0;
		} else if(dist > min) {
			min = dist;
			mini = i;
		}
	}
	
	cpVect n = planes[mini].n;
	cpVect a = poly->tVerts[(mini - 1 + poly->numVerts)%poly->numVerts];
	cpVect b = poly->tVerts[mini];
	cpFloat dta = cpvcross(n, a);
	cpFloat dtb = cpvcross(n, b);
	cpFloat dt = cpvcross(n, circ->tc);
		
	if(dt < dtb){
		return circle2circleQuery(circ->tc, b, circ->r, 0.0f, con);
	} else if(dt < dta) {
		cpContactInit(
			con,
			cpvsub(circ->tc, cpvmult(n, circ->r + min/2.0f)),
			cpvneg(n),
			min,
			0				 
		);
	
		return 1;
	} else {
		return circle2circleQuery(circ->tc, a, circ->r, 0.0f, con);
	}
}

static const collisionFunc builtinCollisionFuncs[9] = {
	circle2circle,
	NULL,
	NULL,
	(collisionFunc)circle2segment,
	NULL,
	NULL,
	circle2poly,
	seg2poly,
	poly2poly,
};
static const collisionFunc *colfuncs = builtinCollisionFuncs;

//static collisionFunc *colfuncs = NULL;
//
//static void
//addColFunc(const cpShapeType a, const cpShapeType b, const collisionFunc func)
//{
//	colfuncs[a + b*CP_NUM_SHAPES] = func;
//}
//
//#ifdef __cplusplus
//extern "C" {
//#endif
//	void cpInitCollisionFuncs(void);
//	
//	// Initializes the array of collision functions.
//	// Called by cpInitChipmunk().
//	void
//	cpInitCollisionFuncs(void)
//	{
//		if(!colfuncs)
//			colfuncs = (collisionFunc *)cpcalloc(CP_NUM_SHAPES*CP_NUM_SHAPES, sizeof(collisionFunc));
//		
//		addColFunc(CP_CIRCLE_SHAPE,  CP_CIRCLE_SHAPE,  circle2circle);
//		addColFunc(CP_CIRCLE_SHAPE,  CP_SEGMENT_SHAPE, circle2segment);
//		addColFunc(CP_SEGMENT_SHAPE, CP_POLY_SHAPE,    seg2poly);
//		addColFunc(CP_CIRCLE_SHAPE,  CP_POLY_SHAPE,    circle2poly);
//		addColFunc(CP_POLY_SHAPE,    CP_POLY_SHAPE,    poly2poly);
//	}	
//#ifdef __cplusplus
//}
//#endif

int
cpCollideShapes(const cpShape *a, const cpShape *b, cpContact *arr)
{
	// Their shape types must be in order.
	cpAssertSoft(a->klass->type <= b->klass->type, "Collision shapes passed to cpCollideShapes() are not sorted.");
	
	collisionFunc cfunc = colfuncs[a->klass->type + b->klass->type*CP_NUM_SHAPES];
	return (cfunc) ? cfunc(a, b, arr) : 0;
}
