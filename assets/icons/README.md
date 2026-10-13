# VoiceTwitch icon

The source artwork is `voicetwitch.png`, created with the built-in imagegen tool.
The purple speech bubble and white waveform match the dashboard's charcoal
backgrounds and violet accents. Transparent corners are preserved.

Prompt: "Use case: logo-brand. Asset type: desktop application icon for
VoiceTwitch, a native app that reads Twitch chat aloud. Create one polished
square icon, 1024x1024. A bold violet speech bubble containing three simple white
vertical rounded audio waveform bars, on a charcoal rounded-square tile.
Palette matches app: charcoal #191920 and accent purple #9961FF with subtle
lavender #B080FF highlights. Flat clean geometric design, restrained subtle
shading, generous bold shapes readable at 16 and 32 pixels, centered with
consistent padding. The bubble should have one clear lower-left tail. Tile
occupies almost entire canvas with a little transparent margin outside rounded
corners. No text, letters, logos, watermark, extra symbols, busy details,
mockup or surrounding objects. Render exactly one icon front-on."

Run `python scripts/generate-icons.py` (requires Pillow) after replacing the
source artwork. The checked-in ICO, ICNS and 64-pixel packed RGBA files are used
directly by CMake, so normal builds require no Python or image libraries.
