# what

This is a crude GCN to IFTTT webhook wrapper

## install

From Debian strech (installs 8.11.1 if using backports) and up :

    sudo apt-get install nodejs git
    sudo adduser gcn
    sudo -i -u gcn git clone https://github.com/nipil/gcn-esp8266
    sudo cp ~gcn/gcn-esp8266/nodejs/gcn.service /etc/systemd/system/gcn.service
    sudo systemctl daemon-reload
    sudo systemctl enable gcn.service
    sudo -u gcn vim ~gcn/gcn-esp8266/nodejs/gcn.js

Change the following to fit your IFTTT account key (xxxx) and webhook name (yyy):

    const IFTTT_ACCOUNT_KEY = "xxxxxxxxxxxxxxxxxxxxxxxx";
    const IFTTT_WEBHOOK_TRIGGER = "yyy";

## use

Start for the first time :

    sudo systemctl start gcn.service

Check :

    sudo systemctl status gcn

Trace :

    sudo journalctl -u gcn -f
