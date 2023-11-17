# Convert video to GIF. The "filter_complex" part improves GIF quality.
ffmpeg -y -filter_complex "[0:v] split [a][b];[a] palettegen [p];[b][p] paletteuse" -i input.mp4 output.gif