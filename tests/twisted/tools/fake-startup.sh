#!/bin/sh
dbus-send --session --type=signal --print-reply \
/org/freedesktop/Telepathy/RegressionTests \
org.freedesktop.Telepathy.RegressionTests.FakeStartup \
string:"$1"
