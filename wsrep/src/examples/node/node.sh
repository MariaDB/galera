#!/bin/sh -eu

NODE_ID=$1

NODE_NAME=${NODE_NAME:-$NODE_ID}

NODE_DIR=${NODE_DIR:-/tmp/node/$NODE_NAME}
rm -rf $NODE_DIR/*
mkdir -p $NODE_DIR

NODE_OPT=${NODE_OPT:-}

NODE_HOST=${NODE_HOST:-localhost}
NODE_PORT=${NODE_PORT:-$((10000 + $NODE_ID))}

NODE_CLIENTS=${NODE_CLIENTS:-1}
NODE_APPLIERS=${NODE_APPLIERS:-1}

NODE_ADDR=${NODE_ADDR:-}

NODE_BIN=${NODE_BIN:-$(dirname $0)/node}

# convert possible relative path to absolute path
NODE_PROVIDER=$(realpath $NODE_PROVIDER)

set -x

$NODE_BIN \
-v "$NODE_PROVIDER" \
-n "$NODE_NAME" \
-f "$NODE_DIR" \
-o "$NODE_OPT" \
-t "$NODE_HOST" \
-p $NODE_PORT \
-s $NODE_APPLIERS \
-m $NODE_CLIENTS \
-d 10 \
-a "$NODE_ADDR"

set +x
