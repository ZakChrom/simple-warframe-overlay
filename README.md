# SWO (Simple Warframe Overlay)
### ⚠️ I AM NOT RESPONSIBLE FOR ANY BANNED ACCOUNTS. USE AT YOUR OWN RISK ⚠️
### The overlay does not modify or interact with the game in any way other than screenshotting the screen to get relic info

## Why another overlay? Isnt alecaframe better and less risky to use?
#### Yes but its terrible to install on linux.
#### I somehow broke my warframe installation when i tried to install overwolf :staring_cat:

## Usage
#### You need tesseract ocr, leptonica and wayland-scanner installed.
```bash
$ make
$ ./main
```

## Performance?
#### Its pretty terrible bcs it has to run tesseract every second but it shouldnt affect the game.
#### Maybe ill switch to a gpu version which will be faster in the future

## Windows or X11 support?
#### No. The overlay heavily depends on wayland so i would have to rewrite the entire thing.
#### Just use alecaframe.

## Credits
#### [hello-wayland](https://github.com/emersion/hello-wayland): Wayland app example i used.
#### [tesseract ocr](https://github.com/tesseract-ocr/tesseract): Text recognition.
#### [leptonica](http://leptonica.org/): Image processing
#### [olive.c](https://github.com/tsoding/olive.c): Drawing library
#### [stb_image.h](https://github.com/nothings/stb): Image loading
#### plat.png: From the wiki (new one)
