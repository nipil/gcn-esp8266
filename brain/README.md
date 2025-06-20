# Brain (in Python)

# specification

- UI
    - recipients
        - email
        - sms
        - twitter

- MQTT
    - tls
        - mandatory -> DONE
        - mandatory verification -> DONE
    - auth
        - login/password -> DONE
    - errors
        - retryable
            - dns resolution -> DONE
            - host unreachable -> DONE
            - port unreachable -> DONE
        - fatal
            - auth failed -> DONE
            - tls failed -> DONE

- notifications
    - manager
        - starting -> DONE
        - exiting -> DONE
    - mqtt connection
        - failing -> DONE
        - established -> DONE
    - client
        - status
            - offline -> DONE
            - online -> DONE
        - heartbeat
            - skewed -> DONE
            - missed -> DONE
        - dropped item -> DONE
    - gpio
        - raising -> DONE
        - falling -> DONE

- brain
    - monitored gpio -> DONE
    - gpio initial -> DONE
    - gpio changed -> DONE
    - untracked -> DONE

# TODO

- delete non-monitored pins from memory (optional) and mqtt (need acl change)

- implement a safeguard regarding notifications : max N/day

- monitor
    - self sms sent and success
    - metrics from gcn client

- dev
    - add tracing
    - add metrics

- mqtt
    - auth client cert (mtls)
    - auth tls preshared key ?
    - using proxy ?

- callback URL
    - id : numéro d'identification du SMS
    - ptt : code qui correspond à un état du SMS. Les différents codes ptt sont décrits dans le premier tableau
      ci-dessous.
    - date : date du DLR (Delivery report)
    - description : ID du DLR . Les différents ID sont décrits dans le second tableau ci-dessous
    - descriptionDlr : description du status du DLR

# python asyncio debug

    export PYTHONASYNCIODEBUG=1
