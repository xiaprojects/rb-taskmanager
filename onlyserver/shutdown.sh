#!/bin/bash
waydroid session stop
sleep 1
curl http://192.168.10.1/shutdown
sudo halt