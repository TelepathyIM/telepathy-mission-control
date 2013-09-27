#!/bin/sh
dbus-send --session --type=signal --print-reply \
/im/telepathy1/RegressionTests \
im.telepathy1.RegressionTests.FakeStartup \
string:"$1"
