# Reusable Slink panel animations

These three five-second loops are saved at their original sizes and colours. Each animated PNG contains all 120 frames and can be used independently on another panel.

| Animation | Dimensions |
| --- | --- |
| [Wire-threaded silver engraving](panel-loops/silver-engraving.apng) | 458 × 56 |
| [Folding checkerboard and crimson disc](panel-loops/checkerboard.apng) | 318 × 56 |
| [Fluorescent stencil](panel-loops/fluorescent-stencil.apng) | 312 × 56 |

The [panel baker](panel-loops/bake.py) rebuilds the loops with NumPy, SciPy and Pillow and checks their decoded pixels against the original desktop release. It relights the [original transparent silver casting](silver-engraving.png) and draws its nickel wire, the paper folds, the disc and the stencil. The [silver and paper palette](panel-loops/silver-paper-palette.png) and the [stencil inks](stencil-palette.png) are included.

The desktop footer uses the [original whip renderer](../../../tools/sp16_thorn/whip-storm.js). Its [desktop baker](../bake_footer.py) captures it at 60 fps.

## Silver engraving prompt

> Use case: stylized-concept. Make one production art master for the first of three tiny, very wide moving prints in Slink, an acid synthesizer. A single original abstract silver ornament, isolated on transparent, no text. Think experimental rave flyer design meeting an exquisite engraved silver object. It must read instantly as one specific shape when reduced to 340 by 40 pixels.
>
> Composition: extremely elongated, low horizontal silhouette, about 7:1 across, centered in a landscape canvas with generous clear margin above and below and both needle ends visible. Strong asymmetric counterpoise. A substantial off-center hollow almond or teardrop aperture like a sail cleft in two, angular cut-metal crescent crossing under a wide rolled silver blade. Two bold, sinuous strokes of different weight interlock at the eye, then taper in opposite directions into long needle terminals. Put most of the material in a few generous sculptural planes, with long areas of rest, a distinctly swelled shoulder and offset fork, crisp dark voids through the object, and a few short unmistakable hand-engraved parallel cuts only along one inner bevel. Interesting crossings must have front and back and a cast shadow. A jeweller's exact bevels, folded black-nickel underside, quiet cobalt reflected in silver, almost-white satin and mirror edges with one pale iridescent line. Dramatic raking studio light, long stable reflections. Type-design discipline without any glyphs or lettering. Orthographic frontal view with slight yaw so thickness is visible. One singular silhouette, no ground plane, no background, no printed label, no logos, no pendant chain, no frame, no particle effects. Pure artwork only; the interface, animation and other two panels are supplied separately.
