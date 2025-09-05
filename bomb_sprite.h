// bomb_sprite.h
#pragma once
#include <stdint.h>
#include <pgmspace.h>

#define BOMB_W 32
#define BOMB_H 24
// Magenta colorkey; any pixel with this value is treated as transparent.
#define BOMB_TRANSPARENCY_KEY 0xF81F

// Minimal fallback sprite (all black). The app scales this to fill the screen.
// You can swap this out later for real pixel art.
static const uint16_t bomb_sprite[BOMB_W * BOMB_H] PROGMEM = { 0x0000 };
