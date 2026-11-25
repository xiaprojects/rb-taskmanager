export WAYFIRE_CONFIG_FILE=/home/stefano/.config/wayfire.ini
export WAYFIRE_SOCKET=/tmp/wayfire-wayland-1.socket
export WAYLAND_DISPLAY=wayland-1
export XCURSOR_SIZE=24
export XDG_DATA_DIRS=/usr/local/share:/usr/share:/var/lib/snapd/desktop
export XDG_RUNTIME_DIR=/run/user/1000
export XDG_SEAT=seat0
export XDG_SESSION_CLASS=user
export XDG_SESSION_ID=3
export XDG_SESSION_TYPE=wayland
export XDG_VTNR=1


exec ./wayfire-layout-switcher \
  --label-a " N" \
  --label-b " A" \
  --label-c " R" \
  --label-combi S \
  --button-x -10 \
  --button-y 10 \
  --button-w 48 \
  --button-h 32 \
  --app-a-id "chrome-__home_stefano_sample_a.html-Default" \
  --app-a-cmd "/home/stefano/sample/launch-app.sh a" \
  --app-b-id "chrome-__home_stefano_sample_b.html-Default" \
  --app-b-cmd "/home/stefano/sample/launch-app.sh b" \
  --app-c-id "chrome-__home_stefano_sample_c.html-Default" \
  --app-c-cmd "/home/stefano/sample/launch-app.sh c" \
  --split-x 280 \
  --hidden 0 \
  --enable-combi 0\
  --download-root "/home/stefano/rb-taskmanager/rbtaskmanager" \
  --api-bind "0.0.0.0" \
  --api-port "8080"
