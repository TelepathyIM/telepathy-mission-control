#!/bin/sh
dbus-send --session --type=signal --print-reply \
/im/telepathy/v1/RegressionTests \
im.telepathy.v1.RegressionTests.FakeStartup \
string:"$1"
