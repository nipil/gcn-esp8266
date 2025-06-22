import logging
import os
import re
import time

from ovh import Client, HTTPError, InvalidKey, ResourceNotFoundError, APIError

from gcn_manager.constants import *

API_MAX_TIME_SKEW = 10

OVH_SMS_SERVICE_NAME = os.environ[ENV_OVH_SMS_SERVICE_NAME]
OVH_SMS_USER_NAME = os.environ[ENV_OVH_SMS_USER_NAME]
OVH_SMS_SENDER_NAME = os.environ[ENV_OVH_SMS_SENDER_NAME]

INTERNATIONAL_FRENCH_MOBILE_NUMBER = re.compile(r'^0033[67][0-9]{8}$')

SMS_NOTIFY_ENABLE = True if int(os.environ[ENV_SMS_NOTIFY_ENABLE]) != 0 else False
SMS_NOTIFY_NUMBERS = [number for number in os.environ[ENV_SMS_NOTIFY_NUMBERS].split(',') if
                      INTERNATIONAL_FRENCH_MOBILE_NUMBER.fullmatch(number)]


def run():
    logging.basicConfig(format="%(levelname)s %(message)s", level=logging.INFO)
    client = Client()

    # unauthenticated
    current_time = client.get("/auth/time")
    try:
        current_time = int(current_time)  # possible ValueError
    except ValueError as e:
        raise Exception(f"Couldn't get current time from API: {e}")
    time_skew = abs(time.time() - current_time)
    if time_skew > API_MAX_TIME_SKEW:
        raise Exception(f"Time skew from host to OVH api is greater ({time_skew}) than acceptable {API_MAX_TIME_SKEW}")
    logging.info(f"OVH api time skew compared to host is {time_skew} seconds")

    result = client.post("/sms/estimate", message="foo", noStopClause=True,  # stop clause = 11 characters
                         senderType="alpha")  # senderType: alpha|numeric|shortcode|virtual
    logging.info(f"OVH SMS estimation of 'foo' message is {result['characters']} "
                 f"characters of class {result['charactersClass']} as {result['parts']} "
                 f"part(s) of {result['maxCharactersPerPart']} max characters")

    # authenticated
    service = client.get(f"/sms/{OVH_SMS_SERVICE_NAME}")
    logging.info(f"OVH sms api service {OVH_SMS_SERVICE_NAME} credits_left={service['creditsLeft']}")

    service_infos = client.get(f"/sms/{OVH_SMS_SERVICE_NAME}/serviceInfos")
    logging.info(f"OVH sms api service {OVH_SMS_SERVICE_NAME} infos status={service_infos['status']}")  # ok

    user = client.get(f"/sms/{OVH_SMS_SERVICE_NAME}/users/{OVH_SMS_USER_NAME}")
    logging.info(f"OVH sms api service {OVH_SMS_SERVICE_NAME} user {OVH_SMS_USER_NAME} : "
                 f"ip_restrictions={user['ipRestrictions']} "
                 f"quota_info={user['quotaInformations']}")

    sender = client.get(f"/sms/{OVH_SMS_SERVICE_NAME}/senders/{OVH_SMS_SENDER_NAME}")
    logging.info(
        f"OVH sms api service {OVH_SMS_SERVICE_NAME} sender {OVH_SMS_SENDER_NAME} : status={sender['status']}")  # enable

    incoming_ids = client.get(f"/sms/{OVH_SMS_SERVICE_NAME}/users/{OVH_SMS_USER_NAME}/incoming")
    logging.info(f"OVH sms api service {OVH_SMS_SERVICE_NAME} user {OVH_SMS_USER_NAME} incoming ids : {incoming_ids}")
    # sample [114856739]

    for incoming_id in incoming_ids:
        incoming = client.get(f"/sms/{OVH_SMS_SERVICE_NAME}/users/{OVH_SMS_USER_NAME}/incoming/{incoming_id}")
        logging.info(f"OVH sms api service {OVH_SMS_SERVICE_NAME} "
                     f"user {OVH_SMS_USER_NAME} incoming {incoming_id} : {incoming}")
    # sample:
    # 114856739: {'creationDatetime': '2025-06-23T09:01:50+02:00', 'tag': 'this_is_a_tag', 'message': 'Merci !',
    #             'id': 114856739, 'sender': '+33612345678', 'credits': 0}

    jobs = client.get(f"/sms/{OVH_SMS_SERVICE_NAME}/users/{OVH_SMS_USER_NAME}/jobs")
    logging.info(f"OVH sms api service {OVH_SMS_SERVICE_NAME} user {OVH_SMS_USER_NAME} jobs : {jobs}")
    # while waiting for send
    # [687605399]
    # after sent:
    # []

    for job_id in jobs:
        job = client.get(f"/sms/{OVH_SMS_SERVICE_NAME}/users/{OVH_SMS_USER_NAME}/jobs/{job_id}")
        logging.info(f"OVH sms api service {OVH_SMS_SERVICE_NAME} user {OVH_SMS_USER_NAME} job {job_id} : {job}")

    # sample on creation :
    # 687605399: {'sentAt': None, 'creationDatetime': '2025-06-22T22:56:03+02:00', 'sender': '',
    #             'receiver': '+33612345678', 'ptt': 1000, 'deliveryReceipt': 0, 'id': 687605399, 'deliveredAt': None,
    #             'credits': 1, 'numberOfSms': 1, 'message': 'GCN test message', 'messageLength': 16,
    #             'differedDelivery': 600}
    # sample after 9h :
    # 687605399: {'receiver': '+33612345678', 'sentAt': None, 'ptt': 1000, 'id': 687605399, 'deliveredAt': None,
    #             'messageLength': 16, 'deliveryReceipt': 0, 'differedDelivery': 600, 'message': 'GCN test message',
    #             'numberOfSms': 1, 'credits': 1, 'sender': '', 'creationDatetime': '2025-06-22T22:56:03+02:00'}

    # pay
    data = {"charset": "UTF-8",  # UTF-8
            "coding": "7bit",  # 7bit|8bit
            "differedPeriod": 60 * 10,  # time in minute before sending
            "message": "GCN test message", "noStopClause": False,  # boolean
            "priority": "medium",  # high|medium|low|veryLow
            "receivers": SMS_NOTIFY_NUMBERS, "sender": OVH_SMS_SENDER_NAME, "senderForResponse": True,
            "tag": "this_is_a_tag", "validityPeriod": 60 * 2  # minutes before the message is dropped
            }

    if SMS_NOTIFY_ENABLE and len(SMS_NOTIFY_NUMBERS) > 0:
        logging.warning(f"Notifying to {SMS_NOTIFY_NUMBERS}")
        # can raise ApiError: No more quota credit for the user xxxxxx. Quota available : 0.00. Credit needed : 1
        new_job = client.post(f"/sms/{OVH_SMS_SERVICE_NAME}/users/{OVH_SMS_USER_NAME}/jobs", **data)
    else:
        logging.info(f"mocking the POST response instead of sending...")
        new_job = {'validReceivers': ['+33612345678'], 'ids': [687605399], 'totalCreditsRemoved': 1,
                   'invalidReceivers': [], 'tag': 'this_is_a_tag'}
    logging.info(f"OVH sms api service service {OVH_SMS_SERVICE_NAME} user {OVH_SMS_USER_NAME} new_job {new_job}")


def main():
    try:
        run()
    except HTTPError as e:
        raise Exception(f"Could not connect to OVH api : {e}")
    except InvalidKey as e:
        raise Exception(f"Could query OVH sms api : {e}")
    except ResourceNotFoundError as e:
        raise Exception(f"OVH sms api resource not found: {e}")
    except APIError as e:
        raise Exception(f"OVH sms api error: {e}")


if __name__ == '__main__':
    main()
