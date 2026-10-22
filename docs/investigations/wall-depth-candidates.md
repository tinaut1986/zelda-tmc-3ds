# Candidates for PORT_DEPTH_WALL

Entities with a drawn sprite within 24 px of the left, right or bottom wall of their room (what `WALL` looks at), excluding the families that already have a rule (ARCHWAY, MINISH_SIZED_ARCHWAY, BOSS_DOOR, GENTARI_CURTAIN, FURNITURE type 7). Generated from `tools/layer-workbench/maps.json` and `sprites.json` (2026-10-22). `near/total` is how many of that kind/id/type sit by a wall out of all of them: a family listed with a low ratio would only change where it stands by a wall; the rest keep the normal rule.

| kind/id/type | near wall (L/R/B) | near / total | rooms | drawn | example room |
|---|---|---|---|---|---|
| OBJECT/CUTSCENE_ORCHESTRATOR 0 | L53 | 53 / 62 | 23 | 53 | 00-00 |
| NPC/NPC_UNK_4E 0 | L22 | 22 / 39 | 10 | 22 | 00-00 |
| OBJECT/BACKGROUND_CLOUD 2 | L10 | 10 / 10 | 5 | 10 | 0D-00 |
| OBJECT/BACKGROUND_CLOUD 0 | L5 | 5 / 5 | 5 | 5 | 0D-00 |
| OBJECT/BACKGROUND_CLOUD 1 | L5 | 5 / 5 | 5 | 5 | 0D-00 |
| OBJECT/HUGE_DECORATION 2 | L2/R2 | 4 / 4 | 1 | 4 | 11-03 |
| OBJECT/MINISH_SIZED_ENTRANCE 0 | L4 | 4 / 34 | 4 | 4 | 22-01 |
| OBJECT/LAVA_PLATFORM 0 | L4 | 4 / 4 | 4 | 4 | 50-00 |
| OBJECT/HUGE_DECORATION 1 | L2/R1 | 3 / 15 | 1 | 3 | 11-02 |
| ENEMY/KEESE 0 | L2/B1 | 3 / 73 | 2 | 3 | 32-15 |
| ENEMY/WALL_MASTER 0 | L3 | 3 / 3 | 2 | 3 | 58-11 |
| ENEMY/CROW 0 | R3 | 3 / 14 | 1 | 3 | 70-18 |
| OBJECT/CUTSCENE_MISC_OBJECT 5 | R3 | 3 / 12 | 1 | 3 | 88-1C |
| NPC/GUARD_1 0 | R2 | 2 / 51 | 1 | 2 | 15-00 |
| OBJECT/KEY_STEALING_TAKKURI 3 | L1 | 1 / 1 | 1 | 1 | 09-00 |
| OBJECT/HUGE_DECORATION 3 | R1 | 1 / 5 | 1 | 1 | 11-04 |
| OBJECT/JAR_PORTAL 198 | R1 | 1 / 2 | 1 | 1 | 15-00 |
| NPC/BIG_GORON 4 | L1 | 1 / 1 | 1 | 1 | 1A-00 |
| NPC/PICOLYTE_BOTTLE 0 | L1 | 1 / 1 | 1 | 1 | 23-08 |
| OBJECT/FURNITURE 54 | L1 | 1 / 9 | 1 | 1 | 2E-00 |
| OBJECT/FURNITURE 44 | B1 | 1 / 1 | 1 | 1 | 2E-00 |
| OBJECT/LIT_AREA 0 | L1 | 1 / 24 | 1 | 1 | 2E-00 |
| OBJECT/FURNITURE 52 | B1 | 1 / 5 | 1 | 1 | 2E-01 |
| OBJECT/FURNITURE 50 | R1 | 1 / 3 | 1 | 1 | 2E-01 |
| OBJECT/FURNITURE 48 | L1 | 1 / 2 | 1 | 1 | 2E-02 |
| OBJECT/FURNITURE 42 | B1 | 1 / 2 | 1 | 1 | 2E-02 |
| OBJECT/GYORG_BOSS_OBJECT 0 | L1 | 1 / 1 | 1 | 1 | 71-00 |
