/* Hand-curated stereo 3D "zone depth fixes" (docs/stereo-3d.md, "Zone depth fixes").
 *
 *   PORT_ZONE_DEPTH(area, room, bg, x, y, w, h, toBg)
 *
 *   area, room  the room the fix belongs to: the game's area id and room id (gRoomControls.area / .room), numbers,
 *               hex ok (0x02, 0x00)
 *   bg          1, 2 or 3: the layer whose pixels inside the rectangle are moved
 *   x, y, w, h  the rectangle, in ROOM pixel coordinates (from the room's origin, the coordinates the layer workbench
 *               shows for the room; multiples of 8 in practice, 16 for a whole metatile)
 *   toBg        1, 2 or 3, not bg: the layer whose stereo plane the rectangle takes
 *
 * Meaning: draw this rectangle of layer bg at the depth of layer toBg. Only in the world scene with 3D on (the slider up,
 * 3D WORLD on), only while the current room is area/room, and only when the two layers have different depths; the GPU
 * renderer draws the rectangle once more over the layer's own picture, moved per eye as toBg is, in toBg's place in the
 * drawing order (or bg's, whichever is in front). Several lines per room are fine (16 at most per room). The software
 * renderer is flat and ignores them. The file is optional.
 *
 * Edited by hand or by tools/layer-workbench; one entry per line, comments directly above an entry belong to it.
 */
/* Hyrule Town (MAIN): the log hut's roof is BG1, but one metatile of it (column 17, row 54) is a hole where BG2, the
 * ground's layer, shows the roof's little chick; without this it sat on the ground's plane inside the raised roof. */
PORT_ZONE_DEPTH(0x02, 0x00, 2, 272, 864, 16, 16, 1)
