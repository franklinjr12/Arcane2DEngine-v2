#include "Player.hpp"

void Player::_update() {
	if (animation.has_animation()) {
		animation.next_frame();
		image->texture_id = animation.images[animation.current_frame]->texture_id;
	}
}

