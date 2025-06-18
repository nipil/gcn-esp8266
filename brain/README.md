# Brain (in Python)

PAHO doc : https://eclipse.dev/paho/files/paho.mqtt.python/html/index.html


# python asyncio debug

    export PYTHONASYNCIODEBUG=1

# TODO
- check if devices are online/offline
- test publish to gcn/#/in/reboot & co
- read all gpio messages for gpio
	- update inital state from latest discovered
	- update list of known pins
- read monitored_pins
- if monitored pins is available
	- control if known pins are in monitored pins
	- if not, delete the known pins
		- from memory
		- from mqtt serrer

- implement a safeguard regarding notifications : max N/day

- notify on monitored pins change
	- if gpio value different from last
	- select provider: OVH or ... free SMS ?

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
  - ptt : code qui correspond à un état du SMS. Les différents codes ptt sont décrits dans le premier tableau ci-dessous.
  - date : date du DLR (Delivery report)
  - description : ID du DLR . Les différents ID sont décrits dans le second tableau ci-dessous
  - descriptionDlr : description du status du DLR


	Vous retrouverez dans le tableau ci-dessous une liste non-exhaustive des codes ptt principaux.

	1	Notification d'état intermédiaire indiquant que le message n'a pas encore été livré en raison d'un problème de téléphone, mais est en cours de nouvel essai (Intermediate state notification that the message has not yet been delivered due to a phone related problem but is being retried).
	2	Utilisé pour indiquer que le message n'a pas encore été livré en raison d'un problème d'opérateur, mais est en cours de nouvel essai au sein du réseau (Used to indicate that the message has not yet been delivered due to some operator related problem but is being retried within the network).
	3	Utilisé pour indiquer que le message a été accepté par l'opérateur (Used to indicate that the message has been accepted by the operator).
	4	Le message a été livré (The message was delivered).
	5	Le message a été confirmé comme non livré, mais aucune information détaillée relative à l'échec n'est connue (The message has been confirmed as undelivered but no detailed information related to the failure is known).
	6	Impossible de déterminer si le message a été livré ou a échoué, en raison d'un manque d'information de livraison de la part de l'opérateur (Cannot determine whether this message has been delivered or has failed due to lack of final delivery state information from the operator).
	8	Utilisé quand un message a expiré (ne pouvait pas être livré dans la durée de vie du message) au sein de l'opérateur SMSC, mais non associé à une raison de l'échec (Used when a message expired (could not be delivered within the life time of the message) within the operator SMSC but is not associated with a reason for failure).
	20	Utilisé quand un message n'est pas livrable dans sa forme actuelle (Used when a message in its current form is undeliverable).
	21	Utilisé uniquement lorsque l'opérateur accepte le message avant d'effectuer la vérification de crédit de l'abonné. Si les crédits sont insuffisants, l'opérateur retente l'envoi jusqu'à ce qu'il y ait assez de crédit ou que le message expire. Si le message epire et que la dernière raison de l'échec est liée au crédit, alors ce code d'erreur sera utilisé (Only occurs where the operator accepts the message before performing the subscriber credit check. If there is insufficient credit then the operator will retry the message until the subscriber tops up or the message expires. If the message expires and the last failure reason is related to credit then this error code will be used).
	23	Utilisé lorsque le message est non distribuable en raison d'un MSiSDN incorrect / invalide / sur liste noire / définitvement interndit pour cet opérateur. Ce MSISDN ne doit pas être utilisé à nouveau pour les requêtes de messages à cet opérateur(Used when the message is undeliverable due to an incorrect / invalid / blacklisted / permanently barred MSISDN for this operator. This MSISDN should not be used again for message submissions to this operator).
	24	Utilisé quand un message est non distribuable parce que l'abonné est temporairement absent, par exemple si son téléphone est éteint ou s'il ne peut pas être localisé sur le réseau (Used when a message is undeliverable because the subscriber is temporarily absent, e.g. their phone is switch off, they cannot be located on the network).
	25	Utilisé lorsque le message a échoué en raison d'un état temporaire dans le réseau de l'opérateur. Cela pourrait être lié à la couche SS7, la passerelle ou SMSC (Used when the message has failed due to a temporary condition in the operator network. This could be related to the SS7 layer, SMSC or gateway).
	26	Utilisé lorsque le message a échoué en raison d'un erreur temporaire du téléphone, par exemple la carte SIM est pleine, PME occupée, mémoire pleine etc. Cela ne signifie pas que le téléphone est incapable de recevoir ce type de message / contenu (voir code d'erreur 27) (Used when a message has failed due to a temporary phone related error, e.g. SIM card full, SME busy, memory exceeded etc. This does not mean the phone is unable to receive this type of message/content (refer to error code 27)).
	27	Utilisé lorsqu'un combiné est définitivement incompatible ou incapable de recevoir ce type de messages (Used when a handset is permanently incompatible or unable to receive this type of message).
	28	Utilisé si un message échoue ou est rejeté en raison de soupçons de SPAM sur le réseau de l'opérateur. Cela pourrait indiquer dans certaines zones géographiques que l'opérateur n'a aucune trace de la MO obligatoire requis pour un MT (Used if a message fails or is rejected due to suspicion of SPAM on the operator network. This could indicate in some geographies that the operator has no record of the mandatory MO required for an MT).
	29	Utilisé lorsque ce contenu spécifique n'est pas autorisé sur le réseau / shortcode (Used when this specific content is not permitted on the network / shortcode).
	33	Utilisé lorsque l'abonné ne peut pas recevoir un contenu pour adultes en raison d'un verrouillage parental (Used when the subscriber cannot receive adult content because of a parental lock).
	39	Nouvelle panne de l'opérateur (New operator failure).
	73	Le message a échoué car les combinaisons portées sont inaccessibles (The message was failed due to the ported combinations being unreachable).
	74	Le message a échoué car le MSISDN est en itinérance (The message was failed due to the MSISDN being roaming).
	76	Le message a échoué car les combinaisons portées sont bloquées pour le client (le client a été sur mis liste noire pour la destination portée) (The message was failed due to the ported combinations being blocked for client (the client has been blacklisted from the ported destination)).
	202	Le message a échoué en raison des combinaisons portées bloquées pour le client. Contactez le support client pour plus d'informations (The message was failed due to the ported combinations being blocked for the client. Please contact Client Support for additional information).

	Les différents ID du DLR

	État	Description
	0	En création ou en attente (Creating or pending)
	1	Succès (Success)
	2	Echoué (Failed)
	4	En attente (Waiting)
	8	Tampon (Buffered)
	16	En erreur / non facturé (Error / not billed)


