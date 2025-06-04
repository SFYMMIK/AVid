# Avid (ASCII Video Player)

Avid is a lightweight command-line tool written in C++17 that converts videos into ASCII art frames and plays them in the terminal, optionally synchronized with audio. This projects idea is based on [MoveFetch](https://github.com/SFYMMIK/MoveFetch), extending its functionality by adding user-selectable color modes, frame rate control, resolution settings, and audio playback synchronization.

---

## Features

- **Video Conversion**: Convert any MP4 (or other FFmpeg-supported) video into a series of ASCII art frames at a specified width, height, and frames per second (FPS).
- **Color or Black & White**: Choose between monochrome ASCII or ANSI colorized ASCII output.
- **Audio Synchronization**: Play the original audio track using FFmpeg's `ffplay`, timed precisely to match ASCII frame display so that playback feels like a normal video.
- **Easy Management**:  
  - Store converted videos in `~/.config/avid/info/<video_name>_<timestamp>/`.  
  - Create a symlink `<video_name>.asciisymlink` pointing to the latest conversion for quick access.  
  - Delete individual videos or all conversions via command-line flags.
- **Cross-Platform Compatibility**: Works on any Unix-like system with C++17 and FFmpeg installed.

---

## Requirements

- A C++17-compatible compiler (e.g., `g++`, `clang++`).
- [FFmpeg](https://ffmpeg.org/) installed and available on your `PATH`.
- Unix-like shell (Linux, macOS, WSL, etc.).

---

## Compilation

1. Open a terminal in the directory containing `avid.cpp`.
2. Run the following command (assuming `g++` is installed):
   ```bash
   g++ -std=c++17 -O2 -o avid avid.cpp
   ```
3. If your compiler version is older and requires explicit linking for `<filesystem>`, use:
   ```bash
   g++ -std=c++17 -O2 -o avid avid.cpp -lstdc++fs
   ```
4. Once compiled, you should see an executable named `avid` in the current directory.

---

## Usage

### 1. Convert a Video (`--setup`)

```bash
./avid --setup <input_video.mp4> [--color] [--fps N] [--width W] [--height H]
```

- `<input_video.mp4>`: Path to the source video file.
- `--color`: Enable ANSI colorized ASCII output. Omit for black & white.
- `--fps N`: Set the output frame rate (default: 10 FPS). Recommended 30+ FPS for audio sync.
- `--width W`: ASCII frame width (default: 80 characters).
- `--height H`: ASCII frame height (default: 40 rows).

Example:
```bash
./avid --setup sample.mp4 --color --fps 30 --width 100 --height 50
```

This command creates:
```
~/.config/avid/info/sample_<timestamp>/
├── audio.wav
├── frame_00001.txt
├── frame_00002.txt
├── ...
├── frame_XXXXX.txt
├── metadata.txt
└── sample.mp4.asciisymlink (symlink to the above directory)
```

### 2. Play an ASCII Video (`--play`)

```bash
./avid --play <video_name>.asciisymlink [--sound]
```

- `<video_name>.asciisymlink`: The symlink created during `--setup`. Points to the directory of ASCII frames.
- `--sound`: Include the audio track during playback (requires FFmpeg’s `ffplay`).

Example:
```bash
./avid --play sample.mp4.asciisymlink --sound
```
This clears the terminal, starts audio playback, and displays ASCII frames at the specified FPS, synchronized to audio.

### 3. Delete a Video (`--delete`)

#### Delete a Single Video
```bash
./avid --delete <video_name>.asciisymlink
```
Removes both the ASCII frames directory and the symlink.

#### Delete All Videos
```bash
./avid --delete
```
Prompts for confirmation, then deletes all converted videos and symlinks under `~/.config/avid/info/`.

---

## Directory Structure

- **`~/.config/avid/info/`**: Base directory for storing all ASCII conversions.
  - **`<video_name>_<timestamp>/`**: Directory containing one converted video’s frames, `audio.wav`, and `metadata.txt`.
  - **`<video_name>.asciisymlink`**: Symlink to the latest conversion directory for `<video_name>`.

---

## Notes & Credits

- The ASCII conversion algorithm scales each frame down to the desired resolution, maps pixel luminance to ASCII characters, and optionally wraps each character in an ANSI escape code for color.
- Audio is extracted to `audio.wav` and played via `ffplay`, launched in a background thread to maintain synchronization.
- Originally inspired by [MoveFetch](https://github.com/YourUsername/MoveFetch). Avid is an evolution of that idea, focusing specifically on video-to-ASCII conversion and synchronized playback.

---

## License

This project is provided “as-is” under the MIT License. See [LICENSE](LICENSE) for details.

