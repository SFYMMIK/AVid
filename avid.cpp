#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <algorithm>
#include <unistd.h>

namespace fs = std::filesystem;

// Expand "~" to the user's home directory. Godspeed.
std::string getHomeDir() {
    if (const char* home = std::getenv("HOME")) {
        return std::string(home);
    }
    std::cerr << "Could not find HOME environment variable. Exiting.\n";
    std::exit(1);
}

// Run a shell command, because dealing with pipes manually in C++ is a fucking nightmare.
bool runCommand(const std::string& cmd) {
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "Command failed: " << cmd << "\n";
    }
    return (ret == 0);
}

// Gets a timestamp like "20250604_142530" – no fucking idea how time formatting works but this does.
std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ttime = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&ttime, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return std::string(buf);
}

// Replaces the file extension of 'filename' with 'new_ext'.
// Example: replaceExtension("foo.mp4", ".ppm") => "foo.ppm"
std::string replaceExtension(const std::string& filename, const std::string& new_ext) {
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos) {
        return filename + new_ext;
    }
    return filename.substr(0, dot) + new_ext;
}

// Convert a single PPM frame to ASCII art (grayscale or color).
// This shit is slow, but fuck it, it works.
void ppmToASCII(const fs::path& ppmPath, const fs::path& outTxtPath, int width, int height, bool colorMode) {
    std::ifstream in(ppmPath, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open PPM file: " << ppmPath << "\n";
        return;
    }

    std::string header;
    int imgW, imgH, maxval;
    in >> header >> imgW >> imgH >> maxval;
    // swallow single whitespace char after maxval
    in.get();

    if (header != "P6") {
        std::cerr << "Unsupported PPM format: " << header << " (only P6 is supported)\n";
        return;
    }

    // Read raw pixel data
    std::vector<unsigned char> pixels(imgW * imgH * 3);
    in.read(reinterpret_cast<char*>(pixels.data()), pixels.size());
    in.close();

    // Scale down to requested ASCII resolution (simple nearest-neighbor)
    // wtf is this shit: I have no fucking idea how to optimize scaling properly.
    float x_ratio = static_cast<float>(imgW) / width;
    float y_ratio = static_cast<float>(imgH) / height;

    std::ofstream out(outTxtPath);
    if (!out) {
        std::cerr << "Cannot write to ASCII file: " << outTxtPath << "\n";
        return;
    }

    // ASCII ramp for grayscale
    const std::string grayscale = "@%#*+=-:. ";
    // For color, we'll embed ANSI escape codes. This is more insane than I expected.
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            int px = static_cast<int>(j * x_ratio);
            int py = static_cast<int>(i * y_ratio);
            size_t idx = (py * imgW + px) * 3;
            unsigned char r = pixels[idx];
            unsigned char g = pixels[idx + 1];
            unsigned char b = pixels[idx + 2];
            // Compute luminance
            float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            int pos = static_cast<int>((lum / 255.0f) * (grayscale.size() - 1));
            pos = std::clamp(pos, 0, static_cast<int>(grayscale.size() - 1));
            char asciiChar = grayscale[pos];

            if (colorMode) {
                // ANSI escape code for 24-bit color. I have no fucking clue if this works on your terminal.
                out << "\033[38;2;" << int(r) << ";" << int(g) << ";" << int(b) << "m"
                << asciiChar << "\033[0m";
            } else {
                out << asciiChar;
            }
        }
        out << "\n";
    }

    out.close();
}

