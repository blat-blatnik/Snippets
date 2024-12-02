#version 460

#define EPSILON 1e-5 // Used to test if float is close to 0. Tweak this if you get problems.

struct Sweep {
	float time;  // Non-negative time of first contact.
	float depth; // Non-negative penetration depth if objects start initially colliding.
	vec3 point;  // Point of first-contact. Only updated when contact occurs.
	vec3 normal; // Unit-length collision normal. Only updated when contact occurs.
};

// Return whether point P is contained inside 3D region delimited by triangle T0,T1,T2 edges.
bool pointInsideTriangle(vec3 p, vec3 t0, vec3 t1, vec3 t2) {
	// Real-Time Collision Detection: 3.4: Barycentric Coordinates (pages 46-52).
	//
	// The book also has a subsection dedicated to point inside triangle tests:
	// Real-Time Collision Detection: 5.4.2: Testing Point in Triangle (pages 203-206).
	// But those tests only work for CCW triangles. This seems to work for either orientation.
	vec3 t01 = t1 - t0;
	vec3 t02 = t2 - t0;
	vec3 t0p = p - t0;
	float t01t01 = dot(t01,t01);
	float t01t02 = dot(t01,t02);
	float t02t02 = dot(t02,t02);
	float t0pt01 = dot(t0p,t01);
	float t0pt02 = dot(t0p,t02);
	float denom = t01t01*t02t02 - t01t02*t01t02;
	
	// Normally I would have to divide vd,wd by denom to get v,w. But divisions are
	// expensive and cause troubles around 0. If denom isn't negative then we don't
	// ever need to divide. If in the future it does turn out denom can be negative
	// then we can always multiply by denom instead of dividing to keep sign the same.
	float vd = t02t02*t0pt01 - t01t02*t0pt02;
	float wd = t01t01*t0pt02 - t01t02*t0pt01;
	return vd >= 0 && wd >= 0 && vd + wd <= denom;
}
// Return whether point P is contained inside 3D region delimited by parallelogram P0,P1,P2 edges.
bool pointInsideParallelogram(vec3 p, vec3 p0, vec3 p1, vec3 p2) {
	// There may be a better way.
	// https://math.stackexchange.com/questions/4381852/point-in-parallelogram-in-3d-space
	vec3 p3 = p2 + (p1 - p0);
	return pointInsideTriangle(p,p0,p1,p2) || pointInsideTriangle(p,p1,p3,p2);
}
// Return whether point P is contained inside a triangular prism A0,A1,A2-B0,B1,B2.
bool pointInsideTriangularPrism(vec3 p, vec3 a0, vec3 a1, vec3 a2, vec3 b0, vec3 b1, vec3 b2) {
	vec3 faces[5][3] = { { a0,a1,a2 }, { b0,b2,b1 }, { a0,b0,a1 }, { a1,b1,a2 }, { a2,b2,a0 } };
	float sgn = 0;
	for (int i = 0; i < faces.length(); i++) {
		vec3 p0 = faces[i][0];
		vec3 p1 = faces[i][1];
		vec3 p2 = faces[i][2];
		
		// Check which side of plane point is in. If it's always on the same side, it's colliding.
		vec3 p01 = p1 - p0;
		vec3 p02 = p2 - p0;
		vec3 n = cross(p01,p02);
		float d = dot(n,p - p0);
		if (i == 0) sgn = d;
		if (sgn*d <= 0) 
			return false;
	}
	return true;
}
// Sweep sphere C,r with velocity Sv against plane N of triangle T0,T1,T2, ignoring edges.
bool sweepSphereTrianglePlane(inout Sweep sweep, vec3 c, float r, vec3 v, vec3 t0, vec3 t1, vec3 t2, vec3 n) {
	// Real-Time Collision Detection 5.5.3: Intersecting Moving Sphere Against Plane (pages 219-223).
	float t;
	float d = dot(n,c - t0);
	float pen = r - d;
	if (pen > 0)
		t = 0; // Sphere already starts coliding with triangle plane.
	else {
		// Sphere isn't immediately colliding with the plane. Check if it's moving away.
		float denom = dot(n,v);
		if (denom >= 0)
			return false; // Sphere is moving away from plane.
		
		// Sphere will collide with plane at some point.
		t = (r - d)/denom;
		pen = 0;
	}
	
	// If sphere misses entire triangle plane, then it definitely misses the triangle too.
	if (t >= sweep.time)
		return false;
	
	// Is the plane collision point inside the triangle?
	// Real-Time Collision Detection: 5.4.2: Testing Point in Triangle (pg 203-206).
	vec3 collision = c + t*v - r*n;
	if (!pointInsideTriangle(collision,t0,t1,t2))
		return false;
		
	// Plane collision point is inside the triangle. So the sphere collides with the triangle.
	sweep.time = t;
	sweep.depth = pen;
	sweep.point = collision;
	sweep.normal = n;
	return true;
}
// Sweep sphere C,r with velocity V against plane N of parallelogram P0,P1,P2 ignoring edges.
bool sweepSphereParallelogramPlane(inout Sweep sweep, vec3 c, float r, vec3 v, vec3 p0, vec3 p1, vec3 p2, vec3 n) {
	// Real-Time Collision Detection 5.5.3: Intersecting Moving Sphere Against Plane (pages 219-223).
	float t;
	float d = dot(c,n - p0);
	float pen = r - d;
	if (pen > 0)
		t = 0; // Sphere already starts coliding with the quad plane.
	else {
		// Sphere isn't immediately colliding with the plane. Check if it's moving away.
		float denom = dot(n,v);
		if (denom >= 0)
			return false; // Sphere is moving away from plane.
		
		// Sphere will collide with plane at some point.
		t = (r - d)/denom;
		pen = 0;
	}
	
	// If sphere misses entire quad plane, then it definitely misses the quad too.
	if (t >= sweep.time)
		return false;
	
	// Is the plane collision point inside the quad?
	// Real-Time Collision Detection: 5.4.2: Testing Point in Triangle (pages 203-206).
	vec3 collision = c + t*v - r*n;
	if (!pointInsideParallelogram(collision,p0,p1,p2))
		return false;
	
	// Plane collision point is inside the quad. So the sphere collides with the quad.
	sweep.time = t;
	sweep.depth = pen;
	sweep.point = collision;
	sweep.normal = n;
	return true;
}
// Sweep point P with velocity V against sphere S,r.
bool sweepPointSphere(inout Sweep sweep, vec3 p, vec3 v, vec3 s, float r, vec3 fallbackNormal) {
	// Real-Time Collision Detection 5.3.2: Intersecting Ray or Segment Against Sphere (pages 177-179).
	
	// Set up quadratic equation.
	vec3 d = p - s;
	float b = dot(d,v);
	float c = dot(d,d) - r*r;
	if (c > 0 && b > 0)
		return false; // Point starts outside (c > 0) and moves away from sphere (b > 0).
	float a = dot(v,v);
	float discr = b*b - a*c;
	if (discr < 0)
		return false; // Point misses sphere.
	
	// Point hits sphere. Compute time of first impact.
	float t = (-b - sqrt(discr))/a;
	if (t >= sweep.time)
		return false;
	
	// The sphere is the first thing the point hits so far.
	t = max(t, 0);
	vec3 collision = p + t*v;
	vec3 vec = collision - s;
	float len = length(vec);
	sweep.time = t;
	sweep.depth = t > 0 ? 0 : r - len;
	sweep.point = collision;
	sweep.normal = len >= EPSILON ? vec/len : fallbackNormal;
	return true;
}
// Sweep point P with velocity V against cylinder C0,C1,r, ignoring the endcaps.
bool sweepPointUncappedCylinder(inout Sweep sweep, vec3 p, vec3 v, vec3 c0, vec3 c1, float r, vec3 fallbackNormal) {
	// Real-Time Collision Detection 5.3.7: Intersecting Ray or Segment Against Cylinder (pages 194-198).
	
	// Test if swept point is fully outside of either endcap.
	vec3 n = c1 - c0;
	vec3 d = p - c0;
	float dn = dot(d,n);
	float vn = dot(v,n);
	float nn = dot(n,n);
	if (dn < 0 && dn + vn < 0)
		return false; // Fully outside c0 end of cylinder.
	if (dn > nn && dn + vn > nn)
		return false; // Fully outside c1 end of cylinder.
	
	// Set up quadratic equations and check if sweep direction is parallel to cylinder.
	float t;
	float vv = dot(v,v);
	float dv = dot(d,v);
	float dd = dot(d,d);
	float a = nn*vv - vn*vn;
	float c = nn*(dd - r*r) - dn*dn;
	if (a < EPSILON) {
		// Sweep direction is parallel to cylinder.
		if (c > 0)
			return false; // Point starts outside of cylinder, so it never collides.
		if (dn < 0)
			return false; // Point starts outside of c0 endcap.
		if (dn > nn)
			return false; // Point starts outside of c1 endcap.
		t = 0;
	} else {
		// Sweep direction is not parallel to cylinder. Solve for time of first contact.
		float b = nn*dv - vn*dn;
		float discr = b*b - a*c;
		if (discr < 0)
			return false; // Sweep misses cylinder.
		t = (-b - sqrt(discr))/a;
	}
	
	// Check if the sweep missed, or if it hits but another collision happens sooner.
	if (t < 0 || t >= sweep.time)
		return false;
	
	// This is the first collision. Find the closest point on the center of the cylinder.
	vec3 collision = p + t*v;
	vec3 center;
	if (nn < EPSILON)
		center = c0; // The cylinder is actually a circle.
	else
		center = c0 + (dot(collision - c0,n)/nn)*n;
	
	// Update collision time, depth, and normal.
	vec3 vec = collision - center;
	float len = length(vec);
	float depth = r - len;
	sweep.time = t;
	sweep.depth = t > 0 ? 0 : depth;
	sweep.point = collision;
	sweep.normal = len >= EPSILON ? vec/len : fallbackNormal;
	return true;
}

