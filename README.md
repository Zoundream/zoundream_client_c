# Zoundream Client - C example implementation

This is an example client for the Zoundream API written in C.

It should be used as a reference when writing your own client, but you will need to adapt it to your platform and add to it.

It has the following known limitations and caveats:
- this client reads audio from pre-recorded audio files, not from the soundcard of your device
- it does not handle any form of concurrency (you will most likely need to keep reading audio while sending requests to the server)
- it uses libraries for HTTP (libcurl) and JSON parsing (lib-json-c) that might not exist in your target environment
- it does not handle any form of authentication, but just passes a fake token to the server

## Building

This example has been built and tested only on Ubuntu 20.04, but should build with minimal changes in most Linux distributions provided the right libraries are installed.

For Ubuntu, this are the packages you need: `build-essential`, `libsndfile1-dev`, `libcurl4-openssl-dev`, `libjson-c-dev`

Once you have installed these packages, run the `./build.sh` script and it should build the `zoundream_client` application.

## Running

The application accepts two parameters on the command line:
- the URL of the Zoundream API endpoint you would like to use (contact Zoundream to be assigned an endpoint URL)
- the path of the audio file that you would like to use (only WAV files with 1 channel and 16KHz sample rate are accepted)
