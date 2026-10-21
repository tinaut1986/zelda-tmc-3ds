/* Hand-curated stereo 3D depth corrections per kind of entity (docs/stereo-3d.md, "Sprite depth corrections").
 *
 * PORT_SPRITE_DEPTH(kind, id, type, mode)
 *   kind  the entity's kind (ENEMY, OBJECT, NPC...)
 *   id    its id inside that kind (BOSS_DOOR, CHUCHU_BOSS...)
 *   type  its type, or PORT_ANY_TYPE
 *   mode  PORT_DEPTH_INVERT  the sprite hangs from its top edge (a door in a south wall): the depth is nil
 *                            there and grows downwards, where the world's normal rule has it upwards
 *         PORT_DEPTH_FLAT    no lean at all
 *
 * The file is optional: without it every sprite follows the normal rule.
 */
PORT_SPRITE_DEPTH(OBJECT, BOSS_DOOR, PORT_ANY_TYPE, PORT_DEPTH_INVERT)
