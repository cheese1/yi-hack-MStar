#!/bin/sh

SYSTEM_CONF_FILE="/home/yi-hack/etc/system.conf"
CAMERA_CONF_FILE="/home/yi-hack/etc/camera.conf"
MQTTV4_CONF_FILE="/home/yi-hack/etc/mqttv4.conf"

PARMS1="
HTTPD=yes
TELNETD=yes
SSHD=yes
FTPD=yes
BUSYBOX_FTPD=no
DISABLE_CLOUD=no
REC_WITHOUT_CLOUD=no
MQTT=no
RTSP=yes
RTSP_STREAM=high
RTSP_AUDIO=yes
RTSP_AUDIO_NR_LEVEL=0
ONVIF=yes
ONVIF_WSDD=yes
ONVIF_PROFILE=high
ONVIF_WM_SNAPSHOT=yes
NTPD=yes
NTP_SERVER=pool.ntp.org
PROXYCHAINSNG=no
RTSP_PORT=554
ONVIF_PORT=80
HTTPD_PORT=8080
USERNAME=
PASSWORD=
FREE_SPACE=0
FTP_UPLOAD=no
FTP_HOST=
FTP_DIR=
FTP_DIR_TREE=no
FTP_USERNAME=
FTP_PASSWORD=
FTP_FILE_DELETE_AFTER_UPLOAD=yes
SSH_PASSWORD=
CRONTAB="

PARMS2="
SWITCH_ON=yes
SAVE_VIDEO_ON_MOTION=yes
SENSITIVITY=low
BABY_CRYING_DETECT=no
LED=no
ROTATE=no
IR=yes"

PARMS3="
MQTT_IP=0.0.0.0
MQTT_PORT=1883
MQTT_CLIENT_ID=yi-cam
MQTT_USER=
MQTT_PASSWORD=
MQTT_PREFIX=yicam
MQTT_PREFIX_CONFIG=yicam-config
TOPIC_BIRTH_WILL=status
TOPIC_MOTION=motion_detection
TOPIC_MOTION_IMAGE=motion_detection_image
TOPIC_MOTION_FILES=motion_files
TOPIC_BABY_CRYING=baby_crying
BIRTH_MSG=online
WILL_MSG=offline
MOTION_START_MSG=motion_start
MOTION_STOP_MSG=motion_stop
BABY_CRYING_MSG=crying
MQTT_KEEPALIVE=120
MQTT_QOS=1
MQTT_RETAIN_BIRTH_WILL=1
MQTT_RETAIN_MOTION=1
MQTT_RETAIN_MOTION_IMAGE=1
MQTT_RETAIN_MOTION_FILES=1
MQTT_RETAIN_BABY_CRYING=1"

for i in $PARMS1
do
    if [ ! -z "$i" ]; then
        PAR=$(echo "$i" | cut -d= -f1)
        MATCH=$(cat $SYSTEM_CONF_FILE | grep $PAR)
        if [ -z "$MATCH" ]; then
            echo "$i" >> $SYSTEM_CONF_FILE
        fi
    fi
done

for i in $PARMS2
do
    if [ ! -z "$i" ]; then
        PAR=$(echo "$i" | cut -d= -f1)
        MATCH=$(cat $CAMERA_CONF_FILE | grep $PAR)
        if [ -z "$MATCH" ]; then
            echo "$i" >> $CAMERA_CONF_FILE
        fi
    fi
done

for i in $PARMS3
do
    if [ ! -z "$i" ]; then
        PAR=$(echo "$i" | cut -d= -f1)
        MATCH=$(cat $MQTTV4_CONF_FILE | grep $PAR)
        if [ -z "$MATCH" ]; then
            echo "$i" >> $MQTTV4_CONF_FILE
        fi
    fi
done