// Sweep a capsule C0,C1,Cr with velocity Cv against the triangle T0,T1,T2.
//   c0,c1      capsule line segment endpoints
//   r          capsule radius
//   v          capsule velocity
//   t0,t1,t2   3 triangle vertices
//   returns    whether the capsule and triangle intersect
bool sweepCapsuleTriangle(inout Sweep s, vec3 c0, vec3 c1, float r, vec3 v, vec3 t0, vec3 t1, vec3 t2) {
	// Compute triangle plane equation.
	vec3 t01 = t1 - t0;
	vec3 t02 = t2 - t0;
	vec3 normal = normalize(cross(t01,t02));
	
	// Extrude triangle along capsule direction.
	vec3 c01 = c1 - c0;
	vec3 a0 = t0;
	vec3 a1 = t1;
	vec3 a2 = t2;
	vec3 b0 = t0 - c01;
	vec3 b1 = t1 - c01;
	vec3 b2 = t2 - c01;
	
	// Test for initial collision with the extruded triangle prism.
	if (pointInsideTriangularPrism(c0,a0,a1,a2,b0,b1,b2)) {
		// Capsule starts off penetrating triangle. Push it out from the triangle plane.
		float d0 = dot(normal,c0 - t0);
		float d1 = dot(normal,c1 - t0);
		float d = abs(d0) <= abs(d1) ? d0 : d1;
		vec3 n = d >= 0 ? normal : -normal;
		s.time = 0;
		s.depth = abs(d) + r;
		s.normal = n;
		s.point = c0 + d0*normal;
		return true;
	}
	
	// Decompose capsule triangle sweep into: 2 sphere-triangle + 3 sphere-parallelogram + 9 point-cylinder + 6 point-sphere sweeps.
	bool hit = false;
	vec3 triangles[2][3] = {{a0,a1,a2}, {b0,b1,b2}};
	vec3 parallelograms[3][3] = {{a0,a1,b0}, {a1,a2,b1}, {a2,a0,b2}};
	vec3 cylinders[9][2] = {{a0,a1}, {a1,a2}, {a2,a0}, {b0,b1}, {b1,b2}, {b2,b0}, {a0,b0}, {a1,b1}, {a2,b2}};
	vec3 spheres[6] = {a0, a1, a2, b0, b1, b2};
	
	// Do sphere-triangle sweeps.
	vec3 triangleNormals[2];
	for (int i = 0; i < triangles.length(); i++) {
		vec3 p0 = triangles[i][0];
		vec3 p1 = triangles[i][1];
		vec3 p2 = triangles[i][2];
		
		// Compute triangle plane normal.
		vec3 n = normal;
		if (dot(n,c0 - p0) < 0) n = -n; // Orient towards sphere.
		triangleNormals[i] = n;
		
		// Test for triangle-plane sphere intersection.
		hit = hit || sweepSphereTrianglePlane(s,c0,r,v,p0,p1,p2,n);
	}
	
	// Do sphere-parallelogram sweeps.
	vec3 parallelogramNormals[3];
	for (int i = 0; i < parallelograms.length(); i++) {
		vec3 p0 = parallelograms[i][0];
		vec3 p1 = parallelograms[i][1];
		vec3 p2 = parallelograms[i][2];
		
		// Check if quad is degenerate. Happens when triangle edge completely parallel to capsule.
		vec3 p01 = p1 - p0;
		vec3 p02 = p2 - p0;
		vec3 c = cross(p01,p02);
		float len = length(c);
		if (len > EPSILON) {
			// Compute quad plane equation.
			vec3 n = c/len;
			if (dot(n,c0 - p0) < 0) n = -n; // Orient towards sphere.
			parallelogramNormals[i] = n;
			
			// Do the sweep test.
			hit = hit || sweepSphereParallelogramPlane(s,c0,r,v,p0,p1,p2,n);
		}
		else parallelogramNormals[i] = triangleNormals[0];
	}
	
	// Do point-cylinder sweeps.
	for (int i = 0; i < cylinders.length(); i++) {
		vec3 p0 = cylinders[i][0];
		vec3 p1 = cylinders[i][1];
		vec3 n;
		if (i < 6)
			n = triangleNormals[i/3];
		else
			n = parallelogramNormals[i - 6];
		hit = hit || sweepPointUncappedCylinder(s,c0,v,p0,p1,r,n);
	}
	
	// Do point-sphere sweeps.
	for (int i = 0; i < spheres.length(); i++) {
		vec3 c = spheres[i];
		vec3 n = triangleNormals[i/3];
		hit = hit || sweepPointSphere(s,c0,v,c,r,n);
	}
	
	return hit;
}

