#!/bin/bash

gcc -o zoundream_client \
zoundream_client.c client_core.c audio.c api.c zc_log.c third_party/cJSON.c \
-I. \
`curl-config --cflags --libs` \
-lm