// Create a new ASCII‐converted video without deleting the old one. Jesus Christ this got complicated.
void handleSetup(const std::string& inputPath, bool colorMode, int fps, int asciiW, int asciiH) {
    fs::path inputFile = inputPath;
    if (!fs::exists(inputFile)) {
        std::cerr << "Input file does not exist: " << inputPath << "\n";
        return;
    }

    std::string home = getHomeDir();
    fs::path baseConfig = fs::path(home) / ".config" / "avid" / "info";
    fs::create_directories(baseConfig);

    // Create a unique folder: <filename>_<timestamp>
    std::string fname = inputFile.filename().string();
    std::string nameNoExt = fname.substr(0, fname.find_last_of('.'));
    std::string timestamp = currentTimestamp();
    fs::path videoDir = baseConfig / (nameNoExt + "_" + timestamp);
    fs::create_directories(videoDir);

    // Extract audio to WAV
    fs::path audioPath = videoDir / "audio.wav";
    {
        std::ostringstream cmdAudio;
        cmdAudio << "ffmpeg -y -i \"" << inputPath << "\" -vn -ac 1 -ar 44100 -q:a 4 \"" << audioPath.string() << "\"";
        if (!runCommand(cmdAudio.str())) {
            std::cerr << "Audio extraction failed, but I'll try to continue anyway.\n";
        }
    }

    // Extract frames as PPM at requested FPS and scaling. Because fuck making this incremental.
    fs::path tempDir = videoDir / "ppm_frames";
    fs::create_directories(tempDir);
    {
        std::ostringstream cmdFrames;
        // note: we multiply asciiW by 8 and asciiH by 16 just to preserve aspect ratio roughly; adjust if needed.
        cmdFrames << "ffmpeg -y -i \"" << inputPath << "\" -vf \"fps=" << fps
        << ",scale=" << (asciiW * 8) << ":" << (asciiH * 16)
        << ":flags=lanczos\" \"" << (tempDir / "frame_%05d.ppm").string() << "\"";
        if (!runCommand(cmdFrames.str())) {
            std::cerr << "Frame extraction failed, aborting.\n";
            return;
        }
    }

    // Convert each PPM to ASCII .txt
    for (auto& entry : fs::directory_iterator(tempDir)) {
        if (entry.path().extension() == ".ppm") {
            std::string stem = entry.path().stem().string();
            fs::path outTxt = videoDir / (stem + ".txt");
            ppmToASCII(entry.path(), outTxt, asciiW, asciiH, colorMode);
            fs::remove(entry.path()); // remove the ppm, cuz we don't need it anymore
        }
    }
    fs::remove_all(tempDir); // clean up tempDir

    // Save metadata (fps and color mode)
    {
        std::ofstream metaFile(videoDir / "metadata.txt");
        metaFile << "fps=" << fps << "\n";
        metaFile << "color=" << (colorMode ? "1" : "0") << "\n";
        metaFile.close();
    }

    // Create / update symlink: <filename>.asciisymlink -> the new folder
    fs::path symlinkPath = baseConfig / (fname + ".asciisymlink");
    if (fs::exists(symlinkPath)) {
        fs::remove(symlinkPath);
    }
    // We make a relative symlink here for portability
    fs::create_directory_symlink(videoDir.filename(), symlinkPath);

    std::cout << "Setup complete. ASCII video stored in: " << videoDir << "\n";
    std::cout << "Symlink created: " << symlinkPath << "\n";
}

// Play an ASCII video, with optional audio, synced so closely it feels like an MP4.
// This is where shit gets real.
void handlePlay(const std::string& linkName, bool sound) {
    std::string home = getHomeDir();
    fs::path baseConfig = fs::path(home) / ".config" / "avid" / "info";

    fs::path symlinkPath = baseConfig / linkName;
    if (!fs::exists(symlinkPath)) {
        std::cerr << "Symlink not found: " << symlinkPath << "\n";
        return;
    }
    fs::path targetDir = fs::read_symlink(symlinkPath);
    fs::path videoDir = baseConfig / targetDir;
    if (!fs::exists(videoDir)) {
        std::cerr << "Target directory not found: " << videoDir << "\n";
        return;
    }

    // Read metadata
    int fps = 10;
    bool colorMode = false;
    {
        std::ifstream meta(videoDir / "metadata.txt");
        if (meta) {
            std::string line;
            while (std::getline(meta, line)) {
                if (line.rfind("fps=", 0) == 0) {
                    fps = std::stoi(line.substr(4));
                } else if (line.rfind("color=", 0) == 0) {
                    colorMode = (line.substr(6) == "1");
                }
            }
        } else {
            std::cerr << "metadata.txt missing or unreadable. Using defaults.\n";
        }
    }

    // Collect all frame text files
    std::vector<fs::path> framePaths;
    for (auto& entry : fs::directory_iterator(videoDir)) {
        if (entry.path().extension() == ".txt" &&
            entry.path().stem().string().rfind("frame_", 0) == 0) {
            framePaths.push_back(entry.path());
            }
    }
    if (framePaths.empty()) {
        std::cerr << "No ASCII frames found in: " << videoDir << "\n";
        return;
    }
    // Sort frames by filename so they play in order. This fucking sort is important.
    std::sort(framePaths.begin(), framePaths.end());

    // Preload all frames into memory
    // If you have a 10k‐frame 80×40 video, it might eat up tens of MB, but fuck it—it’s faster than disk I/O.
    std::vector<std::string> asciiFrames;
    asciiFrames.reserve(framePaths.size());
    for (auto& p : framePaths) {
        std::ifstream in(p);
        if (!in) continue;
        std::ostringstream ss;
        ss << in.rdbuf();
        asciiFrames.push_back(ss.str());
        in.close();
    }

    // Calculate frame duration in nanoseconds
    const auto frameDurationNs = std::chrono::nanoseconds(static_cast<long long>(1e9 / fps));

    // Clear terminal once before starting.
    // We want a blank slate, not some old damn text flying around.
    std::system("clear");

    // Record the "start time" right before firing off audio and video loop
    auto startTime = std::chrono::steady_clock::now();

    // If sound is requested, fire up ffplay in a separate thread at the "same" moment.
    if (sound) {
        fs::path audioPath = videoDir / "audio.wav";
        if (fs::exists(audioPath)) {
            std::thread([audioPath]() {
                std::ostringstream cmd;
                cmd << "ffplay -nodisp -autoexit \"" << audioPath.string() << "\" >/dev/null 2>&1";
                // I have no fucking idea if this will sync perfectly, but launching right now.
                std::system(cmd.str().c_str());
            }).detach();
        } else {
            std::cerr << "No audio file found; playing silent ASCII video.\n";
        }
        // Recommending 30fps+ if they want sound to match. Just saying.
        if (fps < 30) {
            std::cout << "(Pro tip: Convert your video at 30fps or higher for smoother audio sync.)\n";
        }
    }

    // Now run the loop, snapping each frame to the steady clock so there's zero fucking drift.
    auto nextFrameTime = startTime;
    for (size_t i = 0; i < asciiFrames.size(); ++i) {
        // Sleep until the exact moment this frame should appear
        std::this_thread::sleep_until(nextFrameTime);

        // Move cursor to top-left and print frame
        std::cout << "\033[H" << asciiFrames[i];

        nextFrameTime += frameDurationNs;
    }

    // At end, reset colors (in case colorMode was on). Terminal might look crazy otherwise.
    std::cout << "\033[0m";
}

