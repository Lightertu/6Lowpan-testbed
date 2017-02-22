/*
 * Copyright (c) Rui Tu
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net/gcoap.h"
#include "od.h"
#include "fmt.h"
#include "testbed-hardware.h"
#define UNUSED(x) (void)(x)

#define MAX_RESPONSE_LEN 100
#define URI_MAX_LEN 64

typedef struct {
    const char * path;
    const char * data_format;

} netsec_resource_t;

static void _resp_handler(unsigned req_state, coap_pkt_t* pdu);
static ssize_t _stats_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len);
static ssize_t _sensor_temperature_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len);
static ssize_t _actuator_led_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len);

/* this saves the address of the last requester */
/* uint8_t last_response_addr[URI_MAX_LEN]; */

/* CoAP resources */

/* the pathnames have to be sorted alphabetically */
static const coap_resource_t _resources[] = {
    { "/actuator/led", COAP_PUT, _actuator_led_handler },
    { "/cli/stats", COAP_GET, _stats_handler },
    { "/sensor/temperature", COAP_GET, _sensor_temperature_handler },
    { NULL, 0, NULL}, // Mark as the end of the endpoints
};

static const netsec_resource_t _netsec_resources[] = {
    { "/actuator/led", "boolean"},
    { "/cli/stats", "unspecified"},
    { "/sensor/temperature", "number"},
    { NULL, NULL}, // Mark as the end of the endpoints
};

static gcoap_listener_t _actuator_led_listener = {
    (coap_resource_t *)&_resources[0],
    sizeof(_resources) / sizeof(_resources[0]),
    NULL
};

static gcoap_listener_t _request_stats_listener = {
    (coap_resource_t *)&_resources[1],
    sizeof(_resources) / sizeof(_resources[1]),
    NULL
};

static gcoap_listener_t _sensor_temperature_listener = {
    (coap_resource_t *)&_resources[2],
    sizeof(_resources) / sizeof(_resources[2]),
    NULL
};

/* stack for the advertising thread */
static char self_advertising_thread_stack[THREAD_STACKSIZE_MAIN];

/* advertising string */
static char service_string[GCOAP_PDU_BUF_SIZE];

/* Counts requests sent by CLI. */
static uint16_t req_count = 0;

/*
 * Response callback.
 */
static void _resp_handler(unsigned req_state, coap_pkt_t* pdu) {

    if (req_state == GCOAP_MEMO_TIMEOUT) {
        printf("gcoap: timeout for msg ID %02u\n", coap_get_id(pdu));
        return;
    }
    else if (req_state == GCOAP_MEMO_ERR) {
        printf("gcoap: error in response\n");
        return;
    }

    char *class_str = (coap_get_code_class(pdu) == COAP_CLASS_SUCCESS)
                            ? "Success" : "Error";
    printf("gcoap: response %s, code %1u.%02u", class_str,
                                                coap_get_code_class(pdu),
                                                coap_get_code_detail(pdu));
    if (pdu->payload_len) {
        if (pdu->content_type == COAP_FORMAT_TEXT
                || pdu->content_type == COAP_FORMAT_LINK
                || coap_get_code_class(pdu) == COAP_CLASS_CLIENT_FAILURE
                || coap_get_code_class(pdu) == COAP_CLASS_SERVER_FAILURE) {
            /* Expecting diagnostic payload in failure cases */
            printf(", %u bytes\n%.*s\n", pdu->payload_len, pdu->payload_len,
                                                          (char *)pdu->payload);
        }
        else {
            printf(", %u bytes\n", pdu->payload_len);
            od_hex_dump(pdu->payload, pdu->payload_len, OD_WIDTH_DEFAULT);
        }
    }
    else {
        printf(", empty payload\n");
    }
}

/*
 * Server callback for /cli/stats. Returns the count of packets sent by the
 * CLI.
 */
static ssize_t _stats_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len) {
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);

    size_t payload_len = fmt_u16_dec((char *)pdu->payload, req_count);

    return gcoap_finish(pdu, payload_len, COAP_FORMAT_TEXT);
}

/* callback for handling temperature data */
static ssize_t _sensor_temperature_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len) {
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);

    size_t payload_len = fmt_u16_dec((char *)pdu->payload, read_temperature_dummy());

    return gcoap_finish(pdu, payload_len, COAP_FORMAT_TEXT);
}

/* callback for handling led data */
static ssize_t _actuator_led_handler(coap_pkt_t* pdu, uint8_t *buf, size_t len) {
    char ledstats[MAX_RESPONSE_LEN] = {0};
    int ledstats_out;
    memcpy(ledstats, pdu->payload, pdu->payload_len);

    gcoap_resp_init(pdu, buf, len, COAP_CODE_CONTENT);


    if (strcmp(ledstats, "ON") == 0) {
        led_switch(ON);
        ledstats_out = 1;
    } else if (strcmp(ledstats, "OFF") == 0) {
        led_switch(OFF);
        ledstats_out = 0;
    } else {
        ledstats_out = -1;
    }
    
     
    size_t payload_len = fmt_u16_dec((char *)pdu->payload, ledstats_out);

    return gcoap_finish(pdu, payload_len, COAP_FORMAT_TEXT);
}