// Move a capsule and resolve any triangle collisions encountered along the way.
//   p         - capsule base position
//   v         - capsule velocity
//   h         - capsule height
//   r         - capsule radius
//   dt        - time-step length
//   triangles - list of triangles to collide with
void resolveCollisions(inout vec3 p, inout vec3 v, float h, float r, float dt, vec3 triangles[999][3]) {
	// Store the leftover movement in this vector.
	vec3 u = dt*v;

	// Move and resolve collisions while there is still motion. But cap max iterations to ensure simulation terminates.
	const int MAX_ITER = 16;
	for (int iter = 0; iter < MAX_ITER && dot(u,u) > 0; iter++) {
		// Compute capsule endpoints.
		vec3 c0 = p;
		vec3 c1 = p;
		c0.y += r;
		c1.y += h - r;
		
		// Perform the sweep test against all triangles.
		Sweep s;
		s.time = 1;
		for (int i = 0; i < triangles.length(); i++) {
			vec3 t0 = triangles[i][0];
			vec3 t1 = triangles[i][1];
			vec3 t2 = triangles[i][2];
			sweepCapsuleTriangle(s, c0, c1, r, u, t0, t1, t2);
		}

		// Stop objects from intersecting.
		if (s.depth > 0)
			p += (s.depth + EPSILON)*s.normal;

		// Advance the cylinder until the first contact time.
		vec3 dp = s.time*u;
		p += dp;

		// If there were no collisions, entire motion is complete and we can terminate early.
		if (s.time >= 1)
			break;

		// Cancel out motion parallel to the normal. This causes capsule to slide along surface.
		u -= dp;
		u += dot(u,s.normal)*s.normal;
		v += dot(v,s.normal)*s.normal;

		// Nudge the position and velocity slightly away from surface to avoid another collision.
		vec3 offset = EPSILON*s.normal;
		p += offset;
		v += offset;
		u += offset;
	}
}
