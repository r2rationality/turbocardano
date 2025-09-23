#! /usr/bin/bash
test -d /data || sudo mkdir /data
test -d /data && sudo chown -R tada:tada /data
/opt/tada/bin/tada "$@"