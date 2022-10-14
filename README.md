# GPIO change Notifier for ESP8266 devices with ESP Framework

See `make menuconfig` to set up variables in section `GPIO Change Notifier App`

# how it works 

Workflow :

- initialize Wifi using `GCN_WIFI_SSID` and `GCN_WIFI_PASSWORD`
- setup up GPIO number `GCN_WATCH_GPIO_NUMBER` as input *with a pullup*
- if GPIO changes states or `GCN_IDLE_NOTIFICATION_INTERVAL` eslapsed since last notification, send a notification

Notification :

- sends a HTTP POST to URI `GCN_NOTIFY_URL`
- query parameter: host = `GCN_HOST_NAME`
- query parameter: time = seconds since epoch
- query parameter: gpio = `GCN_WATCH_GPIO_NUMBER`
- query parameter: value = current GPIO state (0 or 1)

# TODO

- backlog events while disconnected and flush them on reconnection