## Notifier: OVH SMS

Technical
- ovh http2sms : https://help.ovhcloud.com/csm/fr-sms-sending-via-url-http2sms?id=kb_article_view&sysparm_article=KB0051397
- ovh email2sms : https://help.ovhcloud.com/csm/en-gb-sms-sending-from-email-address?id=kb_article_view&sysparm_article=KB0051384

- SMS API CookBook : https://help.ovhcloud.com/csm/fr-sms-api-cookbook?id=kb_article_view&sysparm_article=KB0039149
- SMS API : https://eu.api.ovh.com/console/?section=%2Fsms&branch=v1

Create an SMS account by using the trial offer or buying a message pack

- Trial offer (20 SMS) : https://www.ovhtelecom.fr/sms/
- Cost per SMS (in France) : 0.072€ TTC / sms (see https://www.ovhcloud.com/fr/sms/prices/)

Create a dedicated user for this application :

1. Add a `sender` from your account number (either from a domain or an OVH account name)
2. credentials
  - name : `gcn-addrandomstring`
  - password : sadly, only 8 alphanum characters ... :'(
2. activate quota, with an amount
  - how many messages available to send) : 10
3. and a limit (how many at which you will recieve a top-up alert) : 5
  - INFO: if you choose SMS (with or without email), the alert is using 1 credit
4. set restrictions
  - add each and every known IP to secure sending SMS through API calls
5. callback URL
  - add dns on same host as brain, host on rp to brain
  - insert an auth token in ovh-provided URL ... with https
6. notification
  - back in the account (not user) configure notifications upon message reception
  - TODO: check for gmail filtering as src could be a gmail account, and email is sent from ovh

Create an API token : https://www.ovh.com/auth/api/createToken
