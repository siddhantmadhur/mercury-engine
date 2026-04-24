function on_ready()
	print("Hi")
end

local SPEED = 100
function on_tick(delta_t)
	print("fps")
	local position = get_position()
	position += SPEED * delta_t
end