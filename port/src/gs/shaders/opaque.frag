#version 450
// Alpha 1 everywhere on the window's picture (drawn with only alpha written). Android composites a window by its
// alpha where the GPU cannot present opaquely ("Opaque presentation unsupported"), and the PS2 picture's alpha is
// whatever the game left there (0 over most of a fight): without this the fight showed as a black screen.
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
}
