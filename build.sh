#!/bin/bash

gcc -o zoundream_client \
zoundream_client.c client_core.c audio.c api.c zc_log.c \
`curl-config --cflags --libs` \
`pkg-config json-c --cflags --libs` \
`pkg-config sndfile --cflags --libs` \
-lm

