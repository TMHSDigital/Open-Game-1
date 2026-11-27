# 0021: Realistic art direction, with adjustable gore and dismemberment later

Status: Accepted
Date: 2026-11-27
Issue: [#6](https://github.com/TMHSDigital/Open-Game-1/issues/6)
Supersedes: none

## Context

Art direction had been open since the project started (OD-006). Everything visual was a placeholder: a faceless base character built by script from a skin modifier, and stand-in locomotion from a CC0 animation pack retargeted onto it. Decision 0017 put original animation off until the style was known, because clips authored with no style to aim at might be thrown away.

A scripted play-test on 2026-11-27 showed why the placeholders now hold the game back. The jog is hunched with the arms bent behind, and the idle has flared elbows. The `motion` scenario showed the same poses with the physics layer hidden, so they come from the stand-in clips as they land on this skeleton, not from the physics body. The model itself has an oversized head, a long forward neck, narrow sloped shoulders and limbs with no joint shape. Fixing either well needed a target look.

The game is a physics-driven melee simulator. The body is simulated, strikes are steered through it, and hits stagger or knock characters down. A realistic look fits that: the simulation reads as weight and impact when the body looks like a body.

## Decision

The game aims for a realistic look: realistic human proportions, anatomy, materials and motion, not stylized or low-poly. Gore and dismemberment are part of the planned game, added after the realistic character is in place, with player settings to reduce them or turn them off. Physics-driven melee games such as Half Sword are the reference for the genre, not for copying any game's content.

## Consequences

- The base character is rebuilt toward realistic human anatomy and proportions. A skin-modifier body can approach realistic proportions but not realistic detail. Where the realistic character comes from (built by our own script, generated from an open human model, or imported) is its own question, recorded separately. It goes through the asset-import policy (decision 0004) like any outside asset.
- Decision 0017 deferred original animation "until art direction is decided". That condition is now met. How realistic motion is made (keep and correct the stand-in clips, author clips, or use motion capture under a usable license) is a separate decision, so 0017 stays in force until that one is made.
- Gore and dismemberment shape the character from now on, even before they are built. The mesh and the physics asset should split cleanly at the joints a limb can come off at. Damage needs a place to live (per body part), and every gory effect needs a switch, so the game also works with them off.
- A settings group for gore (amount, dismemberment on or off, and a fully off level) becomes part of the pause screen's Settings tab when gore exists. The Steam content questionnaire and the store page must describe it.
- Realistic art costs more than stylized art: higher-resolution meshes, skin materials, and more care with lighting. Captures and devlog stills will change as the look changes, so before-and-after comparisons are expected to show large differences.
- Assets that do not fit (the stylized stand-in clips and any low-poly placeholders) are replaced over time. Nothing is removed until its replacement works.

## Alternatives

- Stylized low-poly. Rejected: cheaper to make and to run, but it works against reading a physical fight as real weight and impact, which is the point of the game.
- Mixed from CC0 packs. Rejected as a direction: free packs do not share a style, and a mixed look reads as unfinished. Single CC0 assets that fit the realistic look can still be used.
- Greybox until decided. Rejected: the play-test showed the placeholders now limit how movement and contact can be judged, so waiting longer costs more than deciding.

## Links

- Open decision: OD-006 (Issue #6)
- Related: decision 0017 (animation done in code, original clips waiting for art direction), decision 0004 (third-party asset license policy)
