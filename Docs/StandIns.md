# Stand-ins

Assets taken from the Java 26.3 client jar as stand-ins until the project's own art replaces them (Jesus, 2026-10-14:
"Use any needed texture from Java Minecraft as a stand in so we can replace later"). Each comes through a hash-checked
reference; replacing one means swapping the asset named here and keeping its layout (or changing its consumer).

| Asset | Jar path | Reference | Where used | Step |
|---|---|---|---|---|
| `/Game/Entities/T_ExperienceOrb` | `assets/minecraft/textures/entity/experience/experience_orb.png` | `Reference/minecraft-entities-26.3` | `M_ExperienceOrb`, the XP orbs' quads (`FFoundationExperienceOrbView`; 16 x 16 icons in 4 columns, `xp.value_icons`) | survival 12 |
