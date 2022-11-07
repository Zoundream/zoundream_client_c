#!/bin/bash

gcc -o zoundream_client \
zoundream_client.c audio.c api.c \
`curl-config --cflags --libs` \
`pkg-config json-c --cflags --libs` \
`pkg-config sndfile --cflags --libs` \
-lm

