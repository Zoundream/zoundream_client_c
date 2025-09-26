# Zoundream Client - C example implementation

This is an example client for the Zoundream API written in C.

It can be used as a starting point when writing the firmware for your own client, but it is a minimal implementation.
You will need to adapt it to your platform and add to it.

You will need to modify the code by adding or changing the following:
- read the audio from the microphone input on your device instead of from a file
- read audio and sending HTTP requests concurrently, instead of in separate steps as we do in the example
- in a production environment you will need to obtain a JWT token from the Zoundream account instead of using an API key
- implement a more robust volume detection algorithm than the simple one provided in the example

## Building

This example has been built and tested only on Ubuntu 24.04, but should build with minimal changes in most Linux distributions provided the right libraries are installed.

For Ubuntu, this are the packages you need: `build-essential`, `libsndfile1-dev`, `libcurl4-openssl-dev`, `libjson-c-dev`

Once you have installed these packages, run the `./build.sh` script and it should build the `zoundream_client` application.

You will also need to change the `API_KEY` defined in the `api.h` header to the API key we provided to you together with this sample.
You may also change the `TEST_USER_ID` to any value you like, for example to differentiate separate test runs.

## Running

The application accepts two parameters on the command line:
- the URL of the Zoundream API endpoint you would like to use (contact Zoundream to be assigned an endpoint URL)
- the path of the audio file that you would like to use (only WAV files with 1 channel and 16KHz sample rate are accepted)

We have likely provided together with this example code a set of reference audio files that you can use for testing.
