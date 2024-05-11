USBip*.svg files were created in Inkscape open source vector graphics editor, https://inkscape.org/
check.svg, close.svg, dismiss.svg are from wxMaterialDesignArtProvider, see https://github.com/perazz/wxMaterialDesignArtProvider

Steps to create .ico from .svg

- Open .svg in Inkscape
- For each resolution 512, 256, 128, 64, 48, 32, 16
  - Ctrl+A to select all objects
  - Open Object/Transform, Scale Tab, check "Scale proportionally", set Width=resolution in pixels
  - Open File/Document Properties, Custom Size.
    Set Units=pixels, Width/Height to required resolution
  - Object/Align and Distribute, Align relative to page, horizontally/vertically.
    The drawing must be in the center of the page.
  - File/Export PNG Image, Export Area=Page, Image Size=resolution, Filename=<resolution>.png
- After that you will have 16.png, 32.png, 48.png, 64.png, 128.png, 256.png, 512.png
- Open 512.png in Gimp (https://www.gimp.org/)
  - File/Open As Layers, select all others .png
  - File/Export As, Name=XXX.ico, check all icons