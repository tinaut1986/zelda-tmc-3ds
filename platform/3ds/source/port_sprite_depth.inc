/* Hand-curated stereo 3D depth corrections per kind of entity (docs/stereo-3d.md, "Sprite depth corrections").
 *
 * PORT_SPRITE_DEPTH(kind, id, type, mode)
 *   kind  the entity's kind (ENEMY, OBJECT, NPC...)
 *   id    its id inside that kind (BOSS_DOOR, CHUCHU_BOSS...)
 *   type  its type, or PORT_ANY_TYPE
 *   mode  where the sprite meets the ground, and so which way its depth grows (nil at the ground, rising across the
 *         sprite by the same rule as a standing one's, capped):
 *         PORT_DEPTH_UP      the normal rule (what an entity without an entry gets): ground at the bottom edge, the
 *                            depth grows upwards
 *         PORT_DEPTH_DOWN    (or PORT_DEPTH_INVERT) the sprite hangs from its top edge (a door in a south wall): the
 *                            depth grows downwards
 *         PORT_DEPTH_LEFT    the sprite hangs on a wall to its left (a curtain on a west wall): ground at its right
 *                            edge, the depth grows towards the left of the screen, column of tiles by column
 *         PORT_DEPTH_RIGHT   the mirror: on a wall to its right, ground at its left edge, the depth grows to the right
 *         PORT_DEPTH_FLAT    no lean at all
 *         PORT_DEPTH_RAISED  no lean, the whole sprite at one raised depth: the one the normal rule gives its top edge
 *                            (the entity's own depth plus the rise of its whole height, feet to top; 20 quarters at most)
 *
 * The HUD's sprites (priority 0) ignore every mode. A side lean (LEFT, RIGHT) gives all of its shift to one eye, so its
 * columns never part (docs/stereo-3d.md).
 *
 * The file is optional: without it every sprite follows the normal rule.
 */
PORT_SPRITE_DEPTH(OBJECT, BOSS_DOOR, PORT_ANY_TYPE, PORT_DEPTH_INVERT)
/* The big Chuchu's shadow is an entity of its own (the same graphics as its body with a dark palette): flat on the ground. */
PORT_SPRITE_DEPTH(OBJECT, CHUCHU_BOSS_START_PARTICLE, PORT_ANY_TYPE, PORT_DEPTH_FLAT)
/* The curtain on the left wall of a room (area 0x20, room 00): drawn as a standing sprite, but it hangs on the wall, so
 * its depth grows from the room (its right edge, on the floor's plane) towards the wall on the left. */
PORT_SPRITE_DEPTH(OBJECT, GENTARI_CURTAIN, PORT_ANY_TYPE, PORT_DEPTH_LEFT)
/* The small archway left of that curtain (area 0x20, room 00, x 8 y 80): part of the wall, so it stays flat with the
 * rest of the scenery. Raised (one constant depth at its top's height) was tried and looked odd on the console. Type 2
 * only: type 1 of the same object sits at the bottom of the room and keeps the normal rule. */
PORT_SPRITE_DEPTH(OBJECT, MINISH_SIZED_ARCHWAY, 2, PORT_DEPTH_FLAT)
/* The side archways of the Gentari exit room (area 0x20, room 01): the left one hangs on the left wall, the right one on
 * the right wall, so the depth follows the wall they are on. */
PORT_SPRITE_DEPTH(OBJECT, MINISH_SIZED_ARCHWAY, 3, PORT_DEPTH_LEFT)
PORT_SPRITE_DEPTH(OBJECT, MINISH_SIZED_ARCHWAY, 4, PORT_DEPTH_RIGHT)
