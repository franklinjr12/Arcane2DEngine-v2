#include "ArcaneUtils.hpp"

Image& GetBlueOverlayImage() {
	static Image blue_overlay("assets/blue_overlay.png");
	return blue_overlay;
}

float DistanceVecf(Vecf v1, Vecf v2) {
	float a = v1[0] - v2[0];
	float b = v1[1] - v2[1];
	return sqrt(a * a + b * b);
}

float MagVecf(Vecf v) {
	return sqrt(v[0] * v[0] + v[1] * v[1]);
}

A2D_API bool VectorHasGroupId(std::vector<ObjectGroup>& groups, ObjectGroup& id) {
	for (int i = 0; i < groups.size(); i++) {
		if ((uint32_t)groups[i] == (uint32_t)id)
			return true;
	}
	return false;
}
