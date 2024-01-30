// 2D continuous collision detection tests.
// 
// These are very similar to ray tracing routines, but they're used for
// collision detection in 2D. Instead of moving the object and then testing
// and correcting for collisions after the fact, you can use these routines
// to get the exact time point when the collision will occur and stop right
// before.
// 
// Here is an example loop you can use for moving a circular player in a
// world full of rectangle colliders:
//
// for (int iter = 0; iter < MAX_ITER && (player.vx != 0 || player.vy != 0); iter++) {
// 	Hit nearest = { 1 };
// 	for (int i = 0; i < numColliders; i++) {
// 		Hit hit = circleRect(player.x, player.y, player.radius, collider[i].x, collider[i].y, collider[i].rx, collider[i].ry);
// 		if (hit.t < nearest.t) nearest = hit;
// 	}
// 	player.x += player.vx * nearest.t;
// 	player.y += player.vy * nearest.t;
// 	player.vx *= (1 - nearest.t);
// 	player.vy *= (1 - nearest.t);
// 	if (nearest.t < 1) {
// 		float dot = player.vx * nearest.nx + player.vy * nearest.ny;
// 		player.vx -= hit.nx * dot;
// 		player.vy -= hit.ny * dot;
// 		player.x += hit.nx * EPSILON;
// 		player.y += hit.ny * EPSILON;
// 	}
// }

#include <math.h>

typedef struct Hit {
	float t; // Time of collision. 0 <= t < 1. If no collision: t >= 1.
	float nx; // Collision normal. 0 if no collision.
	float ny;
} Hit;

// Moving point vs stationary circle.
// x,y = point starting position
// vx,vy = point velocity
// cx,cy = circle center position
// r = circle radius
Hit pointCircle(float x, float y, float vx, float vy, float cx, float cy, float r) {
	Hit hit = { 1 };

	// First, check if the ray starts inside of the circle already.
	float dx = x - cx;
	float dy = y - cy;
	float d2 = dx * dx + dy * dy;
	float r2 = r * r;
	if (d2 < r2) { // Ray already starts inside of circle and collides immediately.
		hit.t = 0;
		float d = sqrtf(d2);
		if (d > 0) {
			hit.nx = dx / d;
			hit.ny = dy / d;
		}
		else { // Ray is directly at circle center. Normal is arbitrary.
			hit.nx = 1;
			hit.ny = 0;
		}
		return hit;
	}

	// Now solve quadratic to find the intersection points and get the closest one.
	float a = vx * vx + vy * vy;
	float b = vx * dx + vy * dy;
	float c = d2 - r2;
	float disc = b * b - a * c;
	float root = sqrtf(b * b - a * c);
	float t0 = (-b - root) / a;
	float t1 = (-b + root) / a;
	float t = t0 >= 0 ? t0 : t1;
	if (!(0 <= t && t < 1)) return hit; // No hit. Relies on IEEE NaN behavior.

	hit.t = t;
	hit.nx = (dx + vx * t) / r;
	hit.ny = (dy + vy * t) / r;
	return hit;
}

// Moving point vs stationary rectangle.
// x,y = point starting position
// vx,vy = point velocity
// cx,cy = rectangle center position
// rx,ry = rectangle radius (width/2,height/2)
Hit pointRect(float x, float y, float vx, float vy, float cx, float cy, float rx, float ry) {
	Hit hit = { 1 };

	// First, check if the point starts inside of the rectangle already.
	float dx = x - cx;
	float dy = y - cy;
	float absx = dx < 0 ? -dx : +dx;
	float absy = dy < 0 ? -dy : +dy;
	if (absx < rx && absy < ry) {
		hit.t = 0;
		float penx = rx - absx;
		float peny = ry - absy;
		if (penx <= peny)
			hit.nx = dx < 0 ? -1.0f : +1.0f;
		else
			hit.ny = dy < 0 ? -1.0f : +1.0f;
		return hit;
	}

	// Find when collisions with 4 rectangle edges happen.
	float sx = vx < 0 ? -1.0f : +1.0f;
	float sy = vy < 0 ? -1.0f : +1.0f;
	float tx0 = (-sx * rx - dx) / vx;
	float tx1 = (+sx * rx - dx) / vx;
	float ty0 = (-sy * ry - dy) / vy;
	float ty1 = (+sy * ry - dy) / vy;

	// Find time of entry and exit.
	float tmin = 0;
	float tmax = INFINITY;
	tmin = tx0 > tmin ? tx0 : tmin;
	tmin = ty0 > tmin ? ty0 : tmin;
	tmax = tx1 < tmax ? tx0 : tmax;
	tmax = ty1 < tmax ? tx1 : tmax;
	if (!(tmin < tmax && tmin < 1)) return hit; // No hit.

	hit.t = tmin;
	if (tx0 >= ty0)
		hit.nx = -sx;
	else
		hit.ny = -sy;
	return hit;
}