// Delete either a single ASCII video or all of them
void handleDelete(const std::string& maybeLink) {
    std::string home = getHomeDir();
    fs::path baseConfig = fs::path(home) / ".config" / "avid" / "info";

    if (maybeLink.empty()) {
        // Delete all videos
        std::cout << "Are you sure you want to delete ALL ASCII videos? This is irreversible! (y/N): ";
        char resp;
        std::cin >> resp;
        if (resp != 'y' && resp != 'Y') {
            std::cout << "Aborted deletion.\n";
            return;
        }
        for (auto& entry : fs::directory_iterator(baseConfig)) {
            if (entry.is_symlink()) {
                fs::remove(entry.path());
            } else if (entry.is_directory()) {
                fs::remove_all(entry.path());
            }
        }
        std::cout << "All ASCII videos and symlinks deleted.\n";
    } else {
        // Delete a single video
        fs::path symlinkPath = baseConfig / maybeLink;
        if (!fs::exists(symlinkPath) || !fs::is_symlink(symlinkPath)) {
            std::cerr << "No such asciisymlink: " << symlinkPath << "\n";
            return;
        }
        fs::path targetDir = fs::read_symlink(symlinkPath);
        fs::path videoDir = baseConfig / targetDir;
        if (fs::exists(videoDir)) {
            fs::remove_all(videoDir); // delete frames and audio
        }
        fs::remove(symlinkPath); // delete the symlink
        std::cout << "Deleted ASCII video: " << maybeLink << "\n";
    }
}

void printUsage() {
    std::cout << "Usage:\n";
    std::cout << "  avid --setup <input.mp4> [--color] [--fps N] [--width W] [--height H]\n";
    std::cout << "  avid --play <video.asciisymlink> [--sound]\n";
    std::cout << "  avid --delete [<video.asciisymlink>]\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  avid --setup movie.mp4 --color --fps 30 --width 80 --height 40\n";
    std::cout << "  avid --play movie.mp4.asciisymlink --sound\n";
    std::cout << "  avid --delete movie.mp4.asciisymlink\n";
    std::cout << "  avid --delete\n";
}

int main(int argc, char* argv[]) {
    // Unsync C‐style and C++‐style I/O for maximum speed when spitting out thousands of lines.
    std::ios::sync_with_stdio(false);
    std::cout.tie(nullptr);

    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "--setup") {
        if (argc < 3) {
            printUsage();
            return 1;
        }
        std::string inputFile = argv[2];
        bool colorMode = false;
        int fps = 10;
        int asciiW = 80;
        int asciiH = 40;
        // Parse optional flags
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--color") {
                colorMode = true;
            } else if (arg == "--fps" && i + 1 < argc) {
                fps = std::stoi(argv[++i]);
            } else if (arg == "--width" && i + 1 < argc) {
                asciiW = std::stoi(argv[++i]);
            } else if (arg == "--height" && i + 1 < argc) {
                asciiH = std::stoi(argv[++i]);
            } else {
                std::cerr << "Unknown flag: " << arg << "\n";
            }
        }
        // Making a video without deleting the old one. Holy shit.
        handleSetup(inputFile, colorMode, fps, asciiW, asciiH);

    } else if (cmd == "--play") {
        if (argc < 3) {
            printUsage();
            return 1;
        }
        std::string linkName = argv[2];
        bool sound = false;
        if (argc >= 4 && std::string(argv[3]) == "--sound") {
            sound = true;
        }
        handlePlay(linkName, sound);

    } else if (cmd == "--delete") {
        if (argc == 2) {
            // Delete all
            handleDelete("");
        } else {
            std::string linkName = argv[2];
            handleDelete(linkName);
        }

    } else {
        printUsage();
        return 1;
    }

    return 0;
}