static size_t _send(uint8_t *buf, size_t len, char *addr_str, char *port_str) {

    ipv6_addr_t addr;
    uint16_t port;
    size_t bytes_sent;

    /* parse destination address */
    if (ipv6_addr_from_str(&addr, addr_str) == NULL) {
        puts("gcoap_cli: unable to parse destination address");
        return 0;
    }
    /* parse port */
    port = (uint16_t)atoi(port_str);
    if (port == 0) {
        puts("gcoap_cli: unable to parse destination port");
        return 0;
    }

    bytes_sent = gcoap_req_send(buf, len, &addr, port, _resp_handler);
    /* print("bytes sent, %d", bytes_sent); */
    if (bytes_sent > 0) {
        req_count++;
    }
    return bytes_sent;
}

int gcoap_cli_cmd(int argc, char **argv) {

    /* Ordered like the RFC method code numbers, but off by 1. GET is code 0. */
    char *method_codes[] = {"get", "post", "put"};
    uint8_t buf[GCOAP_PDU_BUF_SIZE];
    coap_pkt_t pdu;
    size_t len;

    if (argc == 1) {
        /* show help for main commands */
        goto end;
    }

    for (size_t i = 0; i < sizeof(method_codes) / sizeof(char*); i++) {
        if (strcmp(argv[1], method_codes[i]) == 0) {
            if (argc == 5 || argc == 6) {
                if (argc == 6) {
                    gcoap_req_init(&pdu, &buf[0], GCOAP_PDU_BUF_SIZE, i+1, argv[4]);
                    memcpy(pdu.payload, argv[5], strlen(argv[5]));
                    len = gcoap_finish(&pdu, strlen(argv[5]), COAP_FORMAT_TEXT);
                }
                else {
                    len = gcoap_request(&pdu, &buf[0], GCOAP_PDU_BUF_SIZE, i+1,
                                                                           argv[4]);
                }
                printf("gcoap_cli: sending msg ID %u, %u bytes\n", coap_get_id(&pdu),
                                                                   (unsigned) len);
                if (!_send(&buf[0], len, argv[2], argv[3])) {
                    puts("gcoap_cli: msg send failed");
                }
                return 0;
            }
            else {
                printf("fuck: %s <get|post|put> <addr> <port> <path> [data]\n",
                                                                       argv[0]);
                return 1;
            }
        }
    }

    if (strcmp(argv[1], "info") == 0) {
        if (argc == 2) {
            uint8_t open_reqs;
            gcoap_op_state(&open_reqs);

            printf("CoAP server is listening on port %u\n", GCOAP_PORT);
            printf(" CLI requests sent: %u\n", req_count);
            printf("CoAP open requests: %u\n", open_reqs);
            return 0;
        }
    }

    end:
    printf("usage: %s <get|post|put|info>\n", argv[0]);
    return 1;
}


void *self_advertising_thread(void* args) {
    int len;
    char *uri = "ff02::1";
    /* char *uri = "fe80::1ac0:ffee:1ac0:ffee"; */
    char *path = "/devices";
    char *payload = (char *) args;
    char *port = "6666";

    while (1) {
        uint8_t buf[GCOAP_PDU_BUF_SIZE];
        coap_pkt_t pdu;
        gcoap_req_init(&pdu, &buf[0], GCOAP_PDU_BUF_SIZE, COAP_POST, path);
        memcpy(pdu.payload, payload, strlen(payload));
        len = gcoap_finish(&pdu, strlen(payload), COAP_FORMAT_TEXT);
        puts("Advertising");
        if (!_send(&buf[0], len, uri, port)) {
            puts("server: adversing message send failed");
            puts("re-advertise");
        } 

        xtimer_sleep(10);
    }

    return NULL;
 }


int build_service_string(char * service_string, int bufsize) {
    int i = 0, slen = 0;
    while (_netsec_resources[i].path != NULL) {
        if (slen >= bufsize) {
            puts("String is too long");
            return 0;
        }

        bufsize -= slen;
        slen += snprintf(service_string + slen, (size_t) bufsize, "%s:%s,", _netsec_resources[i].path, _netsec_resources[i].data_format);
        ++i;
    }

    return 1;
}


void gcoap_cli_init(void) {
    gcoap_register_listener(&_actuator_led_listener);
    gcoap_register_listener(&_request_stats_listener);
    gcoap_register_listener(&_sensor_temperature_listener);

    if (build_service_string(service_string, GCOAP_PDU_BUF_SIZE)) {
        thread_create(self_advertising_thread_stack, 
                      sizeof(self_advertising_thread_stack),
                      THREAD_PRIORITY_MAIN - 1, 
                      THREAD_CREATE_STACKTEST, 
                      self_advertising_thread, 
                      service_string, 
                      "self_adverstising_thread"
        );
    }
    
    printf("%s\n", service_string);
}