// Moving point vs stationary rectangle with rounded corners.
// x,y = point starting position
// vx,vy = point velocity
// cx,cy = rectangle center position
// rx,ry = rectangle radius (width/2, height/2)
// r = rectangle corner radius
Hit pointRoundRect(float x, float y, float vx, float vy, float cx, float cy, float rx, float ry, float r) {
	// First test against the bounding rect.
	Hit hit = pointRect(x, y, vx, vy, cx, cy, rx, ry);
	if (hit.t >= 1) return hit; // No hit.

	// Find where the ray hits the bounding rect.
	float dx = x - cx;
	float dy = y - cy;
	float hx = dx + vx * hit.t;
	float hy = dy + vy * hit.t;

	// Quadrant correction.
	float qx = hx < 0 ? -1.0f : +1.0f;
	float qy = hy < 0 ? -1.0f : +1.0f;
	hx *= qx;
	hy *= qy;

	// If ray hits the non-circular part, then we're already done.
	float circx = rx - r;
	float circy = ry - r;
	if (hx <= circx || hy <= circy) return hit;

	// Test against the circular corner. Quadrant correct the hit normal.
	dx *= qx;
	dy *= qy;
	vx *= qx;
	vy *= qy;
	hit = pointCircle(dx, dy, vx, vy, circx, circy, r);
	hit.nx *= qx;
	hit.ny *= qy;
	return hit;
}

// By taking the Minkowski sum, these other tests can all be implemented using the routines above.

Hit rectCircle(float x, float y, float rx, float ry, float vx, float vy, float cx, float cy, float r) {
	return pointRoundRect(x, y, vx, vy, cx, cy, rx + r, ry + r, r);
}
Hit rectRect(float ax, float ay, float arx, float ary, float vx, float vy, float bx, float by, float brx, float bry) {
	return pointRect(ax, ay, vx, vy, bx, by, brx + arx, bry + ary);
}
Hit rectRoundRect(float ax, float ay, float arx, float ary, float vx, float vy, float bx, float by, float brx, float bry, float br) {
	return pointRoundRect(ax, ay, vx, vy, bx, by, brx + arx, bry + ary, br);
}
Hit circleCircle(float ax, float ay, float ar, float vx, float vy, float bx, float by, float br) {
	return pointCircle(ax, ay, vx, vy, bx, by, br + ar);
}
Hit circleRect(float x, float y, float r, float vx, float vy, float cx, float cy, float rx, float ry) {
	return pointRoundRect(x, y, vx, vy, cx + r, cy + r, rx, ry, r);
}
Hit circleRoundRect(float x, float y, float r, float vx, float vy, float cx, float cy, float rx, float ry, float br) {
	return pointRoundRect(x, y, vx, vy, cx, cy, rx + r, ry + r, r + br);
}

// A similar strategy can be used to test against 2 moving shapes.

Hit pointMovingCircle(float ax, float ay, float avx, float avy, float cx, float cy, float r, float bvx, float bvy) {
	return pointCircle(ax, ay, avx - bvx, avy - bvy, cx, cy, r);
}

int main(void) {

}
