# Zoundream Client - C example implementation

This is an example client for the Zoundream API written in C.

It can be used as a starting point when writing the firmware for your own client, but it is a minimal implementation.
You will need to adapt it to your platform and add to it.

The repository contains two programs built from the same code:
- `zoundream_client`: a command line client, which is the reference for integrating the API in your own device
- `zoundream_gui`: a graphical test client for Linux and Windows, to quickly evaluate the API on a set of audio files without writing any code (see [GUI test client](#gui-test-client) below)

You will need to modify the code by adding or changing the following:
- read the audio from the microphone input on your device instead of from a file
- read audio and sending HTTP requests concurrently, instead of in separate steps as we do in the example
- in a production environment you will need to obtain a JWT token from the Zoundream account instead of using an API key
- add a volume detection gate, so that an activation is only opened when there is sound loud enough to possibly be a cry: the example sends all the audio in the file

## Building

This example has been built and tested only on Ubuntu 24.04, but should build with minimal changes in most Linux distributions provided the right libraries are installed.

For Ubuntu, these are the packages you need: `build-essential`, `libcurl4-openssl-dev`.
The WAV reader ([dr_wav](https://github.com/mackron/dr_libs)) and the JSON parser ([cJSON](https://github.com/DaveGamble/cJSON)) are included in the `third_party` directory and need no installation.

Once you have installed these packages, run the `./build.sh` script and it should build the `zoundream_client` application.

You will also need to change the `API_KEY` defined in the `api.h` header to the API key we provided to you together with this sample.
You may also change the `TEST_USER_ID` to any value you like, for example to differentiate separate test runs.
A millisecond timestamp is appended to it on every run, so each run is seen by the server as a separate device.

## Running

The application accepts two parameters on the command line:
- the URL of the Zoundream API endpoint you would like to use (contact Zoundream to be assigned an endpoint URL)
- the path of the audio file that you would like to use (only WAV files with 1 channel and a sample rate of 8KHz or 16KHz are accepted, at least 10 seconds long)

For example:

```
./zoundream_client https://stage-znd-eu.zoundream-api.com/audio GS_hungry_01.wav
```

The audio is sent one second at a time, waiting one second between each request, the same way a real device would send it while recording from a microphone.
For this reason processing a file takes about as long as the duration of the audio that is sent.

The client stops as soon as it receives a valid cry translation.
If it reaches the end of the file without one, it loops back to the start of the file (as if the baby kept crying) up to 3 times, then stops.

The exit code tells how the run ended:
- `0`: the run completed (the output tells whether a cry was translated)
- `1`: the HTTP library could not be initialized
- `2`: the audio file could not be opened, is not an accepted format, or is too short
- `3`: the server rejected the API key (check that the key is the one assigned to you, and that it is used with the endpoint it was issued for)
- `4`: the endpoint could not be reached (5 requests in a row failed)

We have likely provided together with this example code a set of reference audio files that you can use for testing.

## GUI test client

The `gui` directory contains a graphical version of the client, which runs exactly the same session logic as the command line client.

To use it:
1. choose the endpoint (Europe, China, or Other to enter the URL we assigned to you)
2. enter the API key we provided to you (it is not necessary to change `api.h` for the GUI)
3. add WAV files with "Add files...", or by dropping them onto the window (or onto the executable)
4. press "Start": the files are processed one after the other, with progress and result for each one

The log of the run can be saved with "Save log...", which is useful to send to us when reporting a problem.
The GUI does not save any settings: the endpoint and API key need to be entered every time it is started.

### Building the GUI for Linux

In addition to the packages needed by the command line client, the GUI needs the X11 development headers.
Either install them system wide:

```
sudo apt install xorg-dev
```

or, if you do not have root access, download them locally into `gui/.deps-sysroot` with:

```
cd gui
./fetch_x11_headers.sh
```

Then build it with:

```
cd gui
./build.sh
```

This produces `gui/zoundream_gui`.
The first build compiles the included GLFW and Dear ImGui libraries into `gui/build`, which makes it take a while; later builds reuse them (delete `gui/build` to rebuild them).

### Building the GUI for Windows

The Windows version is cross-compiled from Linux, and produces a single `gui/zoundream_gui.exe` that has no other dependencies and can be copied to any 64 bit Windows machine.

1. Download the [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) toolchain (the `ucrt-ubuntu-*-x86_64` tarball) and extract it.
   Extract it into `~/llvm-mingw`, or set the `LLVM_MINGW` environment variable to the directory where you extracted it:
   ```
   export LLVM_MINGW=/path/to/llvm-mingw
   ```
2. Build a static Windows version of libcurl (only needed once; it downloads the curl sources and installs the result into `gui/.win-deps`):
   ```
   cd gui
   ./build_windows_deps.sh
   ```
3. Build the GUI:
   ```
   ./build-windows.sh
   ```
