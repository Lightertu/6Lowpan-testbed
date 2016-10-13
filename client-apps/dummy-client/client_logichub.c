#include "coap.h"
#include <stdio.h>

/*
 * The response handler
 */ 
static void
message_handler(struct coap_context_t *ctx, const coap_endpoint_t *local_interface, 
                const coap_address_t *remote, coap_pdu_t *sent, coap_pdu_t *received, 
                const coap_tid_t id) 
{
    unsigned char* data;
    size_t         data_len;
    printf("Received: %s\n", data);
    if (COAP_RESPONSE_CLASS(received->hdr->code) == 2) 
    {
        if (coap_get_data(received, &data_len, &data))
        {
            printf("Received: %s\n", data);
        }
    }
}

int main(int argc, char* argv[])
{
    coap_context_t*   ctx;
    coap_address_t    dst_addr, src_addr;
    static coap_uri_t uri;
    fd_set            readfds; 
    coap_pdu_t*       request;
    const char*       server_uri = "coap://[fe80::5844:2342:656a:f846]/.well-known/core";
    unsigned char     get_method = 1;
    /* Prepare coap socket*/
    coap_address_init(&src_addr);
    src_addr.addr.sin6.sin6_family      = AF_INET6;
    src_addr.addr.sin6.sin6_port        = htons(0);
    src_addr.addr.sin6.sin6_scope_id    = 5;
    inet_pton(AF_INET6, "fe80::1ac0:ffee:1ac0:ffee", &(src_addr.addr.sin6.sin6_addr) );
    char str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &(src_addr.addr.sin6.sin6_addr), str, INET6_ADDRSTRLEN);
    ctx = coap_new_context(&src_addr);

    /* The destination endpoint */
    coap_address_init(&dst_addr);
    dst_addr.addr.sin6.sin6_family      = AF_INET6;
    dst_addr.addr.sin6.sin6_port        = htons(5683);
    inet_pton(AF_INET6, "fe80::5844:2342:656a:f846", &(dst_addr.addr.sin6.sin6_addr) );

    /* Prepare the request */
    coap_split_uri(server_uri, strlen(server_uri), &uri);
    request            = coap_new_pdu();    
    request->hdr->type = COAP_MESSAGE_CON;
    request->hdr->id   = coap_new_message_id(ctx);
    request->hdr->code = get_method;
    coap_add_option(request, COAP_OPTION_URI_PATH, uri.path.length, uri.path.s);

    ///* Set the handler and send the request */
    coap_register_response_handler(ctx, message_handler);
    coap_send_confirmed(ctx, ctx->endpoint, &dst_addr, request);
    FD_ZERO(&readfds);
    FD_SET( ctx->sockfd, &readfds );
    int result = select( FD_SETSIZE, &readfds, 0, 0, NULL );
    if ( result < 0 ) /* socket error */
    {
        exit(EXIT_FAILURE);
    } 
    else if ( result > 0 && FD_ISSET( ctx->sockfd, &readfds )) /* socket read*/
    {    
            coap_read( ctx );       
    } 

    return 0;
}