beamer2mlt
==========

1. Create a beamer presentation. For including videos, use the following syntax:
   ```latex
   \href{run:video.mp4}{\XeTeXLinkBox{\includegraphics[height=4cm]{video.jpg}}}
   ```
   where `video.jpg` is an examplary frame from the video (you can generate that using `ffmpeg -i video.mp4 -vframes 1 video.jpg`).
2. Call `beamer2mlt`:
   ```
   beamer2mlt presentation.pdf presentation.mlt
   ```
3. Import into kdenlive:
   Just import the mlt file like any other clip. If you want to edit the arrangement, do the following:
   1. Ungroup the video from the audio & delete the audio
   2. Create enough video tracks
   3. With the clip selected, run "Timeline -> Current Clip -> Expand Clip"


Compilation
-----------

Needs the following dependencies: `libpoppler-qt6-dev`, `libmlt++-dev`
